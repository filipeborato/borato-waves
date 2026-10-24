#include "Rig.h"
#include "PluginEditor.h"
#include "UI/Controls.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#ifdef _WIN32
#include <stdlib.h>
#endif

namespace
{
thread_local bool gCountingAudioHeap = false;
std::atomic<size_t> gAudioHeapOps { 0 };

struct ScopedAudioHeapGuard
{
    ScopedAudioHeapGuard() noexcept { gCountingAudioHeap = true; }
    ~ScopedAudioHeapGuard() noexcept { gCountingAudioHeap = false; }
};
} // namespace

void* operator new (std::size_t size)
{
    if (gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    if (void* ptr = std::malloc (size == 0 ? 1 : size)) return ptr;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)
{
    if (gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    if (void* ptr = std::malloc (size == 0 ? 1 : size)) return ptr;
    throw std::bad_alloc();
}
void operator delete (void* ptr) noexcept
{
    if (ptr != nullptr && gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    std::free (ptr);
}
void operator delete (void* ptr, std::size_t) noexcept { operator delete (ptr); }
void operator delete[] (void* ptr) noexcept { operator delete (ptr); }
void operator delete[] (void* ptr, std::size_t) noexcept { operator delete (ptr); }

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

// Writes a 32-bit float WAV from an explicit buffer (used to inject NaN/Inf).
juce::File writeFloatWav (const juce::String& name, double rate, const juce::AudioBuffer<float>& audio)
{
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav");
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned) audio.getNumChannels(), 32, {}, 0));
    assert (writer != nullptr);
    stream.release();
    assert (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    return file;
}

// Custom reader that simulates an I/O failure mid-read.
struct FailingReader : juce::AudioFormatReader
{
    FailingReader() : juce::AudioFormatReader (nullptr, "Failing")
    {
        sampleRate = 44100.0;
        bitsPerSample = 16;
        lengthInSamples = 1024;
        numChannels = 1;
        usesFloatingPointData = true;
    }
    bool readSamples (int* const*, int, int, juce::int64, int) override { return false; }
};

// The sample source alone.
void sampleOnly (Rig& rig)
{
    rig.set ("oscAOn", 0); rig.set ("sampleOn", 1); rig.set ("sampleLevel", .8f);
}

// Renders numSamples in chunks of blockSize (including a final partial block when numSamples % blockSize != 0)
// while verifying zero heap allocations/deallocations and finite output inside processBlock.
std::vector<float> renderChecked (WaveProcessor& processor, juce::MidiBuffer& pendingMidi, int totalSamples, int blockSize)
{
    std::vector<float> left;
    left.reserve ((size_t) totalSamples);
    juce::AudioBuffer<float> audio (2, blockSize);
    pendingMidi.ensureSize (256);
    for (int done = 0; done < totalSamples;)
    {
        const int chunk = juce::jmin (blockSize, totalSamples - done);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, 0, chunk);
        const size_t beforeOps = gAudioHeapOps.load (std::memory_order_relaxed);
        {
            const ScopedAudioHeapGuard guard;
            processor.processBlock (view, pendingMidi);
        }
        assert (gAudioHeapOps.load (std::memory_order_relaxed) == beforeOps);
        pendingMidi.clear();
        for (int i = 0; i < chunk; ++i)
        {
            assert (std::isfinite (view.getSample (0, i)) && std::isfinite (view.getSample (1, i)));
            left.push_back (view.getSample (0, i));
        }
        done += chunk;
    }
    return left;
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
    // Legacy state with only a file path (no embedded container) reports missing when the file is gone.
    auto legacyXml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    assert (legacyXml != nullptr);
    if (auto* sampleEl = legacyXml->getChildByName ("SAMPLE"))
    {
        sampleEl->removeAttribute ("containerVersion");
        sampleEl->removeAttribute ("channels");
        sampleEl->removeAttribute ("frames");
        sampleEl->removeAttribute ("rate");
        sampleEl->removeAttribute ("pcmBytes");
        sampleEl->removeAttribute ("hash");
        sampleEl->removeAttribute ("data");
    }
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary (*legacyXml, legacyState);
    rig.processor.setStateInformation (legacyState.getData(), (int) legacyState.getSize());
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
    assert (relinked.processor.waitForSampleLoad (5000));
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
    const auto keptPtr = rig.processor.sample();
    rig.set ("sampleLoop", 1);
    rig.send (on (60));
    assert (rms (rig.run (.05), 0, 1200) > .05);

    // 1. One frame over the per-file frame limit is refused and the loaded sample stays.
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
    assert (rig.processor.sampleName() == "keep" && rig.processor.sample() == keptPtr);

    // 2. Empty file (0 bytes) and non-audio file are refused.
    const auto emptyFile = folder.getChildFile ("empty.wav");
    emptyFile.replaceWithData (nullptr, 0);
    assert (rig.processor.loadSample (emptyFile).isNotEmpty() && rig.processor.sample() == keptPtr);

    const auto bogus = folder.getChildFile ("bogus.wav");
    bogus.replaceWithText ("not audio");
    assert (rig.processor.loadSample (bogus).isNotEmpty() && rig.processor.sampleName() == "keep" && rig.processor.sample() == keptPtr);

    // 3. Incomplete read: both a failing AudioFormatReader and a truncated WAV on disk are refused.
    {
        FailingReader failing;
        juce::String readerError;
        assert (bw::decodeSample (failing, readerError) == nullptr && readerError.isNotEmpty());
    }
    const auto validForTruncate = writeTone ("to_truncate", 44100, 0.5, 440);
    juce::MemoryBlock truncatedBytes;
    assert (validForTruncate.loadFileAsData (truncatedBytes));
    const auto truncatedFile = folder.getChildFile ("truncated.wav");
    assert (truncatedFile.replaceWithData (truncatedBytes.getData(), truncatedBytes.getSize() / 2));
    assert (rig.processor.loadSample (truncatedFile).isNotEmpty() && rig.processor.sample() == keptPtr);

    // 4. NaN and Inf samples are refused before publishing.
    {
        juce::AudioBuffer<float> nanBuf (1, 512);
        nanBuf.clear();
        nanBuf.setSample (0, 250, std::numeric_limits<float>::quiet_NaN());
        const auto nanFile = writeFloatWav ("nan_sample", 44100, nanBuf);
        assert (rig.processor.loadSample (nanFile).isNotEmpty() && rig.processor.sample() == keptPtr);

        juce::AudioBuffer<float> infBuf (2, 512);
        infBuf.clear();
        infBuf.setSample (1, 100, std::numeric_limits<float>::infinity());
        const auto infFile = writeFloatWav ("inf_sample", 44100, infBuf);
        assert (rig.processor.loadSample (infFile).isNotEmpty() && rig.processor.sample() == keptPtr);
    }

    // 5. Channel count above kMaxSampleChannels (e.g. 3 channels) is refused.
    const auto threeChannels = writeTone ("three_ch", 44100, 0.2, 440, 3);
    assert (rig.processor.loadSample (threeChannels).isNotEmpty() && rig.processor.sample() == keptPtr);

    // The previous sample is still sounding uninterrupted after all refused loads.
    assert (rms (rig.run (.05), 0, 1200) > .05);

    // Stereo files mix down to mono (until Ticket 04 adds stereo playback).
    assert (rig.processor.loadSample (writeTone ("stereo", 44100, 1, 440, 2)).isEmpty());
    assert (std::abs (rig.processor.sample()->data[100] - .8f * (float) std::sin (juce::MathConstants<double>::twoPi * 440 * 100 / 44100)) < 1.0e-3f);
}

void testMemoryBudgetAndUndoEviction()
{
    // A max-length mono sample is 2^22 floats = 16 MiB. 16 distinct instances equal the 256 MiB aggregate budget.
    folder.createDirectory();
    const auto maxFile = folder.getChildFile ("budget16mb.wav");
    {
        maxFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (maxFile.createOutputStream().release());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 1, 16, {}, 0));
        stream.release();
        juce::AudioBuffer<float> block (1, 1 << 16);
        for (int i = 0; i < block.getNumSamples(); ++i) block.setSample (0, i, 0.25f);
        for (int written = 0; written < bw::kMaxSampleFrames; written += block.getNumSamples())
            assert (writer->writeFromAudioSampleBuffer (block, 0, block.getNumSamples()));
    }

    // Part A: when voices settle between loads, old undo steps are evicted so total memory stays <= 256 MiB.
    {
        Rig rig;
        sampleOnly (rig);
        for (int i = 0; i < 20; ++i)
        {
            assert (rig.processor.loadSample (maxFile).isEmpty());
            rig.run (.02); // voices settle on the new sample; retired is cleared on next check
            assert (rig.processor.assetMemoryBytes() <= WaveProcessor::kAssetMemoryBudgetBytes);
        }
        assert (rig.processor.retiredMemoryBytes() == 0);
        assert (rig.processor.assetMemoryBytes() <= WaveProcessor::kAssetMemoryBudgetBytes);
    }

    // Part B: when an active voice retains the initial sample across swaps, retired samples cannot be freed yet.
    // Loading a 17th 16 MiB sample (272 MiB > 256 MiB) is refused with an explicit budget error message,
    // keeping the 16th sample active; once the voice finishes its fade, retired memory is reclaimed.
    {
        Rig rig (48000);
        sampleOnly (rig);
        rig.set ("sampleLoop", 1);
        rig.processor.setSampleSwapSeconds (2.0f);
        assert (rig.processor.loadSample (maxFile).isEmpty());
        std::weak_ptr<const bw::Sample> firstHeld = rig.processor.sample();
        rig.send (on (60));
        rig.run (.02); // voice is now playing sample #1

        for (int i = 2; i <= 16; ++i)
        {
            assert (rig.processor.loadSample (maxFile).isEmpty());
            rig.run (.01); // 10 ms per step: the 2 s swap fade on sample #1 is still in progress
        }
        assert (! firstHeld.expired());
        assert (rig.processor.assetMemoryBytes() == WaveProcessor::kAssetMemoryBudgetBytes);
        const auto sixteenth = rig.processor.sample();

        // 17th load would require 272 MiB of non-evictable memory: refused with a visible budget message.
        const auto err = rig.processor.loadSample (maxFile);
        assert (err.contains ("256 MB"));
        assert (rig.processor.sample() == sixteenth);
        assert (rig.processor.assetMemoryBytes() == WaveProcessor::kAssetMemoryBudgetBytes);

        // Advance past the 2 s swap retention + the final fade into the 16th sample:
        // retired memory drops to 0 (while undo history still keeps the 16 samples within the 256 MiB cap).
        rig.run (4.2);
        assert (rig.processor.sampleSettled());
        assert (rig.processor.retiredMemoryBytes() == 0);
        assert (rig.processor.assetMemoryBytes() == WaveProcessor::kAssetMemoryBudgetBytes);

        // Now a 17th load succeeds by evicting the oldest undo steps referencing sample #1.
        assert (rig.processor.loadSample (maxFile).isEmpty());
        assert (firstHeld.expired());
        assert (rig.processor.assetMemoryBytes() <= WaveProcessor::kAssetMemoryBudgetBytes);
    }
    maxFile.deleteFile();
}

// Stress: 100 replacements while an old reader is artificially retained for 2 s at 96 kHz (> 65536 samples).
// Verifies no premature free, collection right after final confirmation, ABA undo safety, and zero callback heap ops.
void testStress100SwapsRetainedReader96kHz()
{
    const auto toneA = writeTone ("stressA", 48000, 0.1, 440);
    const auto toneB = writeTone ("stressB", 48000, 0.1, 550);

    Rig rig (96000.0);
    rig.processor.prepareToPlay (96000.0, 512);
    sampleOnly (rig);
    rig.set ("sampleLoop", 1);
    rig.processor.setSampleSwapSeconds (2.0f); // 192,000 samples at 96 kHz (nearly 3x the old 65,536-sample timeout)

    assert (rig.processor.loadSample (toneA).isEmpty());
    std::weak_ptr<const bw::Sample> initialSample = rig.processor.sample();

    // Start 8 voices to exercise polyphonic readers at 96 kHz.
    juce::MidiBuffer midi;
    for (int n = 0; n < 8; ++n) midi.addEvent (on (48 + n * 2), 0);
    renderChecked (rig.processor, midi, 960, 128);

    // Perform 100 replacements over ~1.5 seconds of 96 kHz audio (144,000 samples > 65,536),
    // cycling block sizes 64, 128, 512 and a partial block of 37 samples.
    const std::array<int, 4> blockSizes { 64, 128, 512, 37 };
    for (int swap = 0; swap < 100; ++swap)
    {
        assert (rig.processor.loadSample (swap % 2 == 0 ? toneB : toneA).isEmpty());
        // Exercise ABA undo/redo on one of the swaps (republishing the exact same shared_ptr).
        if (swap == 50)
        {
            assert (rig.processor.undo());
            assert (rig.processor.redo());
        }
        renderChecked (rig.processor, midi, 1440, blockSizes[(size_t) swap % blockSizes.size()]);
        // After 65,536 samples (~swap 46), the old reader is still inside its 2.0 s retention window:
        // initialSample MUST still be alive!
        assert (! initialSample.expired());
    }
    assert (! rig.processor.sampleSettled());
    assert (rig.processor.retiredMemoryBytes() > 0);

    // Finish the remaining 0.55 s of the first 2.0 s retention window: voices then start the second
    // 2.0 s crossfade to the 100th sample. After both complete, everything settles and is collected.
    renderChecked (rig.processor, midi, 96000, 512);
    renderChecked (rig.processor, midi, 160000, 128);
    assert (rig.processor.sampleSettled());
    assert (rig.processor.retiredMemoryBytes() == 0);
    // Step #1's `before` snapshot still holds initialSample at the front of the 100-step undo ring;
    // one more edit step evicts Step #1 from undo history and frees initialSample completely.
    assert (rig.processor.loadSample (toneB).isEmpty());
    renderChecked (rig.processor, midi, 196000, 512);
    assert (rig.processor.sampleSettled());
    assert (initialSample.expired());
    assert (rig.processor.retiredMemoryBytes() == 0);

    // Verify 16-note steal, sustain pedal, mono/legato and all-notes-off release sample references cleanly.
    rig.processor.setSampleSwapSeconds (0.005f);
    for (int n = 0; n < 18; ++n) midi.addEvent (on (36 + n), 0); // steals 2 voices
    midi.addEvent (pedal (true), 0);
    for (int n = 0; n < 18; ++n) midi.addEvent (off (36 + n), 0);
    renderChecked (rig.processor, midi, 2048, 64);
    midi.addEvent (pedal (false), 0);
    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    renderChecked (rig.processor, midi, 8192, 128);
    assert (rig.processor.sampleSettled());

    rig.set ("voiceMode", 2); // Legato
    rig.set ("glide", 0.05f);
    midi.addEvent (on (60), 0);
    renderChecked (rig.processor, midi, 1024, 64);
    midi.addEvent (on (67), 0);
    assert (rig.processor.loadSample (toneA).isEmpty());
    renderChecked (rig.processor, midi, 2048, 64);
    assert (rig.processor.sampleSettled());
}

// Controlled slow reader synchronized via WaitableEvents (zero fragile sleeps).
struct ControlledSlowReader : juce::AudioFormatReader
{
    float value;
    juce::WaitableEvent& entered;
    juce::WaitableEvent& proceed;

