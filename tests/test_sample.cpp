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

int main()
{
#ifdef _WIN32
    // Failed asserts print and exit instead of opening a modal dialog.
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    juce::ScopedJuceInitialiser_GUI init;
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
