#include "PluginProcessor.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace juce;

static void set (WaveProcessor& p, const String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
static double energy (const std::vector<float>& x, size_t from = 0)
{
    double sum = 0;
    for (size_t i = from; i < x.size(); ++i) sum += (double) x[i] * x[i];
    return sum;
}
static std::array<std::vector<float>, 2> render (WaveProcessor& p, int blocks = 40, bool note = true)
{
    p.prepareToPlay (48000, 256);
    AudioBuffer<float> buffer (2, 256);
    MidiBuffer midi;
    if (note) midi.addEvent (MidiMessage::noteOn (1, 60, .8f), 0);
    std::array<std::vector<float>, 2> out;
    for (int b = 0; b < blocks; ++b)
    {
        p.processBlock (buffer, midi); midi.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
            {
                assert (std::isfinite (buffer.getSample (ch, i)));
                out[(size_t) ch].push_back (buffer.getSample (ch, i));
            }
    }
    midi.addEvent (MidiMessage::allSoundOff (1), 0);
    p.processBlock (buffer, midi);
    return out;
}

static void testFilterResponse()
{
    for (double rate : { 44100.0, 96000.0 })
        for (int mode = 0; mode < 6; ++mode)
            for (double hz : { 110.0, 900.0, 2500.0, 9000.0 })
            {
                bw::VoiceFilter filter;
                filter.setMode (mode);
                filter.set (1800, 2.0, rate);
                const int length = (int) rate;
                double in = 0, out = 0;
                for (int i = 0; i < length; ++i)
                {
                    const float x = (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
                    const float y = filter.process (0, x);
                    if (i >= length / 2) { in += (double) x * x; out += (double) y * y; }
                }
                const double measured = std::sqrt (out / in), predicted = bw::VoiceFilter::magnitude (mode, hz, 1800, 2.0, rate);
                assert (std::abs (measured - predicted) < 0.01 * predicted + 1.0e-4);
            }
    // 24 dB slopes are steeper than 12 dB an octave past cutoff.
    assert (bw::VoiceFilter::magnitude (bw::VoiceFilter::low24, 4000, 1000, .707, 48000) < .5 * bw::VoiceFilter::magnitude (bw::VoiceFilter::low12, 4000, 1000, .707, 48000));
}

static bw::EffectParams effectParams (WaveProcessor& p)
{
    return { p.apvts.getRawParameterValue ("distOn"), p.apvts.getRawParameterValue ("distMode"), p.apvts.getRawParameterValue ("distDrive"), p.apvts.getRawParameterValue ("distMix"),
        p.apvts.getRawParameterValue ("delayOn"), p.apvts.getRawParameterValue ("delayTime"), p.apvts.getRawParameterValue ("delaySync"), p.apvts.getRawParameterValue ("delayDivision"),
        p.apvts.getRawParameterValue ("delayFeedback"), p.apvts.getRawParameterValue ("delayPingPong"), p.apvts.getRawParameterValue ("delayMix"),
        p.apvts.getRawParameterValue ("reverbOn"), p.apvts.getRawParameterValue ("reverbSize"), p.apvts.getRawParameterValue ("reverbDamping"),
        p.apvts.getRawParameterValue ("reverbWidth"), p.apvts.getRawParameterValue ("reverbMix"),
        p.apvts.getRawParameterValue ("chorusOn"), p.apvts.getRawParameterValue ("chorusRate"), p.apvts.getRawParameterValue ("chorusDepth"),
        p.apvts.getRawParameterValue ("chorusMix") };
}

static void testEffects()
{
    WaveProcessor p;
    const auto params = effectParams (p);
    for (int mode = 0; mode < 3; ++mode)
        for (float drive : { 1.0f, 4.0f, 63.0f })
        {
            assert (std::abs (bw::shape (mode, 1, drive) - (mode == 2 ? std::sin (drive * juce::MathConstants<float>::halfPi) : 1.0f)) < 1.0e-5f);
            assert (std::abs (bw::shape (mode, -.3f, drive) + bw::shape (mode, .3f, drive)) < 1.0e-6f);
        }

    bw::Effects fx;
    fx.prepare (48000, 128);
    AudioBuffer<float> buffer (2, 300);
    int clock = 0; // continuous across blocks
    auto sine = [&] { for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 300; ++i) buffer.setSample (ch, i, .5f * std::sin ((clock + i) * .01f + ch)); clock += 300; };
    // Everything bypassed: bit-identical, including blocks larger than the prepared size.
    sine(); AudioBuffer<float> copy (buffer);
    fx.process (buffer, params, 120);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 300; ++i) assert (buffer.getSample (ch, i) == copy.getSample (ch, i));

    // Distortion fades in without steps and adds level-limited harmonics.
    set (p, "distOn", 1); set (p, "distDrive", 24); set (p, "distMode", 1);
    float previous = 0, peak = 0;
    for (int block = 0; block < 10; ++block)
    {
        sine(); fx.process (buffer, params, 120);
        for (int i = 0; i < 300; ++i)
        {
            const float y = buffer.getSample (0, i);
            assert (std::isfinite (y) && std::abs (y) < 1.2f);
            if (block > 0 || i > 0) assert (std::abs (y - previous) < .3f);
            previous = y; peak = jmax (peak, std::abs (y));
        }
    }
    assert (peak > .8f); // a 0.5 sine driven by +24 dB hits the clip ceiling
    set (p, "distOn", 0);
    for (int block = 0; block < 10; ++block) { sine(); fx.process (buffer, params, 120); }
    sine(); copy.makeCopyOf (buffer); fx.process (buffer, params, 120);
    for (int i = 0; i < 300; ++i) assert (buffer.getSample (0, i) == copy.getSample (0, i));

    // Delay: an impulse returns after exactly the set time, then again scaled by feedback.
    fx.prepare (48000, 512);
    set (p, "delayOn", 1); set (p, "delayTime", 10); set (p, "delayMix", 1); set (p, "delayFeedback", .5f); set (p, "delaySync", 0);
    AudioBuffer<float> silence (2, 512);
    for (int i = 0; i < 20; ++i) { silence.clear(); fx.process (silence, params, 120); } // finish fade-in and time glide
    AudioBuffer<float> impulse (2, 2048);
    impulse.clear(); impulse.setSample (0, 0, 1); impulse.setSample (1, 0, 1);
    fx.process (impulse, params, 120);
    assert (std::abs (impulse.getSample (0, 480) - 1) < 1.0e-4f && std::abs (impulse.getSample (0, 960) - .5f) < 1.0e-4f);
    assert (std::abs (impulse.getSample (0, 0)) < 1.0e-6f && std::abs (impulse.getSample (0, 700)) < 1.0e-6f);
    // Ping-pong alternates sides.
    set (p, "delayPingPong", 1); set (p, "delayFeedback", .5f);
    fx.reset();
    impulse.clear(); impulse.setSample (0, 0, 1); impulse.setSample (1, 0, 1);
    fx.process (impulse, params, 120);
    assert (impulse.getSample (0, 480) > .9f && std::abs (impulse.getSample (1, 480)) < 1.0e-6f);
    assert (std::abs (impulse.getSample (0, 960)) < 1.0e-6f && impulse.getSample (1, 960) > .45f);
    set (p, "delayOn", 0); set (p, "delayPingPong", 0);
    for (int i = 0; i < 20; ++i) { silence.clear(); fx.process (silence, params, 120); }

    // Reverb keeps a tail after the input stops, and it decays.
    set (p, "reverbOn", 1); set (p, "reverbMix", .5f);
    for (int i = 0; i < 10; ++i) { silence.clear(); fx.process (silence, params, 120); }
    impulse.clear(); for (int i = 0; i < 64; ++i) impulse.setSample (0, i, 1);
    fx.process (impulse, params, 120);
    double early = 0, late = 0;
    for (int b = 0; b < 200; ++b)
    {
        silence.clear(); fx.process (silence, params, 120);
        const double e = silence.getRMSLevel (0, 0, 512);
        if (b < 10) early += e; if (b >= 190) late += e;
    }
    assert (early > 1.0e-3 && late < early * .5);
}

