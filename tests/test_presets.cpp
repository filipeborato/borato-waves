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

// Waits for the background sample load and, for spectral presets, the STFT analysis.
void settle (WaveProcessor& p)
{
    assert (p.waitForSampleLoad (60000));
    for (int waited = 0; p.spectralState() == WaveProcessor::SpectralState::analyzing && waited < 60000; waited += 5)
        Thread::sleep (5);
    assert (p.spectralState() != WaveProcessor::SpectralState::analyzing);
}

// Chord C3-E3-G3 for 1.5 s, 1.5 s release, then C2 for 1 s; 12 s of tail are rendered after that.
Render play (WaveProcessor& p)
{
    p.prepareToPlay (rate, block);
    settle (p);
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
    settle (p);
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
    // Closed tone macros and soft velocities can intentionally be very quiet. Sample presets keep -1 dBFS headroom.
    const auto& preset = bw::factoryPresets()[(size_t) index];
    const double limit = preset.sampleBank != nullptr || preset.sample != nullptr ? .891 : .98;
    if (peak >= limit || energy <= 1.0e-10)
        std::printf ("PERFORMANCE FAIL: %s rate=%.0f velocity=%.2f macros=%.1f peak=%.5f energy=%.8g\n",
                     bw::factoryPresets()[(size_t) index].name, sampleRate, velocity, macro, peak, energy);
    return peak < limit && energy > 1.0e-10;
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

std::vector<float> renderNote (WaveProcessor& p, int midiNote, uint8 velocity, int frames = 12000)
{
    std::vector<float> out ((size_t) frames, 0.0f);
    AudioBuffer<float> buffer (2, block);
    for (int start = 0; start < frames; start += block)
    {
        const int chunk = jmin (block, frames - start);
        AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, 0, chunk);
        MidiBuffer midi;
        if (start == 0)
            midi.addEvent (MidiMessage::noteOn (1, midiNote, velocity), 0);
        if (start <= frames / 2 && start + chunk > frames / 2)
            midi.addEvent (MidiMessage::noteOff (1, midiNote), frames / 2 - start);
        p.processBlock (view, midi);
        for (int i = 0; i < chunk; ++i)
        {
            const float x = 0.5f * (view.getSample (0, i) + view.getSample (1, i));
            assert (std::isfinite (x));
            out[(size_t) (start + i)] = x;
        }
    }
    return out;
}

double diffRms (const std::vector<float>& a, const std::vector<float>& b)
{
    assert (a.size() == b.size() && ! a.empty());
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double d = (double) a[i] - (double) b[i];
        sum += d * d;
    }
    return std::sqrt (sum / (double) a.size());
}

