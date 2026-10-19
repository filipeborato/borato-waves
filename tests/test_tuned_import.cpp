// Pitch-detected wavetable import: detection, tuned cycles, file limits, playback and the import dialog.
#include "Rig.h"
#include "UI/ImportDialog.h"
#include <cstdio>

using namespace juce;

namespace
{
// Harmonics 1..n of hz with amplitudes amps[k-1] and fixed phases.
std::vector<float> tone (double hz, double rate, double seconds, std::vector<double> amps)
{
    std::vector<float> out ((size_t) (seconds * rate));
    for (size_t i = 0; i < out.size(); ++i)
    {
        double x = 0;
        for (size_t k = 0; k < amps.size(); ++k) x += amps[k] * std::sin (MathConstants<double>::twoPi * hz * (double) (k + 1) * (double) i / rate + .7 * (double) k);
        out[i] = (float) (.4 * x);
    }
    return out;
}
const std::vector<double> kSawish { 1, .5, .33, .25, .2, .17, .14, .12 };

File writeWav (const File& file, const std::vector<std::vector<float>>& channels, double rate)
{
    file.deleteFile();
    std::unique_ptr<OutputStream> stream (file.createOutputStream());
    auto writer = WavAudioFormat().createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels ((int) channels.size()).withBitsPerSample (32));
    assert (writer != nullptr);
    std::vector<const float*> pointers;
    for (const auto& c : channels) pointers.push_back (c.data());
    assert (writer->writeFromFloatArrays (pointers.data(), (int) pointers.size(), (int) channels[0].size()));
    return file;
}

// Energy outside the bins a single period of the source occupies.
double leakage (const std::vector<float>& cycle, int harmonics)
{
    std::vector<float> data (2 * bw::Wavetable::kSize);
    std::copy (cycle.begin(), cycle.end(), data.begin());
    dsp::FFT (bw::Wavetable::kOrder).performFrequencyOnlyForwardTransform (data.data());
    double inside = 0, outside = 0;
    for (int k = 1; k < bw::Wavetable::kSize / 2; ++k) (k <= harmonics ? inside : outside) += (double) data[(size_t) k] * data[(size_t) k];
    return outside / (inside + outside);
}

void testDetection()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (double hz : { 41.2, 110.0, 261.63, 1046.5 })
        {
            const auto estimate = bw::detectPeriod (tone (hz, rate, .5, kSawish), rate);
            assert (std::abs (estimate.period * hz / rate - 1) < 5.0e-4); // under a cent
            assert (! estimate.ambiguous && estimate.confidence > .9f);
        }
    // A weak fundamental under a strong octave: the octave above looks right too, so the result is flagged
    // and the octave below the detected period is the true pitch.
    const auto weak = bw::detectPeriod (tone (110, 48000, .5, { .15, 1 }), 48000);
    const double truePeriod = 48000 / 110.0;
    assert (std::abs (weak.period / truePeriod - 1) < 1.0e-3 || (weak.ambiguous && std::abs (2 * weak.period / truePeriod - 1) < 1.0e-3));
    std::printf ("weak fundamental: %s\n", weak.ambiguous ? "flagged ambiguous" : "found the fundamental");
    // Noise has no clear period.
    Random random (3);
    std::vector<float> noise (24000);
    for (auto& x : noise) x = random.nextFloat() - .5f;
    assert (bw::detectPeriod (noise, 48000).ambiguous);
}

void testCycles()
{
    // Each cycle is one period: only the source's harmonics, whatever the fractional period.
    const double rate = 48000, hz = 261.63;
    const auto audio = tone (hz, rate, 1, { 1, .5, .25 });
    const auto cycles = bw::cyclesFromPeriod (audio, rate / hz);
    assert ((int) cycles.size() == bw::kMaxUserFrames); // 261 periods, spread over 256 frames
    for (const auto& cycle : cycles) assert (leakage (cycle, 3) < 1.0e-5);
    // A shorter sound uses each of its whole periods.
    assert (bw::cyclesFromPeriod (tone (hz, rate, .25, { 1 }), rate / hz).size() == 65);
    assert (bw::cyclesFromPeriod (std::vector<float> (100), 200).empty());
}