static void testChorus()
{
    WaveProcessor p;
    const auto params = effectParams (p);
    bw::Effects fx;
    fx.prepare (48000, 256);
    AudioBuffer<float> buffer (2, 256);
    int clock = 0;
    // The same 220 Hz sine (smooth: any step is a click) or saw (broadband, for level) on both sides.
    bool useSaw = false;
    auto saw = [&]
    {
        for (int i = 0; i < 256; ++i)
        {
            const double phase = std::fmod ((clock + i) * 220.0 / 48000, 1.0);
            for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, i, .4f * (float) (useSaw ? phase * 2 - 1 : std::sin (juce::MathConstants<double>::twoPi * phase)));
        }
        clock += 256;
    };
    std::array<std::vector<float>, 2> out;
    auto run = [&] (int blocks, bool input = true)
    {
        for (int b = 0; b < blocks; ++b)
        {
            if (input) saw(); else buffer.clear();
            fx.process (buffer, params, 120);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) { assert (std::isfinite (buffer.getSample (ch, i))); out[(size_t) ch].push_back (buffer.getSample (ch, i)); }
        }
    };
    auto steps = [&] (size_t from)
    {
        float worst = 0;
        for (size_t i = from + 1; i < out[0].size(); ++i) worst = jmax (worst, std::abs (out[0][i] - out[0][i - 1]));
        return worst;
    };
    run (20);
    const float drySteps = steps (0);
    set (p, "chorusOn", 1); set (p, "chorusDepth", 1); set (p, "chorusMix", .5f); set (p, "chorusRate", 1.5f);
    const size_t fadeIn = out[0].size();
    run (40);
    assert (steps (fadeIn) < drySteps * 1.5f);
    useSaw = true;
    run (20);
    // Identical inputs leave as two different sides.
    double l = 0, r = 0, lr = 0;
    for (size_t i = out[0].size() - 9600; i < out[0].size(); ++i) { l += out[0][i] * out[0][i]; r += out[1][i] * out[1][i]; lr += out[0][i] * out[1][i]; }
    assert (lr / std::sqrt (l * r) < .97);
    // Level stays within 3 dB of the dry signal.
    const double sawDb = Decibels::gainToDecibels (std::sqrt (l / 9600) / (.4 / std::sqrt (3.0)));
    std::printf ("chorus level change: saw %+.2f dB", sawDb);
    assert (std::abs (sawDb) < 3);
    // Automating every control each block stays finite and step-free.
    useSaw = false;
    run (5);
    Random random (7);
    const size_t automated = out[0].size();
    for (int b = 0; b < 100; ++b)
    {
        set (p, "chorusRate", .05f + 4.95f * random.nextFloat()); set (p, "chorusDepth", random.nextFloat()); set (p, "chorusMix", random.nextFloat());
        run (1);
    }
    assert (steps (automated) < drySteps * 1.5f);
    // The tail ends within the longest tap.
    run (3, false);
    const size_t tail = out[0].size();
    run (4, false);
    for (size_t i = tail; i < out[0].size(); ++i) assert (out[0][i] == 0 && out[1][i] == 0);
    // Off: fades out, then the signal passes bit-identical.
    run (5);
    set (p, "chorusOn", 0);
    const size_t fadeOut = out[0].size();
    run (10);
    assert (steps (fadeOut) < drySteps * 1.5f);
    saw(); AudioBuffer<float> copy (buffer); fx.process (buffer, params, 120);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) assert (buffer.getSample (ch, i) == copy.getSample (ch, i));

    // On a unison pad through the processor, the output level stays within 3 dB with the chorus on.
    WaveProcessor pad;
    set (pad, "wtTable", 2); set (pad, "oscAUnison", 4); set (pad, "oscADetune", 15); set (pad, "gain", -12);
    const double without = energy (render (pad, 80)[0], 4096);
    set (pad, "chorusOn", 1);
    const double with = energy (render (pad, 80)[0], 4096);
    const double padDb = Decibels::gainToDecibels (std::sqrt (with / without));
    std::printf (", unison pad %+.2f dB\n", padDb);
    assert (std::abs (padDb) < 3);
}

