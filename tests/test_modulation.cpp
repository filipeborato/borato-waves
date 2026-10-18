#include "PluginProcessor.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace
{
void set (WaveProcessor& p, const juce::String& id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    assert (parameter != nullptr);
    if (parameter == nullptr) std::abort();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void initialise (WaveProcessor& p)
{
    set (p, "attack", 0.001f); set (p, "decay", 0.001f); set (p, "sustain", 1);
    set (p, "cutoff", 18000); set (p, "level", 0.5f); set (p, "gain", -12);
}

struct Host : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo result;
        result.setBpm (bpm); result.setPpqPosition (ppq); result.setIsPlaying (playing);
        return result;
    }
};

using Stereo = std::array<std::vector<float>, 2>;
Stereo render (WaveProcessor& p, int samples = 24000, int blockSize = 128,
               double rate = 48000, Host* host = nullptr)
{
    p.setPlayHead (host);
    p.prepareToPlay (rate, blockSize);
    Stereo result;
    for (auto& ch : result) ch.reserve ((size_t) samples);
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int size = std::min (blockSize, samples - offset);
        juce::AudioBuffer<float> audio (2, size);
        juce::MidiBuffer midi;
        if (offset == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        if (host != nullptr) host->ppq = offset / rate * host->bpm / 60.0;
        p.processBlock (audio, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < size; ++i)
            {
                const float sample = audio.getSample (ch, i);
                assert (std::isfinite (sample));
                result[(size_t) ch].push_back (sample);
            }
    }
    p.setPlayHead (nullptr);
    return result;
}

double energy (const std::vector<float>& x, size_t start = 2048)
{
    double sum = 0;
    for (size_t i = start; i < x.size(); ++i) sum += x[i] * x[i];
    return sum / (double) (x.size() - start);
}

double difference (const std::vector<float>& x, const std::vector<float>& y, size_t start = 0)
{
    assert (x.size() == y.size());
    double peak = 0;
    for (size_t i = start; i < x.size(); ++i)
        peak = std::max (peak, std::abs ((double) x[i] - y[i]));
    return peak;
}

void testCurves()
{
    for (int shape = 0; shape < 5; ++shape)
    {
        auto curve = bw::LfoCurve::factory (shape);
        assert (curve.valid());
        assert (curve.evaluate (0) == curve.evaluate (1));
        for (int i = -2000; i <= 2000; ++i)
        {
            float value = curve.evaluate (i / 1000.0);
            assert (std::isfinite (value) && value >= 0 && value <= 1);
            assert (std::abs (value - curve.evaluate (i / 1000.0 + 7)) < 1.0e-4f);
        }
    }
    auto triangle = bw::LfoCurve::factory (1);
    assert (triangle.evaluate (0.25) == 0.5f);
    assert (triangle.evaluate (0.5) == 1);
    assert (triangle.evaluate (0.75) == 0.5f);
    triangle.points[0].curve = 1;
    assert (triangle.evaluate (0.25) > 0.5f);
    triangle.points[0].curve = -1;
    assert (triangle.evaluate (0.25) < 0.5f);
    auto invalid = triangle; invalid.count = 33; assert (! invalid.valid());
    invalid = triangle; invalid.count = 1; assert (! invalid.valid());
    invalid = triangle; invalid.points[1].x = 0; assert (! invalid.valid());
    invalid = triangle; invalid.points[1].y = 1.1f; assert (! invalid.valid());
    invalid = triangle; invalid.points[1].curve = std::numeric_limits<float>::quiet_NaN(); assert (! invalid.valid());
    invalid = triangle; invalid.points[2].y = 0.3f; assert (! invalid.valid());
    assert (bw::divisionNames().size() == 27);
    for (int i = 0; i < 27; i += 3)
    {
        const auto straight = bw::divisionBeats (i);
        assert (straight > 0);
        assert (std::abs (bw::divisionBeats (i + 1) - straight * 1.5) < 1.0e-12);
        assert (std::abs (bw::divisionBeats (i + 2) - straight * 2 / 3) < 1.0e-12);
    }
    assert (bw::divisionBeats (0) == 16 && bw::divisionBeats (12) == 1);
    bw::CurveExchange exchange;
    bw::CurveExchange::Curves curves { triangle, bw::LfoCurve::factory (4) }, read;
    exchange.publish (curves); exchange.consume (read);
    assert (read[0].points[0].curve == -1 && read[1].count == 6);
    curves[0] = bw::LfoCurve::factory (2); exchange.publish (curves);
    curves[0] = bw::LfoCurve::factory (3); exchange.publish (curves);
    exchange.consume (read);
    assert (read[0].points[0].y == 1); // Latest complete snapshot wins.

    bw::CurveExchange threaded;
    std::atomic<bool> finished { false };
    std::thread writer ([&]
    {
        for (int i = 0; i < 10000; ++i)
        {
            bw::CurveExchange::Curves update;
            for (auto& c : update)
                for (int point = 0; point < c.count; ++point) c.points[(size_t) point].y = i / 10000.0f;
            threaded.publish (update);
        }
        finished.store (true, std::memory_order_release);
    });
    do
    {
        threaded.consume (read);
        assert (read[0].valid() && read[1].valid());
        assert (read[0].points[0].y == read[1].points[(size_t) read[1].count - 1].y);
    } while (! finished.load (std::memory_order_acquire));
    writer.join(); threaded.consume (read);
    assert (read[0].points[0].y == 9999 / 10000.0f);
}