    ControlledSlowReader (float sampleValue, int frames, juce::WaitableEvent& onEnter, juce::WaitableEvent& onProceed)
        : juce::AudioFormatReader (nullptr, "ControlledSlow"), value (sampleValue), entered (onEnter), proceed (onProceed)
    {
        sampleRate = 48000.0;
        bitsPerSample = 32;
        lengthInSamples = frames;
        numChannels = 1;
        usesFloatingPointData = true;
    }

    bool readSamples (int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer,
                      juce::int64 startSampleInFile, int numSamples) override
    {
        if (startSampleInFile == 0)
        {
            entered.signal();
            proceed.wait (5000);
        }
        for (int ch = 0; ch < numDestChannels; ++ch)
            if (auto* dst = reinterpret_cast<float*> (destSamples[ch]))
                for (int i = 0; i < numSamples; ++i)
                    dst[startOffsetInDestBuffer + i] = value;
        return true;
    }
};

// Ticket 02: Background loading, progress, cooperative cancellation, out-of-order completion,
// external legacy state restore indicator, editor/processor teardown during active job, and request spam cap.
void testBackgroundLoadCancelAndOutOfOrder()
{
    const auto initialTone = writeTone ("bg_initial", 48000, 0.2, 440);
    const auto fileA = folder.getChildFile ("bg_A.wav");
    const auto fileB = folder.getChildFile ("bg_B.wav");
    const auto fileC = folder.getChildFile ("bg_C.wav");

    Rig rig (48000.0);
    sampleOnly (rig);
    rig.set ("sampleLoop", 1);
    assert (rig.processor.loadSample (initialTone).isEmpty());
    const auto initialPtr = rig.processor.sample();
    rig.send (on (60));
    assert (rms (rig.run (0.05), 0, 1200) > 0.05);

    // 1. Start A, start B, cancel A, finish B first and A second (out of order): only B may publish.
    juce::WaitableEvent enteredA (true), proceedA (true), enteredB (true), proceedB (true);
    const uint64_t idA = rig.processor.startSampleLoad (fileA, [&] (const juce::File&)
    {
        return std::make_unique<ControlledSlowReader> (0.25f, 8192, enteredA, proceedA);
    });
    assert (enteredA.wait (5000));
    assert (rig.processor.sampleLoading());
    // While A is blocked in the background, the previous sample keeps playing on the audio thread.
    assert (rig.processor.sample() == initialPtr);
    assert (rms (rig.run (0.02), 0, 480) > 0.05);

    const uint64_t idB = rig.processor.startSampleLoad (fileB, [&] (const juce::File&)
    {
        return std::make_unique<ControlledSlowReader> (0.75f, 8192, enteredB, proceedB);
    });
    assert (idB > idA);
    assert (enteredB.wait (5000));
    rig.processor.cancelSampleLoad (idA);

    // Let B finish first while A is still stalled.
    proceedB.signal();
    // Wait until B publishes (sampleRevision increments and sample() changes from initialPtr).
    for (int tries = 0; tries < 500 && rig.processor.sample() == initialPtr; ++tries)
        juce::Thread::yield();
    // Now release A so it finishes AFTER B (out-of-order completion).
    proceedA.signal();
    assert (rig.processor.waitForSampleLoad (5000));
    assert (! rig.processor.sampleLoading());
    assert (rig.processor.sample() != nullptr);
    assert (std::abs (rig.processor.sample()->data[0] - 0.75f) < 1.0e-6f);
    assert (rig.processor.samplePath() == fileB);
    const auto activeBPtr = rig.processor.sample();

    // 2. Cancelling the current in-flight job leaves B active and untouched.
    juce::WaitableEvent enteredC (true), proceedC (true);
    const uint64_t idC = rig.processor.startSampleLoad (fileC, [&] (const juce::File&)
    {
        return std::make_unique<ControlledSlowReader> (0.5f, 16384, enteredC, proceedC);
    });
    assert (enteredC.wait (5000));
    rig.processor.cancelSampleLoad (idC);
    assert (! rig.processor.sampleLoading());
    proceedC.signal();
    assert (rig.processor.waitForSampleLoad (5000));
    assert (rig.processor.sample() == activeBPtr);
    assert (rig.processor.samplePath() == fileB);

    // 3. Restoring a legacy external path uses the background loader and exposes loading state.
    {
        juce::MemoryBlock stateBlock;
        rig.processor.getStateInformation (stateBlock);
        auto xml = juce::AudioProcessor::getXmlFromBinary (stateBlock.getData(), (int) stateBlock.getSize());
        assert (xml != nullptr);
        if (auto* sampleEl = xml->getChildByName ("SAMPLE"))
        {
            sampleEl->removeAttribute ("containerVersion");
            sampleEl->removeAttribute ("channels");
            sampleEl->removeAttribute ("frames");
            sampleEl->removeAttribute ("rate");
            sampleEl->removeAttribute ("pcmBytes");
            sampleEl->removeAttribute ("hash");
            sampleEl->removeAttribute ("data");
            sampleEl->setAttribute ("path", initialTone.getFullPathName());
        }
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary (*xml, legacyState);

        juce::WaitableEvent restoreEntered (true), restoreProceed (true);
        Rig restoredRig;
        restoredRig.processor.setRestoreReaderFactory ([&] (const juce::File&)
        {
            return std::make_unique<ControlledSlowReader> (0.6f, 8192, restoreEntered, restoreProceed);
        });
        restoredRig.processor.setStateInformation (legacyState.getData(), (int) legacyState.getSize());
        assert (restoreEntered.wait (5000));
        assert (restoredRig.processor.sampleLoading());
        assert (! restoredRig.processor.sampleMissing());
        restoreProceed.signal();
        assert (restoredRig.processor.waitForSampleLoad (5000));
        assert (! restoredRig.processor.sampleLoading());
        assert (restoredRig.processor.sample() != nullptr);
        assert (std::abs (restoredRig.processor.sample()->data[0] - 0.6f) < 1.0e-6f);
    }

    // 4. Closing the editor and destroying the processor while a background job is running is safe.
    {
        juce::WaitableEvent teardownEntered (true), teardownProceed (true);
        auto proc = std::make_unique<WaveProcessor>();
        auto ed = std::make_unique<WaveEditor> (*proc);
        proc->startSampleLoad (fileA, [&] (const juce::File&)
        {
            return std::make_unique<ControlledSlowReader> (0.4f, 32768, teardownEntered, teardownProceed);
        });
        assert (teardownEntered.wait (5000));
        ed.reset(); // close editor while job is running
        teardownProceed.signal();
        proc.reset(); // destroy processor while job is finishing
    }

    // Cancellation after reader creation must finish without recursively locking the state mutex.
    {
        juce::WaitableEvent factoryEntered (true), factoryProceed (true);
        const auto kept = rig.processor.sample();
        const auto request = rig.processor.startSampleLoad (initialTone, [&] (const juce::File& file)
        {
            factoryEntered.signal();
            assert (factoryProceed.wait (5000));
            return bw::openAudio (file);
        });
        assert (factoryEntered.wait (5000));
        rig.processor.cancelSampleLoad (request);
        factoryProceed.signal();
        assert (rig.processor.waitForSampleLoad (5000));
        assert (rig.processor.sample() == kept);
    }

    // 5. Spamming 40 load requests stays bounded in memory and publishes only the last request.
    for (int i = 1; i <= 40; ++i)
    {
        const float targetVal = 0.01f * (float) i;
        rig.processor.startSampleLoad (folder.getChildFile ("spam_" + juce::String (i) + ".wav"), [targetVal] (const juce::File&)
        {
            struct FastValueReader : juce::AudioFormatReader
            {
                float v;
                FastValueReader (float val) : juce::AudioFormatReader (nullptr, "Fast"), v (val)
                {
                    sampleRate = 48000.0; bitsPerSample = 32; lengthInSamples = 4096; numChannels = 1; usesFloatingPointData = true;
                }
                bool readSamples (int* const* dst, int chs, int off, juce::int64, int n) override
                {
                    for (int c = 0; c < chs; ++c)
                        if (auto* p = reinterpret_cast<float*> (dst[c]))
                            for (int s = 0; s < n; ++s) p[off + s] = v;
                    return true;
                }
            };
            return std::make_unique<FastValueReader> (targetVal);
        });
    }
    assert (rig.processor.waitForSampleLoad (5000));
    assert (rig.processor.sample() != nullptr);
    assert (std::abs (rig.processor.sample()->data[0] - 0.40f) < 1.0e-6f);
    assert (rig.processor.assetMemoryBytes() <= WaveProcessor::kAssetMemoryBudgetBytes);
}

// Helper to write a stereo WAV with distinct Left and Right frequencies.
juce::File writeStereoTone (const juce::String& name, double rate, double seconds, double hzL, double hzR)
{
    folder.createDirectory();
    const auto file = folder.getChildFile (name + ".wav");
    file.deleteFile();
    const int length = (int) (rate * seconds);
    juce::AudioBuffer<float> audio (2, length);
    for (int i = 0; i < length; ++i)
    {
        audio.setSample (0, i, 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * hzL * i / rate));
        audio.setSample (1, i, 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * hzR * i / rate));
    }
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 32, {}, 0));
    assert (writer != nullptr);
    stream.release();
    assert (writer->writeFromAudioSampleBuffer (audio, 0, length));
    return file;
}

// Ticket 03: Portable sample container export/import, deleted original file resilience,
// corruption/truncation/hash/oversize rejection without state loss, and relinkSample with undo/redo.
void testPortableContainerAndRelink()
{
    const auto sourceWav = writeStereoTone ("portable_src", 48000, 0.25, 440.0, 660.0);
    const auto presetDirA = folder.getChildFile ("presets_a");
    const auto presetDirB = folder.getChildFile ("presets_b");
    presetDirA.createDirectory();
    presetDirB.createDirectory();
    const auto presetFileA = presetDirA.getChildFile ("PortablePatch.bwpreset");
    const auto presetFileB = presetDirB.getChildFile ("MovedPortablePatch.bwpreset");

    uint64_t expectedHash = 0;
    std::vector<float> expectedL, expectedR;
    juce::MemoryBlock hostState;
    {
        Rig src (48000.0);
        sampleOnly (src);
        src.set ("sampleStart", 0.12f);
        src.set ("sampleEnd", 0.88f);
        src.set ("sampleLoop", 1.0f);
        src.set ("sampleLevel", 0.77f);
        src.set ("samplePitch", 3.0f);
        src.set ("sampleRoot", 64.0f);
        src.set ("samplePan", -0.25f);
        src.set ("sampleRoute", 2.0f); // Direct
        assert (src.processor.loadSample (sourceWav).isEmpty());
        const auto s = src.processor.sample();
        assert (s != nullptr && s->isStereo() && s->contentHash != 0);
        expectedHash = s->contentHash;
        expectedL = s->data;
        expectedR = s->right;

        assert (src.processor.savePreset (presetFileA));
        src.processor.getStateInformation (hostState);
    }

    // Move the preset to another folder and delete the original WAV completely.
    assert (presetFileA.moveFileTo (presetFileB));
    assert (! presetFileA.existsAsFile() && presetFileB.existsAsFile());
    assert (sourceWav.deleteFile() && ! sourceWav.existsAsFile());

    // Re-import from the moved preset file and from host binary state: PCM, hash, and params must match bit-for-bit.
    for (int mode = 0; mode < 2; ++mode)
    {
        Rig dst (48000.0);
        if (mode == 0)
            assert (dst.processor.loadPreset (presetFileB));
        else
            dst.processor.setStateInformation (hostState.getData(), (int) hostState.getSize());

        const auto restored = dst.processor.sample();
        assert (restored != nullptr && ! dst.processor.sampleMissing());
        assert (restored->isStereo() && restored->numChannels() == 2);
        assert (restored->numFrames() == expectedL.size());
        assert (restored->rate == 48000.0);
        assert (restored->contentHash == expectedHash);
        assert (restored->data == expectedL);
        assert (restored->right == expectedR);
        auto paramVal = [&] (const char* id) { return dst.processor.apvts.getRawParameterValue (id)->load(); };
        assert (std::abs (paramVal ("sampleStart") - 0.12f) < 1.0e-5f);
        assert (std::abs (paramVal ("sampleEnd") - 0.88f) < 1.0e-5f);
        assert (std::abs (paramVal ("sampleLoop") - 1.0f) < 1.0e-5f);
        assert (std::abs (paramVal ("sampleLevel") - 0.77f) < 1.0e-5f);
        assert (std::abs (paramVal ("samplePitch") - 3.0f) < 1.0e-5f);
        assert (std::abs (paramVal ("sampleRoot") - 64.0f) < 1.0e-5f);
        assert (std::abs (paramVal ("samplePan") - -0.25f) < 1.0e-5f);
        assert (std::abs (paramVal ("sampleRoute") - 2.0f) < 1.0e-5f);
    }

    // Reject corrupted/malicious containers without losing the currently loaded sample or parameters.
    Rig guarded (48000.0);
    assert (guarded.processor.loadPreset (presetFileB));
    const auto goodSample = guarded.processor.sample();
    assert (goodSample != nullptr);

    auto testCorruptedPreset = [&] (const juce::String& label, std::function<void (juce::XmlElement&)> mutate)
    {
        auto xml = juce::parseXML (presetFileB);
        assert (xml != nullptr);
        auto* sampleEl = xml->getChildByName ("SAMPLE");
        assert (sampleEl != nullptr);
        mutate (*sampleEl);
        const auto badFile = presetDirB.getChildFile ("corrupt_" + label + ".bwpreset");
        assert (xml->writeTo (badFile));
        assert (! guarded.processor.loadPreset (badFile));
        assert (guarded.processor.sampleLoadError().isNotEmpty());
        assert (guarded.processor.sample() == goodSample);
        assert (std::abs (guarded.processor.apvts.getRawParameterValue ("sampleRoot")->load() - 64.0f) < 1.0e-5f);
    };

    // 1. Truncated Base64 payload
    testCorruptedPreset ("truncated", [] (juce::XmlElement& el)
    {
        const auto data = el.getStringAttribute ("data");
        el.setAttribute ("data", data.substring (0, data.length() / 2));
    });
    // 2. False declared pcmBytes
    testCorruptedPreset ("bad_size", [] (juce::XmlElement& el)
    {
        el.setAttribute ("pcmBytes", el.getIntAttribute ("pcmBytes") + 16);
    });
    // 3. Invalid content hash
    testCorruptedPreset ("bad_hash", [] (juce::XmlElement& el)
    {
        el.setAttribute ("hash", "0000000000000000");
    });
    // 4. Unsupported containerVersion
    testCorruptedPreset ("bad_version", [] (juce::XmlElement& el)
    {
        el.setAttribute ("containerVersion", 99);
    });
    // 5. Oversized declared payload (> 256 MiB) rejected before allocation
    testCorruptedPreset ("oversize", [] (juce::XmlElement& el)
    {
        el.setAttribute ("pcmBytes", "300000000");
    });

    // Legacy missing reference + relinkSample + undo/redo preserving sample parameters.
    const auto missingLegacyPath = folder.getChildFile ("legacy_gone.wav");
    missingLegacyPath.deleteFile();
    {
        auto xml = juce::parseXML (presetFileB);
        assert (xml != nullptr);
        if (auto* sampleEl = xml->getChildByName ("SAMPLE"))
        {
            sampleEl->removeAllAttributes();
            sampleEl->setAttribute ("path", missingLegacyPath.getFullPathName());
        }
        const auto legacyPreset = presetDirB.getChildFile ("legacy_missing.bwpreset");
        assert (xml->writeTo (legacyPreset));
        assert (guarded.processor.loadPreset (legacyPreset));
        assert (guarded.processor.sample() == nullptr && guarded.processor.sampleMissing());
        assert (guarded.processor.samplePath() == missingLegacyPath);

        const auto replacementWav = writeStereoTone ("relinked_stereo", 48000, 0.2, 550.0, 880.0);
        assert (guarded.processor.relinkSample (replacementWav).isEmpty());
        assert (guarded.processor.sample() != nullptr && ! guarded.processor.sampleMissing());
        assert (guarded.processor.samplePath() == replacementWav);
        assert (guarded.processor.undoName() == "SAMPLE RELINK");
        // Sample parameters remain preserved across relink.
        assert (std::abs (guarded.processor.apvts.getRawParameterValue ("sampleRoot")->load() - 64.0f) < 1.0e-5f);
        assert (std::abs (guarded.processor.apvts.getRawParameterValue ("samplePan")->load() - -0.25f) < 1.0e-5f);

        // Undo reverts to the missing state; Redo restores the relinked sample.
        assert (guarded.processor.undo());
        assert (guarded.processor.sample() == nullptr && guarded.processor.sampleMissing());
        assert (guarded.processor.samplePath() == missingLegacyPath);
        assert (guarded.processor.redo());
        assert (guarded.processor.sample() != nullptr && ! guarded.processor.sampleMissing());
        assert (guarded.processor.samplePath() == replacementWav);
    }
}

