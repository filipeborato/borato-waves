#include "PluginProcessor.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace juce;

namespace
{
constexpr double rate = 48000;
constexpr int block = 512;

struct Render
{
    AudioBuffer<float> audio;
    double peak = 0, rms = 0, brightness = 0, tail = 0;
};

// Chord C3-E3-G3 for 1.5 s, 1.5 s release, then C2 for 1 s; 12 s of tail are rendered after that.
Render play (WaveProcessor& p)
{
    p.prepareToPlay (rate, block);
    const int total = (int) (rate * 16);
    Render r;
    r.audio.setSize (2, total);
    r.audio.clear();
    AudioBuffer<float> buffer (2, block);
    const auto at = [] (double seconds) { return (int) (seconds * rate); };
    double sum = 0, diff = 0; int count = 0;
    for (int start = 0; start + block <= total; start += block)
    {
        MidiBuffer midi;
        const auto event = [&] (double seconds, const MidiMessage& m) { if (at (seconds) >= start && at (seconds) < start + block) midi.addEvent (m, at (seconds) - start); };
        for (int note : { 48, 52, 55 }) { event (0, MidiMessage::noteOn (1, note, .8f)); event (1.5, MidiMessage::noteOff (1, note)); }
        event (3, MidiMessage::noteOn (1, 36, .8f)); event (4, MidiMessage::noteOff (1, 36));
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch) r.audio.copyFrom (ch, start, buffer, ch, 0, block);
    }
    for (int i = 0; i < total; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = r.audio.getSample (ch, i);
            assert (std::isfinite (x));
            r.peak = jmax (r.peak, (double) std::abs (x));
            if (i < at (4))
            {
                sum += (double) x * x; ++count;
                if (i > 0) { const double d = x - r.audio.getSample (ch, i - 1); diff += d * d; }
            }
        }
    r.rms = std::sqrt (sum / count);
    r.brightness = std::sqrt (diff / jmax (1.0e-20, sum)); // rises with high-frequency content
    for (int ch = 0; ch < 2; ++ch)
        r.tail = jmax (r.tail, (double) r.audio.getRMSLevel (ch, total - at (0.25), at (0.25)));
    return r;
}
double db (double gain) { return Decibels::gainToDecibels (gain, -120.0); }

bool checkPerformance (int index, double sampleRate, float velocity, float macro)
{
    WaveProcessor p;
    p.loadFactoryPreset (index);
    for (int m = 1; m <= 4; ++m)
        p.apvts.getParameter ("macro" + String (m))->setValueNotifyingHost (macro);
    p.prepareToPlay (sampleRate, block);
    AudioBuffer<float> buffer (2, block);
    double energy = 0, peak = 0;
    const int total = (int) (sampleRate * 3);
    for (int start = 0; start < total; start += block)
    {
        MidiBuffer midi;
        if (start == 0)
            for (int note : { 36, 60, 67 }) midi.addEvent (MidiMessage::noteOn (1, note, velocity), 0);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < block; ++s)
            {
                const double x = buffer.getSample (ch, s);
                assert (std::isfinite (x));
                energy += x * x;
                peak = jmax (peak, std::abs (x));
            }
    }
    // Closed tone macros and soft velocities can intentionally be very quiet.
    if (peak >= .98 || energy <= 1.0e-10)
        std::printf ("PERFORMANCE FAIL: %s rate=%.0f velocity=%.2f macros=%.1f peak=%.5f energy=%.8g\n",
                     bw::factoryPresets()[(size_t) index].name, sampleRate, velocity, macro, peak, energy);
    return peak < .98 && energy > 1.0e-10;
}

void writeWav (const File& file, const AudioBuffer<float>& audio)
{
    file.deleteFile();
    std::unique_ptr<OutputStream> stream (file.createOutputStream().release());
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
    assert (writer != nullptr);
    stream.release();
    // Trim the silent part of the tail for listening.
    int length = audio.getNumSamples();
    while (length > (int) rate * 5 && audio.getMagnitude (length - block, block) < 1.0e-4f) length -= block;
    assert (writer->writeFromAudioSampleBuffer (audio, 0, length));
}
}