void testTableFade (const bw::WavetableBank& bank)
{
    for (double rate : { 8000.0, 48000.0, 192000.0 })
    {
        bw::TableOscillator one (bank), eight (bank);
        one.prepare (rate); eight.prepare (rate);
        one.reset (0); eight.reset (0); one.setTable (6); eight.setTable (6);
        const int length = (int) std::ceil (rate * 0.015);
        for (int sample = 0; sample <= length + 2; ++sample)
        {
            float expected = one.readCurrent (.123, .45f, 0);
            for (int voice = 0; voice < 8; ++voice)
                assert (eight.readCurrent (.123, .45f, 0) == expected);
            if (sample == 0) assert (expected == bank[0].read (.123, .45f, 0));
            if (sample >= length) assert (expected == bank[6].read (.123, .45f, 0));
            one.advance(); eight.advance();
        }
    }
}

void route (WaveProcessor& p, int slot, int source, int destination, float amount, bool bipolar = false)
{
    const auto prefix = "mod" + juce::String (slot);
    set (p, prefix + "Source", (float) source); set (p, prefix + "Destination", (float) destination);
    set (p, prefix + "Amount", amount); set (p, prefix + "Bipolar", bipolar ? 1.0f : 0.0f);
    set (p, prefix + "Enabled", 1);
}

void testMatrix (WaveProcessor& p)
{
    initialise (p);
    set (p, "level", 0.25f);
    const auto base = render (p);
    set (p, "macro1", 0.25f); route (p, 1, 3, 1, 1);
    const auto twice = render (p);
    assert (std::abs (energy (twice[0]) / energy (base[0]) - 4) < 0.02);
    assert (p.apvts.getRawParameterValue ("level")->load() == 0.25f);
    route (p, 2, 3, 1, -1);
    const auto cancel = render (p);
    assert (difference (cancel[0], base[0], 4096) < 2.0e-5);
    set (p, "mod2Enabled", 0);
    set (p, "macro1", 0.5f); set (p, "mod1Bipolar", 1);
    assert (difference (render (p)[0], base[0], 4096) < 2.0e-5);
    set (p, "macro1", 0); // Bipolar -1 clamps level at zero.
    assert (energy (render (p)[0], 4096) < 1.0e-12);
    set (p, "macro1", 1); // Bipolar +1 clamps at one.
    const auto clamped = render (p);
    assert (std::abs (energy (clamped[0]) / energy (base[0]) - 16) < 0.05);
    set (p, "mod1Enabled", 0); set (p, "level", 0.5f);

    // Route edits and macro automation while a note is held must remain bounded.
    p.prepareToPlay (48000, 64);
    float previous = 0, maximumJump = 0;
    for (int block = 0; block < 300; ++block)
    {
        if (block == 40) route (p, 1, 3, 1, 1);
        if (block == 80) set (p, "macro1", 0);
        if (block == 120) { set (p, "macro1", 1); set (p, "mod1Destination", 2); }
        if (block == 160) set (p, "mod1Source", 8);
        if (block == 200) set (p, "mod1Enabled", 0);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        p.processBlock (audio, midi);
        for (int i = 0; i < 64; ++i)
        {
            const float value = audio.getSample (0, i);
            assert (std::isfinite (value));
            maximumJump = std::max (maximumJump, std::abs (value - previous)); previous = value;
        }
    }
    assert (maximumJump < 0.05f);

    set (p, "macro1", 1); route (p, 1, 3, 1, 1);
    p.prepareToPlay (48000, 64);
    previous = 0; maximumJump = 0;
    for (int block = 0; block < 180; ++block)
    {
        if (block == 40) set (p, "oscAOn", 0);
        if (block == 100) set (p, "oscAOn", 1);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        p.processBlock (audio, midi);
        for (int i = 0; i < 64; ++i)
        {
            const float value = audio.getSample (0, i);
            maximumJump = std::max (maximumJump, std::abs (value - previous)); previous = value;
            if (block >= 70 && block < 100) assert (std::abs (value) < 1.0e-6f);
        }
    }
    assert (maximumJump < .05f);
    set (p, "mod1Enabled", 0);
}

