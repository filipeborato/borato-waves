#include "Rig.h"
#include <iostream>
#ifdef _WIN32
#include <cstdlib>
#include <stdlib.h>
#endif

namespace
{
using namespace rig;

const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("borato-wave-samples");

// A sine at hz, written as a 16-bit WAV.
juce::File writeTone (const juce::String& name, double rate, double seconds, double hz, int channels = 1)
{
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav");
    file.deleteFile();
    const int length = (int) (rate * seconds);
    juce::AudioBuffer<float> audio (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < length; ++i) audio.setSample (ch, i, .8f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned) channels, 16, {}, 0));
    assert (writer != nullptr);
    stream.release();
    assert (writer->writeFromAudioSampleBuffer (audio, 0, length));
    return file;
}

// The sample source alone.
void sampleOnly (Rig& rig)
{
    rig.set ("oscAOn", 0); rig.set ("sampleOn", 1); rig.set ("sampleLevel", .8f);
}

void testPlaysAtPitch()
{
    const auto tone = writeTone ("a440", 44100, 2, 440);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Rig rig (rate);
        sampleOnly (rig);
        assert (rig.processor.loadSample (tone).isEmpty());
        assert (rig.processor.sampleName() == "a440");
        rig.send (on (60)); const auto root = rig.run (.3);
        assert (std::abs (frequency (root, root.size() / 2, root.size(), rate) / 440 - 1) < .005); // note 60 plays the file's own pitch
        rig.send (off (60)); rig.run (.2);
        rig.send (on (72)); const auto octave = rig.run (.3);
        assert (std::abs (frequency (octave, octave.size() / 2, octave.size(), rate) / 880 - 1) < .005);
        rig.send (off (72)); rig.run (.2);
        rig.set ("samplePitch", -12);
        rig.send (on (60)); const auto down = rig.run (.3);
        assert (std::abs (frequency (down, down.size() / 2, down.size(), rate) / 220 - 1) < .005);
    }
}

// A tone whose period does not divide the loop: without the crossfade every wrap would step.
void testLoopIsSeamless()
{
    const auto tone = writeTone ("odd", 44100, 1, 441.3);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 48, 60, 72 })
        {
            Rig rig (rate);
            sampleOnly (rig);
            assert (rig.processor.loadSample (tone).isEmpty());
            rig.set ("sampleLoop", 1); rig.set ("sampleStart", .1f); rig.set ("sampleEnd", .35f);
            rig.send (on (note));
            const auto audio = rig.run (1.5); // several wraps at every pitch
            const size_t from = (size_t) (rate * .02);
            const double hz = 441.3 * std::exp2 ((note - 60) / 12.0);
            const float slope = (float) (juce::MathConstants<double>::twoPi * hz / rate) * peak (audio, from);
            assert (maxStep (audio, from, audio.size(), audio[from - 1]) < 1.5f * slope);
            assert (rms (audio, audio.size() - (size_t) (rate * .2), audio.size()) > .05); // still looping
        }
}

void testOneShotEnds()
{
    const auto tone = writeTone ("short", 44100, 1, 440);
    Rig rig;
    sampleOnly (rig);
    assert (rig.processor.loadSample (tone).isEmpty());
    rig.set ("sampleEnd", .2f);
    rig.send (on (60));
    const auto audio = rig.run (.5);
    assert (rms (audio, 0, 4800) > .05);
    assert (silent (audio, (size_t) (48000 * .25))); // the key is still down, the sample has ended
    const float slope = (float) (juce::MathConstants<double>::twoPi * 440 / 48000) * peak (audio);
    assert (maxStep (audio, 480, audio.size(), audio[479]) < 1.5f * slope); // faded, not cut
}

// Start/end move with the next loop pass, so automating them never steps the output.
void testAutomation()
{
    const auto tone = writeTone ("odd", 44100, 1, 441.3);
    Rig rig;
    sampleOnly (rig);
    assert (rig.processor.loadSample (tone).isEmpty());
    for (auto* id : { "sampleOn", "sampleLevel", "samplePitch", "sampleStart", "sampleEnd", "sampleLoop" })
        assert (rig.processor.apvts.getParameter (id)->isAutomatable());
    rig.set ("sampleLoop", 1);
    rig.send (on (60));
    auto audio = rig.run (.2);
    const float slope = (float) (juce::MathConstants<double>::twoPi * 441.3 / 48000) * peak (audio, 960);
    for (int step = 0; step < 20; ++step)
    {
        const float previous = audio.back();
        rig.set ("sampleStart", .02f * (float) (step % 7)); rig.set ("sampleEnd", .3f + .03f * (float) (step % 5));
        audio = rig.run (.05);
        assert (maxStep (audio, 0, audio.size(), previous) < 1.5f * slope);
    }
}

// Replacing the sample under a sounding note crossfades; the old audio is freed once no voice reads it.
void testSwapDuringNotes()
{
    const auto first = writeTone ("first", 44100, 2, 440), second = writeTone ("second", 44100, 2, 660), third = writeTone ("third", 44100, 2, 550);
    Rig rig;
    sampleOnly (rig);
    rig.set ("sampleStart", .3131f); // the new sample starts mid-cycle
    // One undo step, so the history keeps only the samples before and after it.
    const WaveProcessor::ScopedEdit swaps (rig.processor);
    assert (rig.processor.loadSample (first).isEmpty());
    std::weak_ptr<const bw::Sample> old = rig.processor.sample();
    rig.send (on (60));
    auto audio = rig.run (.2135); // swaps away mid-cycle too
    const float slope = (float) (juce::MathConstants<double>::twoPi * 660 / 48000) * peak (audio, 960);
    float previous = audio.back();
    assert (rig.processor.loadSample (second).isEmpty());
    audio = rig.run (.2);
    assert (maxStep (audio, 0, audio.size(), previous) < 1.5f * slope);
    assert (rms (audio, 4800, audio.size()) > .05); // the note keeps sounding
    assert (! old.expired()); // still retired: nothing has collected it
    assert (rig.processor.loadSample (third).isEmpty());
    assert (old.expired()); // every voice moved to the second sample long ago
    previous = audio.back();
    audio = rig.run (.1);
    assert (maxStep (audio, 0, audio.size(), previous) < 1.5f * slope);
}