void checkSampleMultisamplePresets()
{
    const auto& presets = bw::factoryPresets();
    auto findPreset = [&] (const char* name) -> int
    {
        for (int i = 0; i < (int) presets.size(); ++i)
            if (String (presets[(size_t) i].name) == name)
                return i;
        return -1;
    };

    const int velvetIdx = findPreset ("Atelier Sample - Velvet Keys");
    const int pluckIdx  = findPreset ("Atelier Sample - Round Robin Pluck");
    const int sliceIdx  = findPreset ("Atelier Sample - Sliced Groove");
    assert (velvetIdx >= 0 && pluckIdx >= 0 && sliceIdx >= 0);

    // 1. Portable state & .bwpreset round-trip, asset memory budget, and macro responsiveness for all 3 Sample presets.
    const File tempDir = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-wave-sample-presets");
    tempDir.createDirectory();
    for (int idx : { velvetIdx, pluckIdx, sliceIdx })
    {
        const auto& preset = presets[(size_t) idx];
        assert (preset.sampleBank != nullptr && preset.sampleBank->valid());

        WaveProcessor p;
        p.loadFactoryPreset (idx);
        assert (p.matchingFactoryPreset() == idx);
        assert (p.sampleBank() != nullptr && p.sampleBank()->zones.size() == preset.sampleBank->zones.size());
        assert (p.sampleBank()->zones[0].sample->contentHash == preset.sampleBank->zones[0].sample->contentHash);
        assert (p.assetMemoryBytes() < 16u * 1024u * 1024u);

        // Every zone sounds at its key and velocity boundaries.
        p.prepareToPlay (rate, block);
        for (const auto& z : preset.sampleBank->zones)
            for (int key : { z.lowKey, z.highKey })
                for (int vel : { z.lowVel, z.highVel })
                {
                    const auto out = renderNote (p, key, (uint8) vel, 2048);
                    double e = 0;
                    for (float x : out) e += x * x;
                    if (e <= 1.0e-8) std::printf ("ZONE FAIL: %s key=%d vel=%d\n", preset.name, key, vel);
                    assert (e > 1.0e-8);
                }

        MemoryBlock state;
        p.getStateInformation (state);
        WaveProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        assert (restored.waitForSampleLoad (10000));
        assert (restored.matchingFactoryPreset() == idx);
        assert (restored.sampleBank() != nullptr && restored.sampleBank()->zones[0].sample->contentHash == preset.sampleBank->zones[0].sample->contentHash);

        const File presetFile = tempDir.getChildFile (File::createLegalFileName (String (preset.name) + ".bwpreset"));
        assert (p.savePreset (presetFile));
        WaveProcessor fromFile;
        assert (fromFile.loadPreset (presetFile));
        assert (fromFile.waitForSampleLoad (10000));
        assert (fromFile.matchingFactoryPreset() == idx);
        assert (fromFile.sampleBank() != nullptr && fromFile.sampleBank()->zones[0].sample->contentHash == preset.sampleBank->zones[0].sample->contentHash);

        for (int m = 1; m <= 4; ++m)
        {
            WaveProcessor p0, p1;
            p0.loadFactoryPreset (idx);
            p1.loadFactoryPreset (idx);
            p0.apvts.getParameter ("macro" + String (m))->setValueNotifyingHost (0.0f);
            p1.apvts.getParameter ("macro" + String (m))->setValueNotifyingHost (1.0f);
            p0.prepareToPlay (rate, block);
            p1.prepareToPlay (rate, block);
            const auto out0 = renderNote (p0, 60, (uint8) 100, 16000);
            const auto out1 = renderNote (p1, 60, (uint8) 100, 16000);
            // Relative to the patch level, as for the engine presets.
            const double delta = db (diffRms (out0, out1)) - db (diffRms (out0, std::vector<float> (out0.size(), 0.0f)));
            if (delta <= -30.0)
                std::printf ("MACRO FAIL: %s macro%d %.1f dB re patch\n", preset.name, m, delta);
            assert (delta > -30.0);
        }
    }
    tempDir.deleteRecursively();

    // 2. Velocity layer differentiation on "Atelier Sample - Velvet Keys".
    {
        const auto& bank = *presets[(size_t) velvetIdx].sampleBank;
        assert (bank.zones.size() >= 2);
        bool hasSoft = false, hasHard = false;
        for (const auto& z : bank.zones)
        {
            hasSoft |= (z.lowVel == 1 && z.highVel < 127 && z.crossfadeVel > 0 && z.crossfadeLaw == bw::VelocityCrossfadeLaw::equalPower);
            hasHard |= (z.lowVel > 1 && z.highVel == 127 && z.crossfadeVel > 0 && z.crossfadeLaw == bw::VelocityCrossfadeLaw::equalPower);
        }
        assert (hasSoft && hasHard);

        auto renderVel = [&] (uint8 vel)
        {
            WaveProcessor p;
            p.loadFactoryPreset (velvetIdx);
            p.prepareToPlay (rate, block);
            return renderNote (p, 60, vel, 12000);
        };
        const auto v20  = renderVel ((uint8) 20);
        const auto v70  = renderVel ((uint8) 70);
        const auto v120 = renderVel ((uint8) 120);
        assert (diffRms (v20, v70) > 1.0e-3);
        assert (diffRms (v70, v120) > 1.0e-3);
        assert (diffRms (v20, v120) > 1.0e-3);
    }

    // 3. Round-robin 4-variant cycling on "Atelier Sample - Round Robin Pluck".
    {
        const auto& bank = *presets[(size_t) pluckIdx].sampleBank;
        assert (bank.zones.size() >= 3);
        for (const auto& z : bank.zones)
        {
            assert (z.numVariants == 4);
            for (int v = 0; v < 4; ++v)
                assert (z.sampleForVariant (v) != nullptr);
        }

        WaveProcessor p;
        p.loadFactoryPreset (pluckIdx);
        // Disable free-running chorus / delay / reverb for pure round-robin cycle comparison.
        p.apvts.getParameter ("chorusOn")->setValueNotifyingHost (0.0f);
        p.apvts.getParameter ("delayOn")->setValueNotifyingHost (0.0f);
        p.apvts.getParameter ("reverbOn")->setValueNotifyingHost (0.0f);
        p.prepareToPlay (rate, block);
        std::array<std::vector<float>, 5> hits;
        for (size_t h = 0; h < 5; ++h)
        {
            hits[h] = renderNote (p, 60, (uint8) 100, 12000);
            // Let voice release completely before next noteOn.
            AudioBuffer<float> silent (2, block);
            for (int i = 0; i < 40; ++i)
            {
                MidiBuffer empty;
                p.processBlock (silent, empty);
            }
        }
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = i + 1; j < 4; ++j)
                assert (diffRms (hits[i], hits[j]) > 1.0e-3);
        assert (db (diffRms (hits[0], hits[4])) < -80.0);
    }

    // 4. Manual slice differentiation on "Atelier Sample - Sliced Groove".
    {
        const auto& bank = *presets[(size_t) sliceIdx].sampleBank;
        assert (bank.sliced && bank.slices.markers.size() >= 8 && bank.zones.size() >= 8);
        for (const auto& z : bank.zones)
            assert (z.lowKey == z.highKey && z.lowKey == z.rootKey);

        std::array<std::vector<float>, 4> slices;
        for (int s = 0; s < 4; ++s)
        {
            WaveProcessor p;
            p.loadFactoryPreset (sliceIdx);
            p.prepareToPlay (rate, block);
            slices[(size_t) s] = renderNote (p, 60 + s, (uint8) 100, 8000);
        }
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = i + 1; j < 4; ++j)
                assert (diffRms (slices[i], slices[j]) > 1.0e-3);
    }
    std::printf ("ok: 3 Sample/Multisample factory presets (velocity layers, 4x round-robin, manual slices, macros and portable state)\n");
}