// Renders stereo output (Left and Right vectors) from Rig.
std::pair<std::vector<float>, std::vector<float>> runStereo (Rig& rig, double seconds)
{
    const int total = (int) (seconds * rig.rate);
    std::vector<float> left, right;
    left.reserve ((size_t) total);
    right.reserve ((size_t) total);
    juce::AudioBuffer<float> audio (2, Rig::kBlock);
    for (int done = 0; done < total; done += Rig::kBlock)
    {
        rig.processor.processBlock (audio, rig.pending);
        rig.pending.clear();
        for (int i = 0; i < Rig::kBlock; ++i)
        {
            left.push_back (audio.getSample (0, i));
            right.push_back (audio.getSample (1, i));
        }
    }
    return { std::move (left), std::move (right) };
}

// High-precision frequency estimation using interpolated zero-crossings for < 1 cent accuracy.
double preciseFrequency (const std::vector<float>& x, size_t from, size_t to, double rate)
{
    double firstCross = -1.0, lastCross = -1.0;
    int cycles = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i)
    {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            const double frac = (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
            const double pos = (double) (i - 1) + frac;
            if (firstCross < 0.0) firstCross = pos;
            else { lastCross = pos; ++cycles; }
        }
    }
    return cycles > 0 && lastCross > firstCross ? (double) cycles * rate / (lastCross - firstCross) : 0.0;
}

// Ticket 04: Stereo L/R preservation, sampleRoot (< 1 cent error), one-shot duration (< 1 sample error),
// pan extremes, mono bus downmix, Filter 1 / Filter 2 / Direct routing, and 2x stereo memory accounting.
void testStereoRootPanAndRouting()
{
    const double rate = 48000.0;
    const auto stereoWav = writeStereoTone ("stereo_440_660", rate, 2.0, 440.0, 660.0);

    // 1. Stereo channel preservation (L = 440 Hz, R = 660 Hz) and 2x memory accounting.
    {
        Rig rig (rate);
        sampleOnly (rig);
        rig.set ("sampleRoute", 2.0f); // Direct (no filter coloration)
        assert (rig.processor.loadSample (stereoWav).isEmpty());
        const auto s = rig.processor.sample();
        assert (s != nullptr && s->isStereo() && s->numChannels() == 2);
        assert (s->bytes() == 2 * s->numFrames() * sizeof (float));

        rig.send (on (60));
        const auto [left, right] = runStereo (rig, 0.5);
        const double freqL = preciseFrequency (left, 2400, left.size() - 1200, rate);
        const double freqR = preciseFrequency (right, 2400, right.size() - 1200, rate);
        const double centsL = std::abs (1200.0 * std::log2 (freqL / 440.0));
        const double centsR = std::abs (1200.0 * std::log2 (freqR / 660.0));
        assert (centsL < 1.0);
        assert (centsR < 1.0);
    }

    // 2. Root note tuning accuracy (< 1 cent error when sampleRoot = 69).
    {
        Rig rig (rate);
        sampleOnly (rig);
        rig.set ("sampleRoute", 2.0f);
        rig.set ("sampleRoot", 69.0f); // A4 is the sample's native pitch
        assert (rig.processor.loadSample (stereoWav).isEmpty());

        // Playing note 69 plays the file at unity pitch (440 Hz on L, 660 Hz on R).
        rig.send (on (69));
        const auto [left69, right69] = runStereo (rig, 0.6);
        const double freq69 = preciseFrequency (left69, 2400, left69.size() - 1200, rate);
        assert (std::abs (1200.0 * std::log2 (freq69 / 440.0)) < 1.0);
        rig.send (off (69));
        rig.run (0.25);

        // Playing note 60 (9 semitones below root 69) transposes by -900 cents within < 1 cent.
        rig.send (on (60));
        const auto [left60, right60] = runStereo (rig, 0.6);
        const double expectedHz = 440.0 * std::exp2 (-9.0 / 12.0);
        const double freq60 = preciseFrequency (left60, 2400, left60.size() - 1200, rate);
        assert (std::abs (1200.0 * std::log2 (freq60 / expectedHz)) < 1.0);
    }

    // 3. Non-looping one-shot duration accuracy (< 1 sample error, accounting for the end fade).
    {
        bw::Sample oneShot;
        oneShot.rate = rate;
        oneShot.data.assign (4800, 0.5f);
        oneShot.right.assign (4800, -0.5f);
        const auto reg = oneShot.region (0.0f, 1.0f);
        bw::SamplePlayer player;
        player.reset (reg);
        int activeSamples = 0;
        const int fadeFrames = (int) (rate * 0.01);
        while (! player.finished && activeSamples < 10000)
        {
            const auto [l, r] = player.nextStereo (oneShot, reg, false, 1.0);
            if (! player.finished)
            {
                if (activeSamples < 4800 - fadeFrames)
                    assert (std::abs (l - 0.5f) < 1.0e-6f && std::abs (r - -0.5f) < 1.0e-6f);
                ++activeSamples;
            }
        }
        // Exact frame count rendered before finished is 4800 (error == 0 samples < 1 sample).
        assert (activeSamples == 4800);
    }

    // 4. Pan extremes (-1 = hard left, +1 = hard right) and mono bus downmix.
    {
        Rig rig (rate);
        sampleOnly (rig);
        rig.set ("sampleRoute", 2.0f);
        assert (rig.processor.loadSample (stereoWav).isEmpty());

        rig.set ("samplePan", -1.0f);
        rig.send (on (60));
        const auto [hardL_L, hardL_R] = runStereo (rig, 0.25);
        assert (rms (hardL_L, 2400, hardL_L.size()) > 0.05);
        assert (rms (hardL_R, 2400, hardL_R.size()) < 1.0e-6);
        rig.send (off (60));
        rig.run (0.2);

        rig.set ("samplePan", 1.0f);
        rig.send (on (60));
        const auto [hardR_L, hardR_R] = runStereo (rig, 0.25);
        assert (rms (hardR_L, 2400, hardR_L.size()) < 1.0e-6);
        assert (rms (hardR_R, 2400, hardR_R.size()) > 0.05);
        rig.send (off (60));
        rig.run (0.2);

        // Mono output bus downmix (1-channel buffer passed to processBlock).
        rig.set ("samplePan", 0.0f);
        juce::AudioBuffer<float> monoBus (1, 512);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        rig.processor.processBlock (monoBus, midi);
        midi.clear();
        rig.processor.processBlock (monoBus, midi);
        assert (monoBus.getRMSLevel (0, 0, 512) > 0.02f);
    }

    // 5. Sample routing: Filter 1 vs Filter 2 vs Direct.
    {
        const auto highTone = writeTone ("high6k", rate, 0.5, 6000.0);
        Rig rig (rate);
        sampleOnly (rig);
        assert (rig.processor.loadSample (highTone).isEmpty());
        // Filter 1: low-pass at 250 Hz (attenuates 6 kHz heavily).
        // Filter 2: enabled in parallel, high-pass at 2000 Hz (passes 6 kHz).
        rig.set ("filterType", 0.0f); // Low 12
        rig.set ("cutoff", 250.0f);
        rig.set ("filter2On", 1.0f);
        rig.set ("filter2Type", 2.0f); // High 12
        rig.set ("cutoff2", 2000.0f);
        rig.set ("filterRouting", 1.0f); // Parallel

        rig.set ("sampleRoute", 0.0f); // Filter 1
        rig.send (on (60));
        const double rmsF1 = rms (rig.run (0.2), 2400, 9600);
        rig.send (off (60));
        rig.run (0.2);

        rig.set ("sampleRoute", 1.0f); // Filter 2
        rig.send (on (60));
        const double rmsF2 = rms (rig.run (0.2), 2400, 9600);
        rig.send (off (60));
        rig.run (0.2);

        rig.set ("sampleRoute", 2.0f); // Direct
        rig.send (on (60));
        const double rmsDirect = rms (rig.run (0.2), 2400, 9600);

        assert (rmsF1 < 0.005);
        assert (rmsF2 > 0.04 && rmsF2 > 10.0 * rmsF1);
        assert (rmsDirect > 0.04 && rmsDirect > 10.0 * rmsF1);
    }
}
} // namespace

namespace
{
void testTranspositionAliasingAndPassband ()
{
    constexpr int count = 16384;
    double worst = 0, worstStop = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int pitch : { -24, -12, -5, 0, 7, 12, 19, 24 })
        {
            const double increment = 48000.0 / rate * std::exp2 (pitch / 12.0);
            const double scale = std::max (1.0, increment);
            bw::Sample sample; sample.data.resize ((size_t) (count * increment) + 4096);
            const double omega = juce::MathConstants<double>::twoPi;
            for (size_t n = 0; n < sample.data.size (); ++n)
                sample.data[n] = (float) ((std::sin (omega * .07 * n / scale)
                    + std::sin (omega * .21 * n / scale) + std::sin (omega * .35 * n / scale)) / 3);
            std::vector<float> residual (count);
            double error = 0, reference = 0, actual = 0;
            for (int n = 0; n < count; ++n)
            {
                const double x = 1024.123 + n * increment;
                const double ideal = (std::sin (omega * .07 * x / scale)
                    + std::sin (omega * .21 * x / scale) + std::sin (omega * .35 * x / scale)) / 3;
                const double y = sample.readFilteredStereo (x, increment).first;
                residual[(size_t) n] = (float) (y - ideal);
                error += (y - ideal) * (y - ideal); reference += ideal * ideal; actual += y * y;
            }
            worst = std::max (worst, std::sqrt (error / reference));
            assert (error / reference < 1.0e-6); // All residual energy, including aliases, below -60 dB.
            assert (std::abs (10 * std::log10 (actual / reference)) < .5);
            const auto bins = spectrum (residual);
            assert (*std::max_element (bins.begin (), bins.end ()) < count * count * 1.0e-6);
            if (increment >= 1.5)
            {
                for (size_t n = 0; n < sample.data.size (); ++n) sample.data[n] = (float) std::sin (omega * .65 * n / increment);
                double energy = 0;
                for (int n = 0; n < count; ++n) { const double y = sample.readFilteredStereo (1024.123 + n * increment, increment).first; energy += y * y; }
                worstStop = std::max (worstStop, std::sqrt (2 * energy / count));
                assert (2 * energy / count < 1.0e-6);
            }
            // Smooth chirp in the useful passband, with continuously varying playback speed.
            const double slope = .1 / (sample.data.size () * scale);
            for (size_t n = 0; n < sample.data.size (); ++n) sample.data[n] = (float) std::sin (omega * (.04 * n / scale + .5 * slope * n * n));
            double position = 1024.123;
            for (int n = 0; n < 2048; ++n)
            {
                const double step = increment * (1 + .01 * std::sin (n * .001));
                const double expected = std::sin (omega * (.04 * position / scale + .5 * slope * position * position));
                assert (std::abs (sample.readFilteredStereo (position, step).first - expected) < .001);
                position += step;
            }
        }
    bw::Sample impulse; impulse.data.resize (4096); impulse.data[2048] = 1;
    assert (std::abs (impulse.readFilteredStereo (2048, 1).first - 1) < 1.0e-6);
    assert (std::abs (impulse.readFilteredStereo (2047, 1).first) < 1.0e-6);
    for (double step : { .25, 1.0, 4.0, 16.0, 63.9, 64.0, 1.0e20 })
        for (double position : { 0.0, 4095.0 }) assert (std::isfinite (impulse.readFilteredStereo (position, step).first));
    volatile float sink = 0;
    const auto begin = juce::Time::getHighResolutionTicks ();
    for (int n = 0; n < count; ++n) sink = impulse.read (1024.123 + n % 100);
    const auto middle = juce::Time::getHighResolutionTicks ();
    for (int n = 0; n < count; ++n) sink = impulse.readFilteredStereo (1024.123 + n % 100, 4).first;
    const auto end = juce::Time::getHighResolutionTicks ();
    juce::ignoreUnused (sink);
    std::cout << "sinc residual dB=" << 20 * std::log10 (worst) << " stop dB=" << 20 * std::log10 (worstStop)
              << " Hermite ms=" << juce::Time::highResolutionTicksToSeconds (middle - begin) * 1000
              << " sinc(step4) ms=" << juce::Time::highResolutionTicksToSeconds (end - middle) * 1000 << std::endl;
}

