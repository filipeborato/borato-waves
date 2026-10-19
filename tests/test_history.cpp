// Undo/redo through the processor's public API and the editor's shortcuts.
#include "Rig.h"
#include "PluginEditor.h"
#include <cstdio>

using namespace juce;

namespace
{
float plain (WaveProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

// A user dragging a control: one gesture, several values.
void drag (WaveProcessor& p, const char* id, std::initializer_list<float> values)
{
    auto* parameter = p.apvts.getParameter (id);
    parameter->beginChangeGesture();
    for (float v : values) parameter->setValueNotifyingHost (parameter->convertTo0to1 (v));
    parameter->endChangeGesture();
}

std::shared_ptr<const bw::Wavetable> table (int harmonic)
{
    std::vector<std::vector<float>> cycles (2, std::vector<float> (bw::Wavetable::kSize));
    for (auto& cycle : cycles)
        for (int i = 0; i < bw::Wavetable::kSize; ++i) cycle[(size_t) i] = (float) std::sin (MathConstants<double>::twoPi * harmonic * i / bw::Wavetable::kSize);
    auto result = std::make_shared<bw::Wavetable>();
    result->setFrames (cycles);
    return result;
}

void testGesturesAreSteps()
{
    WaveProcessor p;
    assert (! p.canUndo() && ! p.canRedo());
    drag (p, "cutoff", { 3000, 2000, 1000 });
    assert (p.canUndo() && p.undoName() == "CUTOFF");
    // Host automation between edits is not an edit, and undo leaves it alone.
    p.apvts.getParameter ("reso")->setValueNotifyingHost (p.apvts.getParameter ("reso")->convertTo0to1 (4));
    drag (p, "level", { .2f });
    assert (p.undo() && std::abs (plain (p, "level") - .75f) < 1.0e-4f && std::abs (plain (p, "reso") - 4) < 1.0e-3f);
    assert (p.undo() && std::abs (plain (p, "cutoff") - 8000) < .5f); // the whole drag is one step
    assert (! p.canUndo() && ! p.undo());
    assert (p.redo() && std::abs (plain (p, "cutoff") - 1000) < .5f);
    assert (p.redo() && std::abs (plain (p, "level") - .2f) < 1.0e-4f && ! p.canRedo());
    // A new edit after undo drops what could have been redone.
    assert (p.undo());
    drag (p, "sustain", { .1f });
    assert (! p.canRedo() && p.undoName() == "SUSTAIN");
    // A gesture that ends where it started is not a step.
    drag (p, "decay", { 1, .3f });
    assert (p.undoName() == "SUSTAIN");
    // Undo while a control is still held waits for the gesture to end.
    auto* held = p.apvts.getParameter ("attack");
    held->beginChangeGesture();
    assert (! p.undo());
    held->endChangeGesture();
}

void testGroupsCurvesTablesAndNames()
{
    WaveProcessor p;
    const auto original = p.getLfoCurve (0);
    p.setLfoCurve (0, bw::LfoCurve::factory (3));
    p.setMacroName (1, "Bloom");
    p.setUserTable (0, table (3), "Third");
    drag (p, "oscAUser", { 1 });
    // A grouped edit (one click that changes several things) is one step.
    {
        const WaveProcessor::ScopedEdit edit (p, "ROUTE");
        drag (p, "mod1Source", { 1 });
        drag (p, "mod1Destination", { 12 });
        drag (p, "mod1Enabled", { 1 });
    }
    assert (p.undoName() == "ROUTE" && p.undo());
    assert (plain (p, "mod1Enabled") == 0 && plain (p, "mod1Source") == 0 && plain (p, "mod1Destination") == 0);
    assert (p.undo() && ! p.usingUserTable (0));
    assert (p.undoName() == "OSC A TABLE" && p.undo() && p.userTable (0) == nullptr && p.userTableName (0).isEmpty());
    assert (p.undoName() == "MACRO NAME" && p.undo() && p.getMacroName (1) == "Macro 2");
    assert (p.undoName() == "LFO 1 SHAPE" && p.undo() && p.getLfoCurve (0).count == original.count);
    assert (p.redo() && p.getLfoCurve (0).count == bw::LfoCurve::factory (3).count);
    assert (p.redo() && p.redo() && p.userTableName (0) == "Third" && p.userTable (0)->numFrames == 2);
}

void testPresetLoads()
{
    WaveProcessor p;
    const int before = p.matchingFactoryPreset();
    p.loadFactoryPreset (5);
    assert (p.matchingFactoryPreset() == 5 && p.undo() && p.matchingFactoryPreset() == before);
    assert (p.redo() && p.matchingFactoryPreset() == 5);
    // A user preset file is one step too, parameters included.
    const auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-history").getChildFile ("Dark.bwpreset");
    drag (p, "cutoff", { 600 });
    assert (p.savePreset (file));
    p.loadFactoryPreset (0);
    assert (p.loadPreset (file) && std::abs (plain (p, "cutoff") - 600) < .5f);
    assert (p.undo() && p.matchingFactoryPreset() == 0);
    file.getParentDirectory().deleteRecursively();
    // A host state restore starts a new history.
    MemoryBlock state; p.getStateInformation (state);
    p.setStateInformation (state.getData(), (int) state.getSize());
    assert (! p.canUndo() && ! p.canRedo());
}

// Edit, undo, redo, save, reopen: audio and state follow every step.
void testRoundTrip()
{
    rig::Rig r, host, library;
    auto& p = r.processor;
    auto play = [] (rig::Rig& r)
    {
        r.processor.prepareToPlay (r.rate, rig::Rig::kBlock); // as after a reopen
        r.send (rig::on (57));
        const auto audio = r.run (.1);
        r.send (juce::MidiMessage::allSoundOff (1));
        r.run (.01);
        return audio;
    };
    const auto same = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i] - b[i]) > 1.0e-6f) return false;
        return true;
    };
    const auto original = play (r);
    drag (p, "wtTable", { 2 });
    drag (p, "cutoff", { 900 });
    p.setUserTable (1, table (2), "Second");
    const auto edited = play (r);
    assert (p.undo() && p.undo() && p.undo());
    assert (! same (edited, original));
    assert (same (play (r), original));
    assert (p.redo() && p.redo() && p.redo());
    assert (same (play (r), edited));
    // Reopened as host state and as a preset file, it sounds the same.
    MemoryBlock state; p.getStateInformation (state);
    host.processor.setStateInformation (state.getData(), (int) state.getSize());
    assert (host.processor.selectedTable() == 2 && host.processor.userTableName (1) == "Second");
    assert (same (play (host), edited));
    const auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("borato-history-trip.bwpreset");
    assert (p.savePreset (file) && library.processor.loadPreset (file));
    file.deleteFile();
    assert (same (play (library), edited));
    // Undoing the preset load returns the library instance to its own sound.
    assert (library.processor.undo() && same (play (library), original));
}