void testLiveCurves (WaveProcessor& p)
{
    set (p, "level", 0); set (p, "lfo1Rate", 1); set (p, "lfo1Mode", 0);
    auto low = bw::LfoCurve(); low.points[0].y = low.points[1].y = 0;
    auto high = low; high.points[0].y = high.points[1].y = 1;
    p.setLfoCurve (0, low); route (p, 1, 1, 1, 1);
    p.prepareToPlay (48000, 64);
    float previous = 0, jump = 0;
    double finalEnergy = 0;
    for (int block = 0; block < 260; ++block)
    {
        if (block == 40) p.setLfoCurve (0, high);
        if (block == 100) p.setLfoCurve (0, low);
        if (block >= 130 && block < 180) p.setLfoCurve (0, block % 2 ? low : high);
        if (block == 180) p.setLfoCurve (0, high);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        p.processBlock (audio, midi);
        for (int i = 0; i < 64; ++i)
        {
            const float value = audio.getSample (0, i);
            assert (std::isfinite (value));
            jump = std::max (jump, std::abs (value - previous)); previous = value;
            if (block < 40) assert (std::abs (value) < 1.0e-6f);
            if (block >= 230) finalEnergy += value * value;
        }
    }
    assert (jump < .05f);
    assert (finalEnergy / (30 * 64) > .01); // Final requested snapshot settles to full level.
    p.setLfoCurve (0, bw::LfoCurve::factory (0));
    set (p, "mod1Enabled", 0); set (p, "level", .5f);
}

float phaseAfter (WaveProcessor& p, int mode, bool sync, int blockSize, int noteAt, Host* host = nullptr)
{
    set (p, "lfo1Mode", (float) mode); set (p, "lfo1Sync", sync ? 1.0f : 0.0f);
    set (p, "lfo1Rate", 2); set (p, "lfo1Division", 12); set (p, "internalBpm", 120);
    p.setPlayHead (host); p.prepareToPlay (48000, blockSize);
    for (int offset = 0; offset < 6000; offset += blockSize)
    {
        int n = std::min (blockSize, 6000 - offset);
        juce::AudioBuffer<float> audio (2, n); juce::MidiBuffer midi;
        if (noteAt >= offset && noteAt < offset + n)
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), noteAt - offset);
        if (host != nullptr) host->ppq = offset / 48000.0 * host->bpm / 60.0;
        p.processBlock (audio, midi);
    }
    p.setPlayHead (nullptr);
    return p.lfoDisplayPhase[0].load();
}

