#include "Rig.h"
#include <iostream>
#ifdef _WIN32
#include <cstdlib>
#include <stdlib.h>
#endif

namespace
{
using namespace rig;

void testSustainPedal()
{
    Rig rig;
    rig.send (on (60)); rig.send (pedal (true)); rig.run (.1);
    rig.send (off (60));
    const auto held = rig.run (.3);
    assert (rms (held, held.size() / 2, held.size()) > .01); // pedal keeps the released key sounding
    rig.send (pedal (false));
    assert (silent (rig.run (.2), 4800)); // release (50 ms) ends once the pedal lifts

    // A key still down when the pedal lifts keeps sounding.
    rig.send (pedal (true)); rig.send (on (64)); rig.run (.05);
    rig.send (pedal (false));
    const auto down = rig.run (.2);
    assert (rms (down, down.size() / 2, down.size()) > .01);
    rig.send (off (64));
    assert (silent (rig.run (.2), 4800));
}

void testAllNotesOff()
{
    Rig rig;
    rig.send (pedal (true));
    for (int note = 48; note < 60; ++note) rig.send (on (note));
    rig.run (.1);
    rig.send (juce::MidiMessage::allNotesOff (1));
    assert (silent (rig.run (.2), 4800));
}

// Stealing a sounding voice must not step the output (declick), at any phase of the stolen note.
void testStealIsContinuous()
{
    Rig rig;
    for (int note = 40; note < 56; ++note) rig.send (on (note));
    auto audio = rig.run (.1);
    for (int k = 0; k < 24; ++k)
    {
        const float previous = audio.back();
        rig.send (off (56 + k - 1)); // keeps 16 voices busy: the next note always steals
        rig.send (on (56 + k));
        audio = rig.run (.01 + k * .0007);
        // The steal window may not step harder than the same voices do right after it.
        assert (maxStep (audio, 0, 96, previous) < 1.5f * maxStep (audio, 96, 400, audio[95]));
    }
}

void fuzz (int mode)
{
    Rig rig;
    if (mode != 0) { rig.set ("voiceMode", (float) (mode % 3)); rig.set ("glide", .03f); } // 3: switches modes
    juce::Random random (1234 + mode);
    std::array<bool, 128> down {};
    for (int step = 0; step < 1500; ++step)
    {
        const int roll = random.nextInt (100), note = 36 + random.nextInt (48);
        if (roll < 45) { rig.send (on (note, .2f + .8f * random.nextFloat())); down[(size_t) note] = true; }
        else if (roll < 88) { rig.send (off (note)); down[(size_t) note] = false; }
        else if (roll < 97) rig.send (pedal (random.nextBool()));
        else if (roll < 98 && mode == 3) { rig.set ("voiceMode", (float) random.nextInt (3)); down = {}; }
        else { rig.send (juce::MidiMessage::allNotesOff (1)); down = {}; }
        rig.run (random.nextInt (3) * Rig::kBlock / rig.rate);
    }
    for (int note = 0; note < 128; ++note) if (down[(size_t) note]) rig.send (off (note));
    rig.send (pedal (false));
    assert (silent (rig.run (.25), 4800)); // no stuck notes
}

void testMonoPriority()
{
    Rig rig;
    rig.set ("voiceMode", 1);
    rig.send (on (60)); assert (near (frequency (rig.run (.2)), 60));
    rig.send (on (64)); assert (near (frequency (rig.run (.2)), 64)); // one voice: only the newest note
    rig.send (on (67)); rig.run (.05);
    rig.send (off (64)); assert (near (frequency (rig.run (.2)), 67)); // releasing a buried key changes nothing
    rig.send (off (67)); assert (near (frequency (rig.run (.2)), 60)); // back to the last key still down
    rig.send (off (60)); assert (silent (rig.run (.2), 4800));
}

// RMS just after the second key relative to the held note: legato keeps the envelope, mono restarts it.
double retriggerDip (int mode)
{
    Rig rig;
    rig.set ("voiceMode", (float) mode); rig.set ("attack", .1f);
    rig.send (on (60));
    const auto held = rig.run (.3);
    rig.send (on (64));
    const auto next = rig.run (.02);
    return rms (next, 240, next.size()) / rms (held, held.size() - 960, held.size()); // skips the 5 ms declick
}

void testLegatoAndGlide()
{
    assert (retriggerDip (2) > .8);
    assert (retriggerDip (1) < .3);

    Rig rig;
    rig.set ("voiceMode", 2); rig.set ("glide", .1f);
    rig.send (on (60)); rig.run (.2);
    rig.send (on (72));
    const auto glide = rig.run (.3);
    const double early = frequency (glide, 0, 960), middle = frequency (glide, 1920, 2880);
    assert (early > 250 && early < middle && middle < 500);
    assert (near (frequency (glide, 7200, glide.size()), 72));
    rig.send (off (72)); assert (near (frequency (rig.run (.2)), 60)); // returning to a held key glides too

    // Legato glides only between overlapping notes; a fresh phrase starts on pitch.
    rig.send (off (60)); rig.run (.2);
    rig.send (on (48)); assert (near (frequency (rig.run (.04), 480, 1920), 48));

    // Mono glides even when the previous note is only releasing.
    rig.set ("voiceMode", 1);
    rig.send (off (48)); rig.run (.02);
    rig.send (on (72)); assert (frequency (rig.run (.02), 0, 960) < 500);
}

void testLegatoPedal()
{
    Rig rig;
    rig.set ("voiceMode", 2); rig.set ("attack", .1f);
    rig.send (on (60)); rig.send (pedal (true));
    const auto held = rig.run (.3);
    rig.send (off (60)); rig.run (.05); // sustained by the pedal
    rig.send (on (64));
    const auto next = rig.run (.2);
    assert (rms (next, 0, 960) > .8 * rms (held, held.size() - 960, held.size())); // still legato
    assert (near (frequency (next), 64));
    rig.send (pedal (false)); assert (near (frequency (rig.run (.2)), 64)); // 64 is still down
    rig.send (off (64)); assert (silent (rig.run (.2), 4800));
    rig.send (on (60)); rig.send (pedal (true)); rig.send (off (60)); rig.run (.1);
    rig.send (pedal (false)); assert (silent (rig.run (.2), 4800)); // pedal up with no keys releases
}

// The pedal survives mode changes in both directions.
void testModeSwitchKeepsPedal()
{
    Rig rig;
    rig.set ("voiceMode", 1); rig.send (pedal (true)); rig.run (.02);
    rig.set ("voiceMode", 0); rig.send (pedal (false)); rig.run (.02);
    rig.set ("voiceMode", 1);
    rig.send (on (60)); rig.run (.1);
    rig.send (off (60)); assert (silent (rig.run (.2), 4800)); // pedal is up: no stuck note

    rig.set ("voiceMode", 0); rig.send (pedal (true)); rig.run (.02);
    rig.set ("voiceMode", 2);
    rig.send (on (62)); rig.run (.1);
    rig.send (off (62));
    const auto held = rig.run (.2);
    assert (rms (held, 4800, held.size()) > .01); // pedal is down: still sustained
    rig.set ("voiceMode", 0);
    rig.send (on (64)); rig.run (.1);
    rig.send (off (64));
    const auto poly = rig.run (.2);
    assert (rms (poly, 4800, poly.size()) > .01); // and still sustains in poly
    rig.send (pedal (false)); assert (silent (rig.run (.2), 4800));
}

void testModeDefaultsAndMigration()
{
    Rig rig;
    assert (rig.processor.apvts.getRawParameterValue ("voiceMode")->load() == 0); // poly
    assert (rig.processor.apvts.getRawParameterValue ("glide")->load() == 0);
    assert (rig.processor.apvts.getRawParameterValue ("oscAWarpOn")->load() == 0);
    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    for (auto* id : { "voiceMode", "glide", "oscAWarpOn", "oscAWarp", "oscBWarpOn", "oscBWarp", "fmDepth" }) xml->removeChildElement (xml->getChildByAttribute ("id", id), true);
    rig.set ("voiceMode", 2); rig.set ("glide", 1); rig.set ("oscAWarpOn", 1); rig.set ("oscAWarp", .5f); rig.set ("fmDepth", .5f);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
    rig.processor.setStateInformation (legacy.getData(), (int) legacy.getSize());
    assert (rig.processor.apvts.getRawParameterValue ("voiceMode")->load() == 0);
    assert (rig.processor.apvts.getRawParameterValue ("glide")->load() == 0);
    assert (rig.processor.apvts.getRawParameterValue ("oscAWarpOn")->load() == 0);
    assert (rig.processor.apvts.getRawParameterValue ("oscAWarp")->load() == 0);
    assert (rig.processor.apvts.getRawParameterValue ("fmDepth")->load() == 0);
}

// 0.1 s to settle, then exactly enough for one spectrum (Debug renders are slow).
double settled (double rate) { return .1 + (kSpectrumSize + Rig::kBlock) / rate; }

// One sustained note on OSC A; returns the settled left channel.
std::vector<float> warped (double rate, int note, float position, bool warpOn, float amount, int oscillator = 0)
{
    Rig rig (rate);
    rig.set ("wtPos", position); rig.set ("wtPosB", position);
    rig.set ("oscAOn", oscillator == 0 ? 1.0f : 0.0f); rig.set ("oscBOn", oscillator == 1 ? 1.0f : 0.0f); rig.set ("levelB", .5f);
    rig.set (oscillator == 0 ? "oscAWarpOn" : "oscBWarpOn", warpOn ? 1.0f : 0.0f);
    rig.set (oscillator == 0 ? "oscAWarp" : "oscBWarp", amount);
    rig.send (on (note));
    const auto audio = rig.run (settled (rate));
    return { audio.begin() + (long) (rate * .1), audio.end() };
}

void testWarpTuningAndLevel()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 28, 60, 96 })
            for (float amount : { -1.0f, 1.0f })
            {
                const auto audio = warped (rate, note, 0, true, amount);
                assert (near (frequency (audio, 0, audio.size(), rate), note)); // warp keeps the period
            }
    // Same tuning on B.
    assert (near (frequency (warped (48000, 60, 0, true, 1, 1)), 60));
    for (float position : { 0.0f, .5f, 1.0f })
    {
        const auto dry = warped (48000, 48, position, false, 1), wet = warped (48000, 48, position, true, 1);
        const double ratio = 20 * std::log10 (rms (wet, 0, wet.size()) / rms (dry, 0, dry.size()));
        assert (std::abs (ratio) < 3); // no gross loudness jump; dry/wet A/B is level-matched by ear
        double change = 0;
        for (size_t i = 0; i < dry.size(); ++i) change += std::abs (wet[i] - dry[i]);
        // Audibly different, except the square: its edges sit on the bend's fixed points (0 and 1/2).
        if (position < 1) assert (change / (double) dry.size() > .01);
    }
    const auto dry = warped (48000, 48, .5f, false, 0), off = warped (48000, 48, .5f, false, 1), zero = warped (48000, 48, .5f, true, 0);
    for (size_t i = 0; i < dry.size(); ++i) assert (off[i] == dry[i] && std::abs (zero[i] - dry[i]) < 1.0e-6f);
}

