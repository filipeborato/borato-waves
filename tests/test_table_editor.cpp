#include "Rig.h"
#include "TableEdit.h"
#include "PluginEditor.h"
#include "UI/TableEditor.h"
#include <iostream>
#ifdef _WIN32
#include <cstdlib>
#include <stdlib.h>
#endif

namespace
{
using namespace rig;
using juce::Component;

Component* findControl (Component& root, const juce::String& title)
{
    if (root.getTitle() == title) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findControl (*child, title)) return found;
    return nullptr;
}
juce::MouseEvent mouseEvent (Component& c, float x, float y)
{
    const juce::Point<float> point (x, y);
    return { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
             &c, &c, juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 1, false };
}
// JUCE runs pending timers for at most 100 ms per call, and the editor's own timer is slow in Debug.
template <typename Condition>
bool pumpTimersUntil (Condition done)
{
    for (int attempt = 0; attempt < 20 && ! done(); ++attempt)
    {
        juce::Thread::sleep (80);
        juce::Timer::callPendingTimersSynchronously();
    }
    return done();
}
void press (Component& editor, const juce::String& title)
{
    auto* control = findControl (editor, title);
    assert (control != nullptr);
    control->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
}

// Amplitude of harmonic k of a note in rendered audio: energy of its Hann main lobe, so bin placement does not matter.
double harmonic (const std::vector<float>& audio, double rate, int note, int k)
{
    const auto energy = spectrum (audio);
    const int bin = (int) std::round (k * juce::MidiMessage::getMidiNoteInHertz (note) * kSpectrumSize / rate);
    double lobe = 0;
    for (int b = bin - 4; b <= bin + 4; ++b) lobe += energy[(size_t) b];
    return std::sqrt (lobe);
}

// One note on OSC A playing a user table; returns the settled left channel.
std::vector<float> play (std::shared_ptr<const bw::Wavetable> table, int note, double rate = 48000)
{
    Rig rig (rate);
    rig.processor.setUserTable (0, std::move (table), "edit");
    rig.set ("oscAUser", 1);
    rig.send (on (note));
    const auto audio = rig.run (.5);
    return { audio.begin() + (long) (rate * .1), audio.end() };
}

void checkFrames (const bw::Wavetable& table)
{
    for (int f = 0; f < table.numFrames; ++f)
    {
        float peak = 0; double mean = 0;
        for (int i = 0; i < bw::Wavetable::kSize; ++i) { peak = std::max (peak, std::abs (table.ptr (f, 0)[i])); mean += table.ptr (f, 0)[i]; }
        assert (std::abs (peak - 1) < 1.0e-5f);                    // normalised: never louder than a factory table
        assert (std::abs (mean / bw::Wavetable::kSize) < 1.0e-5); // no DC
        for (int l = 0; l < bw::Wavetable::kLevels; ++l)
            assert (table.ptr (f, l)[bw::Wavetable::kSize] == table.ptr (f, l)[0]); // the cycle wraps
    }
}

void testHarmonicsSound()
{
    bw::TableEdit edit; // one sine frame
    assert (edit.frames() == 1);
    auto bars = edit.harmonics (0);
    assert (std::abs (bars[0] - 1) < 1.0e-5f && bars[2] < 1.0e-4f);
    edit.setHarmonic (0, 3, .5f);
    bars = edit.harmonics (0);
    assert (std::abs (bars[2] - .5f) < 1.0e-4f && std::abs (bars[0] - 1) < 1.0e-5f);
    const auto table = edit.build();
    checkFrames (*table);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto audio = play (table, 48, rate);
        const double ratio = harmonic (audio, rate, 48, 3) / harmonic (audio, rate, 48, 1);
        assert (std::abs (ratio - .5) < .03); // what the bars show is what plays
        assert (harmonic (audio, rate, 48, 2) < .01 * harmonic (audio, rate, 48, 1));
    }
}