void sampleRoute (Rig& r, int destination, float depth, bool bipolar = false)
{
    r.set ("mod1Enabled", 1); r.set ("mod1Source", 3); r.set ("mod1Destination", (float) destination);
    r.set ("mod1Amount", depth); r.set ("mod1Bipolar", bipolar ? 1.0f : 0.0f);
}
void testSampleModulation ()
{
    const std::array<const char*, 24> legacy { "wtPos", "level", "oscAPan", "oscAPitch", "oscADetune", "oscASpread",
        "wtPosB", "levelB", "oscBPan", "oscBPitch", "oscBDetune", "oscBSpread", "cutoff", "reso", "subLevel", "noiseLevel",
        "cutoff2", "reso2", "distDrive", "delayMix", "reverbMix", "oscAWarp", "oscBWarp", "fmDepth" };
    for (size_t i = 0; i < legacy.size (); ++i) assert (juce::String (legacy[i]) == bw::destinationIds ()[i]);
    const auto file = writeTone ("mod-sample", 48000, 1.0, 337);
    for (int destination : { bw::kSampleLevelDestination, bw::kSamplePanDestination, bw::kSamplePitchDestination })
    {
        Rig base, modulated;
        for (auto* r : { &base, &modulated }) { sampleOnly (*r); r->set ("sampleRoute", 2); assert (r->processor.loadSample (file).isEmpty ()); r->send (on (60)); }
        sampleRoute (modulated, destination, 0, true);
        assert (base.run (.15) == modulated.run (.15));
        sampleRoute (modulated, destination, 1, true);
        for (int i = 0; i < 12; ++i) { modulated.set ("macro1", (float) (i % 2)); modulated.run (.005); }
        juce::MemoryBlock state; modulated.processor.getStateInformation (state);
        Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
        assert ((int) restored.processor.apvts.getRawParameterValue ("mod1Destination")->load () == destination);
        assert (restored.processor.apvts.getRawParameterValue ("mod1Bipolar")->load () > .5f);
    }
    for (const int destination : { bw::kSampleLevelDestination, bw::kSamplePanDestination, bw::kSamplePitchDestination })
        for (float macro : { 0.0f, 1.0f })
        {
            Rig r; sampleOnly (r); r.set ("sampleRoute", 2); r.set ("macro1", macro);
            assert (r.processor.loadSample (file).isEmpty ()); sampleRoute (r, destination, 1, true); r.send (on (60));
            const auto audio = r.run (.2);
            if (destination == bw::kSamplePitchDestination)
                assert (std::abs (frequency (audio) / (337 * (macro < .5f ? .25 : 4)) - 1) < .005);
            if (destination == bw::kSampleLevelDestination)
                assert (macro < .5f ? silent (audio) : peak (audio) > .1f);
            if (destination == bw::kSamplePanDestination)
            {
                juce::AudioBuffer<float> stereo (2, 512); juce::MidiBuffer midi;
                r.processor.processBlock (stereo, midi);
                const double left = stereo.getRMSLevel (0, 0, 512), right = stereo.getRMSLevel (1, 0, 512);
                assert (macro < .5f ? (left > .05 && right < 1.0e-6) : (right > .05 && left < 1.0e-6));
            }
        }
    Rig running, muted;
    for (auto* r : { &running, &muted }) { sampleOnly (*r); r->set ("sampleRoute", 2); assert (r->processor.loadSample (file).isEmpty ()); r->send (on (60)); }
    sampleRoute (muted, bw::kSampleLevelDestination, 1, true); muted.set ("macro1", 0);
    running.run (.1); assert (silent (muted.run (.1)));
    muted.set ("mod1Amount", 0); running.run (.05); muted.run (.05);
    assert (running.run (.1) == muted.run (.1)); // Exact head continuity after a silent interval.
}

juce::Component* sampleControl (juce::Component& parent, const juce::String& title)
{
    if (parent.getTitle () == title) return &parent;
    for (auto* child : parent.getChildren ()) if (auto* found = sampleControl (*child, title)) return found;
    return nullptr;
}
void testSampleZoneUI ()
{
    Rig rig;
    const auto file = writeTone ("ui-zone", 48000, .1, 220);
    assert (rig.processor.loadSample (file).isEmpty ());
    std::unique_ptr<juce::AudioProcessorEditor> editor (rig.processor.createEditor ());
    for (const auto* name : { "SAMPLE LEVEL", "SAMPLE PAN", "SAMPLE PITCH" })
    {
        auto* dial = dynamic_cast<bw::ui::Dial*> (sampleControl (*editor, name));
        assert (dial && dial->onModulationDrop);
        dial->onModulationDrop (3, dial->parameterId ());
    }
    for (int i = 1; i <= 3; ++i)
        assert ((int) rig.processor.apvts.getRawParameterValue ("mod" + juce::String (i) + "Destination")->load () == 23 + i);
    auto* add = sampleControl (*editor, "ADD ZONE"); assert (add);
    add->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    assert (rig.processor.sampleBank ()->zones.size () == 2);
    auto* low = dynamic_cast<juce::Slider*> (sampleControl (*editor, "Zone low key")); assert (low);
    low->setValue (60, juce::sendNotificationSync); // Overlaps zone 0, must preserve the valid map.
    assert (rig.processor.sampleBank ()->zones[1].lowKey == 64);
    low->setValue (65, juce::sendNotificationSync);
    assert (rig.processor.sampleBank ()->zones[1].lowKey == 65);
    rig.processor.startSampleLoad (file, {}, 1);
    assert (rig.processor.waitForSampleLoad (5000));
    assert (rig.processor.sampleBank ()->zones.size () == 2);
    assert (rig.processor.sampleBank ()->zones[1].path == file);
    sampleControl (*editor, "SAMPLE")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    juce::Thread::sleep (80);
    juce::Timer::callPendingTimersSynchronously ();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds ());
    assert (image.isValid ());
    auto output = juce::File::getCurrentWorkingDirectory ().getChildFile ("build/sample-zones.png").createOutputStream ();
    if (output) { output->setPosition (0); output->truncate (); juce::PNGImageFormat ().writeImageToStream (image, *output); }
}

void testMultisampleTwoZonesAndLegato ()
{
    const auto lowFile = writeTone ("zone-low", 48000, .5, 220), highFile = writeTone ("zone-high", 48000, .5, 880);
    juce::String error;
    bw::SampleBank bank;
    bank.zones = { { bw::loadSample (lowFile, error), lowFile, 36, 59, 57, 0, 1, true },
                   { bw::loadSample (highFile, error), highFile, 60, 72, 81, 0, 1, true } };
    assert (bank.valid ());
    Rig r; sampleOnly (r); r.set ("sampleRoute", 2);
    assert (r.processor.setSampleBank (bank).isEmpty ());
    for (int note : { 24, 36, 59, 60, 72, 84 })
    {
        r.send (juce::MidiMessage::allSoundOff (1)); r.run (.15); r.send (on (note));
        const auto audio = r.run (.12);
        if (note == 24 || note == 84) assert (silent (audio));
        else { assert (near (frequency (audio), note)); assert (r.processor.activeSampleZone.load () == (note < 60 ? 0 : 1)); }
    }
    auto invalid = bank; invalid.zones[0].highKey = 60;
    assert (r.processor.setSampleBank (invalid).contains ("overlapping"));
    invalid = bank; invalid.zones[0].start = std::numeric_limits<float>::quiet_NaN ();
    assert (r.processor.setSampleBank (invalid).isNotEmpty ());
    const auto bytes = r.processor.assetMemoryBytes ();
    auto duplicate = bank; duplicate.zones[1].sample = duplicate.zones[0].sample;
    assert (duplicate.bytes () == duplicate.zones[0].sample->bytes ());
    const auto sharedRoundTrip = bw::deserializeSampleBank (bw::serializeSampleBank (duplicate), error);
    assert (sharedRoundTrip && sharedRoundTrip->zones[0].sample == sharedRoundTrip->zones[1].sample);
    auto badReference = bw::serializeSampleBank (duplicate);
    badReference.getChild (1).setProperty ("sampleZone", 1, nullptr);
    assert (! bw::deserializeSampleBank (badReference, error));
    assert (bytes <= WaveProcessor::kAssetMemoryBudgetBytes);
    r.set ("voiceMode", 2); r.send (juce::MidiMessage::allSoundOff (1)); r.run (.05);
    r.send (on (59)); const auto previous = r.run (.1);
    r.send (on (60)); const auto switched = renderChecked (r.processor, r.pending, 9600, 64);
    assert (maxStep (switched, 0, switched.size (), previous.back ()) < .1f);
    assert (r.processor.activeSampleZone.load () == 1);
    auto changed = bank; changed.zones[0].sample = bank.zones[1].sample;
    r.processor.setSampleSwapSeconds (.1f);
    assert (r.processor.setSampleBank (changed).isEmpty ());
    r.run (.001); assert (! r.processor.sampleSettled ());
    renderChecked (r.processor, r.pending, 9600, 64); assert (r.processor.sampleSettled ());
    assert (r.processor.undo ()); assert (r.processor.sampleBank ()->zones[0].sample == bank.zones[0].sample);
    juce::MemoryBlock state; r.processor.getStateInformation (state);
    const auto preset = folder.getChildFile ("zone-bank.bwpreset");
    assert (r.processor.savePreset (preset));
    lowFile.deleteFile (); highFile.deleteFile ();
    Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
    assert (restored.processor.sampleBank ()->zones.size () == 2);
    assert (restored.processor.loadPreset (preset));
    assert (restored.processor.sampleBank ()->zones.size () == 2);
    assert (restored.processor.sampleBank ()->zones[1].sample->contentHash == bank.zones[1].sample->contentHash);
    restored.send (on (60)); assert (! silent (restored.run (.1)));
    {
        bw::SampleBank oversized;
        for (int i = 0; i < 17; ++i)
        {
            auto asset = std::make_shared<bw::Sample> (); asset->data.resize (bw::kMaxSampleFrames);
            oversized.zones.push_back ({ asset, {}, i, i, 60, 0, 1, false });
        }
        const auto before = restored.processor.sampleBank ();
        assert (restored.processor.setSampleBank (oversized).contains ("256 MB"));
        assert (restored.processor.sampleBank () == before);
    }
    auto fullMap = bank; fullMap.zones.clear ();
    for (int note = 0; note < 128; ++note) fullMap.zones.push_back ({ bank.zones[0].sample, {}, note, note, note, 0, 1, false });
    assert (fullMap.valid ()); fullMap.zones.push_back (fullMap.zones[0]); assert (! fullMap.valid ());
    auto malformed = bw::serializeSampleBank (bank); malformed.getChild (1).setProperty ("low", 59, nullptr);
    assert (! bw::deserializeSampleBank (malformed, error));
    malformed.setProperty ("version", 99, nullptr); assert (! bw::deserializeSampleBank (malformed, error));
    std::cout << "ok: sample modulation and portable zones" << std::endl;
}
} // namespace