void testWarpAlias()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 84, 96, 103 })
        {
            const double dry = inharmonic (warped (rate, note, .5f, false, 0), rate, note);
            const double wet = inharmonic (warped (rate, note, .5f, true, 1), rate, note);
            const double bent = inharmonic (warped (rate, note, .5f, true, -1), rate, note);
            assert (std::max (wet, bent) < std::max (1.0e-4, 10 * dry)); // warp aliasing stays below -40 dB
        }
}

// A 40 Hz LFO sweeping the full warp range through the matrix steps no harder than either static extreme.
void testWarpSweep()
{
    for (double rate : { 44100.0, 96000.0 })
    {
        Rig bent (rate), opposite (rate), swept (rate);
        for (auto* rig : { &bent, &opposite, &swept }) { rig->set ("wtPos", .5f); rig->set ("oscAWarpOn", 1); }
        bent.set ("oscAWarp", 1); opposite.set ("oscAWarp", -1);
        swept.set ("lfo1Rate", 40); swept.set ("lfo1Mode", 1);
        swept.set ("mod1Source", 1); swept.set ("mod1Destination", 21); swept.set ("mod1Amount", 1);
        swept.set ("mod1Bipolar", 1); swept.set ("mod1Enabled", 1);
        for (auto* rig : { &bent, &opposite, &swept }) rig->send (on (72));
        const auto a = bent.run (.4), c = opposite.run (.4), b = swept.run (.4);
        const size_t from = (size_t) (rate * .1);
        const float extreme = std::max (maxStep (a, from, a.size(), a[from - 1]), maxStep (c, from, c.size(), c[from - 1]));
        assert (maxStep (b, from, b.size(), b[from - 1]) < 1.1f * extreme);
        double change = 0;
        for (size_t i = from; i < a.size(); ++i) change += std::abs (a[i] - b[i]);
        assert (change / (double) (a.size() - from) > .01);
    }
}