int main (int argc, char** argv)
{
   #if JUCE_WINDOWS
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    const File out = argc > 1 ? File (String (argv[1])) : File();
    if (out != File()) out.createDirectory();
    const auto& presets = bw::factoryPresets();
    assert (presets.size() >= 45);
    WaveProcessor p;
    StringArray names;
    std::printf ("%-22s %-6s %8s %8s %8s %8s\n", "preset", "cat", "peak dB", "rms dB", "bright", "tail dB");
    bool ok = true;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& preset = presets[(size_t) i];
        assert (String (preset.name).isNotEmpty() && ! names.contains (preset.name));
        names.add (preset.name);
        assert (String (preset.category).isNotEmpty() && String (preset.tags).isNotEmpty() && String (preset.description).isNotEmpty());
        assert (preset.routes.size() <= 16 && preset.curve (0).valid() && preset.curve (1).valid());
        for (const auto& value : preset.values)
        {
            const auto* parameter = p.apvts.getParameter (value.id);
            assert (parameter != nullptr && std::isfinite (value.value));
            const auto range = parameter->getNormalisableRange();
            assert (value.value >= range.start && value.value <= range.end);
        }
        for (const auto& route : preset.routes)
        {
            assert (route.source > 0 && route.source < bw::kSources);
            assert (route.destination >= 0 && route.destination < bw::kDestinations);
            assert (std::isfinite (route.amount) && std::abs (route.amount) <= 1);
            if (route.destination >= bw::kFirstGlobalDestination && route.destination < bw::kFirstGlobalDestination + bw::kGlobalDestinations)
                assert (route.source >= 3 && route.source <= 6);
        }
        for (int m = 0; m < 4; ++m)
        {
            assert (String (preset.macros[(size_t) m]).isNotEmpty());
            bool routed = false;
            for (const auto& route : preset.routes) routed |= route.source == m + 3 && route.amount != 0;
            if (String (preset.category) != "INIT") assert (routed);
        }
        p.loadFactoryPreset (i);
        // Every preset is reachable and distinct: the state matches exactly this preset.
        assert (p.matchingFactoryPreset() == i);
        MemoryBlock saved;
        p.getStateInformation (saved);
        WaveProcessor restored;
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        assert (restored.matchingFactoryPreset() == i);
        const auto r = play (p);
        const bool loud = db (r.rms) > -36 && db (r.rms) < -10, safe = r.peak < .98, quiet = db (r.tail) < -60;
        std::printf ("%-22s %-6s %8.1f %8.1f %8.3f %8.1f %s\n", presets[(size_t) i].name, presets[(size_t) i].category,
                     db (r.peak), db (r.rms), r.brightness, db (r.tail), loud && safe && quiet ? "" : "<-- FAIL");
        ok = ok && loud && safe && quiet;
        if (out != File())
        {
            writeWav (out.getChildFile (File::createLegalFileName (String (presets[(size_t) i].category) + " - " + presets[(size_t) i].name + ".wav")), r.audio);
            const auto file = out.getChildFile (File::createLegalFileName (String (preset.category) + " - " + preset.name + ".bwpreset"));
            assert (p.savePreset (file) && restored.loadPreset (file));
            assert (restored.matchingFactoryPreset() == i);
        }
        // A bounded sweep covers both simultaneous macro endpoints and three rates/velocities.
        ok = checkPerformance (i, 44100, .25f, 0) && ok;
        ok = checkPerformance (i, 48000, 1.0f, 1) && ok;
        ok = checkPerformance (i, 96000, .65f, .5f) && ok;
        std::fflush (stdout);
    }
    assert (ok);

    // Env 2 closes the filter during a Rubber Floor note: brightness falls after the attack.
    int rubber = -1;
    for (int i = 0; i < (int) presets.size(); ++i) if (String (presets[(size_t) i].name) == "Rubber Floor") rubber = i;
    assert (rubber >= 0);
    p.loadFactoryPreset (rubber);
    const auto r = play (p);
    const auto brightness = [&] (double from, double to)
    {
        double s = 0, d = 0;
        for (int i = (int) (from * rate) + 1; i < (int) (to * rate); ++i) { const double x = r.audio.getSample (0, i); s += x * x; d += std::pow (x - r.audio.getSample (0, i - 1), 2); }
        return std::sqrt (d / s);
    };
    assert (brightness (0.005, 0.05) > 2 * brightness (1.0, 1.4));

    // Macro 4 (Space) raises the global reverb mix.
    p.loadFactoryPreset (rubber);
    const double dry = play (p).tail;
    p.apvts.getParameter ("macro4")->setValueNotifyingHost (1);
    const auto wet = play (p);
    assert (wet.audio.getRMSLevel (0, (int) (4.6 * rate), (int) (0.4 * rate)) > 10 * (dry + 1.0e-9));

    // State round trip keeps a factory preset recognisable.
    p.loadFactoryPreset (rubber);
    MemoryBlock state; p.getStateInformation (state);
    WaveProcessor restored; restored.setStateInformation (state.getData(), (int) state.getSize());
    assert (restored.matchingFactoryPreset() == rubber);
    std::printf ("ok: %d factory presets; metadata, routes, state, three rates/velocities, macro endpoints, loudness, headroom, tails, Env 2 and macro FX\n", (int) presets.size());
}