void testLimitsAndContinuity()
{
    bw::TableEdit edit;
    for (int k = 1; k <= bw::TableEdit::kHarmonics; ++k) edit.setHarmonic (0, k, 1);
    checkFrames (*edit.build());
    // A random scribble with a jump at the cycle boundary.
    juce::Random random (7);
    float x = 0, y = 0;
    for (int stroke = 0; stroke < 40; ++stroke)
    {
        const float nx = random.nextFloat(), ny = random.nextFloat() * 2 - 1;
        edit.draw (0, x, y, nx, ny);
        x = nx; y = ny;
    }
    edit.draw (0, .99f, 1, 1, 1); edit.draw (0, 0, -1, .01f, -1);
    const auto table = edit.build();
    checkFrames (*table);
    for (double rate : { 44100.0, 96000.0 })
        assert (inharmonic (play (table, 96, rate), rate, 96) < 1.0e-3); // band-limited: no alias
    // Frame limits.
    while (edit.frames() < bw::kMaxUserFrames) assert (edit.insertFrame (edit.frames() - 1));
    assert (! edit.insertFrame (0));
    while (edit.frames() > 1) assert (edit.removeFrame (0));
    assert (! edit.removeFrame (0));
}

// Publishing edits under a held note never interrupts it, and superseded tables are freed once every
// voice has crossfaded (15 ms) to a newer one.
void testLiveEdits()
{
    Rig rig;
    bw::TableEdit edit;
    edit.insertFrame (0);
    rig.processor.setUserTable (0, edit.build(), "edit");
    rig.set ("oscAUser", 1); rig.set ("wtPos", .5f);
    rig.send (on (48));
    const auto start = rig.run (.1);
    const double level = rms (start, start.size() / 2, start.size());
    std::weak_ptr<const bw::Wavetable> early;
    // One stroke is one undo step, so the history keeps only the tables before and after it.
    const WaveProcessor::ScopedEdit stroke (rig.processor);
    for (int step = 0; step < 60; ++step)
    {
        edit.setHarmonic (step % 2, 1 + step % 8, (float) (1 + step % 4) / 4); // never empties a frame
        auto table = edit.build();
        if (step == 0) early = table;
        rig.processor.setUserTable (0, std::move (table), "edit");
        const auto audio = rig.run (1.0 / 15); // the panel publishes at most 15 times a second
        assert (rms (audio, 0, audio.size()) > .2 * level); // never drops out
    }
    assert (early.expired());
}

void testSaveAndReopen()
{
    bw::TableEdit edit;
    edit.insertFrame (0);
    edit.setHarmonic (1, 5, .7f);
    edit.draw (0, .2f, .9f, .4f, -.3f);
    const auto table = edit.build();
    WaveProcessor processor;
    processor.setUserTable (0, table, "my edit");
    processor.apvts.getParameter ("oscAUser")->setValueNotifyingHost (1);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    WaveProcessor reopened;
    reopened.setStateInformation (state.getData(), (int) state.getSize());
    assert (reopened.usingUserTable (0) && reopened.userTableName (0) == "my edit");
    auto same = [] (const bw::Wavetable& a, const bw::Wavetable& b)
    {
        if (a.numFrames != b.numFrames) return false;
        for (int f = 0; f < a.numFrames; ++f)
            for (int i = 0; i < bw::Wavetable::kSize; ++i)
                if (std::abs (a.ptr (f, 0)[i] - b.ptr (f, 0)[i]) > 1.0e-5f) return false;
        return true;
    };
    assert (same (*reopened.userTable (0), *table));
    // Exported WAV files import back as the same table.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("borato-wave-edit.wav");
    assert (bw::exportTable (*table, file));
    WaveProcessor imported;
    assert (imported.importWavetable (1, file).isEmpty());
    assert (same (*imported.userTable (1), *table));
    file.deleteFile();
}