void testWarpMatrixDestinations()
{
    assert (bw::destinationNames()[21] == "A Warp" && bw::destinationNames()[22] == "B Warp");
    for (int oscillator = 0; oscillator < 2; ++oscillator)
    {
        Rig dry, macro;
        for (auto* rig : { &dry, &macro })
        {
            rig->set ("oscAOn", oscillator == 0 ? 1.0f : 0.0f); rig->set ("oscBOn", oscillator == 1 ? 1.0f : 0.0f);
            rig->set ("wtPos", .5f); rig->set ("wtPosB", .5f);
            rig->set (oscillator == 0 ? "oscAWarpOn" : "oscBWarpOn", 1);
        }
        macro.set ("macro1", 1);
        macro.set ("mod1Source", 3); macro.set ("mod1Destination", (float) (21 + oscillator)); macro.set ("mod1Amount", 1);
        macro.set ("mod1Bipolar", 0); macro.set ("mod1Enabled", 1);
        for (auto* rig : { &dry, &macro }) rig->send (on (48));
        const auto a = dry.run (.3), b = macro.run (.3);
        double change = 0;
        for (size_t i = 4800; i < a.size(); ++i) change += std::abs (a[i] - b[i]);
        assert (change / (double) (a.size() - 4800) > .01);
    }
    // Warp destinations are per voice: a macro route to them leaves the global FX untouched.
    Rig fx;
    fx.set ("delayOn", 1); fx.set ("delayMix", 0); fx.set ("macro1", 1);
    fx.set ("mod1Source", 3); fx.set ("mod1Destination", 21); fx.set ("mod1Amount", 1); fx.set ("mod1Bipolar", 0); fx.set ("mod1Enabled", 1);
    fx.send (on (60)); fx.run (.1);
    fx.send (off (60));
    const auto tail = fx.run (.4);
    assert (silent (tail, 9600)); // delay mix stays 0, so nothing echoes
}