void testMissingFileKeepsState()
{
    const auto tone = writeTone ("vanishing", 44100, 1, 440);
    juce::MemoryBlock state;
    {
        Rig rig;
        sampleOnly (rig);
        rig.set ("sampleStart", .25f); rig.set ("sampleLoop", 1);
        assert (rig.processor.loadSample (tone).isEmpty());
        rig.processor.getStateInformation (state);
    }
    tone.deleteFile();
    Rig rig;
    rig.processor.setStateInformation (state.getData(), (int) state.getSize());
    assert (rig.processor.sample() == nullptr && rig.processor.sampleMissing());
    assert (rig.processor.samplePath() == tone && rig.processor.sampleName() == "vanishing");
    assert (rig.processor.apvts.getRawParameterValue ("sampleOn")->load() == 1);
    assert (std::abs (rig.processor.apvts.getRawParameterValue ("sampleStart")->load() - .25f) < 1.0e-6f);
    rig.send (on (60));
    assert (silent (rig.run (.2))); // OSC A is off and the sample is missing: silence, no crash
    // Saving again keeps the reference, so relinking the file restores the sound.
    juce::MemoryBlock again;
    rig.processor.getStateInformation (again);
    writeTone ("vanishing", 44100, 1, 440);
    Rig relinked;
    relinked.processor.setStateInformation (again.getData(), (int) again.getSize());
    assert (relinked.processor.sample() != nullptr && ! relinked.processor.sampleMissing());
    relinked.send (on (60));
    assert (rms (relinked.run (.2), 960, 9600) > .05);
}

void testLegacyStateAndPresets()
{
    Rig rig;
    sampleOnly (rig);
    assert (rig.processor.loadSample (writeTone ("legacy", 44100, 1, 440)).isEmpty());
    juce::MemoryBlock state;
    WaveProcessor fresh;
    fresh.getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    for (auto* id : { "sampleOn", "sampleLevel", "samplePitch", "sampleStart", "sampleEnd", "sampleLoop" }) xml->removeChildElement (xml->getChildByAttribute ("id", id), true);
    xml->removeChildElement (xml->getChildByName ("SAMPLE"), true);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary (*xml, legacy);
    rig.processor.setStateInformation (legacy.getData(), (int) legacy.getSize());
    assert (rig.processor.sample() == nullptr && ! rig.processor.sampleMissing() && rig.processor.samplePath() == juce::File());
    auto value = [&] (const char* id) { return rig.processor.apvts.getRawParameterValue (id)->load(); };
    assert (value ("sampleOn") == 0 && value ("sampleStart") == 0 && value ("sampleEnd") == 1 && value ("sampleLoop") == 0);
    // A factory preset clears the sample, like it clears user tables.
    assert (rig.processor.loadSample (writeTone ("legacy", 44100, 1, 440)).isEmpty());
    assert (rig.processor.matchingFactoryPreset() != 0 || rig.processor.sample() == nullptr);
    rig.processor.loadFactoryPreset (0);
    assert (rig.processor.sample() == nullptr && rig.processor.samplePath() == juce::File() && rig.processor.matchingFactoryPreset() == 0);
}

void testLimits()
{
    Rig rig;
    sampleOnly (rig);
    assert (rig.processor.loadSample (writeTone ("keep", 44100, 1, 440)).isEmpty());
    // One frame over the memory limit is refused and the loaded sample stays.
    folder.createDirectory();
    const auto huge = folder.getChildFile ("huge.wav");
    {
        huge.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (huge.createOutputStream().release());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 1, 16, {}, 0));
        stream.release();
        juce::AudioBuffer<float> zeros (1, 1 << 16);
        zeros.clear();
        for (int written = 0; written <= bw::kMaxSampleFrames; written += zeros.getNumSamples())
            assert (writer->writeFromAudioSampleBuffer (zeros, 0, juce::jmin (zeros.getNumSamples(), bw::kMaxSampleFrames + 1 - written)));
    }
    assert (rig.processor.loadSample (huge).isNotEmpty());
    assert (rig.processor.sampleName() == "keep" && rig.processor.sample() != nullptr);
    const auto bogus = folder.getChildFile ("bogus.wav");
    bogus.replaceWithText ("not audio");
    assert (rig.processor.loadSample (bogus).isNotEmpty() && rig.processor.sampleName() == "keep");
    // Stereo files mix down to mono.
    assert (rig.processor.loadSample (writeTone ("stereo", 44100, 1, 440, 2)).isEmpty());
    assert (std::abs (rig.processor.sample()->data[100] - .8f * (float) std::sin (juce::MathConstants<double>::twoPi * 440 * 100 / 44100)) < 1.0e-3f);
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
    testPlaysAtPitch();
    testLoopIsSeamless();
    testOneShotEnds();
    testAutomation();
    testSwapDuringNotes();
    testMissingFileKeepsState();
    testLegacyStateAndPresets();
    testLimits();
    folder.deleteRecursively();
    std::cout << "ok: sample pitch at 44.1/48/96 kHz, seamless loop, one-shot fade, automation, live swap and release, "
                 "missing file, legacy state, factory reset, memory limit\n";
}
