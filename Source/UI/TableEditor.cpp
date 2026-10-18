#include "TableEditor.h"

namespace bw::ui
{
using namespace juce;

TableEditor::TableEditor (WaveProcessor& p)
    : processor (p),
      editA ("EDIT A", [this] { select (0); }), editB ("EDIT B", [this] { select (1); }),
      previous ("< FRAME", [this] { selectFrame (frame - 1); }), next ("FRAME >", [this] { selectFrame (frame + 1); }),
      add ("+ FRAME", [this] { if (edit.insertFrame (frame)) { selectFrame (frame + 1); publish(); } }),
      remove ("- FRAME", [this] { if (edit.removeFrame (frame)) { selectFrame (frame); publish(); } }),
      save ("EXPORT WAV", [this] { exportWav(); })
{
    setTitle ("Table editor");
    for (auto* c : { &editA, &editB, &previous, &next, &add, &remove, &save }) addAndMakeVisible (c);
    select (0);
    startTimerHz (15);
}

void TableEditor::resized()
{
    editA.setBounds (20, 52, 90, 28); editB.setBounds (116, 52, 90, 28);
    previous.setBounds (250, 52, 90, 28); next.setBounds (470, 52, 90, 28);
    add.setBounds (580, 52, 80, 28); remove.setBounds (666, 52, 80, 28);
    save.setBounds (1060, 52, 120, 28);
}

void TableEditor::paint (Graphics& g)
{
    const auto colour = oscillator == 0 ? mint : violet;
    surface (g, getLocalBounds());
    g.setColour (colour); g.fillRoundedRectangle (14, 16, 4, 13, 2);
    label (g, "TABLE EDITOR", { 27, 10, 400, 24 }, 13, text, Justification::centredLeft, true);
    badge (g, oscillator == 0 ? "OSC A" : "OSC B", { getWidth() - 86, 13, 72, 20 }, colour);
    label (g, "FRAME " + String (frame + 1) + " / " + String (edit.frames()), { 340, 52, 130, 28 }, 12, text, Justification::centred);
    label (g, name.toUpperCase(), { 760, 52, 290, 28 }, 10, muted, Justification::centredRight);

    const auto wave = waveArea();
    surface (g, wave, inset); grid (g, wave.toFloat().reduced (10));
    const auto& cycle = edit.frame (frame);
    Path path;
    const auto r = wave.toFloat().reduced (10);
    for (int x = 0; x <= (int) r.getWidth(); ++x)
    {
        const float y = cycle[(size_t) jmin (Wavetable::kSize - 1, (int) (x * Wavetable::kSize / r.getWidth()))];
        const Point<float> point (r.getX() + (float) x, r.getCentreY() - y * r.getHeight() * .5f);
        if (x == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    curve (g, path, colour);
    label (g, "DRAW THE CYCLE", { wave.getX(), wave.getBottom() + 6, 300, 16 }, 9, muted);

    const auto bars = harmonicArea();
    surface (g, bars, inset);
    const auto levels = edit.harmonics (frame);
    const float width = (float) bars.getWidth() / TableEdit::kHarmonics;
    for (int k = 0; k < TableEdit::kHarmonics; ++k)
    {
        const float height = levels[(size_t) k] * (float) (bars.getHeight() - 20);
        g.setColour (colour.withAlpha (k % 2 == 0 ? .9f : .7f));
        g.fillRect (Rectangle<float> ((float) bars.getX() + k * width + 1, (float) bars.getBottom() - 10 - height, width - 2, height));
    }
    label (g, "HARMONICS 1 - 32 / DRAG TO SET", { bars.getX(), bars.getBottom() + 6, 300, 16 }, 9, muted);
    label (g, status.isNotEmpty() ? status : "Edits play at once as OSC " + String (oscillator == 0 ? "A" : "B")
                                                + "'s user table. The selected frame is the one the oscillator plays (its POSITION follows).",
           { 20, getHeight() - 40, getWidth() - 40, 20 }, 11, status.isNotEmpty() ? mint : muted);
}

void TableEditor::stroke (const MouseEvent& e, bool start)
{
    const auto point = e.position;
    if (start) drag = waveArea().contains (point.toInt()) ? Drag::wave : harmonicArea().contains (point.toInt()) ? Drag::bars : Drag::none;
    if (drag == Drag::wave)
    {
        const auto r = waveArea().toFloat().reduced (10);
        const Point<float> at (jlimit (0.0f, 1.0f, (point.x - r.getX()) / r.getWidth()), jlimit (-1.0f, 1.0f, (r.getCentreY() - point.y) / (r.getHeight() * .5f)));
        edit.draw (frame, start ? at.x : last.x, start ? at.y : last.y, at.x, at.y);
        last = at;
    }
    else if (drag == Drag::bars)
    {
        const auto r = harmonicArea().toFloat();
        const int k = jlimit (1, TableEdit::kHarmonics, 1 + (int) ((point.x - r.getX()) * TableEdit::kHarmonics / r.getWidth()));
        edit.setHarmonic (frame, k, jlimit (0.0f, 1.0f, (r.getBottom() - 10 - point.y) / (r.getHeight() - 20)));
    }
    if (drag != Drag::none) repaint();
}
void TableEditor::mouseDown (const MouseEvent& e) { stroke (e, true); }
void TableEditor::mouseDrag (const MouseEvent& e) { stroke (e, false); }
void TableEditor::mouseUp (const MouseEvent&)
{
    if (drag != Drag::none && edit.changed()) publish();
    drag = Drag::none;
}

const Wavetable& TableEditor::sourceTable() const
{
    if (processor.usingUserTable (oscillator)) return *processor.userTable (oscillator);
    return processor.currentWavetable (oscillator == 1);
}

void TableEditor::select (int index)
{
    oscillator = index;
    editA.setSelected (oscillator == 0); editB.setSelected (oscillator == 1);
    source = nullptr;
    reloadIfReplaced();
}

juce::RangedAudioParameter& TableEditor::position() const
{
    return *processor.apvts.getParameter (oscillator == 0 ? "wtPos" : "wtPosB");
}

void TableEditor::selectFrame (int index)
{
    frame = jlimit (0, edit.frames() - 1, index);
    auto& parameter = position();
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (edit.frames() > 1 ? (float) frame / (float) (edit.frames() - 1) : 0.0f));
    parameter.endChangeGesture();
    repaint();
}

void TableEditor::followPosition()
{
    const int playing = jlimit (0, edit.frames() - 1, roundToInt (position().convertFrom0to1 (position().getValue()) * (float) (edit.frames() - 1)));
    if (playing != frame) { frame = playing; repaint(); }
}

// Another part of the instrument (preset, import, table choice) replaced the oscillator's table.
void TableEditor::reloadIfReplaced()
{
    const auto& table = sourceTable();
    if (&table == source) return;
    source = &table;
    edit = TableEdit (table);
    const auto base = processor.usingUserTable (oscillator) ? processor.userTableName (oscillator)
                                                            : String (WavetableBank::descriptions[(size_t) processor.selectedTable (oscillator == 1)].name);
    name = base.endsWith (" edit") ? base : base + " edit";
    status.clear();
    frame = -1;
    followPosition();
}

void TableEditor::publish()
{
    auto table = edit.build();
    source = table.get();
    processor.setUserTable (oscillator, std::move (table), name);
    if (auto* user = processor.apvts.getParameter (oscillator == 0 ? "oscAUser" : "oscBUser"); user->getValue() < .5f)
    {
        user->beginChangeGesture(); user->setValueNotifyingHost (1); user->endChangeGesture();
    }
    repaint();
}

void TableEditor::timerCallback()
{
    // Live while dragging, but only once the previous edit is freed: without running audio, edits wait for mouse up.
    if (drag != Drag::none && edit.changed() && processor.userTableSettled (oscillator)) publish();
    else if (drag == Drag::none) { reloadIfReplaced(); followPosition(); }
}

void TableEditor::exportWav()
{
    if (edit.changed()) publish();
    chooser = std::make_unique<FileChooser> ("Export wavetable", File::getSpecialLocation (File::userMusicDirectory).getChildFile (File::createLegalFileName (name) + ".wav"), "*.wav");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
    {
        const auto file = fc.getResult().withFileExtension ("wav");
        if (fc.getResult() == File()) return;
        status = exportTable (sourceTable(), file) ? "EXPORTED " + file.getFileName().toUpperCase() : "COULD NOT WRITE " + file.getFullPathName().toUpperCase();
        repaint();
    });
}
}