// The editor page: harmonic bars and drawing reach the oscillator, and preset loads reload the editor.
void testEditorPanel()
{
    WaveProcessor processor;
    processor.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    press (*editor, "EDITOR");
    auto* panel = dynamic_cast<bw::ui::TableEditor*> (findControl (*editor, "Table editor"));
    assert (panel != nullptr && panel->isVisible());
    assert (! processor.usingUserTable (0));

    // Drag harmonic 3 to the top: the published frame now has harmonic 3 as strong as the fundamental.
    const auto bars = panel->harmonicArea().toFloat();
    const float x3 = bars.getX() + bars.getWidth() * 2.5f / bw::TableEdit::kHarmonics;
    panel->mouseDown (mouseEvent (*panel, x3, bars.getBottom() - 2));
    panel->mouseDrag (mouseEvent (*panel, x3, bars.getY()));
    panel->mouseUp (mouseEvent (*panel, x3, bars.getY()));
    assert (processor.usingUserTable (0) && processor.apvts.getRawParameterValue ("oscAUser")->load() == 1);
    assert (processor.userTable (0)->numFrames == processor.wavetableBank[0].numFrames);
    const auto spectrum = bw::TableEdit (*processor.userTable (0)).harmonics (0);
    assert (std::abs (spectrum[2] - 1) < .02f && spectrum[0] > .95f);
    // ...and the oscillator plays it: harmonic 3 is now in the output.
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
        std::vector<float> audio;
        for (int block = 0; block < 64; ++block)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int i = 0; i < 512; ++i) audio.push_back (buffer.getSample (0, i));
        }
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        juce::AudioBuffer<float> buffer (2, 512);
        processor.processBlock (buffer, midi);
        audio.erase (audio.begin(), audio.begin() + 4800);
        assert (harmonic (audio, 48000, 48, 3) > .5 * harmonic (audio, 48000, 48, 1));
    }

    // Draw across the cycle: the published frame follows the stroke.
    const auto wave = panel->waveArea().toFloat();
    panel->mouseDown (mouseEvent (*panel, wave.getX() + 1, wave.getY() + 1));
    panel->mouseDrag (mouseEvent (*panel, wave.getCentreX(), wave.getY() + 1));
    panel->mouseUp (mouseEvent (*panel, wave.getCentreX(), wave.getY() + 1));
    const auto* drawn = processor.userTable (0)->ptr (0, 0);
    // A flat positive plateau over the stroke (lowered by DC removal and normalisation).
    assert (drawn[bw::Wavetable::kSize / 8] > .3f && std::abs (drawn[bw::Wavetable::kSize / 8] - drawn[bw::Wavetable::kSize / 4]) < .01f);

    // Frames: the selected frame and the oscillator position move together.
    const int frames = processor.userTable (0)->numFrames;
    press (*editor, "FRAME >");
    assert (panel->selectedFrame() == 1);
    assert (std::abs (processor.apvts.getRawParameterValue ("wtPos")->load() - 1.0f / (float) (frames - 1)) < 1.0e-4f);
    processor.apvts.getParameter ("wtPos")->setValueNotifyingHost (1);
    assert (pumpTimersUntil ([&] { return panel->selectedFrame() == frames - 1; }));
    press (*editor, "< FRAME");
    press (*editor, "< FRAME");
    assert (panel->selectedFrame() == frames - 3);
    press (*editor, "+ FRAME");
    assert (processor.userTable (0)->numFrames == frames + 1);
    press (*editor, "- FRAME");
    assert (processor.userTable (0)->numFrames == frames);

    // OSC B is edited on its own.
    press (*editor, "EDIT B");
    panel->mouseDown (mouseEvent (*panel, x3, bars.getY()));
    panel->mouseUp (mouseEvent (*panel, x3, bars.getY()));
    assert (processor.usingUserTable (1) && processor.userTable (1)->numFrames == processor.wavetableBank[0].numFrames);

    press (*editor, "+ FRAME");
    assert (panel->frames() == processor.wavetableBank[0].numFrames + 1);

    // A preset load replaces the table under the editor; the editor follows instead of republishing.
    processor.loadFactoryPreset (0);
    assert (pumpTimersUntil ([&] { return panel->frames() == processor.wavetableBank[0].numFrames; }));
    assert (! processor.usingUserTable (1));

    // Screenshots for review.
    const auto shots = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("borato-wave-ui");
    shots.createDirectory();
    for (auto page : { "EDITOR", "SAMPLE", "SYNTH" })
    {
        press (*editor, page);
        juce::PNGImageFormat png;
        auto out = shots.getChildFile (juce::String (page).toLowerCase() + ".png");
        out.deleteFile();
        auto stream = out.createOutputStream();
        assert (png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), *stream));
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
    testHarmonicsSound();
    testLimitsAndContinuity();
    testLiveEdits();
    testSaveAndReopen();
    testEditorPanel();
    std::cout << "ok: harmonics sound as drawn, normalised and wrapped frames, alias-free drawing, frame limits, "
                 "live edits under a note, save/reopen and WAV export, editor panel\n";
}