// Wheel ticks and arrow steps on one control, close together, are one step.
void testRepeatedSteps()
{
    WaveProcessor p;
    for (float v : { 1000.0f, 1100.0f, 1200.0f }) drag (p, "cutoff", { v });
    drag (p, "reso", { 2 });
    assert (p.undo() && p.undo() && std::abs (plain (p, "cutoff") - 8000) < .5f && ! p.canUndo());
}

// Ctrl+Z / Ctrl+Shift+Z in the editor, and its UNDO / REDO buttons.
void testEditor()
{
    WaveProcessor p;
    WaveEditor editor (p);
    drag (p, "cutoff", { 500 });
    auto* ui = editor.getChildComponent (0);
    assert (ui->keyPressed (KeyPress ('z', ModifierKeys::commandModifier, 0)) && std::abs (plain (p, "cutoff") - 8000) < .5f);
    assert (ui->keyPressed (KeyPress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0)) && std::abs (plain (p, "cutoff") - 500) < .5f);
    Component* undo = nullptr;
    for (auto* child : ui->getChildren()) if (child->getTitle() == "UNDO") undo = child;
    assert (undo != nullptr && undo->keyPressed (KeyPress (KeyPress::returnKey)) && std::abs (plain (p, "cutoff") - 8000) < .5f);
}
}

int main()
{
   #if JUCE_WINDOWS
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
    ScopedJuceInitialiser_GUI juce;
    testGesturesAreSteps();
    testGroupsCurvesTablesAndNames();
    testPresetLoads();
    testRoundTrip();
    testRepeatedSteps();
    testEditor();
    std::puts ("ok: gestures as steps, grouped edits, curves/tables/names, preset loads, edit-undo-redo-save-reopen, shortcuts");
}