// OSC A carrier, OSC B modulator. B is muted, so it is heard only through A. Returns the settled left channel.
std::vector<float> fm (double rate, int note, float depth, float positionA = 0, float pitchB = 0, bool hearB = false)
{
    Rig rig (rate);
    rig.set ("wtPos", positionA); rig.set ("oscBOn", hearB ? 1.0f : 0.0f); rig.set ("levelB", .5f); rig.set ("oscBPitch", pitchB);
    rig.set ("fmDepth", depth);
    rig.send (on (note));
    const auto audio = rig.run (settled (rate));
    return { audio.begin() + (long) (rate * .1), audio.end() };
}

// Fraction of spectral energy in harmonics 2 and up (Hann window).
double overtones (const std::vector<float>& audio, double rate, int note)
{
    const auto energy = spectrum (audio);
    const double f0 = juce::MidiMessage::getMidiNoteInHertz (note), bin = rate / kSpectrumSize;
    double upper = 0, total = 0;
    for (size_t k = 1; k < energy.size(); ++k)
    {
        total += energy[k];
        if ((double) k * bin > 1.5 * f0) upper += energy[k];
    }
    return upper / total;
}

void testFmTimbreAndTuning()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 28, 60, 96 })
            for (float ratio : { 0.0f, 12.0f })
                assert (inharmonic (fm (rate, note, 1, 0, ratio), rate, note) < 1.0e-3); // 1:1 and 2:1 stay on the note's harmonics
    // Two sines without interaction have no overtones; FM from B builds them.
    assert (overtones (fm (48000, 48, 0, 0, 0, true), 48000, 48) < 1.0e-3);
    assert (overtones (fm (48000, 48, .5f), 48000, 48) > .1);
    // Phase modulation leaves the carrier level alone: no peaks.
    for (float position : { 0.0f, .5f })
        assert (peak (fm (48000, 48, 1, position)) < 1.2f * peak (fm (48000, 48, 0, position)));
}