namespace
{
std::shared_ptr<const bw::Sample> engineFixture (double hz = 0, int frames = 16384, double rate = 48000)
{
    auto sample = std::make_shared<bw::Sample> (); sample->rate = rate; sample->data.resize ((size_t) frames);
    for (int i = 0; i < frames; ++i) sample->data[(size_t) i] = hz > 0 ? .5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate) : .5f;
    sample->updateHash (); return sample;
}
void testVelocityLayersAndCrossfadeLaws ()
{
    const auto sample = engineFixture (375);
    bw::SampleBank bank; bank.zones = { { sample, {}, 0, 127, 60, 0, 1, true }, { sample, {}, 0, 127, 60, 0, 1, true } };
    bank.zones[0].highVel = 63; bank.zones[1].lowVel = 64;
    for (auto& z : bank.zones) z.crossfadeVel = 12;
    assert (bank.valid ());
    assert (bank.selectVelocityLayers (60, 0).zones[0] == -1);
    double worstRms = 0;
    for (auto law : { bw::VelocityCrossfadeLaw::linear, bw::VelocityCrossfadeLaw::equalPower })
    {
        for (auto& z : bank.zones) z.crossfadeLaw = law;
        for (int velocity = 1; velocity <= 127; ++velocity)
        {
            const auto selected = bank.selectVelocityLayers (60, velocity);
            const double a = selected.weights[0], b = selected.weights[1];
            if (law == bw::VelocityCrossfadeLaw::linear)
            {
                assert (std::abs (a + b - 1) < 1.0e-6);
                assert (20 * std::log10 (a + b) <= .1);
            }
            else
            {
                double energy = 0;
                for (int n = 0; n < 1024; ++n)
                {
                    const double x = a * std::sin (juce::MathConstants<double>::twoPi * 8 * n / 1024)
                                   + b * std::sin (juce::MathConstants<double>::twoPi * 13 * n / 1024);
                    energy += x * x;
                }
                worstRms = std::max (worstRms, std::abs (10 * std::log10 (energy / 512)));
                assert (worstRms < .001);
            }
        }
    }
    for (auto& z : bank.zones) z.crossfadeLaw = bw::VelocityCrossfadeLaw::linear;
    Rig layered, baseline;
    for (auto* r : { &layered, &baseline }) { sampleOnly (*r); r->set ("sampleRoute", 2); }
    assert (layered.processor.setSampleBank (bank).isEmpty ());
    auto single = bank; single.zones.resize (1); single.zones[0].highVel = 127; single.zones[0].crossfadeVel = 0;
    assert (baseline.processor.setSampleBank (single).isEmpty ());
    for (int velocity = 1; velocity <= 127; ++velocity)
    {
        for (auto* r : { &layered, &baseline }) { r->processor.prepareToPlay (48000, 64); r->send (on (60, (float) velocity / 127)); }
        const auto a = layered.run (.004), b = baseline.run (.004);
        double diff = 0, reference = 0;
        for (size_t n = 0; n < a.size (); ++n) { diff += (a[n] - b[n]) * (a[n] - b[n]); reference += b[n] * b[n]; }
        assert (diff / std::max (reference, 1.0e-20) < 1.0e-10);
    }
    juce::String error;
    const auto portable = bw::deserializeSampleBank (bw::serializeSampleBank (bank), error);
    assert (portable && portable->zones[0].crossfadeVel == 12 && portable->zones[1].lowVel == 64);
    auto bad = bank; bad.zones.push_back (bank.zones[0]); assert (! bad.valid ());
    std::cout << "ok: all 127 velocities, linear null, equal-power RMS variation dB=" << worstRms << std::endl;
}
void testRoundRobinSequenceAndDeterminism ()
{
    bw::SampleBank bank; bank.zones.push_back ({ engineFixture (440), {}, 0, 60, 60, 0, 1, true });
    bank.zones.push_back ({ engineFixture (220), {}, 61, 127, 60, 0, 1, true });
    auto& zone = bank.zones[0]; zone.numVariants = 4;
    for (int v = 1; v < 4; ++v) zone.variants[(size_t) v].sample = engineFixture (v == 1 ? 550 : v == 2 ? 660 : 880);
    Rig r; sampleOnly (r); r.set ("sampleRoute", 2); assert (r.processor.setSampleBank (bank).isEmpty ());
    for (double hz : { 440.0, 550.0, 660.0, 880.0 })
    {
        r.send (on (60)); const auto audio = r.run (.1);
        assert (std::abs (frequency (audio) / hz - 1) < .005);
        r.send (off (60)); r.run (.1);
    }
    for (int i = 0; i < 100; ++i)
    {
        r.send (on (60)); r.run (.001);
        assert (r.processor.samplePlayback.lastVariant[0].load () == i % 4);
        r.send (off (60)); r.send (pedal ((i % 2) != 0)); r.send (on (72)); r.send (off (72)); r.send (on (60, 0)); r.run (.001);
        assert (r.processor.samplePlayback.lastVariant[0].load () == i % 4);
    }
    r.send (juce::MidiMessage::allNotesOff (1)); r.run (.1); r.send (on (60)); r.run (.01);
    assert (r.processor.samplePlayback.lastVariant[0].load () == 0);
    bank.zones[0].variants[1] = { nullptr, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("missing-rr.wav"), true };
    assert (r.processor.setSampleBank (bank).isEmpty ()); r.processor.prepareToPlay (48000, 64);
    r.send (on (60)); r.run (.01); r.send (off (60)); r.run (.1); r.send (on (60));
    assert (silent (r.run (.02))); assert (r.processor.samplePlayback.lastVariant[0].load () == 1);
    juce::MemoryBlock state; r.processor.getStateInformation (state);
    auto render = [&] ()
    {
        Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ()); restored.processor.prepareToPlay (48000, 64);
        assert (restored.processor.sampleBank ()->zones[0].numVariants == 4);
        assert (restored.processor.sampleBank ()->zones[0].variants[1].missing);
        std::vector<float> audio;
        for (int i = 0; i < 8; ++i) { restored.send (on (60)); auto chunk = restored.run (.015); audio.insert (audio.end (), chunk.begin (), chunk.end ()); restored.send (off (60)); restored.run (.06); }
        return audio;
    };
    assert (render () == render ());
    std::cout << "ok: 100 round-robin notes, independent zones, missing slot and repeatable restore" << std::endl;
}
void testGranularCloudPoolAndRipple ()
{
    const auto dc = engineFixture ();
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        bw::GranularVoiceEngine engine; engine.reset (rate, 1);
        bw::GrainSources sources { bw::GrainSource { dc.get (), 64, 16384, 1, 1 }, {} };
        bw::GrainControls controls; controls.spread = 0;
        double lo = 1, hi = 0;
        for (int n = 0; n < (int) (rate * .3); ++n)
        {
            const auto before = gAudioHeapOps.load ();
            std::pair<float, float> value;
            { ScopedAudioHeapGuard guard; value = engine.process (sources, controls, 1); }
            assert (before == gAudioHeapOps.load ()); assert (engine.activeCount () <= 32);
            if (n > (int) (rate * .15)) { lo = std::min (lo, (double) value.first); hi = std::max (hi, (double) value.first); }
        }
        // 40 Hz x 100 ms is a Hann overlap of 4 (window sum 2): power normalisation lifts correlated DC by sqrt (2).
        assert (20 * std::log10 (hi / lo) < .1); assert (std::abs (hi - .5 * std::sqrt (2.0)) < 1.0e-3);
    }
    std::array<bw::GranularVoiceEngine, 16> voices;
    for (auto& voice : voices) voice.reset (8000, 9);
    bw::GrainSources sources { bw::GrainSource { dc.get (), 10, 100, 1, 1 }, {} };
    bw::GrainControls dense { 100, 500, 1, 2, 1 }; dense.loop = true; // looped, so long grains keep a 90-frame region
    const float ceiling = .5f * std::sqrt (32.0f) + 1.0e-5f;        // 32 correlated DC grains
    int maximum = 0;
    for (int n = 0; n < 4800; ++n)
    {
        int count = 0;
        for (auto& voice : voices) { const auto value = voice.process (sources, dense, 1); assert (std::isfinite (value.first) && std::abs (value.first) <= ceiling); count += voice.activeCount (); }
        maximum = std::max (maximum, count); assert (count <= 512);
    }
    assert (maximum == 512); for (auto& voice : voices) assert (voice.saturated);
    auto bounded = std::make_shared<bw::Sample> (); bounded->data.assign (1024, 100); std::fill (bounded->data.begin () + 101, bounded->data.begin () + 200, .5f);
    bw::GranularVoiceEngine edge; edge.reset (8000, 1);
    sources[0] = { bounded.get (), 100.5, 200, 1, 1 };
    for (int i = 0; i < 1600; ++i) { const auto value = edge.process (sources, dense, 4); assert (value.first <= ceiling); }
    std::cout << "ok: Hann DC ripple <0.1 dB, region bounds, 32/512 caps and allocation-free engine" << std::endl;
}
void testGranularDensityDurationAndBlockInvariance ()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (double density : { 1.0, 37.3, 100.0 })
        {
            bw::GranularScheduler scheduler; int events = 0;
            for (int n = 0; n < (int) (rate * 10); ++n) if (scheduler.tick (rate, density)) ++events;
            assert (std::abs (events / (density * 10) - 1) <= .01);
        }
    const auto sample = engineFixture (317, 8192, 8000);
    auto render = [&] (int block)
    {
        bw::GranularVoiceEngine engine; engine.reset (8000, 1234);
        bw::GrainSources sources { bw::GrainSource { sample.get (), 0, 8192, 1, 1 }, {} };
        bw::GrainControls controls { 37.3f, 93, .3f, -.4f, .2f };
        std::vector<float> audio; std::vector<int> events;
        for (int base = 0; base < 8000; base += block)
            for (int n = base; n < std::min (base + block, 8000); ++n)
            {
                const auto before = engine.spawned; audio.push_back (engine.process (sources, controls, 1).first);
                if (engine.spawned != before) events.push_back (n);
            }
        return std::make_pair (audio, events);
    };
    const auto reference = render (64);
    for (int block : { 37, 128, 512 }) assert (render (block) == reference);
    for (float size : { 5.0f, 500.0f }) for (float density : { 1.0f, 100.0f })
    {
        bw::GranularVoiceEngine engine; engine.reset (8000, 1);
        bw::GrainSources sources { bw::GrainSource { sample.get (), 0, 8192, 1, 1 }, {} };
        for (int n = 0; n < 1600; ++n) assert (std::isfinite (engine.process (sources, { density, size, .5f, 2, 1 }, .25).first));
    }
    bw::GranularVoiceEngine latched; latched.reset (8000, 1);
    const bw::GrainSources latchedSources { bw::GrainSource { sample.get (), 0, 8192, 1, 1 }, {} };
    for (int n = 0; n < 100; ++n) latched.process (latchedSources, { 1, 500, .1f, 0, 0 }, 1);
    bw::GrainDisplaySnapshot before, after; latched.publish (before);
    latched.process (latchedSources, { 100, 5, .8f, 0, 0 }, 2); latched.publish (after);
    assert (latched.spawned == 1);
    assert (std::abs ((after.grains[0].normPos.load () - before.grains[0].normPos.load ()) * 8192 - 2) < .001);
    assert (std::abs (after.grains[0].normPhase.load () - before.grains[0].normPhase.load () - 1.0f / 4000) < 1.0e-6);
    bw::GranularVoiceEngine invalid; invalid.reset (std::numeric_limits<double>::quiet_NaN (), 1);
    const float nan = std::numeric_limits<float>::quiet_NaN ();
    for (int i = 0; i < 100; ++i) assert (std::isfinite (invalid.process (latchedSources, { nan, nan, nan, nan, nan }, nan).first));
    std::cout << "ok: 10-second density accuracy and bit-identical 37/64/128/512 scheduling" << std::endl;
}
void testGranularModulationAndPortableRoundTrip ()
{
    const auto file = writeTone ("portable-grains", 8000, .25, 317);
    Rig setup (8000); sampleOnly (setup); setup.set ("sampleRoute", 2); setup.set ("sampleMode", 1);
    setup.set ("grainSeed", 42); setup.set ("grainSpreadPos", .2f); setup.set ("grainScan", .3f);
    assert (setup.processor.loadSample (file).isEmpty ());
    sampleRoute (setup, bw::kGrainPositionDestination, .2f); setup.set ("macro1", .25f);
    const auto preset = folder.getChildFile ("granular-portable.bwpreset"); assert (setup.processor.savePreset (preset));
    juce::MemoryBlock state; setup.processor.getStateInformation (state); file.deleteFile ();
    auto render = [&] (int block, bool withEditor, bool usePreset = false)
    {
        Rig r (8000);
        if (usePreset) assert (r.processor.loadPreset (preset)); else r.processor.setStateInformation (state.getData (), (int) state.getSize ());
        r.processor.prepareToPlay (8000, block);
        std::unique_ptr<juce::AudioProcessorEditor> editor (withEditor ? r.processor.createEditor () : nullptr);
        r.send (on (60)); return renderChecked (r.processor, r.pending, 4000, block);
    };
    const auto reference = render (64, false);
    assert (reference == render (64, true));
    assert (reference == render (64, false, true));
    for (int block : { 37, 128, 512 })
    {
        const auto audio = render (block, false); double error = 0;
        for (size_t n = 0; n < audio.size (); ++n) error += (audio[n] - reference[n]) * (audio[n] - reference[n]);
        assert (std::sqrt (error / audio.size ()) < 3.1623e-5); // -90 dBFS.
    }
    Rig stress (8000); stress.processor.setStateInformation (state.getData (), (int) state.getSize ());
    stress.set ("grainDensity", 100); stress.set ("grainSize", 500); stress.set ("grainLoop", 1); // long grains wrap in the short tone
    for (int note = 48; note < 64; ++note) stress.send (on (note));
    renderChecked (stress.processor, stress.pending, 2800, 64);
    assert (stress.processor.activeGrains.load () == 512 && stress.processor.grainPoolSaturated.load ());
    for (int destination : { bw::kGrainPositionDestination, bw::kGrainSizeDestination, bw::kGrainDensityDestination, bw::kGrainPitchDestination })
    {
        sampleRoute (stress, destination, 1, true);
        for (int n = 0; n < 4; ++n) { stress.set ("macro1", (float) (n % 2)); renderChecked (stress.processor, stress.pending, 256, 37); }
    }
    const auto replacement = writeTone ("grain-swap", 8000, .25, 550);
    stress.processor.setSampleSwapSeconds (.001f); assert (stress.processor.loadSample (replacement).isEmpty ());
    renderChecked (stress.processor, stress.pending, 64, 64); assert (! stress.processor.sampleSettled ());
    stress.set ("sampleMode", 0); renderChecked (stress.processor, stress.pending, 4800, 64);
    assert (stress.processor.sampleSettled ());
    assert (stress.processor.assetMemoryBytes () <= bw::kMaxAssetMemoryBytes);
    std::cout << "ok: granular UI invariance, portable null, 16-voice automation and retained grains" << std::endl;
}
void testLayersAndGranularUI ()
{
    Rig r; sampleOnly (r);
    bw::SampleBank bank; bank.zones.push_back ({ engineFixture (440), {}, 0, 127, 60, 0, 1, true });
    assert (r.processor.setSampleBank (bank).isEmpty ());
    std::unique_ptr<juce::AudioProcessorEditor> editor (r.processor.createEditor ());
    sampleControl (*editor, "SAMPLE")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    auto* panel = sampleControl (*editor, "Sample source"); assert (panel);
    sampleControl (*editor, "ADD LAYER")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    assert (r.processor.sampleBank ()->zones.size () == 2);
    auto* law = dynamic_cast<juce::ComboBox*> (sampleControl (*editor, "Velocity crossfade law")); assert (law);
    law->setSelectedItemIndex (1, juce::sendNotificationSync);
    assert (r.processor.sampleBank ()->zones[0].crossfadeLaw == bw::VelocityCrossfadeLaw::equalPower);
    assert (r.processor.undo ()); assert (r.processor.sampleBank ()->zones[0].crossfadeLaw == bw::VelocityCrossfadeLaw::linear);
    assert (r.processor.redo ()); panel->visibilityChanged ();
    auto* count = dynamic_cast<juce::Slider*> (sampleControl (*editor, "Round robin count")); assert (count);
    count->setValue (4, juce::sendNotificationSync); panel->visibilityChanged ();
    auto* choice = dynamic_cast<juce::ComboBox*> (sampleControl (*editor, "Round robin variant")); assert (choice);
    assert (choice->getNumItems () == 4); choice->setSelectedItemIndex (1, juce::sendNotificationSync);
    assert (choice->getText ().contains ("MISSING"));
    assert (sampleControl (*editor, "RELINK")->isEnabled ());
    const auto file = writeTone ("rr-relink", 48000, .1, 550);
    r.processor.startSampleLoad (file, {}, 1, 1); assert (r.processor.waitForSampleLoad (5000));
    assert (r.processor.sampleBank ()->zones.size () == 2 && r.processor.sampleBank ()->zones[1].sampleForVariant (1));
    sampleControl (*editor, "Sample playback mode")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    assert (r.processor.apvts.getRawParameterValue ("sampleMode")->load () > .5f);
    int slot = 1;
    for (const auto* title : { "POSITION", "GRAIN SIZE", "DENSITY", "GRAIN PITCH" })
    {
        auto* dial = dynamic_cast<bw::ui::Dial*> (sampleControl (*panel, title)); assert (dial && dial->onModulationDrop);
        dial->onModulationDrop (3, dial->parameterId ());
        assert ((int) r.processor.apvts.getRawParameterValue ("mod" + juce::String (slot) + "Destination")->load () == 26 + slot);
        ++slot;
    }
    panel->visibilityChanged ();
    sampleControl (*editor, "SAMPLE")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    panel->visibilityChanged ();
    auto stream = juce::File::getCurrentWorkingDirectory ().getChildFile ("build/granular-layers.png").createOutputStream ();
    if (stream) { stream->setPosition (0); stream->truncate (); juce::PNGImageFormat ().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds ()), *stream); }
    // Splitting a key range carries all its velocity layers into the new range.
    sampleControl (*editor, "ADD ZONE")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    assert (r.processor.sampleBank ()->zones.size () == 4 && r.processor.sampleBank ()->valid ());
    std::cout << "ok: velocity/RR UI, undo/redo, granular route binding and layered key split" << std::endl;
}

// A ramp whose value is its frame index identifies every frame the head reads.
std::vector<int> playedFrames (const bw::Sample& ramp, std::pair<double, double> region, const bw::PlaybackSettings& s,
                               int count, int releaseAt = -1, double increment = 1)
{
    bw::SamplePlayer player; player.reset (region);
    std::vector<int> frames;
    for (int i = 0; i < count && ! player.finished; ++i)
    {
        const auto value = player.nextStereo (ramp, region, s, increment, releaseAt >= 0 && i >= releaseAt).first;
        if (! player.finished) frames.push_back ((int) std::lround (value));
    }
    return frames;
}
std::vector<int> span (int first, int last) // inclusive, either direction
{
    std::vector<int> result;
    for (int f = first; ; f += first <= last ? 1 : -1) { result.push_back (f); if (f == last) break; }
    return result;
}
std::vector<int> join (std::initializer_list<std::vector<int>> parts)
{
    std::vector<int> result;
    for (const auto& p : parts) result.insert (result.end (), p.begin (), p.end ());
    return result;
}