static void testRouting()
{
    WaveProcessor p;
    set (p, "cutoff", 20); set (p, "reso", .707f); set (p, "gain", -12);
    const double filtered = energy (render (p)[0], 2048);
    set (p, "oscARoute", 2); // Direct bypasses both filters
    const double direct = energy (render (p)[0], 2048);
    assert (direct > filtered * 100);
    set (p, "oscARoute", 1); // Filter 2 is a pass-through while disabled
    assert (std::abs (energy (render (p)[0], 2048) - direct) < direct * 1.0e-3);
    set (p, "filter2On", 1); set (p, "filter2Type", bw::VoiceFilter::high12); set (p, "cutoff2", 20000);
    assert (energy (render (p)[0], 2048) < direct * .01);
    // Serial: Filter 1's output also passes Filter 2. Parallel: it does not.
    set (p, "oscARoute", 0); set (p, "cutoff", 20000); set (p, "filterType", bw::VoiceFilter::low12);
    const double serial = energy (render (p)[0], 2048);
    set (p, "filterRouting", 1);
    const double parallel = energy (render (p)[0], 2048);
    assert (parallel > serial * 100);
}

static File writeWav (const File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream (file.createOutputStream().release());
    std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 1, 32, {}, 0));
    assert (writer != nullptr);
    stream.release();
    const float* channels[] { samples.data() };
    assert (writer->writeFromFloatArrays (channels, 1, (int) samples.size()));
    return file;
}