void setParam (WaveProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    assert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// Holds `notes` keys from C3 upward; returns the stereo peak and checks finite output and the grain pool.
double holdNotes (WaveProcessor& p, int notes, float velocity, int frames, std::vector<float>* mono = nullptr)
{
    AudioBuffer<float> buffer (2, block);
    double peak = 0;
    for (int start = 0; start < frames; start += block)
    {
        MidiBuffer midi;
        if (start == 0)
            for (int n = 0; n < notes; ++n) midi.addEvent (MidiMessage::noteOn (1, 48 + n * 2, velocity), 0);
        p.processBlock (buffer, midi);
        assert (p.activeGrains.load() <= 32 * notes);
        for (int s = 0; s < block; ++s)
        {
            const float l = buffer.getSample (0, s), r = buffer.getSample (1, s);
            assert (std::isfinite (l) && std::isfinite (r));
            peak = jmax (peak, (double) std::abs (l), (double) std::abs (r));
            if (mono != nullptr) mono->push_back (.5f * (l + r));
        }
    }
    return peak;
}

// Issues 26/27: granular and spectral factory presets.
void checkEnginePresets()
{
    const auto& presets = bw::factoryPresets();
    std::vector<int> engines;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].sample != nullptr) engines.push_back (i);
    assert (engines.size() == 6);

    const File tempDir = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-wave-engine-presets");
    tempDir.createDirectory();
    for (int idx : engines)
    {
        const auto& preset = presets[(size_t) idx];
        const bool spectral = String (preset.name).startsWith ("Spectral");

        // Rates x polyphony x velocity x macro extremes: finite, -1 dBFS headroom, bounded grains.
        const std::array<double, 3> rates { 44100, 48000, 96000 };
        const std::array<int, 3> polyphony { 1, 8, 16 };
        const std::array<float, 3> velocities { .25f, .65f, 1.0f };
        double worst = 0;
        for (size_t r = 0; r < rates.size(); ++r)
            for (size_t n = 0; n < polyphony.size(); ++n)
            {
                WaveProcessor p;
                p.loadFactoryPreset (idx);
                const float macro = (r + n) % 2 ? 1.0f : 0.0f;
                for (int m = 1; m <= 4; ++m) setParam (p, ("macro" + String (m)).toRawUTF8(), macro);
                p.prepareToPlay (rates[r], block);
                settle (p);
                worst = jmax (worst, holdNotes (p, polyphony[n], velocities[(r + n) % 3], (int) (rates[r] * 1.5)));
            }
        std::printf ("%-34s worst peak %.1f dB\n", preset.name, db (worst));
        assert (worst < .891 && worst > 1.0e-3);

        // Every macro is audible, and a fixed grain seed renders bit-identically.
        auto render = [&] (int macro, float value)
        {
            WaveProcessor p;
            p.loadFactoryPreset (idx);
            if (macro > 0) setParam (p, ("macro" + String (macro)).toRawUTF8(), value);
            p.prepareToPlay (rate, block);
            settle (p);
            std::vector<float> out;
            holdNotes (p, 1, .8f, (int) (rate * 1.5), &out);
            return out;
        };
        const auto base = render (0, 0);
        assert (diffRms (base, render (0, 0)) == 0.0);
        for (int m = 1; m <= 4; ++m)
        {
            // Relative to the patch level: quiet pads still need a clearly audible change.
            const double delta = db (diffRms (render (m, 0), render (m, 1))) - db (diffRms (base, std::vector<float> (base.size(), 0.0f)));
            if (delta <= -30) std::printf ("MACRO FAIL: %s macro%d %.1f dB re patch\n", preset.name, m, delta);
            assert (delta > -30);
        }

        // Host state and .bwpreset round trips; spectral analysis is rebuilt on reopen.
        WaveProcessor p;
        p.loadFactoryPreset (idx);
        settle (p);
        assert (p.matchingFactoryPreset() == idx);
        MemoryBlock state;
        p.getStateInformation (state);
        WaveProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        settle (restored);
        assert (restored.matchingFactoryPreset() == idx);
        const File file = tempDir.getChildFile (File::createLegalFileName (String (preset.name) + ".bwpreset"));
        assert (p.savePreset (file));
        WaveProcessor fromFile;
        assert (fromFile.loadPreset (file));
        settle (fromFile);
        assert (fromFile.matchingFactoryPreset() == idx);
        if (spectral)
            assert (restored.spectralState() == WaveProcessor::SpectralState::ready
                    && fromFile.spectralState() == WaveProcessor::SpectralState::ready);
    }
    tempDir.deleteRecursively();

    // Freeze holds still: with motion, LFO and effects off, the frozen level does not drift.
    for (int idx : engines)
        if (String (presets[(size_t) idx].name).contains ("Freeze"))
        {
            WaveProcessor p;
            p.loadFactoryPreset (idx);
            for (auto id : { "reverbOn", "delayOn", "chorusOn", "mod1Enabled" }) setParam (p, id, 0);
            setParam (p, "sustain", 1);
            p.prepareToPlay (rate, block);
            settle (p);
            std::vector<float> out;
            holdNotes (p, 1, .8f, (int) (rate * 3), &out);
            auto level = [&] (double from, double to)
            {
                double sum = 0;
                for (auto i = (size_t) (from * rate); i < (size_t) (to * rate); ++i) sum += out[i] * out[i];
                return db (std::sqrt (sum / ((to - from) * rate)));
            };
            std::printf ("freeze drift %.3f dB\n", level (2.5, 3.0) - level (1.5, 2.0));
            assert (std::abs (level (2.5, 3.0) - level (1.5, 2.0)) < .5);
        }

    // 100 preset swaps do not grow asset memory.
    WaveProcessor p;
    p.prepareToPlay (rate, block);
    AudioBuffer<float> buffer (2, block);
    size_t warm = 0;
    for (int swap = 0; swap < 100; ++swap)
    {
        p.loadFactoryPreset (engines[(size_t) swap % engines.size()]);
        MidiBuffer midi;
        p.processBlock (buffer, midi);
        if (swap == 11) { settle (p); warm = p.assetMemoryBytes(); }
    }
    p.loadFactoryPreset (engines[(size_t) 11 % engines.size()]);
    settle (p);
    for (int i = 0; i < 8; ++i) { MidiBuffer midi; p.processBlock (buffer, midi); }
    std::printf ("asset memory after warm-up %zu, after 100 swaps %zu\n", warm, p.assetMemoryBytes());
    assert (p.assetMemoryBytes() <= warm);
    std::printf ("ok: 6 granular/spectral factory presets (rates, polyphony, macros, seed, portable state, freeze, 100 swaps)\n");
}
}

int main (int argc, char** argv)
{
   #if JUCE_WINDOWS
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    if (argc > 1 && String (argv[1]) == "--sample")
    {
        checkSampleMultisamplePresets();
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "--engines")
    {
        checkEnginePresets();
        return 0;
    }
    // --only <text>: run the bank checks for matching preset names only (fast tuning loop).
    const String only = argc > 2 && String (argv[1]) == "--only" ? String (argv[2]) : String();
    const File out = argc > 1 && only.isEmpty() ? File (String (argv[1])) : File();
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
        if (only.isNotEmpty() && ! String (preset.name).contains (only)) continue;
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
    if (only.isNotEmpty()) return 0;
    checkSampleMultisamplePresets();
    checkEnginePresets();

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