void testRegionLoopDirectionsAndTailedRelease ()
{
    bw::Sample ramp; ramp.rate = 48000;
    for (int i = 0; i < 400; ++i) ramp.data.push_back ((float) i);
    const std::pair<double, double> region { 100, 300 };
    bw::PlaybackSettings exact; exact.crossfadeSeconds = 0; exact.fadeOutSeconds = 0;
    auto loopWith = [&] (bw::LoopMode mode) { auto s = exact; s.loop = true; s.mode = mode; s.linked = false; s.loopStart = 150 / 400.0f; s.loopEnd = 250 / 400.0f; return s; };

    // One-shot in both directions plays the region once, frame by frame, then stops.
    assert (playedFrames (ramp, region, exact, 1000) == span (100, 299));
    auto reverse = exact; reverse.reverse = true;
    assert (playedFrames (ramp, region, reverse, 1000) == span (299, 100));
    // Forward, Reverse and Ping-Pong loops over the inner window [150, 250).
    assert (playedFrames (ramp, region, loopWith (bw::LoopMode::forward), 350) == join ({ span (100, 249), span (150, 249), span (150, 249) }));
    assert (playedFrames (ramp, region, loopWith (bw::LoopMode::reverse), 450) == join ({ span (100, 249), span (248, 150), span (249, 150), span (249, 150), span (249, 249) }));
    assert (playedFrames (ramp, region, loopWith (bw::LoopMode::pingPong), 450) == join ({ span (100, 249), span (248, 150), span (151, 249), span (248, 150), span (151, 153) }));
    // Reverse direction mirrors the loop: from the region end down to the window, then cycling through it.
    auto mirrored = loopWith (bw::LoopMode::forward); mirrored.reverse = true;
    assert (playedFrames (ramp, region, mirrored, 350) == join ({ span (299, 150), span (249, 150), span (249, 150) }));
    // Linked points are fractions of the region: [0.25, 0.75] of [100, 300) is [150, 250).
    auto linked = loopWith (bw::LoopMode::forward); linked.linked = true; linked.loopStart = .25f; linked.loopEnd = .75f;
    assert (playedFrames (ramp, region, linked, 350) == playedFrames (ramp, region, loopWith (bw::LoopMode::forward), 350));
    // Tailed: loops while held; the release seen at the next loop end lets it play on to the region end.
    const auto tailed = loopWith (bw::LoopMode::tailed);
    assert (playedFrames (ramp, region, tailed, 600) == playedFrames (ramp, region, loopWith (bw::LoopMode::forward), 600));
    assert (playedFrames (ramp, region, tailed, 1000, 200) == join ({ span (100, 249), span (150, 249), span (250, 299) }));
    assert (playedFrames (ramp, region, tailed, 1000, 0) == span (100, 299)); // released before the first loop end
    // With a crossfade the decision is taken on entering it: a release inside the crossfade completes that wrap.
    auto faded = tailed; faded.crossfadeSeconds = 10 / 48000.0f;
    const auto tail = playedFrames (ramp, region, faded, 1000, 245);
    assert (tail.size () > 200 && tail.back () == 299);

    // Crossed points, minimum windows, extreme increments and every mode never read outside the asset.
    for (int mode = 0; mode < 4; ++mode)
        for (bool reversed : { false, true })
            for (double increment : { .37, 1.0, 7.5, 63.0 })
                for (auto [a, b] : { std::pair<float, float> { .9f, .1f }, { .5f, .5f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } })
                {
                    auto s = exact; s.loop = true; s.mode = (bw::LoopMode) mode; s.reverse = reversed; s.loopStart = a; s.loopEnd = b;
                    s.linked = mode % 2 == 0; s.crossfadeSeconds = .001f; s.fadeOutSeconds = .001f;
                    bw::SamplePlayer player; const std::pair<double, double> tiny { 200, 264 };
                    for (const auto& r : { region, tiny })
                    {
                        player.reset (r);
                        for (int i = 0; i < 2000 && ! player.finished; ++i)
                        {
                            const float v = player.nextStereo (ramp, r, s, increment, i > 1000).first;
                            assert (std::isfinite (v) && v >= 0 && v <= 399);
                        }
                    }
                }

    // A -12 dBFS sine looped over whole periods: the crossfaded wrap leaves no residue against the continuous sine.
    bw::Sample tone; tone.rate = 48000;
    for (int i = 0; i < 48000; ++i) tone.data.push_back (.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 480 * i / 48000.0));
    for (float crossfadeMs : { 0.0f, 10.0f })
    {
        bw::PlaybackSettings s; s.loop = true; s.linked = false; s.crossfadeSeconds = crossfadeMs * .001f;
        // Period of the loop is (length - crossfade), a multiple of the 100-frame sine period.
        const double length = 10000 + crossfadeMs * 48;
        s.loopStart = 1000 / 48000.0f; s.loopEnd = (float) ((1000 + length) / 48000.0);
        bw::SamplePlayer player; player.reset ({ 0, 48000 });
        double worst = 0;
        for (int i = 0; i < 96000; ++i)
        {
            const float v = player.nextStereo (tone, { 0, 48000 }, s, 1).first;
            worst = std::max (worst, std::abs (v - .25 * std::sin (juce::MathConstants<double>::twoPi * 480 * i / 48000.0)));
        }
        std::cout << "loop wrap residue (" << crossfadeMs << " ms crossfade): " << 20 * std::log10 (std::max (worst, 1e-12)) << " dBFS" << std::endl;
        assert (20 * std::log10 (std::max (worst, 1e-12)) < -50);
    }

    // Processor: Tailed leaves the loop after note-off (and only after the pedal lifts) while the release still runs.
    const auto file = writeTone ("tailed", 48000, 1, 440);
    auto tailedRig = [&] (bool tailMode)
    {
        auto r = std::make_unique<Rig> ();
        sampleOnly (*r); r->set ("release", 4); r->set ("sampleLoop", 1); r->set ("sampleLoopMode", tailMode ? 3.0f : 0.0f);
        r->set ("sampleLoopLink", 0); r->set ("sampleLoopStart", .1f); r->set ("sampleLoopEnd", .2f);
        assert (r->processor.loadSample (file).isEmpty ());
        return r;
    };
    for (bool tailMode : { false, true })
    {
        auto r = tailedRig (tailMode);
        r->send (on (60)); r->run (1);       // well past the region's natural end: still looping
        r->send (off (60));
        const auto after = r->run (1.5);     // the tail plays .2 -> 1 of the file (0.8 s), then silence
        assert (rms (after, 0, 4800) > .02);
        const double late = rms (after, (size_t) (48000 * 1.0), after.size ());
        assert (tailMode ? late < 1.0e-6 : late > .02); // after the filter the tail decays to (near) zero
    }
    {
        auto r = tailedRig (true);
        r->send (pedal (true)); r->send (on (60)); r->run (.5);
        r->send (off (60)); auto held = r->run (1.2);
        assert (rms (held, held.size () - 4800, held.size ()) > .02); // the pedal keeps it looping
        r->send (pedal (false)); const auto after = r->run (1.5);
        assert (rms (after, (size_t) (48000 * 1.0), after.size ()) < 1.0e-6);
    }

    // Zones keep their own loop window and mode through the portable bank format.
    bw::SampleBank bank; bank.zones.push_back ({}); auto& z = bank.zones[0];
    auto shared = std::make_shared<bw::Sample> (tone); shared->updateHash ();
    z.sample = shared; z.loop = true; z.loopMode = 2; z.loopStart = .2f; z.loopEnd = .6f; z.loopLinked = false; z.reverse = true;
    assert (bank.valid ());
    juce::String error;
    const auto restored = bw::deserializeSampleBank (bw::serializeSampleBank (bank), error);
    assert (restored && restored->zones[0].loopMode == 2 && restored->zones[0].loopStart == .2f && restored->zones[0].loopEnd == .6f
            && ! restored->zones[0].loopLinked && restored->zones[0].reverse);
    // A bank saved before these fields existed restores the original forward whole-region loop.
    auto legacyTree = bw::serializeSampleBank (bank);
    for (auto key : { "loopMode", "loopStart", "loopEnd", "loopLink", "reverse" }) legacyTree.getChild (0).removeProperty (key, nullptr);
    const auto legacy = bw::deserializeSampleBank (legacyTree, error);
    assert (legacy && legacy->zones[0].loopMode == 0 && legacy->zones[0].loopStart == 0 && legacy->zones[0].loopEnd == 1
            && legacy->zones[0].loopLinked && ! legacy->zones[0].reverse);
    std::cout << "ok: region/loop windows, forward/reverse/ping-pong/tailed frame sequences, bounds, wrap residue, note-off/pedal tails and zone persistence" << std::endl;
}

void testManualSlicesSharedAssetAndUndo ()
{
    // Four 0.25 s segments at 300/500/700/900 Hz.
    const std::array<double, 4> tones { 300, 500, 700, 900 };
    juce::AudioBuffer<float> audio (1, 48000);
    for (int i = 0; i < 48000; ++i) audio.setSample (0, i, .5f * (float) std::sin (juce::MathConstants<double>::twoPi * tones[(size_t) i / 12000] * i / 48000.0));
    const auto file = writeFloatWav ("slices", 48000, audio);
    Rig r; sampleOnly (r); r.set ("sampleRoute", 2);
    assert (r.processor.loadSample (file).isEmpty ());
    assert (r.processor.setSlices ({ 12000, 24000, 36000 }, 60, false).isEmpty ());
    auto bank = r.processor.sampleBank ();
    assert (bank->sliced && bank->zones.size () == 4 && (bank->slices.markers == std::vector<int> { 0, 12000, 24000, 36000 }));
    // One shared asset, counted once, however many slices (and undo steps) reference it.
    for (const auto& z : bank->zones) assert (z.sample == bank->zones[0].sample);
    assert (r.processor.assetMemoryBytes () == bank->zones[0].sample->bytes ());

    auto play = [&] (int note, double seconds)
    {
        r.send (on (note)); auto out = r.run (seconds); r.send (off (note)); r.run (.2);
        return out;
    };
    for (int i = 0; i < 4; ++i)
    {
        const auto out = play (60 + i, .4);
        assert (std::abs (frequency (out, 480, 11000) / tones[(size_t) i] - 1) < .005);
        assert (rms (out, (size_t) (48000 * .27), out.size ()) < 1.0e-6); // single slice stops at the next marker
    }
    assert (silent (play (64, .2)) && silent (play (59, .2))); // outside the slices
    // Play to end: slice 1 runs through slices 2 and 3 to the end of the file.
    assert (r.processor.setSlices ({ 12000, 24000, 36000 }, 60, true).isEmpty ());
    const auto toEnd = play (61, .8);
    assert (std::abs (frequency (toEnd, 480, 11000) / 500 - 1) < .005 && std::abs (frequency (toEnd, 25000, 35000) / 900 - 1) < .005);

    // Markers at 0 and the end, beyond the end, and closer than the minimum region are dropped.
    assert (r.processor.setSlices ({ 0, 48000, 47990, 10, 30000, 30030, -5 }, 60, false).isEmpty ());
    assert ((r.processor.sampleBank ()->slices.markers == std::vector<int> { 0, 30000 }));
    // Slices past MIDI 127 are refused and the current map stays.
    assert (r.processor.setSlices ({ 12000, 24000, 36000 }, 126, false).isNotEmpty ());
    assert ((r.processor.sampleBank ()->slices.markers == std::vector<int> { 0, 30000 }));

    // Undo/redo step through the slice edits.
    assert (r.processor.undo () && (r.processor.sampleBank ()->slices.markers == std::vector<int> { 0, 12000, 24000, 36000 }) && r.processor.sampleBank ()->slices.playToEnd);
    assert (r.processor.redo () && (r.processor.sampleBank ()->slices.markers == std::vector<int> { 0, 30000 }));
    assert (r.processor.assetMemoryBytes () == r.processor.sample ()->bytes ());

    // Editing slices under a held note keeps the old bank alive until the voice lets go.
    r.send (on (60)); renderChecked (r.processor, r.pending, 2400, 64);
    assert (r.processor.setSlices ({ 6000, 18000 }, 60, false).isEmpty ());
    renderChecked (r.processor, r.pending, 2400, 64);
    r.send (off (60)); renderChecked (r.processor, r.pending, 9600, 64);
    assert (r.processor.sampleSettled ());

    // The portable state carries one copy of the audio and the slice map; the source file can go.
    juce::MemoryBlock state; r.processor.getStateInformation (state); file.deleteFile ();
    Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
    const auto back = restored.processor.sampleBank ();
    assert (back && back->sliced && (back->slices.markers == std::vector<int> { 0, 6000, 18000 }) && back->slices.root == 60 && ! back->slices.playToEnd);
    assert (back->zones.size () == 3 && back->zones[2].sample == back->zones[0].sample);
    assert (restored.processor.assetMemoryBytes () == back->zones[0].sample->bytes ());
    // Clearing slices returns to the single sample.
    restored.processor.clearSlices ();
    assert (! restored.processor.sampleZonesMapped () && restored.processor.sample () == back->zones[0].sample);
    std::cout << "ok: manual slices on consecutive keys, single/to-end, marker limits, shared asset, undo/redo, live edit and portable state" << std::endl;
}