static void testWavetableImport()
{
    const auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-wave-tests");
    folder.createDirectory();
    // Four 2048-sample frames: harmonic 1..4.
    std::vector<float> audio;
    for (int f = 1; f <= 4; ++f)
        for (int i = 0; i < bw::Wavetable::kSize; ++i) audio.push_back (.5f * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / bw::Wavetable::kSize));
    const auto file = writeWav (folder.getChildFile ("four.wav"), audio);
    String error;
    const auto cycles = bw::loadCycles (file, error);
    assert (error.isEmpty() && cycles.size() == 4);
    for (int i = 0; i < bw::Wavetable::kSize; i += 97) assert (std::abs (cycles[2][(size_t) i] - audio[(size_t) (2 * bw::Wavetable::kSize + i)]) < 1.0e-6f);

    // Odd frame sizes resample to kSize; a short file is one cycle.
    std::vector<float> small;
    for (int f = 0; f < 3; ++f) for (int i = 0; i < 256; ++i) small.push_back ((float) std::sin (juce::MathConstants<double>::twoPi * i / 256));
    const auto resampled = bw::cyclesFromAudio (small, 256);
    assert (resampled.size() == 3);
    for (int i = 0; i < bw::Wavetable::kSize; ++i) assert (std::abs (resampled[1][(size_t) i] - std::sin (juce::MathConstants<double>::twoPi * i / bw::Wavetable::kSize)) < 1.0e-3);
    assert (bw::cyclesFromAudio (std::vector<float> (600, 1.0f), 0).size() == 1);
    assert (bw::cyclesFromAudio (std::vector<float> (8, 1.0f), 0).empty());
    assert (bw::cyclesFromAudio (std::vector<float> (bw::Wavetable::kSize * 300), 0).size() == bw::kMaxUserFrames);

    // Serum-style clm chunk declares the frame size.
    MemoryOutputStream riff;
    riff.write ("RIFF", 4); riff.writeInt (0); riff.write ("WAVE", 4);
    riff.write ("junk", 4); riff.writeInt (3); riff.write ("abc\0", 4);
    riff.write ("clm ", 4); riff.writeInt (12); riff.write ("<!>256 00000", 12);
    const auto clm = folder.getChildFile ("clm.bin");
    clm.replaceWithData (riff.getData(), riff.getDataSize());
    assert (bw::clmFrameSize (clm) == 256 && bw::clmFrameSize (file) == 0);
    assert (bw::loadCycles (clm, error).empty() && error.isNotEmpty());

    // The processor plays, saves and restores imported tables.
    WaveProcessor p;
    set (p, "cutoff", 18000); set (p, "wtPos", 1);
    const auto factory = render (p)[0];
    assert (p.importWavetable (0, file).isEmpty() && p.userTable (0)->numFrames == 4 && p.userTableName (0) == "four");
    assert (! p.usingUserTable (0));
    set (p, "oscAUser", 1);
    assert (p.usingUserTable (0));
    const auto user = render (p)[0];
    double difference = 0;
    for (size_t i = 0; i < user.size(); ++i) difference += std::abs (user[i] - factory[i]);
    assert (difference > 1.0);
    MemoryBlock state; p.getStateInformation (state);
    WaveProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    assert (restored.usingUserTable (0) && restored.userTable (0)->numFrames == 4 && restored.userTableName (0) == "four");
    for (int f = 0; f < 4; ++f) for (int i = 0; i < bw::Wavetable::kSize; i += 31)
        assert (std::abs (restored.userTable (0)->ptr (f, 3)[i] - p.userTable (0)->ptr (f, 3)[i]) < 1.0e-5f);
    const auto replay = render (restored)[0];
    for (size_t i = 0; i < replay.size(); ++i) assert (std::abs (replay[i] - user[i]) < 1.0e-4f);
    // A state without tables clears the slot.
    WaveProcessor fresh; MemoryBlock empty; fresh.getStateInformation (empty);
    restored.setStateInformation (empty.getData(), (int) empty.getSize());
    assert (restored.userTable (0) == nullptr && ! restored.usingUserTable (0));
    folder.deleteRecursively();
}