void testLfo (WaveProcessor& p)
{
    assert (std::abs (phaseAfter (p, 0, false, 64, 0) - .25f) < .002f);
    assert (std::abs (phaseAfter (p, 0, false, 257, 3000) - .125f) < .002f);
    assert (std::abs (phaseAfter (p, 1, false, 64, 3000) - .25f) < .002f);
    assert (std::abs (phaseAfter (p, 2, true, 257, 3000) - .25f) < .002f); // No host: internal BPM.
    Host host; host.bpm = 90;
    assert (std::abs (phaseAfter (p, 2, true, 64, 3000, &host) - .1875f) < .002f);
    host.bpm = 180;
    assert (std::abs (phaseAfter (p, 2, true, 257, 3000, &host) - .375f) < .002f);
    host.bpm = 120; host.ppq = .73; host.playing = false; p.setPlayHead (&host);
    juce::AudioBuffer<float> transportAudio (2, 128); juce::MidiBuffer noMidi;
    p.processBlock (transportAudio, noMidi);
    assert (std::abs (p.lfoDisplayPhase[0].load() - .73f) < 1.0e-5f);
    host.ppq = 8.2; host.playing = true;
    p.processBlock (transportAudio, noMidi);
    assert (std::abs (p.lfoDisplayPhase[0].load() - (float) (.2 + 127 * 2.0 / 48000)) < 1.0e-5f);
    p.setPlayHead (nullptr);
    // Audio output must be independent of host block partitioning.
    set (p, "lfo1Mode", 0); set (p, "lfo1Sync", 0); set (p, "lfo1Rate", 3.7f);
    route (p, 1, 1, 1, .2f, true);
    const auto small = render (p, 12000, 64);
    const auto odd = render (p, 12000, 257);
    assert (difference (small[0], odd[0]) < 2.0e-5);
    set (p, "mod1Enabled", 0);
}

double frequency (const std::vector<float>& audio)
{
    std::vector<double> crossings;
    for (size_t i = 2049; i < audio.size(); ++i)
        if (audio[i - 1] <= 0 && audio[i] > 0)
            crossings.push_back ((double) i - audio[i] / (double) (audio[i] - audio[i - 1]));
    assert (crossings.size() > 2);
    return (crossings.size() - 1) * 48000.0 / (crossings.back() - crossings.front());
}