void testFiles()
{
    const auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-tuned-import");
    folder.createDirectory();
    String error;
    // Stereo: both sides count.
    const auto left = tone (220, 44100, .5, kSawish), right = tone (220, 44100, .5, { 0, 1 });
    auto source = bw::loadTunedSource (writeWav (folder.getChildFile ("stereo.wav"), { left, right }, 44100), error);
    assert (error.isEmpty() && source.sampleRate == 44100 && std::abs (source.sampleRate / source.pitch.period - 220) < .1);
    // Long files are read up to the duration limit.
    source = bw::loadTunedSource (writeWav (folder.getChildFile ("long.wav"), { tone (200, 8000, bw::kMaxImportSeconds + 5, kSawish) }, 8000), error);
    assert (error.isEmpty() && source.mono.size() == (size_t) (bw::kMaxImportSeconds * 8000));
    // Silence, too short and not audio: refused with a reason.
    source = bw::loadTunedSource (writeWav (folder.getChildFile ("silent.wav"), { std::vector<float> (48000) }, 48000), error);
    assert (error == "Audio is silent" && source.mono.empty());
    bw::loadTunedSource (writeWav (folder.getChildFile ("short.wav"), { tone (220, 48000, .01, kSawish) }, 48000), error);
    assert (error == "Audio is too short to detect a pitch");
    const auto text = folder.getChildFile ("text.wav");
    text.replaceWithText ("not audio");
    bw::loadTunedSource (text, error);
    assert (error == "Unsupported or unreadable audio file");
    bw::loadTunedSource (folder.getChildFile ("missing.wav"), error);
    assert (error.isNotEmpty());
    folder.deleteRecursively();
}

// The tuned table plays in tune; the frame conversion of the same recording (5.6 periods a frame) does not.
void testPlayback()
{
    const auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-tuned-play");
    folder.createDirectory();
    const auto file = writeWav (folder.getChildFile ("Cello.wav"), { tone (130.81, 48000, 1, kSawish) }, 48000);
    String error;
    const auto source = bw::loadTunedSource (file, error);
    auto measure = [&] (bool tuned)
    {
        rig::Rig r;
        if (tuned) assert (r.processor.importTuned (0, source, source.pitch.period, "Cello").isEmpty());
        else assert (r.processor.importWavetable (0, file).isEmpty());
        r.set ("oscAUser", 1); r.set ("wtPos", .5f);
        r.send (rig::on (60));
        const auto audio = r.run (.5);
        return std::vector<float> (audio.begin() + 4800, audio.end());
    };
    // The strongest partial of the played note is its fundamental only for the tuned table.
    const auto strongest = [] (const std::vector<float>& audio)
    {
        const auto energy = rig::spectrum (audio);
        return (double) (std::max_element (energy.begin() + 1, energy.end()) - energy.begin()) * 48000 / rig::kSpectrumSize;
    };
    const auto tuned = measure (true), frames = measure (false);
    const double c4 = MidiMessage::getMidiNoteInHertz (60), tunedPeak = strongest (tuned), framesPeak = strongest (frames);
    std::printf ("strongest partial at C4 (%.1f Hz): tuned %.1f Hz, frames %.1f Hz\n", c4, tunedPeak, framesPeak);
    assert (std::abs (tunedPeak - c4) < 4 && std::abs (framesPeak - c4) > 100);
    WaveProcessor p;
    assert (p.importTuned (1, source, source.pitch.period, "Cello").isEmpty() && p.userTableName (1) == "Cello" && p.userTable (1)->numFrames == 130);
    assert (p.importTuned (1, source, 1.0e6, "Cello").isNotEmpty()); // a period longer than the sound
    folder.deleteRecursively();
}

void testDialog()
{
    double accepted = 0;
    int mode = -1, dismissed = 0;
    bw::ui::ImportDialog dialog ([&] (bool tuned, double period) { mode = tuned; accepted = period; }, [&] { ++dismissed; });
    dialog.setBounds (0, 0, 1240, 940);
    const double rate = 48000;
    bw::TunedSource source { tone (110, rate, .5, kSawish), rate, bw::detectPeriod (tone (110, rate, .5, kSawish), rate) };
    dialog.show ("Bass", source, {});
    assert (dialog.pitchText().startsWith ("A2"));
    auto press = [&] (const String& title)
    {
        for (auto* child : dialog.getChildren())
            if (child->getTitle() == title && child->isVisible()) return child->keyPressed (KeyPress (KeyPress::returnKey));
        return false;
    };
    // Octave corrections, then import: the period doubles per octave down.
    assert (press ("OCTAVE -") && dialog.pitchText().startsWith ("A1"));
    assert (press ("OCTAVE +") && press ("OCTAVE +") && dialog.pitchText().startsWith ("A3"));
    assert (press ("IMPORT TUNED") && mode == 1 && std::abs (accepted - source.pitch.period / 2) < 1.0e-9);
    assert (press ("IMPORT FRAMES") && mode == 0);
    assert (press ("CANCEL") && dismissed == 1);
    // A refused file can still use the frame conversion, but not the tuned one.
    dialog.show ("Silence", {}, "Audio is silent");
    assert (! press ("IMPORT TUNED") && ! press ("OCTAVE -") && press ("IMPORT FRAMES"));
}
}

int main()
{
   #if JUCE_WINDOWS
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    testDetection();
    testCycles();
    testFiles();
    testPlayback();
    testDialog();
    std::puts ("ok: pitch detection at 44.1/48/96 kHz, octave ambiguity, one-period cycles, stereo/long/silent/short/invalid files, tuned playback, dialog");
}