void testSfzSubsetImportAndDiagnostics ()
{
    // Note names and limits of the subset parser.
    assert (bw::sfzNote ("c4") == 60 && bw::sfzNote ("F#3") == 54 && bw::sfzNote ("eb-1") == 3 && bw::sfzNote ("c-1") == 0);
    assert (bw::sfzNote ("g9") == 127 && bw::sfzNote ("127") == 127 && bw::sfzNote ("128") < 0 && bw::sfzNote ("h4") < 0 && bw::sfzNote ("g#9") < 0);

    const auto kit = folder.getChildFile ("sfz kit");
    kit.deleteRecursively ();
    const auto samples = kit.getChildFile ("Samples With Space");
    samples.createDirectory ();
    for (auto [name, hz] : { std::pair<const char*, double> { "low tone", 300 }, { "high tone", 600 }, { "loud", 450 } })
        assert (writeTone (name, 48000, .5, hz).moveFileTo (samples.getChildFile (juce::String (name) + ".wav")));
    assert (writeTone ("outside", 48000, .5, 200).moveFileTo (folder.getChildFile ("outside.wav")));
    const auto sfz = kit.getChildFile ("kit.sfz");
    assert (sfz.replaceWithText (
        "// A small kit\n"
        "<control> default_path=Samples With Space/\n"
        "<global> volume=-6 amp_veltrack=50\n"
        "<group> lokey=c3 hikey=b3 pitch_keycenter=c4\n"
        "<region> sample=low tone.wav pan=-50\n"
        "<group> key=72\n"
        "<region> sample=high tone.wav lovel=1 hivel=63 loop_mode=loop_continuous loop_start=1000 loop_end=10999\n"
        "<region> sample=loud.wav lovel=64 hivel=127 /* block\n comment */ tune=10 offset=2400 end=21599\n"
        "<region> sample=missing file.wav key=80\n"
        "<region> sample=../../outside.wav key=90\n"
        "<curve> foo=1\n"
        "#define $X 1\n"));

    bw::SfzImportResult result;
    const auto bank = bw::importSfzBank (sfz, result);
    std::cout << "sfz diagnostics: " << result.summary () << std::endl;
    assert (bank && result.error.isEmpty () && result.regions.size () == 4 && bank->zones.size () == 4);
    assert (result.unsupported.contains ("amp_veltrack") && result.unsupported.contains ("<curve>") && result.unsupported.contains ("#define"));
    assert (result.missingFiles.contains ("missing file.wav") && result.summary ().contains ("outside the SFZ folder"));
    const auto& low = bank->zones[0]; const auto& soft = bank->zones[1]; const auto& loud = bank->zones[2]; const auto& missing = bank->zones[3];
    assert (low.lowKey == 48 && low.highKey == 59 && low.rootKey == 60 && low.gainDb == -6 && low.pan == -.5f && ! low.loop);
    assert (soft.lowKey == 72 && soft.highKey == 72 && soft.rootKey == 72 && soft.lowVel == 1 && soft.highVel == 63 && soft.loop
            && soft.loopMode == (int) bw::LoopMode::forward && ! soft.loopLinked && soft.loopStart == 1000 / 24000.0f && soft.loopEnd == 11000 / 24000.0f);
    assert (loud.lowVel == 64 && loud.tuneCents == 10 && loud.start == .1f && loud.end == .9f);
    assert (missing.lowKey == 80 && missing.sampleForVariant (0) == nullptr && missing.variants[0].missing);
    assert (low.sample != soft.sample && bank->zones[1].sample != loud.sample);

    // The same map built by hand plays bit-identically.
    bw::SampleBank manual;
    auto asset = [&] (const char* name) { juce::String error; return bw::loadSample (samples.getChildFile (name), error); };
    manual.zones.resize (4);
    auto& m0 = manual.zones[0]; m0.sample = asset ("low tone.wav"); m0.lowKey = 48; m0.highKey = 59; m0.rootKey = 60; m0.gainDb = -6; m0.pan = -.5f;
    auto& m1 = manual.zones[1]; m1.sample = asset ("high tone.wav"); m1.lowKey = m1.highKey = m1.rootKey = 72; m1.highVel = 63; m1.gainDb = -6;
    m1.loop = true; m1.loopLinked = false; m1.loopStart = 1000 / 24000.0f; m1.loopEnd = 11000 / 24000.0f;
    auto& m2 = manual.zones[2]; m2.sample = asset ("loud.wav"); m2.lowKey = m2.highKey = m2.rootKey = 72; m2.lowVel = 64; m2.gainDb = -6; m2.tuneCents = 10;
    m2.start = .1f; m2.end = .9f;
    auto& m3 = manual.zones[3]; m3.lowKey = m3.highKey = m3.rootKey = 80; m3.variants[0].missing = true;
    assert (manual.valid ());
    auto render = [] (Rig& r)
    {
        std::vector<float> all;
        for (auto [note, velocity] : { std::pair<int, float> { 55, .8f }, { 72, .2f }, { 72, .9f }, { 80, .8f }, { 40, .8f } })
        {
            r.send (on (note, velocity)); auto a = r.run (.3); r.send (off (note)); auto b = r.run (.1);
            all.insert (all.end (), a.begin (), a.end ()); all.insert (all.end (), b.begin (), b.end ());
        }
        return all;
    };
    Rig byHand; sampleOnly (byHand); assert (byHand.processor.setSampleBank (manual).isEmpty ());
    Rig imported; sampleOnly (imported); assert (imported.processor.importSfz (sfz).isEmpty ());
    assert (imported.processor.sfzDiagnostics ().contains ("UNSUPPORTED: amp_veltrack"));
    const auto reference = render (byHand), audio = render (imported);
    assert (audio == reference && rms (audio, 0, 14400) > .02);
    // Notes 80 (missing sample) and 40 (no region) are silent.
    const size_t perNote = (size_t) (48000 * .4);
    assert (rms (audio, 3 * perNote, audio.size ()) < 1.0e-6);
    assert (imported.processor.canUndo () && imported.processor.undoName () == "SFZ IMPORT");

    // Background import with progress, then the portable state outlives the files.
    Rig background; sampleOnly (background);
    background.processor.startSfzImport (sfz);
    assert (background.processor.waitForSampleLoad (30000) && background.processor.sampleZonesMapped ());
    assert (background.processor.sampleLoadProgress () == 1.0f && background.processor.sfzDiagnostics ().contains ("MISSING: missing file.wav"));
    juce::MemoryBlock state; background.processor.getStateInformation (state);
    kit.deleteRecursively ();
    Rig restored; sampleOnly (restored); restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
    sampleOnly (restored);
    assert (render (restored) == reference);

    // Limits and failures are reported, never half-imported as if complete.
    const auto big = folder.getChildFile ("big.sfz");
    assert (big.replaceWithText (juce::String::repeatedString ("// padding\n", 100000)));
    bw::SfzImportResult tooBig; assert (! bw::importSfzBank (big, tooBig) && tooBig.error.contains ("1 MB"));
    juce::String many = "<group> sample=x.wav\n";
    for (int i = 0; i < 130; ++i) many << "<region> key=" << (i % 128) << "\n";
    const auto parsed = bw::parseSfz (many, folder);
    assert (parsed.regions.size () == 128 && parsed.summary ().contains ("more than 128 regions"));
    const auto overlapping = folder.getChildFile ("overlap.sfz");
    assert (writeTone ("overlap", 48000, .1, 300).existsAsFile ());
    assert (overlapping.replaceWithText ("<region> sample=overlap.wav lokey=60 hikey=70\n<region> sample=overlap.wav lokey=65 hikey=75\n"));
    bw::SfzImportResult overlap; assert (! bw::importSfzBank (overlapping, overlap) && overlap.error.contains ("Overlapping"));
    const auto bad = bw::parseSfz ("<region> sample=a.wav lokey=zz hivel=300 loop_mode=sometimes\n", folder);
    assert (bad.summary ().contains ("lokey=zz") && bad.summary ().contains ("hivel=300") && bad.summary ().contains ("loop_mode=sometimes"));
    std::cout << "ok: SFZ subset (headers, precedence, spaces, notes, layers, loops, offsets), diagnostics, manual equivalence, background import, portability and limits" << std::endl;
}

void testGranularSyncDistributionsJumpStartAndKeyTrack ()
{
    constexpr double rate = 48000;
    bw::Sample flat; flat.rate = rate; flat.data.assign (48000, .5f);
    bw::GrainSources sources {}; sources[0] = { &flat, 0, 48000, 1.0 / 440, 1 };
    // Sample indices at which the engine spawned grains while running `samples` frames.
    auto spawnTimes = [&] (bw::GranularVoiceEngine& engine, const bw::GrainControls& c, int samples, auto clockAt)
    {
        std::vector<int> times;
        for (int n = 0; n < samples; ++n)
        {
            const auto before = engine.spawned;
            engine.process (sources, c, 440, true, clockAt (n));
            for (auto k = before; k < engine.spawned; ++k) times.push_back (n);
        }
        return times;
    };
    bw::GrainControls synced; synced.densityMode = bw::GrainDensityMode::sync; synced.densityBeats = .25; synced.sizeMs = 5;

    // Transport-locked events land on the 1/16 grid at 60/120/180 BPM within one sample.
    for (double bpm : { 60.0, 120.0, 180.0 })
    {
        bw::GranularVoiceEngine engine; engine.reset (rate, 1);
        const double ppq0 = 3.1;
        const auto times = spawnTimes (engine, synced, (int) (rate * 4), [&] (int n) { return bw::GrainClock { bpm, ppq0 + n * bpm / 60 / rate, true }; });
        assert (times.front () == 0); // the note-on emits at once
        for (size_t k = 1; k < times.size (); ++k)
        {
            const double grid = (std::floor (ppq0 / .25) + (double) k) * .25;
            assert (std::abs (times[k] - (grid - ppq0) * 60 / bpm * rate) <= 1);
        }
        assert (std::abs ((double) times.size () - 1 - 4 * bpm / 60 / .25) <= 1);
    }
    // A tempo change, a stop (note-relative at the current tempo), play again and a seek.
    {
        bw::GranularVoiceEngine engine; engine.reset (rate, 1);
        double ppq = 0; std::vector<int> times;
        for (int n = 0; n < (int) rate * 6; ++n)
        {
            const double bpm = n < rate * 2 ? 120 : 180;
            const bool playing = n < rate * 3 || n >= rate * 4;
            if (n == (int) rate * 5) ppq = 100.03; // seek
            const auto before = engine.spawned;
            engine.process (sources, synced, 440, true, { bpm, ppq, playing });
            if (engine.spawned != before) times.push_back (n);
            if (playing) ppq += bpm / 60 / rate;
        }
        auto inRange = [&] (double a, double b) { return (int) std::count_if (times.begin (), times.end (), [&] (int t) { return t >= a * rate && t < b * rate; }); };
        assert (std::abs (inRange (0, 2) - 16) <= 1 && std::abs (inRange (2, 3) - 12) <= 1);  // 8 then 12 events per second
        assert (std::abs (inRange (3, 4) - 12) <= 1);                                           // stopped: still 12/s from the note
        const auto seek = std::find_if (times.begin (), times.end (), [&] (int t) { return t >= (int) rate * 5; });
        assert (seek != times.end () && *seek == (int) rate * 5);                              // the jump crosses a grid line
        assert (std::abs (*(seek + 1) - (rate * 5 + (100.25 - 100.03) * 60 / 180 * rate)) <= 1);
    }

    // Batch distributions over a 10 Hz event interval (4800 frames), four grains per event.
    bw::GrainControls batched; batched.density = 10; batched.sizeMs = 5; batched.batch = 4;
    const std::array<std::vector<int>, 3> expected { std::vector<int> { 0, 0, 0, 0 }, { 0, 1200, 2400, 3600 }, { 0, 320, 960, 2240 } };
    for (int d = 0; d < 4; ++d)
    {
        batched.distribution = (bw::GrainDistribution) d;
        bw::GranularVoiceEngine engine; engine.reset (rate, 7);
        const auto times = spawnTimes (engine, batched, 9600, [] (int) { return bw::GrainClock {}; });
        assert (times.size () == 8);
        std::vector<int> first (times.begin (), times.begin () + 4);
        std::sort (first.begin (), first.end ());
        if (d < 3) for (size_t k = 0; k < 4; ++k) assert (std::abs (first[k] - expected[(size_t) d][k]) <= 1);
        else
        {
            assert (first.front () >= 0 && first.back () < 4800 && first != expected[0]);
            bw::GranularVoiceEngine again; again.reset (rate, 7);
            assert (spawnTimes (again, batched, 9600, [] (int) { return bw::GrainClock {}; }) == times); // same seed, same offsets
        }
        for (int t : std::vector<int> (times.begin () + 4, times.end ())) assert (t >= 4800 && t < 9600);
    }

    // Jump-start fills the expected overlap (at most the pool) on the first sample.
    for (int n : { 1, 4, 16, 31, 32 })
    {
        bw::GrainControls c; c.densityMode = bw::GrainDensityMode::overlap; c.overlap = (float) n; c.sizeMs = 200;
        bw::GranularVoiceEngine engine; engine.reset (rate, 1); engine.requestJumpStart ();
        engine.process (sources, c, 440, true);
        assert (engine.activeCount () == std::min (32, n + 1)); // jump-started grains plus the note-on emission
    }
    // Target overlap: mean occupancy over ten seconds within one grain of the request.
    for (float n : { 2.0f, 4.0f, 8.0f, 32.0f })
    {
        bw::GrainControls c; c.densityMode = bw::GrainDensityMode::overlap; c.overlap = n; c.sizeMs = 100;
        bw::GranularVoiceEngine engine; engine.reset (rate, 1); engine.requestJumpStart ();
        double sum = 0;
        for (int s = 0; s < (int) rate * 10; ++s) { engine.process (sources, c, 440, true); sum += engine.activeCount (); }
        std::cout << "target overlap " << n << ": mean " << sum / (rate * 10) << std::endl;
        assert (std::abs (sum / (rate * 10) - n) <= 1);
    }

    // Lengths: key tracking by octaves, percent of the interval, BPM sync with dotted and triplet divisions.
    bw::GrainControls tracked; tracked.sizeMs = 100; tracked.keyTrack = 1; tracked.root = 60;
    for (auto [note, factor] : { std::pair<int, double> { 60, 1 }, { 72, .5 }, { 84, .25 }, { 48, 2 } })
    {
        tracked.note = note;
        assert (std::abs (bw::resolveGrainTiming (tracked, 120).lengthMs - 100 * factor) < 1.0e-9);
        bw::GranularVoiceEngine engine; engine.reset (rate, 1);
        int alive = 0;
        for (int s = 0; s < (int) rate; ++s) { engine.process (sources, tracked, 440, s == 0); alive += engine.activeCount (); }
        assert (alive == (int) std::round (rate * .1 * factor) - 1); // active after each call until its last sample
    }
    tracked.note = 72; tracked.keyTrack = .5f;
    assert (std::abs (bw::resolveGrainTiming (tracked, 120).lengthMs - 100 / std::sqrt (2.0)) < 1.0e-6);
    bw::GrainControls percent; percent.density = 10; percent.lengthMode = bw::GrainLengthMode::percent; percent.lengthPercent = 50;
    assert (std::abs (bw::resolveGrainTiming (percent, 120).lengthMs - 50) < 1.0e-9);
    bw::GrainControls lengthSync; lengthSync.lengthMode = bw::GrainLengthMode::sync;
    for (auto [division, ms] : { std::pair<int, double> { 18, 125 }, { 19, 187.5 }, { 20, 125 * 2 / 3.0 } })
    {
        lengthSync.lengthBeats = bw::divisionBeats (division);
        assert (std::abs (bw::resolveGrainTiming (lengthSync, 120).lengthMs - ms) < 1.0e-9);
    }
    // Dotted divides the synced event rate by 1.5, triplet multiplies it by 1.5.
    bw::GrainControls rateSync; rateSync.densityMode = bw::GrainDensityMode::sync;
    rateSync.densityBeats = bw::divisionBeats (18); const double straight = bw::resolveGrainTiming (rateSync, 120).eventHz;
    rateSync.densityBeats = bw::divisionBeats (19); assert (std::abs (bw::resolveGrainTiming (rateSync, 120).eventHz - straight / 1.5) < 1.0e-9);
    rateSync.densityBeats = bw::divisionBeats (20); assert (std::abs (bw::resolveGrainTiming (rateSync, 120).eventHz - straight * 1.5) < 1.0e-9);

    // A looping grain that crosses the region end wraps inside it; without loop it is moved back and shortened to
    // end inside the region, never holding the last frame.
    for (bool loop : { false, true })
    {
        bw::GrainControls c; c.sizeMs = 500; c.position = .95f; c.loop = loop;
        bw::GrainSources region {}; region[0] = { &flat, 1000, 2000, 1.0 / 440, 1 };
        bw::GranularVoiceEngine engine; engine.reset (rate, 1);
        bw::GrainDisplaySnapshot display;
        float lowest = 1.0e9f, highest = 0, previous = -1;
        int alive = 0, held = 0;
        for (int s = 0; s < 20000; ++s)
        {
            engine.process (region, c, 440, s == 0);
            engine.publish (display);
            if (display.count.load () == 0) continue;
            const float at = display.grains[0].normPos.load () * 48000;
            lowest = std::min (lowest, at); highest = std::max (highest, at);
            held += std::abs (at - previous) < .5f ? 1 : 0; previous = at; ++alive;
        }
        assert (lowest >= 999.9f && highest <= 1999.1f && held == 0);
        assert (loop ? alive == 20000 : alive < 1000 && lowest < 1002);
    }

    // Extremes: zero density clamps to 1 Hz; long synced grains saturate the pool predictably, finite throughout.
    bw::GrainControls zero; zero.density = 0; zero.sizeMs = 5;
    bw::GranularVoiceEngine slow; slow.reset (rate, 1);
    assert (spawnTimes (slow, zero, (int) rate * 3, [] (int) { return bw::GrainClock {}; }).size () == 3);
    bw::GrainControls dense; dense.densityMode = bw::GrainDensityMode::sync; dense.densityBeats = 1 / 16.0;
    dense.lengthMode = bw::GrainLengthMode::sync; dense.lengthBeats = 4; dense.batch = 8; dense.distribution = bw::GrainDistribution::random;
    bw::GranularVoiceEngine full; full.reset (rate, 1);
    for (int s = 0; s < (int) rate * 2; ++s)
    {
        const auto out = full.process (sources, dense, 440, true, { 30.0 + s / rate * 150, s * 2.0 / rate, true });
        assert (std::isfinite (out.first) && full.activeCount () <= 32);
    }
    assert (full.saturated && full.activeCount () == 32);

    // The processor exposes and restores every new control.
    Rig r; for (auto id : { "grainDensityMode", "grainDensityDivision", "grainOverlap", "grainLengthMode", "grainLengthDivision", "grainLengthPercent",
                            "grainDistribution", "grainBatch", "grainJumpStart", "grainKeyTrack", "grainLoop" })
        assert (r.processor.apvts.getParameter (id) != nullptr && r.processor.apvts.getParameter (id)->isAutomatable ());
    r.set ("grainDensityMode", 2); r.set ("grainBatch", 3); r.set ("grainKeyTrack", 50); r.set ("grainJumpStart", 1);
    juce::MemoryBlock state; r.processor.getStateInformation (state);
    Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
    auto value = [&] (const char* id) { return restored.processor.apvts.getRawParameterValue (id)->load (); };
    assert (value ("grainDensityMode") == 2 && value ("grainBatch") == 3 && value ("grainKeyTrack") == 50 && value ("grainJumpStart") == 1);
    std::cout << "ok: grain BPM grid/tempo/stop/seek within 1 sample, batch distributions, jump-start, target overlap, key tracking, percent/dotted/triplet, grain loop and saturation" << std::endl;
}
void testGranularJitterPanAndLevel ()
{
    constexpr double rate = 48000;
    bw::Sample noise; noise.rate = rate; noise.data.resize (96000);
    bw::DeterministicRng source; source.state = 99;
    for (auto& x : noise.data) x = (float) (source.next () - .5);
    const bw::GrainSources sources { bw::GrainSource { &noise, 0, 96000, 1, 1 }, {} };

    // Jitter delays each grain by [0, jitter x interval) after its event, reproducibly per seed.
    bw::GrainControls jittered; jittered.density = 10; jittered.sizeMs = 5; jittered.jitter = .5f;
    auto spawnTimes = [&] (uint64_t seed)
    {
        bw::GranularVoiceEngine engine; engine.reset (rate, seed); std::vector<int> times;
        for (int n = 0; n < (int) rate; ++n)
        {
            const auto before = engine.spawned;
            { ScopedAudioHeapGuard guard; engine.process (sources, jittered, 1); }
            if (engine.spawned != before) times.push_back (n);
        }
        return times;
    };
    const auto times = spawnTimes (3);
    assert (times.size () == 10 && times == spawnTimes (3) && times != spawnTimes (4));
    bool late = false;
    for (size_t k = 0; k < times.size (); ++k)
    {
        assert (times[k] >= (int) k * 4800 && times[k] <= (int) k * 4800 + 2400);
        late |= times[k] > (int) k * 4800 + 10;
    }
    assert (late);

    // Pan spread keeps equal power against the centred cloud with the same timing; no spread keeps L == R.
    const auto dc = engineFixture ();
    const bw::GrainSources flat { bw::GrainSource { dc.get (), 0, 16384, 1, 1 }, {} };
    bw::GrainControls centred; centred.density = 5; centred.sizeMs = 100; centred.position = 0;
    auto panned = centred; panned.panSpread = 1;
    bw::GranularVoiceEngine a, b; a.reset (rate, 5); b.reset (rate, 5);
    bool wide = false;
    for (int n = 0; n < (int) rate; ++n)
    {
        const auto mono = a.process (flat, centred, 1), stereo = b.process (flat, panned, 1);
        assert (mono.first == mono.second);
        assert (std::abs (stereo.first * stereo.first + stereo.second * stereo.second - 2 * mono.first * mono.first) < 1.0e-6);
        wide |= std::abs (stereo.first - stereo.second) > .05f;
    }
    assert (wide);

    // Power normalisation: an overlapped noise cloud keeps its RMS within 3 dB from 2 to 10 overlapping grains, with
    // or without position spread.
    auto level = [&] (float density, float spread)
    {
        bw::GrainControls c; c.density = density; c.sizeMs = 100; c.spread = spread; c.jitter = .2f; c.scan = .5f;
        bw::GranularVoiceEngine engine; engine.reset (rate, 11); double sum = 0;
        for (int n = 0; n < (int) rate * 2; ++n) { const auto value = engine.process (sources, c, 1); if (n >= rate) sum += value.first * value.first; }
        return 10 * std::log10 (sum / rate);
    };
    const double reference = level (20, 0);
    for (float density : { 20.0f, 50.0f, 100.0f })
        for (float spread : { 0.0f, 1.0f })
        {
            const double db = level (density, spread);
            std::cout << "grain cloud " << density << " Hz spread " << spread << ": " << db - reference << " dB" << std::endl;
            assert (std::abs (db - reference) < 3);
        }

    Rig r; for (auto id : { "grainJitter", "grainPan" }) assert (r.processor.apvts.getParameter (id)->isAutomatable ());
    r.set ("grainJitter", 70); r.set ("grainPan", 40);
    juce::MemoryBlock state; r.processor.getStateInformation (state);
    Rig restored; restored.processor.setStateInformation (state.getData (), (int) state.getSize ());
    assert (restored.processor.apvts.getRawParameterValue ("grainJitter")->load () == 70 && restored.processor.apvts.getRawParameterValue ("grainPan")->load () == 40);
    std::cout << "ok: grain jitter bounds and determinism, equal-power pan spread, overlap-stable cloud level" << std::endl;
}
} // namespace