void testUnison (WaveProcessor& p)
{
    const auto single = render (p);
    set (p, "oscADetune", 50); set (p, "oscASpread", 1);
    const auto stillSingle = render (p);
    assert (difference (single[0], stillSingle[0]) < 1.0e-6);
    set (p, "oscAUnison", 2);
    const auto pair = render (p, 48000);
    assert (std::abs (frequency (pair[0]) - 440 * std::exp2 (-50.0 / 1200.0)) < .2);
    assert (std::abs (frequency (pair[1]) - 440 * std::exp2 (50.0 / 1200.0)) < .2);
    assert (difference (pair[0], pair[1], 4096) > .05);
    set (p, "oscASpread", 0);
    const auto centred = render (p);
    assert (difference (centred[0], centred[1]) < 1.0e-6);
    set (p, "oscAUnison", 8); set (p, "oscASpread", 1);
    const auto eight = render (p);
    assert (energy (eight[0]) > 1.0e-6);
    assert (energy (eight[0]) < energy (single[0]) * 1.1);
    assert (energy (eight[1]) < energy (single[1]) * 1.1);

    // Mono fold-down matches stereo (L+R)/2 for identical deterministic voices.
    auto layout = p.getBusesLayout(); layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    const bool monoAccepted = p.setBusesLayout (layout); assert (monoAccepted);
    p.prepareToPlay (48000, 128);
    size_t offset = 0;
    while (offset < eight[0].size())
    {
        const int n = std::min (128, (int) (eight[0].size() - offset));
        juce::AudioBuffer<float> audio (1, n); juce::MidiBuffer midi;
        if (offset == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        p.processBlock (audio, midi);
        for (int i = 0; i < n; ++i)
        {
            const auto index = offset + (size_t) i;
            assert (std::abs (audio.getSample (0, i) - (eight[0][index] + eight[1][index]) * .5f) < 2.0e-5f);
        }
        offset += (size_t) n;
    }
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    const bool stereoAccepted = p.setBusesLayout (layout); assert (stereoAccepted);

    p.prepareToPlay (48000, 64);
    float previous = 0, jump = 0;
    for (int block = 0; block < 200; ++block)
    {
        if (block == 40) set (p, "oscAUnison", 1);
        if (block == 80) set (p, "oscAUnison", 8);
        if (block == 120) set (p, "oscAUnison", 3);
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        p.processBlock (audio, midi);
        for (int i = 0; i < 64; ++i)
        {
            const float value = audio.getSample (0, i);
            jump = std::max (jump, std::abs (value - previous)); previous = value;
        }
    }
    assert (jump < .05f);
    set (p, "oscAUnison", 1); set (p, "oscADetune", 0); set (p, "oscASpread", 0);
}

void testState (WaveProcessor& p)
{
    auto custom = bw::LfoCurve::factory (1); custom.points[1].x = .3f; custom.points[0].curve = .7f;
    p.setLfoCurve (0, custom); p.setMacroName (0, "Brightness");
    set (p, "macro1", .73f); route (p, 16, 3, 12, -.42f, true); set (p, "oscBUnison", 7);
    juce::MemoryBlock saved; p.getStateInformation (saved);
    p.setLfoCurve (0, bw::LfoCurve::factory (4)); p.setMacroName (0, "Changed");
    set (p, "macro1", 0); set (p, "mod16Enabled", 0); set (p, "oscBUnison", 1);
    p.setStateInformation (saved.getData(), (int) saved.getSize());
    assert (p.getLfoCurve (0).count == 3 && p.getLfoCurve (0).points[1].x == .3f);
    assert (p.getLfoCurve (0).points[0].curve == .7f);
    assert (p.getMacroName (0) == "Brightness");
    assert (std::abs (p.apvts.getRawParameterValue ("macro1")->load() - .73f) < 1.0e-6f);
    assert (p.apvts.getRawParameterValue ("mod16Enabled")->load() == 1);
    assert (p.apvts.getRawParameterValue ("oscBUnison")->load() == 7);
    // A pre-modulation APVTS state must not inherit current modulation or unison.
    auto legacy = p.apvts.copyState();
    for (int i = legacy.getNumChildren(); --i >= 0;)
    {
        auto child = legacy.getChild (i); auto id = child.getProperty ("id").toString();
        if (! child.hasType ("PARAM") || id.startsWith ("mod") || id.startsWith ("macro")
            || id.startsWith ("lfo") || id == "internalBpm" || id.endsWith ("Unison")
            || id.endsWith ("Detune") || id.endsWith ("Spread") || id.endsWith ("Phase") || id.endsWith ("Random"))
            legacy.removeChild (i, nullptr);
    }
    auto xml = legacy.createXml(); juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    p.setStateInformation (old.getData(), (int) old.getSize());
    assert (p.apvts.getRawParameterValue ("oscBUnison")->load() == 1);
    assert (p.apvts.getRawParameterValue ("mod16Enabled")->load() == 0);
    assert (p.apvts.getRawParameterValue ("macro1")->load() == 0);
    assert (p.getMacroName (0) == "Macro 1");
    assert (p.getLfoCurve (0).count == bw::LfoCurve::factory (0).count);
    auto invalid = custom; invalid.points[1].x = 0; p.setLfoCurve (0, invalid);
    assert (p.getLfoCurve (0).valid());
}

void stress (WaveProcessor& p, bool benchmark)
{
    set (p, "oscBOn", 1); set (p, "oscAUnison", 8); set (p, "oscBUnison", 8);
    set (p, "oscADetune", 50); set (p, "oscBDetune", 50);
    set (p, "oscASpread", 1); set (p, "oscBSpread", 1); set (p, "gain", -30);
    for (int i = 1; i <= 16; ++i) route (p, i, i % 8 + 1, i - 1, .5f, true);
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        constexpr int blockSize = 128;
        const int blocks = benchmark ? (int) rate / blockSize : 80;
        set (p, "oscAUnison", 8);
        p.prepareToPlay (rate, blockSize);
        juce::AudioBuffer<float> audio (2, blockSize); juce::MidiBuffer midi;
        const auto start = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
        {
            midi.clear();
            if (block == 0)
                for (int note = 48; note < 64; ++note)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, .7f), 0);
            if (block % 13 == 0)
            {
                set (p, "wtTable", (float) (block % 8)); set (p, "wtTableB", (float) ((block + 3) % 8));
                if (! benchmark) set (p, "oscAUnison", (float) ((block / 13) % 8 + 1));
                set (p, "macro1", (float) (block % 2)); set (p, "oscBPitch", block % 2 ? -24.0f : 24.0f);
            }
            p.processBlock (audio, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    float value = audio.getSample (ch, i);
                    if (! std::isfinite (value) || std::abs (value) > 16) std::abort();
                }
        }
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (benchmark) std::cout << "benchmark " << rate << " Hz: " << seconds << " s wall / "
                                 << blocks * blockSize / rate << " s audio, "
                                 << seconds * rate / (blocks * blockSize) * 100 << "% single-core budget\n";
    }
}

