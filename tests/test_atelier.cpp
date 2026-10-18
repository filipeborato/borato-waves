#include "PluginProcessor.h"
#include <cassert>
#include <cstdio>

using namespace juce;

namespace
{
void set (WaveProcessor& p, const String& id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    assert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct Render
{
    AudioBuffer<float> audio;
    double peak = 0, rms = 0, tail = 0;
};

Render play (WaveProcessor& p, const String& category, double rate, float velocity, bool fullTail)
{
    constexpr int block = 256;
    const bool bass = category == "BASS", pad = category == "PAD";
    const int root = bass ? 36 : 60;
    const double duration = fullTail ? 16.0 : 4.0;
    const int total = (int) (rate * duration);
    Render result;
    result.audio.setSize (2, total);
    result.audio.clear ();
    p.prepareToPlay (rate, block);
    for (int start = 0; start < total; start += block)
    {
        const int count = jmin (block, total - start);
        AudioBuffer<float> buffer (result.audio.getArrayOfWritePointers (), 2, start, count);
        MidiBuffer midi;
        auto event = [&] (double seconds, const MidiMessage& message)
        {
            const int sample = (int) (seconds * rate);
            if (sample >= start && sample < start + count) midi.addEvent (message, sample - start);
        };
        // Low and high notes, then a chord; pads get time to develop.
        for (int phrase = 0; phrase < 3; ++phrase)
        {
            const double time = phrase == 2 ? 1.3 : phrase * .55;
            const int note = root + (phrase == 0 ? -12 : phrase == 1 ? 12 : 0);
            for (int interval : { 0, 4, 7 })
            {
                if (phrase < 2 && interval != 0) continue;
                event (time, MidiMessage::noteOn (1, note + interval, velocity));
                event (time + (pad ? 1.5 : .42), MidiMessage::noteOff (1, note + interval));
            }
        }
        p.processBlock (buffer, midi);
    }
    double sum = 0;
    const int active = jmin (total, (int) (3 * rate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float sample = result.audio.getSample (ch, i);
            assert (std::isfinite (sample));
            result.peak = jmax (result.peak, (double) std::abs (sample));
            if (i < active) sum += (double) sample * sample;
        }
    result.rms = std::sqrt (sum / (2 * active));
    for (int ch = 0; ch < 2; ++ch)
        result.tail = jmax (result.tail, (double) result.audio.getRMSLevel (ch, total - (int) (rate * .25), (int) (rate * .25)));
    return result;
}

void preview (const File& directory, const String& name, Render& render)
{
    // Listening copies only: match active RMS to -23 dBFS, bounded by -1 dBFS peak.
    const float scale = (float) jmin (Decibels::decibelsToGain (-23.0) / jmax (render.rms, 1.0e-9),
                                     Decibels::decibelsToGain (-1.0) / jmax (render.peak, 1.0e-9));
    render.audio.applyGain (scale);
    const auto file = directory.getChildFile (File::createLegalFileName (name) + ".wav");
    auto fileStream = file.createOutputStream ();
    assert (fileStream != nullptr && fileStream->setPosition (0) && fileStream->truncate ().wasOk ());
    std::unique_ptr<OutputStream> stream = std::move (fileStream);
    WavAudioFormat format;
    auto writer = format.createWriterFor (stream, AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    assert (writer != nullptr);
    assert (writer->writeFromAudioSampleBuffer (render.audio, 0, render.audio.getNumSamples ()));
}
}

int main (int argc, char** argv)
{
   #if JUCE_WINDOWS
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    const File output = argc > 1 ? File (String (argv[1])) : File ();
    if (output != File ()) assert (output.createDirectory ());
    WaveProcessor p;
    const auto& presets = bw::factoryPresets ();
    int checked = 0;
    for (int index = 0; index < (int) presets.size (); ++index)
    {
        const auto& preset = presets[(size_t) index];
        if (! String (preset.name).startsWith ("Atelier - ")) continue;
        ++checked;
        assert (preset.routes.size () <= 16 && preset.curve (0).valid () && preset.curve (1).valid ());
        const auto& table = *preset.tables[0];
        for (int f = 0; f < table.numFrames; ++f)
            for (int level = 0; level < bw::Wavetable::kLevels; ++level)
            {
                const auto* cycle = table.ptr (f, level);
                double dc = 0;
                for (int s = 0; s < bw::Wavetable::kSize; ++s)
                {
                    assert (std::isfinite (cycle[s]));
                    dc += cycle[s];
                }
                assert (std::abs (dc / bw::Wavetable::kSize) < 1.0e-6 && cycle[0] == cycle[bw::Wavetable::kSize]);
            }
        for (const auto& value : preset.values)
        {
            const auto* parameter = p.apvts.getParameter (value.id);
            assert (parameter != nullptr);
            const auto range = parameter->getNormalisableRange ();
            assert (value.value >= range.start && value.value <= range.end);
        }
        p.loadFactoryPreset (index);
        assert (p.matchingFactoryPreset () == index);
        MemoryBlock saved;
        p.getStateInformation (saved);
        WaveProcessor restored;
        restored.setStateInformation (saved.getData (), (int) saved.getSize ());
        assert (restored.matchingFactoryPreset () == index);
        assert (restored.userTableName (0) == preset.tableNames[0]);
        // Equal parameters with a different embedded table must not match a factory patch.
        restored.setUserTable (0, std::make_shared<bw::Wavetable> (bw::Wavetable::makeBasic ()), "Different");
        assert (restored.matchingFactoryPreset () == -1);
        restored.loadFactoryPreset (0);
        assert (restored.userTable (0) == nullptr && restored.matchingFactoryPreset () == 0);

        double worst = 0;
        // Each rate once, with velocity and the simultaneous macro position stepping alongside.
        const double rates[] { 44100, 48000, 96000 };
        const float velocities[] { .25f, .65f, 1.0f }, positions[] { 0, .5f, 1 };
        for (int r = 0; r < 3; ++r)
        {
            p.loadFactoryPreset (index);
            for (int m = 1; m <= 4; ++m) set (p, "macro" + String (m), positions[(2 * r) % 3]); // 0, 1, 0.5
            const auto result = play (p, preset.category, rates[r], velocities[r], false);
            worst = jmax (worst, result.peak);
            if (result.peak >= .98) std::printf ("OVER: %s rate=%.0f velocity=%.2f peak=%.4f\n", preset.name, rates[r], velocities[r], result.peak);
            assert (result.peak < .98 && result.rms > 1.0e-5);
        }
        for (int m = 1; m <= 4; ++m)
        {
            bool routed = false;
            for (const auto& route : preset.routes) routed |= route.source == m + 2 && route.amount != 0;
            assert (routed);
            double firstRms = 0;
            for (float position : { 0.0f, 1.0f }) // the endpoints
            {
                // Equal PRNG history: noise drift must not masquerade as macro response.
                WaveProcessor macroProcessor;
                macroProcessor.loadFactoryPreset (index);
                set (macroProcessor, "macro" + String (m), position);
                const auto result = play (macroProcessor, preset.category, 48000, .8f, false);
                worst = jmax (worst, result.peak);
                assert (result.peak < .98);
                if (position == 0) firstRms = result.rms;
                if (position == 1) assert (std::abs (result.rms - firstRms) > 1.0e-8);
            }
        }
        p.loadFactoryPreset (index);
        auto wet = play (p, preset.category, 48000, .8f, true);
        assert (wet.tail < .001);
        if (output != File ())
        {
            // The dry render is only a listening preview: the wet tail above covers the voice tail too.
            p.loadFactoryPreset (index);
            set (p, "distOn", 0); set (p, "delayOn", 0); set (p, "reverbOn", 0);
            auto dry = play (p, preset.category, 48000, .8f, true);
            preview (output, String (preset.name) + " - wet", wet);
            preview (output, String (preset.name) + " - dry", dry);
            p.loadFactoryPreset (index);
            const auto file = output.getChildFile (String (preset.name) + ".bwpreset");
            assert (p.savePreset (file) && restored.loadPreset (file));
            assert (restored.matchingFactoryPreset () == index);
        }
        std::printf ("%s: peak %.2f dBFS; tail %.1f dBFS; state, curves, table and macros OK\n", preset.name,
                     Decibels::gainToDecibels (worst), Decibels::gainToDecibels (wet.tail, -120.0));
        std::fflush (stdout);
    }
    assert (checked == 12);
    // Reject malformed embedded audio before FFT; never feed NaN/Inf into playback.
    p.loadFactoryPreset ((int) presets.size () - 1);
    MemoryBlock state;
    p.getStateInformation (state);
    auto xml = AudioProcessor::getXmlFromBinary (state.getData (), (int) state.getSize ());
    auto* entry = xml->getChildByName ("USERTABLES")->getChildByName ("TABLE");
    MemoryBlock bytes;
    assert (bytes.fromBase64Encoding (entry->getStringAttribute ("data")));
    const float invalid = std::numeric_limits<float>::quiet_NaN ();
    bytes.copyFrom (&invalid, 0, sizeof (invalid));
    entry->setAttribute ("data", bytes.toBase64Encoding ());
    AudioProcessor::copyXmlToBinary (*xml, state);
    p.setStateInformation (state.getData (), (int) state.getSize ());
    assert (p.userTable (0) == nullptr && p.matchingFactoryPreset () == -1);
    std::printf ("ok: %d Atelier patches\n", checked);
}