void testGrainWindowEndpoints ()
{
    // Independent double evaluator of the ticket's formulas, not the production table builder.
    const double pi = std::acos (-1.0);
    for (int w = 0; w < 10; ++w)
        for (int i = 0; i <= 4096; ++i)
        {
            const double t = i/4096.0, x = 2*t-1;
            const double fade = std::pow (std::sin (pi*.5*std::min (1.0, std::min (t,1-t)/.02)), 2);
            double expected = 0;
            switch (w)
            {
                case 0: expected = std::pow (std::sin (pi*t), 2); break;
                case 1: expected = 4*t*(1-t); break;
                case 2: expected = std::exp (-(t-.5)*(t-.5)/(.18*.18*2))*fade; break;
                case 3: expected = (.35875-.48829*std::cos (2*pi*t)+.14128*std::cos (4*pi*t)-.01168*std::cos (6*pi*t))*fade; break;
                case 4: expected = x == 0 ? 1 : std::pow (std::sin (4*pi*x)/(4*pi*x),2); break;
                case 5: expected = t < .25 ? std::pow (std::sin (2*pi*t),2) : t > .75 ? std::pow (std::sin (2*pi*(1-t)),2) : 1; break;
                case 6: expected = 2*std::min (t,1-t); break;
                case 7: expected = std::min (1.0,4*std::min (t,1-t)); break;
                case 8: expected = std::exp (-6*t)*fade; break;
                case 9: expected = std::exp (-6*(1-t))*fade; break;
            }
            if (i == 0 || i == 4096) expected = 0;
            assert (std::abs (bw::evaluateGrainWindow ((bw::GrainWindowType) w,t)-expected) <= 1e-5);
            assert (std::abs (bw::evaluateGrainWindow (bw::GrainWindowType::expDec,t)-bw::evaluateGrainWindow (bw::GrainWindowType::expDecRev,1-t)) <= 1e-6);
        }
    for (double skew : { .01, .5, .99 })
    {
        double previous = -1;
        for (int i = 0; i <= 4096; ++i)
        {
            const double u = bw::applyGrainSkew (i/4096.0,skew);
            assert (u >= previous && u >= 0 && u <= 1); previous = u;
        }
        assert (bw::applyGrainSkew (0,skew) == 0 && bw::applyGrainSkew (1,skew) == 1);
    }
    for (int w = 0; w < 10; ++w)
        for (float skew : { .01f, .5f, .99f })
            for (float amount : { 0.0f, .5f, 1.0f })
            {
                bw::Grain grain; grain.window = (bw::GrainWindowType) w; grain.windowAmount = amount; grain.windowSkew = skew;
                grain.windowGain = bw::grainWindowGain (grain.window,amount,skew);
                double energy = 0;
                for (int n = 0; n <= 16384; ++n) { const double v = bw::grainWindow (grain,n/16384.0); energy += v*v/16384; }
                assert (std::isfinite (energy) && std::abs (energy-.375) < .002);
            }
    bw::DeterministicRng a, b; double mean = 0, second = 0;
    for (int i = 0; i < 10000; ++i)
    {
        const double v = a.next (); assert (v == b.next () && v >= 0 && v < 1);
        mean += v/10000; second += v*v/10000;
    }
    assert (std::abs (mean-.5) < .015 && std::abs (second-mean*mean-1.0/12) < .005);
    for (auto type : { bw::GrainWarpType::identity, bw::GrainWarpType::bend, bw::GrainWarpType::asym })
        for (double amount : { -1.0, 0.0, 1.0 })
        {
            double previous = -1;
            for (int i = 0; i <= 4096; ++i)
            {
                const double t = i/4096.0, u = bw::applyGrainWarp (t,type,amount);
                assert (u >= previous && u >= 0 && u <= 1); previous = u;
                if (amount == 0) assert (u == t);
            }
        }
    auto sample = std::make_shared<bw::Sample> (); sample->rate = 48000;
    for (int i = 0; i < 48000; ++i) sample->data.push_back ((float) (.3*std::sin (i*.043)));

    bw::GrainSources sources {}; sources[0] = { sample.get (), 0, 48000, 1.0/440, 1 };
    bw::GrainControls c; c.loop = true; c.spread = .2f;
    bw::GranularVoiceEngine original, neutral, changed, repeat;
    original.reset (48000,123); neutral.reset (48000,123); changed.reset (48000,123); repeat.reset (48000,123);
    auto zeroWarp = c; zeroWarp.warp[0].type = bw::GrainWarpType::bend; zeroWarp.warp[1].type = bw::GrainWarpType::asym;
    auto scattered = c; scattered.window = bw::GrainWindowType::expDec; scattered.windowAmount = 1;
    scattered.directionSpread = .5f; scattered.pitchSpread = 24; scattered.lengthSpread = 1; scattered.gainSpread = 1;
    scattered.amountSpread = scattered.skewSpread = scattered.warpSpread = scattered.panSpread = 1;
    scattered.warp[0] = { bw::GrainWarpType::bend, .5f }; scattered.warp[1] = { bw::GrainWarpType::asym, -.5f };
    double difference = 0;
    for (int i = 0; i < 24000; ++i)
    {
        const auto base = original.process (sources,c,440), zero = neutral.process (sources,zeroWarp,440);
        assert (base == zero);
        const auto v = changed.process (sources,scattered,440), r = repeat.process (sources,scattered,440);
        assert (v == r && std::isfinite (v.first) && std::isfinite (v.second));
        difference += std::pow (v.first-base.first,2);
    }
    assert (difference > .01);
    c.directionSpread = 1; original.reset (48000,123); double reverseEnergy = 0;
    for (int n = 0; n < 10000; ++n) { const auto v = original.process (sources,c,440); reverseEnergy += v.first*v.first; }
    assert (reverseEnergy > 1);
    Rig rig; rig.set ("grainWindow",9); rig.set ("grainWindowAmount",.8f); rig.set ("grainWarp1Type",2);
    rig.set ("spectralPhaseLock",0); rig.set ("spectralTransients",1); rig.set ("spectralStage1Type",1); rig.set ("spectralStage1Amount",.25f);
    juce::MemoryBlock state; rig.processor.getStateInformation (state);
    Rig restored; restored.processor.setStateInformation (state.getData (),(int) state.getSize ());
    for (auto id : { "grainWindow", "grainWindowAmount", "grainWarp1Type", "spectralPhaseLock", "spectralTransients", "spectralStage1Type", "spectralStage1Amount" })
        assert (rig.processor.apvts.getRawParameterValue (id)->load () == restored.processor.apvts.getRawParameterValue (id)->load ());

    std::unique_ptr<juce::AudioProcessorEditor> editor (rig.processor.createEditor ());
    sampleControl (*editor,"SAMPLE")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    auto* panel = sampleControl (*editor,"Sample source"); assert (panel);
    auto snapshot = [&] (const char* name)
    {
        const auto file = juce::File::getCurrentWorkingDirectory ().getChildFile (juce::String ("build/")+name+".png");
        assert (file.getParentDirectory ().createDirectory ().wasOk ());
        auto stream = file.createOutputStream ();
        assert (stream); stream->setPosition (0); stream->truncate ();
        assert (juce::PNGImageFormat ().writeImageToStream (panel->createComponentSnapshot (panel->getLocalBounds ()),*stream));
    };
    rig.set ("sampleMode",kSampleModeGranular); panel->visibilityChanged ();
    for (auto page : { "WINDOW", "SPREAD", "WARP" })
    {
        auto* button = sampleControl (*panel,juce::String (page)+" PAGE"); assert (button); button->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        snapshot (page);
    }
    rig.set ("sampleMode",kSampleModeSpectral); panel->visibilityChanged ();
    sampleControl (*panel,"STAGES")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    for (auto title : { "STAGE 1 AMT", "STAGE 2 AMT" })
    {
        auto* dial = dynamic_cast<bw::ui::Dial*> (sampleControl (*panel,title));
        assert (dial && dial->isVisible () && dial->getHeight () >= 80 && dial->onModulationDrop);
        dial->onModulationDrop (3,dial->parameterId ());
    }
    snapshot ("STAGES");
    sampleControl (*panel,"PLAYBACK")->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); snapshot ("SPECTRAL");
    std::cout << "ok: ten windows, skew, PRNG, warp identity and deterministic dispersed stereo grains\n";
}

int main (int argc, char**)
{
#ifdef _WIN32
    // Failed asserts print and exit instead of opening a modal dialog.
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    testGrainWindowEndpoints ();
    if (argc > 1) return 0;
    testRegionLoopDirectionsAndTailedRelease ();
    testManualSlicesSharedAssetAndUndo ();
    testSfzSubsetImportAndDiagnostics ();
    testGranularJitterPanAndLevel ();
    testGranularSyncDistributionsJumpStartAndKeyTrack ();
    testVelocityLayersAndCrossfadeLaws ();
    testRoundRobinSequenceAndDeterminism ();
    testGranularCloudPoolAndRipple ();
    testGranularDensityDurationAndBlockInvariance ();
    testGranularModulationAndPortableRoundTrip ();
    testLayersAndGranularUI ();
    testTranspositionAliasingAndPassband ();
    testSampleModulation ();
    testMultisampleTwoZonesAndLegato ();
    testSampleZoneUI ();
    testPlaysAtPitch();
    testLoopIsSeamless();
    testOneShotEnds();
    testAutomation();
    testSwapDuringNotes();
    testMissingFileKeepsState();
    testLegacyStateAndPresets();
    testLimits();
    testMemoryBudgetAndUndoEviction();
    testStress100SwapsRetainedReader96kHz();
    testBackgroundLoadCancelAndOutOfOrder();
    testPortableContainerAndRelink();
    testStereoRootPanAndRouting();
    folder.deleteRecursively();
    std::cout << "ok: sample pitch at 44.1/48/96 kHz, seamless loop, one-shot fade, automation, live swap and release, "
                 "missing file, legacy state, factory reset, validation/256 MB budget, 100-swap 96 kHz stress, "
                 "background load/cancel/out-of-order, portable container/relink, stereo/root/pan/routing and zero callback allocs\n";
}