void benchmarkProfiles()
{
    struct Profile { const char* name; int notes, unison, routes; bool effects; };
    constexpr Profile profiles[] {
        { "light", 1, 1, 0, false },
        { "layered", 4, 2, 4, false },
        { "extreme", 16, 8, 16, false },
        { "extreme_fx", 16, 8, 16, true }
    };
    constexpr int blockSize = 128;
    for (const auto& profile : profiles)
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            WaveProcessor p;
            initialise (p);
            set (p, "oscBOn", profile.notes == 1 ? 0.0f : 1.0f);
            set (p, "oscAUnison", (float) profile.unison);
            set (p, "oscBUnison", (float) profile.unison);
            set (p, "oscADetune", 50); set (p, "oscBDetune", 50);
            set (p, "oscASpread", 1); set (p, "oscBSpread", 1);
            set (p, "gain", -30);
            for (int i = 1; i <= profile.routes; ++i)
                route (p, i, i % 8 + 1, i - 1, .5f, true);
            if (profile.effects)
            {
                set (p, "distOn", 1); set (p, "distMix", .5f);
                set (p, "delayOn", 1); set (p, "delayMix", .3f);
                set (p, "reverbOn", 1); set (p, "reverbMix", .3f);
            }
            p.prepareToPlay (rate, blockSize);
            juce::AudioBuffer<float> audio (2, blockSize);
            juce::MidiBuffer midi;
            const int blocks = (int) rate / blockSize;
            const double deadline = blockSize / rate;
            double total = 0, worst = 0;
            float peak = 0;
            int overBudget = 0;
            for (int block = 0; block < blocks; ++block)
            {
                midi.clear();
                if (block == 0)
                    for (int note = 48; note < 48 + profile.notes; ++note)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, .7f), 0);
                if (block % 13 == 0)
                {
                    set (p, "wtTable", (float) (block % 8));
                    set (p, "wtTableB", (float) ((block + 3) % 8));
                    set (p, "macro1", (float) (block % 2));
                    set (p, "oscBPitch", block % 2 ? -24.0f : 24.0f);
                }
                const auto start = std::chrono::steady_clock::now();
                p.processBlock (audio, midi);
                const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                total += elapsed;
                worst = std::max (worst, elapsed);
                if (elapsed > deadline) ++overBudget;
                for (int ch = 0; ch < 2; ++ch)
                    for (int sample = 0; sample < blockSize; ++sample)
                    {
                        const float value = audio.getSample (ch, sample);
                        if (! std::isfinite (value)) std::abort();
                        peak = std::max (peak, std::abs (value));
                    }
            }
            if (peak <= 1.0e-6f) std::abort();
            const double audioTime = blocks * deadline;
            std::cout << "benchmark profile=" << profile.name << " rate=" << rate
                      << " block=" << blockSize << " notes=" << profile.notes
                      << " unison=" << profile.unison << " routes=" << profile.routes
                      << " effects=" << profile.effects << " process_s=" << total
                      << " audio_s=" << audioTime << " budget_pct=" << total / audioTime * 100
                      << " worst_block_pct=" << worst / deadline * 100
                      << " over_budget_blocks=" << overBudget << "/" << blocks << '\n';
        }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool benchmark = argc > 1 && juce::String (argv[1]) == "--benchmark";
    if (benchmark) { benchmarkProfiles(); return 0; }
    WaveProcessor processor;
    initialise (processor);
    testCurves(); testTableFade (processor.wavetableBank);
    testMatrix (processor); testLfo (processor); testLiveCurves (processor); testUnison (processor); testState (processor);
    stress (processor, false);
    std::cout << "ok: curves, table-fade timing, matrix polarity/sum/clamp, live edits, LFO timing/block partitions, "
                 "unison tuning/stereo/mono, state migration, 16-note stress at 8-192 kHz\n";
}