static void testTableLifetime()
{
    bw::UserTableSlot slot;
    auto first = std::make_shared<bw::Wavetable> (bw::Wavetable::makeBasic (2));
    std::weak_ptr<const bw::Wavetable> watch = first;
    slot.publish (std::move (first));
    assert (slot.acquire (256) == watch.lock().get());
    slot.publish (std::make_shared<bw::Wavetable> (bw::Wavetable::makeBasic (2)));
    slot.collect();
    assert (! watch.expired()); // audio has not switched yet
    slot.acquire (100000);       // switches; this block is not rendered yet
    slot.collect();
    assert (! watch.expired());
    slot.acquire (256);          // the 100000-sample block is now rendered
    slot.collect();
    assert (watch.expired());
}

static void testPresetFiles()
{
    const auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-wave-presets");
    folder.deleteRecursively();
    WaveProcessor p;
    set (p, "cutoff", 1234); set (p, "reverbOn", 1); set (p, "filterType", 3); p.setMacroName (0, "Bloom");
    const auto file = folder.getChildFile ("Mine.bwpreset");
    assert (p.savePreset (file) && file.existsAsFile());
    set (p, "cutoff", 8000); set (p, "reverbOn", 0); set (p, "filterType", 0); p.setMacroName (0, "Macro 1");
    assert (p.loadPreset (file));
    assert (std::abs (p.apvts.getRawParameterValue ("cutoff")->load() - 1234) < .5f);
    assert (p.apvts.getRawParameterValue ("reverbOn")->load() == 1 && p.apvts.getRawParameterValue ("filterType")->load() == 3);
    assert (p.getMacroName (0) == "Bloom");
    auto xml = parseXML (file);
    xml->setAttribute ("formatVersion", WaveProcessor::kStateVersion + 1);
    const auto future = folder.getChildFile ("Future.bwpreset");
    assert (xml->writeTo (future) && ! p.loadPreset (future));
    const auto bogus = folder.getChildFile ("Bogus.bwpreset");
    bogus.replaceWithText ("<OTHER/>");
    assert (! p.loadPreset (bogus) && ! p.loadPreset (folder.getChildFile ("missing.bwpreset")));
    folder.deleteRecursively();
}

int main()
{
   #if JUCE_WINDOWS
    // Failed asserts print to stderr instead of opening a modal dialog.
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    testFilterResponse();
    testEffects();
    testChorus();
    testRouting();
    testWavetableImport();
    testTableLifetime();
    testPresetFiles();
    std::puts ("ok: filter response, distortion/chorus/delay/reverb, routing, WAV import, table lifetime, preset files");
}