void testFmAlias()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 84, 96, 103 })
            for (float position : { 0.0f, .5f })
            {
                const double dry = inharmonic (fm (rate, note, 0, position), rate, note);
                assert (inharmonic (fm (rate, note, 1, position), rate, note) < std::max (1.0e-4, 10 * dry));
            }
}

// A 40 Hz LFO sweeping the full FM depth steps no harder than the static extremes.
void testFmSweep()
{
    for (double rate : { 44100.0, 96000.0 })
    {
        Rig dry (rate), full (rate), swept (rate);
        for (auto* rig : { &dry, &full, &swept }) rig->set ("oscBOn", 0);
        full.set ("fmDepth", 1);
        swept.set ("lfo1Rate", 40); swept.set ("lfo1Mode", 1);
        swept.set ("mod1Source", 1); swept.set ("mod1Destination", 23); swept.set ("mod1Amount", 1);
        swept.set ("mod1Bipolar", 0); swept.set ("mod1Enabled", 1);
        for (auto* rig : { &dry, &full, &swept }) rig->send (on (72));
        const auto a = dry.run (.4), c = full.run (.4), b = swept.run (.4);
        const size_t from = (size_t) (rate * .1);
        const float extreme = std::max (maxStep (a, from, a.size(), a[from - 1]), maxStep (c, from, c.size(), c[from - 1]));
        assert (maxStep (b, from, b.size(), b[from - 1]) < 1.1f * extreme);
        double change = 0;
        for (size_t i = from; i < a.size(); ++i) change += std::abs (a[i] - b[i]);
        assert (change / (double) (a.size() - from) > .01);
    }
}

void testFmMatrixAndChords()
{
    assert (bw::destinationNames()[23] == "FM Depth");
    Rig dry, macro;
    macro.set ("macro1", 1);
    macro.set ("mod1Source", 3); macro.set ("mod1Destination", 23); macro.set ("mod1Amount", 1);
    macro.set ("mod1Bipolar", 0); macro.set ("mod1Enabled", 1);
    for (auto* rig : { &dry, &macro }) rig->send (on (48));
    const auto a = dry.run (.3), b = macro.run (.3);
    double change = 0;
    for (size_t i = 4800; i < a.size(); ++i) change += std::abs (a[i] - b[i]);
    assert (change / (double) (a.size() - 4800) > .01);

    // Chords with unison on both oscillators: FM adds no peaks at any rate.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::array<float, 2> peaks {};
        for (int pass = 0; pass < 2; ++pass)
        {
            Rig rig (rate);
            rig.set ("oscBOn", 1); rig.set ("wtPosB", .5f); rig.set ("fmDepth", pass == 0 ? 0.0f : .8f);
            for (auto* id : { "oscAUnison", "oscBUnison" }) rig.set (id, 8);
            for (auto* id : { "oscADetune", "oscBDetune" }) rig.set (id, 20);
            for (int note : { 48, 52, 55, 60, 64 }) rig.send (on (note));
            peaks[(size_t) pass] = peak (rig.run (.4));
        }
        assert (peaks[1] < 1 && peaks[1] < 1.5f * peaks[0]);
    }
}
}

int main()
{
#ifdef _WIN32
    // Failed asserts print and exit instead of opening a modal dialog.
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    testSustainPedal();
    testAllNotesOff();
    testStealIsContinuous();
    testModeDefaultsAndMigration();
    testMonoPriority();
    testLegatoAndGlide();
    testLegatoPedal();
    testModeSwitchKeepsPedal();
    for (int mode = 0; mode < 4; ++mode) fuzz (mode);
    testWarpTuningAndLevel();
    testWarpAlias();
    testWarpSweep();
    testWarpMatrixDestinations();
    testFmTimbreAndTuning();
    testFmAlias();
    testFmSweep();
    testFmMatrixAndChords();
    std::cout << "ok: sustain pedal, all-notes-off, click-free steal, mono priority, legato/glide, legato pedal, "
                 "state migration, poly/mono/legato fuzz, warp tuning/level/alias/sweep/matrix, FM timbre/tuning/alias/sweep/matrix/chords\n";
}
