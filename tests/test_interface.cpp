#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/Controls.h"
#include "UI/WavetableBrowser.h"
#include <cassert>
#include <cstdio>

using namespace juce;

static Component* findControl (Component& root, const String& title)
{
    if (root.getTitle() == title) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findControl (*child, title)) return found;
    return nullptr;
}
struct GestureProbe : AudioProcessorParameter::Listener
{
    int starts = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool start) override { start ? ++starts : ++ends; }
};
static MouseEvent mouseEvent (Component& c, float x, float y, int flags = ModifierKeys::leftButtonModifier)
{
    const Point<float> point (x, y);
    return { Desktop::getInstance().getMainMouseSource(), point, ModifierKeys (flags), 1, 0, 0, 0, 0,
             &c, &c, Time::getCurrentTime(), point, Time::getCurrentTime(), 1, false };
}
static void snapshot (Component& editor, const File& directory, const String& name)
{
    // Let message-thread polling catch state restores before recording the view.
    Thread::sleep (100);
    Timer::callPendingTimersSynchronously();
    directory.createDirectory();
    auto out = directory.getChildFile (name + ".png").createOutputStream();
    assert (out != nullptr);
    assert (out->setPosition (0));
    assert (out->truncate().wasOk());
    PNGImageFormat png;
    assert (png.writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *out));
}
static void testTableAudio (WaveProcessor& processor)
{
    MemoryBlock saved;
    processor.getStateInformation (saved);
    auto set = [&] (const char* id, float value)
    {
        auto* param = processor.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    for (int i = 1; i <= 16; ++i) set (("mod" + String (i) + "Enabled").toRawUTF8(), 0);
    set ("oscAUnison", 1); set ("oscADetune", 0); set ("oscASpread", 0);
    set ("oscAOn", 1); set ("oscBOn", 0); set ("subOn", 0); set ("noiseOn", 0);
    set ("oscAPitch", 0); set ("oscAFine", 0); set ("oscAPan", 0);
    set ("wtPos", .62f); set ("gain", -12); set ("attack", .001f);
    set ("cutoff", 12000); set ("reso", .707f);
    std::array<std::vector<float>, bw::WavetableBank::kCount> rendered;
    AudioBuffer<float> output (2, 256);
    MidiBuffer events;
    auto silence = [&]
    {
        events.clear(); events.addEvent (MidiMessage::allSoundOff (1), 0);
        processor.processBlock (output, events); events.clear();
    };
    for (int bank = 0; bank < bw::WavetableBank::kCount; ++bank)
    {
        silence(); set ("wtTable", (float) bank);
        processor.prepareToPlay (48000, 256);
        events.addEvent (MidiMessage::noteOn (1, 60, .6f), 0);
        for (int block = 0; block < 16; ++block)
        {
            processor.processBlock (output, events); events.clear();
            for (int i = 0; i < 256; ++i)
            {
                const float value = output.getSample (0, i);
                assert (std::isfinite (value) && std::abs (value) < 2.0f);
                rendered[(size_t) bank].push_back (value);
            }
        }
        assert (output.getMagnitude (0, 0, 256) > .0001f);
        for (int other = 0; other < bank; ++other)
        {
            double delta = 0;
            for (size_t i = 0; i < rendered[(size_t) bank].size(); ++i)
                delta += std::abs (rendered[(size_t) bank][i] - rendered[(size_t) other][i]);
            assert (delta > .1);
        }
    }
    // Automate bank choice and position on a held note across device sample rates.
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0 })
    {
        silence(); processor.prepareToPlay (rate, 256);
        events.addEvent (MidiMessage::noteOn (1, 48, .5f), 0);
        for (int block = 0; block < 100; ++block)
        {
            set ("wtTable", (float) (block % bw::WavetableBank::kCount));
            set ("wtPos", (block % 17) / 16.0f);
            processor.processBlock (output, events); events.clear();
            for (int i = 0; i < 256; ++i)
                assert (std::isfinite (output.getSample (0, i)) && std::abs (output.getSample (0, i)) < 2);
        }
    }
    silence(); processor.prepareToPlay (8000, 256);
    events.addEvent (MidiMessage::noteOn (1, 127, .5f), 0);
    processor.processBlock (output, events); events.clear();
    assert (output.getMagnitude (0, 0, 256) == 0);
    processor.prepareToPlay (48000, 256);
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
}
static void testModulationUI (WaveProcessor& processor, WaveEditor& editor, const File& images, bool capture)
{
    auto set = [&] (const String& id, float value)
    {
        auto* p = processor.apvts.getParameter (id); assert (p);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    auto value = [&] (const String& id) { return processor.apvts.getRawParameterValue (id)->load(); };
    findControl (editor, "SYNTH")->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "LFO 1 shape TRI")->keyPressed (KeyPress (KeyPress::returnKey));
    auto* curve = findControl (editor, "LFO 1 curve editor");
    assert (curve && processor.getLfoCurve (0).count == 3);
    const float x = 9 + (curve->getWidth() - 18) * .25f;
    curve->mouseDoubleClick (mouseEvent (*curve, x, 30));
    assert (processor.getLfoCurve (0).count == 4);
    curve->keyPressed (KeyPress (KeyPress::deleteKey));
    assert (processor.getLfoCurve (0).count == 3);
    // Move a point and bend a segment through real component events.
    auto down = mouseEvent (*curve, (float) curve->getWidth() * .5f, 9);
    curve->mouseDown (down);
    curve->mouseDrag (down.withNewPosition (Point<float> ((float) curve->getWidth() * .55f, 25)));
    curve->mouseUp (down);
    assert (processor.getLfoCurve (0).valid() && processor.getLfoCurve (0).points[1].y < 1);
    auto bend = mouseEvent (*curve, x, (float) curve->getHeight() * .5f);
    curve->mouseDown (bend); curve->mouseDrag (bend.withNewPosition (bend.position.translated (0, 20))); curve->mouseUp (bend);
    assert (processor.getLfoCurve (0).points[0].curve != 0);
    findControl (editor, "LFO 2")->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "LFO 2 shape SQUARE")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.getLfoCurve (1).count == 6 && processor.getLfoCurve (0).count == 3);
    findControl (editor, "LFO 1")->keyPressed (KeyPress (KeyPress::returnKey));
    for (int i = 1; i <= 16; ++i) set ("mod" + String (i) + "Enabled", 0);
    auto* dial = dynamic_cast<bw::ui::Dial*> (findControl (editor, "POSITION"));
    const float base = value ("wtPos");
    DragAndDropTarget::SourceDetails drop (var ("mod-source:1"), findControl (editor, "DRAG LFO 1"), { 40, 40 });
    assert (dial->isInterestedInDragSource (drop));
    dial->itemDropped (drop);
    assert (value ("mod1Enabled") == 1 && value ("mod1Source") == 1 && value ("mod1Destination") == 0);
    assert (value ("wtPos") == base);
    auto* matrix = findControl (editor, "Modulation matrix"); assert (matrix && matrix->isVisible());
    auto* name = findControl (editor, "Macro 1 name");
    name->keyPressed (KeyPress (KeyPress::returnKey));
    name->keyPressed (KeyPress ('A', ModifierKeys::ctrlModifier, 0));
    for (auto c : String ("Movement")) name->keyPressed (KeyPress ((int) c, {}, c));
    name->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.getMacroName (0) == "Movement");
    auto* macro = findControl (editor, "MACRO 1");
    macro->keyPressed (KeyPress (KeyPress::endKey)); assert (value ("macro1") == 1);
    findControl (editor, "Route 1 destination")->keyPressed (KeyPress (KeyPress::rightKey));
    assert (value ("mod1Destination") == 1);
    if (capture) snapshot (editor, images, "modulation-matrix");
    findControl (editor, "ROUTES 9 - 16")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (findControl (editor, "Route 9 source")->getParentComponent()->isVisible());
    findControl (editor, "Route 9 source")->keyPressed (KeyPress (KeyPress::rightKey));
    assert (value ("mod9Source") == 1);
    // Full matrix rejects a drop without replacing a configured slot.
    for (int i = 1; i <= 16; ++i) { set ("mod" + String (i) + "Enabled", 1); set ("mod" + String (i) + "Source", 1); }
    findControl (editor, "SYNTH")->keyPressed (KeyPress (KeyPress::returnKey));
    DragAndDropTarget::SourceDetails macroDrop (var ("mod-source:3"), macro, { 40, 40 });
    dial->itemDropped (macroDrop);
    for (int i = 1; i <= 16; ++i) assert (value ("mod" + String (i) + "Source") == 1);
    // Free slot can be reached from a macro dragged over the Synth tab.
    set ("mod16Enabled", 0);
    auto* synthTab = dynamic_cast<bw::ui::Action*> (findControl (editor, "SYNTH"));
    assert (synthTab->isInterestedInDragSource (macroDrop)); synthTab->itemDragEnter (macroDrop);
    dial->itemDropped (macroDrop); assert (value ("mod16Source") == 3 && value ("mod16Bipolar") == 0);
    MemoryBlock saved; processor.getStateInformation (saved);
    findControl (editor, "Init / Pure wave    /    INIT")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (value ("oscAUnison") == 1 && value ("mod1Enabled") == 0);
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    assert (processor.getMacroName (0) == "Movement" && processor.getLfoCurve (0).count == 3 && value ("mod16Source") == 3);
    // A demonstrative factory sound for the updated interface snapshot.
    findControl (editor, "Slow coastline    /    PAD")->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "SYNTH")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (value ("oscAUnison") == 4 && value ("oscBUnison") == 4 && value ("mod1Enabled") == 1);
    if (capture) snapshot (editor, images, "modulation-unison");
}
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
   #if JUCE_WINDOWS
    // Verify the linked audio-device module registers ASIO, not just its headers.
    {
        AudioDeviceManager devices;
        OwnedArray<AudioIODeviceType> types;
        devices.createAudioDeviceTypes (types);
        bool hasAsio = false;
        for (auto* type : types)
            if (type->getTypeName() == "ASIO")
            {
                hasAsio = true;
                type->scanForDevices();
                std::printf ("ASIO available; installed drivers: %s\n",
                             type->getDeviceNames().joinIntoString (", ").toRawUTF8());
            }
        assert (hasAsio);
    }
   #endif
    WaveProcessor processor;
    processor.prepareToPlay (48000, 256);
    WaveEditor editor (processor);
    auto* position = dynamic_cast<bw::ui::Dial*> (findControl (editor, "POSITION"));
    assert (position != nullptr);
    auto* parameter = processor.apvts.getParameter ("wtPos");
    GestureProbe probe;
    parameter->addListener (&probe);
    position->keyPressed (KeyPress (KeyPress::endKey));
    assert (parameter->getValue() == 1.0f);
    assert (probe.starts == 1 && probe.ends == 1 && probe.changes > 0);
    position->keyPressed (KeyPress (KeyPress::returnKey));
    assert (parameter->getValue() == parameter->getDefaultValue());
    parameter->setValueNotifyingHost (.43f);
    assert (std::abs (position->normalised() - .43f) < .0001f);
    position->mouseDown (mouseEvent (*position, 40, 60));
    position->mouseDrag (mouseEvent (*position, 40, 40));
    position->mouseUp (mouseEvent (*position, 40, 40));
    assert (std::abs (position->normalised() - .53f) < .0001f);
    assert (probe.starts == probe.ends);
    position->mouseDown (mouseEvent (*position, 40, 60));
    position->mouseDrag (mouseEvent (*position, 40, 40, ModifierKeys::leftButtonModifier | ModifierKeys::shiftModifier));
    position->mouseUp (mouseEvent (*position, 40, 40));
    assert (std::abs (position->normalised() - .54f) < .0001f);
    parameter->setValueNotifyingHost (.43f);
    parameter->removeListener (&probe);

    // A host state restore must reach the custom controls, including skewed ranges.
    MemoryBlock saved;
    processor.getStateInformation (saved);
    parameter->setValueNotifyingHost (.8f);
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    assert (std::abs (position->normalised() - .43f) < .0001f);

    auto* tableParam = processor.apvts.getParameter ("wtTable");
    tableParam->setValueNotifyingHost (tableParam->convertTo0to1 (5));
    MemoryBlock tableState;
    processor.getStateInformation (tableState);
    tableParam->setValueNotifyingHost (0);
    processor.setStateInformation (tableState.getData(), (int) tableState.getSize());
    assert (processor.selectedTable() == 5);
    assert (dynamic_cast<bw::ui::Action*> (findControl (editor, "Choose wavetable"))->getText() == "Formant Sweep");
    // Loading an older patch after selecting a new table must restore Basic Shapes.
    auto legacyState = processor.apvts.copyState();
    legacyState.removeChild (legacyState.getChildWithProperty ("id", "wtTable"), nullptr);
    MemoryBlock legacy;
    AudioProcessor::copyXmlToBinary (*legacyState.createXml(), legacy);
    processor.setStateInformation (legacy.getData(), (int) legacy.getSize());
    assert (processor.selectedTable() == 0);

    auto* keyboard = dynamic_cast<bw::ui::Piano*> (findControl (editor, "Performance keyboard"));
    assert (keyboard != nullptr);
    for (int i = 0; i < 20; ++i) keyboard->shiftOctave (1);
    assert (keyboard->octave() == 5);
    for (int i = 0; i < 20; ++i) keyboard->shiftOctave (-1);
    assert (keyboard->octave() == 0);
    keyboard->shiftOctave (3);
    keyboard->mouseDown (mouseEvent (*keyboard, 10, 60));
    assert (processor.keyboardState.isNoteOn (1, 48));
    keyboard->mouseDrag (mouseEvent (*keyboard, 48, 60));
    assert (! processor.keyboardState.isNoteOn (1, 48));
    assert (processor.keyboardState.isNoteOn (1, 50));
    keyboard->mouseUp (mouseEvent (*keyboard, 48, 60));
    assert (! processor.keyboardState.isNoteOn (1, 50));
    keyboard->mouseDown (mouseEvent (*keyboard, 10, 60));
    keyboard->focusLost (Component::focusChangedDirectly);
    assert (! processor.keyboardState.isNoteOn (1, 48));

    // Render MIDI through the actual processor, checking the UI's output meter source.
    AudioBuffer<float> audio (2, 256);
    MidiBuffer midi;
    midi.addEvent (MidiMessage::noteOn (1, 60, .5f), 0);
    processor.processBlock (audio, midi);
    assert (processor.outputPeak[0].load() > 0);
    assert (std::abs (processor.outputPeak[0].load() - audio.getMagnitude (0, 0, 256)) < .00001f);
    midi.clear(); midi.addEvent (MidiMessage::noteOff (1, 60), 0);
    processor.processBlock (audio, midi);
    midi.clear();
    for (int block = 0; block < 200; ++block) processor.processBlock (audio, midi);
    assert (processor.outputPeak[0].load() < .00001f);

    for (const char* view : { "2D", "3D" })
        assert (findControl (editor, view)->keyPressed (KeyPress (KeyPress::returnKey)));
    // Restore the init patch before the reference screenshots.
    findControl (editor, "Init / Pure wave")->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "Init / Pure wave    /    INIT")->keyPressed (KeyPress (KeyPress::returnKey));

    // Exercise every page and factory patch through the same keyboard actions as users.
    for (const char* title : { "SYNTH", "EFFECTS", "MATRIX", "EDITOR", "SAMPLE", "ABOUT  0.1" })
    {
        auto* tab = findControl (editor, title);
        assert (tab != nullptr && tab->keyPressed (KeyPress (KeyPress::returnKey)));
        if (argc > 1)
            snapshot (editor, File (String (argv[1])), String (title).toLowerCase());
    }
    findControl (editor, "SYNTH")->keyPressed (KeyPress (KeyPress::returnKey));
    auto* patch = findControl (editor, "Glass current    /    PLUCK");
    assert (patch != nullptr);
    // Open the browser first so its selection follows a normal visible interaction.
    auto* browserButton = findControl (editor, "Init / Pure wave");
    browserButton->mouseDown (mouseEvent (*browserButton, 20, 20));
    browserButton->mouseUp (mouseEvent (*browserButton, 20, 20));
    assert (! position->isEnabled());
    if (argc > 1) snapshot (editor, File (String (argv[1])), "browser");
    patch->keyPressed (KeyPress (KeyPress::returnKey));
    assert (position->isEnabled());
    assert (std::abs (processor.apvts.getRawParameterValue ("cutoff")->load() - 6500) < .1f);
    assert (std::abs (position->normalised() - .42f) < .0001f);
    // The arrows walk every factory preset and wrap back to the same one.
    for (size_t i = 0; i < bw::factoryPresets().size(); ++i) findControl (editor, ">")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (std::abs (position->normalised() - .42f) < .0001f);
    // Search and category filters narrow the library; the arrows follow the filtered list.
    browserButton->keyPressed (KeyPress (KeyPress::returnKey));
    auto* presetSearch = dynamic_cast<bw::ui::SearchField*> (findControl (editor, "Search presets"));
    assert (presetSearch != nullptr);
    for (auto c : String ("reese")) presetSearch->keyPressed (KeyPress ((int) c, {}, c));
    assert (findControl (editor, "Reese Current    /    BASS")->isVisible());
    assert (! findControl (editor, "Glacier Hall    /    PAD")->isVisible());
    presetSearch->clear();
    findControl (editor, "Category PAD")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (findControl (editor, "Glacier Hall    /    PAD")->isVisible() && ! findControl (editor, "Reese Current    /    BASS")->isVisible());
    if (argc > 1) snapshot (editor, File (String (argv[1])), "preset-library");
    findControl (editor, "Glacier Hall    /    PAD")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (dynamic_cast<bw::ui::Action*> (browserButton)->getText() == "Glacier Hall");
    findControl (editor, ">")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (String (bw::factoryPresets()[(size_t) processor.matchingFactoryPreset()].category) == "PAD");
    browserButton->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "Category ALL")->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "Glass current    /    PLUCK")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (std::abs (position->normalised() - .42f) < .0001f);
    assert (processor.selectedTable() == 6);
    assert (dynamic_cast<bw::ui::Action*> (findControl (editor, "Choose wavetable"))->getText() == "Spectral Bells");
    auto* tableButton = findControl (editor, "Choose wavetable");
    tableButton->keyPressed (KeyPress (KeyPress::returnKey));
    if (argc > 1) snapshot (editor, File (String (argv[1])), "wavetables");
    auto* search = dynamic_cast<bw::ui::SearchField*> (findControl (editor, "Search wavetables"));
    assert (search != nullptr && search->isEnabled());
    for (auto c : String ("pulse")) search->keyPressed (KeyPress ((int) c, {}, c));
    assert (findControl (editor, "Pulse Sweep")->isVisible());
    assert (! findControl (editor, "Sine Triangle")->isVisible());
    if (argc > 1) snapshot (editor, File (String (argv[1])), "wavetable-search");
    search->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.selectedTable() == 4 && position->isEnabled());
    GestureProbe tableProbe;
    tableParam->addListener (&tableProbe);
    findControl (editor, "TABLE >")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.selectedTable() == 5);
    assert (dynamic_cast<bw::ui::Action*> (tableButton)->getText() == "Formant Sweep");
    assert (tableProbe.starts == 1 && tableProbe.ends == 1);
    tableParam->removeListener (&tableProbe);
    auto* display = findControl (editor, "Wavetable display");
    display->mouseDown (mouseEvent (*display, 16, 40));
    display->mouseDrag (mouseEvent (*display, (float) display->getWidth() - 16, 40));
    display->mouseUp (mouseEvent (*display, (float) display->getWidth() - 16, 40));
    assert (position->normalised() == 1.0f);
    findControl (editor, "FFT")->keyPressed (KeyPress (KeyPress::returnKey));
    if (argc > 1) snapshot (editor, File (String (argv[1])), "wavetable-spectrum");
    findControl (editor, "3D")->keyPressed (KeyPress (KeyPress::returnKey));
    // The B browser and waveform must edit only B, including host gestures.
    const int tableA = processor.selectedTable();
    const float positionA = processor.apvts.getRawParameterValue ("wtPos")->load();
    findControl (editor, "Choose wavetable B")->keyPressed (KeyPress (KeyPress::returnKey));
    search->clear();
    findControl (editor, "Phase Bend")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.selectedTable (true) == 7 && processor.selectedTable() == tableA);
    auto* bDisplay = findControl (editor, "Wavetable B display");
    auto* bPosition = processor.apvts.getParameter ("wtPosB");
    GestureProbe bProbe; bPosition->addListener (&bProbe);
    bDisplay->mouseDown (mouseEvent (*bDisplay, 16, 40));
    bDisplay->mouseDrag (mouseEvent (*bDisplay, (float) bDisplay->getWidth() - 16, 40));
    bDisplay->mouseUp (mouseEvent (*bDisplay, (float) bDisplay->getWidth() - 16, 40));
    assert (bPosition->getValue() == 1 && bProbe.starts == 1 && bProbe.ends == 1);
    assert (processor.apvts.getRawParameterValue ("wtPos")->load() == positionA);
    bPosition->removeListener (&bProbe);
    auto* bToggle = dynamic_cast<bw::ui::ParameterToggle*> (findControl (editor, "B enable"));
    assert (bToggle && bToggle->selected);
    bToggle->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.apvts.getRawParameterValue ("oscBOn")->load() == 0);
    bToggle->keyPressed (KeyPress (KeyPress::returnKey));
    for (auto title : { "SUB enable", "NOISE enable" })
    {
        auto* control = dynamic_cast<bw::ui::ParameterToggle*> (findControl (editor, title));
        assert (control && ! control->selected);
        control->keyPressed (KeyPress (KeyPress::returnKey));
        assert (control->selected);
    }
    if (argc > 1) snapshot (editor, File (String (argv[1])), "dual-sources");
    MemoryBlock layered; processor.getStateInformation (layered);
    findControl (editor, "Init / Pure wave    /    INIT")->keyPressed (KeyPress (KeyPress::returnKey));
    assert (processor.apvts.getRawParameterValue ("oscBOn")->load() == 0);
    assert (processor.apvts.getRawParameterValue ("subOn")->load() == 0);
    assert (processor.apvts.getRawParameterValue ("noiseOn")->load() == 0);
    processor.setStateInformation (layered.getData(), (int) layered.getSize());
    assert (bToggle->selected && processor.selectedTable (true) == 7);
    assert (dynamic_cast<bw::ui::Action*> (findControl (editor, "Choose wavetable B"))->getText() == "Phase Bend");
    testTableAudio (processor);
    processor.keyboardState.reset();
    testModulationUI (processor, editor, argc > 1 ? File (String (argv[1])) : File(), argc > 1);
    findControl (editor, "SYNTH")->keyPressed (KeyPress (KeyPress::returnKey));
    browserButton->keyPressed (KeyPress (KeyPress::returnKey));
    findControl (editor, "Category ALL")->keyPressed (KeyPress (KeyPress::returnKey));
    auto* atelier = findControl (editor, "Atelier - Elastic Circuit    /    BASS");
    assert (atelier != nullptr);
    atelier->keyPressed (KeyPress (KeyPress::returnKey));
    Timer::callPendingTimersSynchronously ();
    assert (processor.getMacroName (0) == "Shape" && processor.getLfoCurve (0).count == 6);
    assert (processor.userTableName (0) == "Atelier Prism");
    assert (dynamic_cast<bw::ui::Action*> (findControl (editor, "Choose wavetable"))->getText ().contains ("Atelier Prism"));
    if (argc > 1) snapshot (editor, File (String (argv[1])), "atelier-patch");
    editor.setSize (992, 752);
    const auto small = editor.createComponentSnapshot (editor.getLocalBounds());
    assert (small.isValid() && small.getWidth() == 992 && small.getHeight() == 752);
    if (argc > 1) snapshot (editor, File (String (argv[1])), "minimum-size");
    processor.releaseResources();
    std::puts ("ok: controls, automation, state migration, MIDI, eight audible tables, live switching, library search, FFT, resizing");
}
