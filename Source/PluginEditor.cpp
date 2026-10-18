#include "PluginEditor.h"
#include "UI/Controls.h"
#include "UI/Displays.h"
#include "UI/WavetableBrowser.h"
#include "UI/ModulationUI.h"

using namespace juce;
using namespace bw::ui;

namespace
{
constexpr int designWidth = 1240, designHeight = 940;
void section (Graphics& g, Rectangle<int> r, const String& name, const String& subtitle, Colour colour, bool preview = false)
{
    surface (g, r);
    g.setColour (colour); g.fillRoundedRectangle ((float) r.getX() + 14, (float) r.getY() + 16, 4, 13, 2);
    label (g, name, { r.getX() + 27, r.getY() + 10, r.getWidth() - 125, 24 }, 13, text, Justification::centredLeft, true);
    if (preview) badge (g, "PREVIEW", { r.getRight() - 86, r.getY() + 13, 72, 20 }, muted);
    else badge (g, subtitle, { r.getRight() - 86, r.getY() + 13, 72, 20 }, colour);
}
void ghostDial (Graphics& g, Point<float> c, const String& name, Colour tint = faint)
{
    Path arc; arc.addCentredArc (c.x, c.y, 18, 18, 0, -2.4f, 2.4f, true);
    g.setColour (border); g.strokePath (arc, PathStrokeType (2));
    g.setColour (inset); g.fillEllipse (c.x - 13, c.y - 13, 26, 26);
    g.setColour (tint); g.drawLine (c.x, c.y - 7, c.x, c.y - 11, 1.5f);
    label (g, name, { (int) c.x - 42, (int) c.y + 28, 84, 18 }, 9, tint, Justification::centred);
}

class Browser : public Component
{
public:
    inline static const StringArray categories { "ALL", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "SYNTH", "ARP", "SFX", "USER" };
    Browser (std::function<void (int)> chooseFactory, std::function<void (const File&)> chooseFile,
             std::function<void()> save, std::function<void()> open, std::function<void()> dismiss)
        : close ("Close  x", std::move (dismiss)), saveAs ("SAVE AS...", std::move (save)), openFile ("OPEN FILE...", std::move (open)),
          factory (std::move (chooseFactory)), pick (std::move (chooseFile))
    {
        setTitle ("Patch library");
        search.setTitle ("Search presets");
        search.onChange = [this] { filter(); };
        search.onReturn = [this] { if (! visible.empty()) activate (visible.front()); };
        for (Component* c : std::initializer_list<Component*> { &close, &saveAs, &openFile, &search, &viewport }) addAndMakeVisible (c);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        for (int i = 0; i < categories.size(); ++i)
        {
            categoryButtons.push_back (std::make_unique<Action> (categories[i], [this, i] { setCategory (i); }));
            categoryButtons.back()->setTitle ("Category " + categories[i]);
            addAndMakeVisible (*categoryButtons.back());
        }
        const auto& presets = bw::factoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            borato::PresetInfo info;
            info.name = presets[(size_t) i].name; info.category = presets[(size_t) i].category; info.author = "Borato";
            info.description = presets[(size_t) i].description; info.tags = StringArray::fromTokens (presets[(size_t) i].tags, false);
            add (info, i);
        }
        factoryCount = (int) entries.size();
        setCategory (0);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    }
    void setUserPresets (const Array<borato::PresetInfo>& presets)
    {
        entries.resize ((size_t) factoryCount);
        rows.resize ((size_t) factoryCount);
        for (const auto& info : presets) add (info, -1);
        filter();
    }
    void setCurrent (int factoryIndex)
    {
        current = factoryIndex;
        for (size_t i = 0; i < rows.size(); ++i) rows[i]->setSelected (entries[i].factory >= 0 && entries[i].factory == current);
    }
    // Factory indices shown by the current filter, in list order.
    std::vector<int> visibleFactory() const
    {
        std::vector<int> result;
        for (auto i : visible) if (entries[i].factory >= 0) result.push_back (entries[i].factory);
        return result;
    }
    void focusSearch() { search.grabKeyboardFocus(); }
    void resized() override
    {
        close.setBounds (1010, 130, 90, 28);
        search.setBounds (150, 215, 440, 36);
        for (int i = 0; i < (int) categoryButtons.size(); ++i) categoryButtons[(size_t) i]->setBounds (150 + i * 95, 265, 89, 30);
        viewport.setBounds (150, 310, 560, 380);
        saveAs.setBounds (740, 656, 170, 34); openFile.setBounds (920, 656, 170, 34);
        layoutRows();
    }
    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.72f));
        surface (g, { 120, 110, 1000, 640 });
        label (g, "PATCH LIBRARY", { 150, 130, 420, 28 }, 12, mint, Justification::centredLeft, true);
        label (g, "Find your starting point.", { 150, 162, 600, 38 }, 27, text);
        label (g, String (factoryCount) + " FACTORY  /  " + String ((int) entries.size() - factoryCount) + " USER  /  "
                  + String ((int) visible.size()) + " SHOWN", { 610, 223, 480, 20 }, 10, muted, Justification::centredRight);
        if (visible.empty()) label (g, "No presets match. Try another search or category.", { 150, 330, 560, 30 }, 13, muted);
        label (g, "Loading a patch replaces the current parameter values.", { 150, 700, 560, 20 }, 11, muted);
        const Rectangle<int> details (740, 310, 350, 330);
        surface (g, details, inset);
        if (detail < entries.size())
        {
            const auto& info = entries[detail].info;
            label (g, info.category.toUpperCase() + (entries[detail].factory >= 0 ? "  /  FACTORY" : "  /  USER"), { 760, 326, 310, 16 }, 10, mint);
            label (g, info.name, { 760, 348, 310, 34 }, 22, text);
            g.setColour (muted);
            g.setFont (FontOptions (14.0f));
            g.drawFittedText (info.description.isEmpty() ? info.file.getFullPathName() : info.description, { 760, 392, 310, 120 }, Justification::topLeft, 6);
            label (g, info.tags.joinIntoString ("  /  ").toUpperCase(), { 760, 600, 310, 18 }, 9, faint);
        }
    }
private:
    struct Entry { borato::PresetInfo info; int factory = -1; };
    class Row : public Action
    {
    public:
        Row (String s, std::function<void()> click, std::function<void()> hover) : Action (std::move (s), std::move (click)), onHover (std::move (hover)) {}
        void mouseEnter (const MouseEvent& e) override { Action::mouseEnter (e); onHover(); }
        void focusGained (FocusChangeType reason) override { Action::focusGained (reason); onHover(); }
    private:
        std::function<void()> onHover;
    };
    void add (const borato::PresetInfo& info, int factoryIndex)
    {
        const size_t index = entries.size();
        entries.push_back ({ info, factoryIndex });
        rows.push_back (std::make_unique<Row> (info.name + "    /    " + info.category.toUpperCase(),
                                               [this, index] { activate (index); }, [this, index] { detail = index; repaint (740, 310, 350, 330); }));
        rows.back()->setSelected (factoryIndex >= 0 && factoryIndex == current);
        list.addChildComponent (*rows.back());
    }
    void activate (size_t index)
    {
        if (entries[index].factory >= 0) factory (entries[index].factory);
        else pick (entries[index].info.file);
    }
    void setCategory (int index)
    {
        category = index;
        for (int i = 0; i < (int) categoryButtons.size(); ++i) categoryButtons[(size_t) i]->setSelected (i == index);
        filter();
    }
    void filter()
    {
        const bool userOnly = categories[category] == "USER";
        borato::PresetQuery query;
        query.text = search.query();
        query.category = category == 0 || userOnly ? String() : categories[category];
        visible.clear();
        for (size_t i = 0; i < entries.size(); ++i)
            if ((! userOnly || entries[i].factory < 0) && borato::PresetIndex::matches (entries[i].info, query)) visible.push_back (i);
        if (! visible.empty() && std::find (visible.begin(), visible.end(), detail) == visible.end()) detail = visible.front();
        layoutRows();
        repaint();
    }
    void layoutRows()
    {
        const int width = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
        for (auto& row : rows) row->setVisible (false);
        for (size_t k = 0; k < visible.size(); ++k)
        {
            rows[visible[k]]->setBounds (0, (int) k * 42, width, 38);
            rows[visible[k]]->setVisible (true);
        }
        list.setSize (jmax (1, width), jmax (1, (int) visible.size() * 42));
    }
    Action close, saveAs, openFile;
    SearchField search;
    Viewport viewport;
    Component list;
    std::function<void (int)> factory;
    std::function<void (const File&)> pick;
    std::vector<std::unique_ptr<Action>> categoryButtons;
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<size_t> visible;
    size_t detail = 0;
    int factoryCount = 0, category = 0, current = 0;
};
}

class WaveEditor::Interface : public Component, public DragAndDropContainer, private Timer
{
public:
    explicit Interface (WaveProcessor& p)
        : processor (p),
          wt (p, Graph::Kind::wavetable), wtB (p, Graph::Kind::wavetable, true), env (p, Graph::Kind::envelope), filter (p, Graph::Kind::filter), lfo (p), matrix (p),
          wtPos (p.apvts, "wtPos", "POSITION"), level (p.apvts, "level", "LEVEL"),
          wtPosB (p.apvts, "wtPosB", "B POSITION", violet), levelB (p.apvts, "levelB", "B LEVEL", violet),
          pitchA (p.apvts, "oscAPitch", "PITCH"), fineA (p.apvts, "oscAFine", "FINE"), panA (p.apvts, "oscAPan", "PAN"),
          pitchB (p.apvts, "oscBPitch", "B PITCH", violet), fineB (p.apvts, "oscBFine", "B FINE", violet), panB (p.apvts, "oscBPan", "B PAN", violet),
          subLevel (p.apvts, "subLevel", "SUB LEVEL"), subOctave (p.apvts, "subOctave", "OCTAVE"), noiseLevel (p.apvts, "noiseLevel", "NOISE LEVEL"),
          onA (p.apvts, "oscAOn", "A"), onB (p.apvts, "oscBOn", "B"), subOn (p.apvts, "subOn", "SUB"), noiseOn (p.apvts, "noiseOn", "NOISE"),
          cutoff (p.apvts, "cutoff", "CUTOFF", coral), reso (p.apvts, "reso", "RESONANCE", coral),
          attack (p.apvts, "attack", "ATTACK", blue), decay (p.apvts, "decay", "DECAY", blue),
          sustain (p.apvts, "sustain", "SUSTAIN", blue), release (p.apvts, "release", "RELEASE", blue),
          gain (p.apvts, "gain", "MASTER"), piano (p.keyboardState), meter (p),
          preset ("Init / Pure wave", [this] { showBrowser (true); }),
          previous ("<", [this] { stepPreset (-1); }), next (">", [this] { stepPreset (1); }),
          flat ("2D", [this] { setWaveView (true); }), stack ("3D", [this] { setWaveView (false); }),
          fftView ("FFT", [this] { wt.setSpectrum(); flat.setSelected (false); stack.setSelected (false); fftView.setSelected (true); }),
          tableSelector ("Basic Shapes", [this] { showTableBrowser (true); }),
          tablePrevious ("< TABLE", [this] { changeTable ((processor.selectedTable() + bw::WavetableBank::kCount - 1) % bw::WavetableBank::kCount); }),
          tableNext ("TABLE >", [this] { changeTable ((processor.selectedTable() + 1) % bw::WavetableBank::kCount); }),
          flatB ("B 2D", [this] { setWaveViewB (true); }), stackB ("B 3D", [this] { setWaveViewB (false); }),
          fftB ("B FFT", [this] { wtB.setSpectrum(); flatB.setSelected (false); stackB.setSelected (false); fftB.setSelected (true); }),
          selectorB ("Basic Shapes", [this] { showTableBrowser (true, true); }),
          previousB ("B < TABLE", [this] { changeTable ((processor.selectedTable (true) + 7) % 8, true); }),
          nextB ("B TABLE >", [this] { changeTable ((processor.selectedTable (true) + 1) % 8, true); }),
          octaveDown ("-", [this] { piano.shiftOctave (-1); repaint(); }), octaveUp ("+", [this] { piano.shiftOctave (1); repaint(); }),
          browser ([this] (int i) { loadPatch (i); showBrowser (false); }, [this] (const File& f) { loadUserPreset (f); },
                   [this] { savePresetAs(); }, [this] { openPresetFile(); }, [this] { showBrowser (false); }),
          tableBrowser (p.wavetableBank, [this] (int i) { changeTable (i, browsingB); showTableBrowser (false); }, [this] { showTableBrowser (false); }),
          cutoff2 (p.apvts, "cutoff2", "F2 CUTOFF", coral), reso2 (p.apvts, "reso2", "F2 RESONANCE", coral),
          distDrive (p.apvts, "distDrive", "DRIVE", coral), distMix (p.apvts, "distMix", "DIST MIX", coral),
          delayTime (p.apvts, "delayTime", "TIME", coral), delayFeedback (p.apvts, "delayFeedback", "FEEDBACK", coral), delayMix (p.apvts, "delayMix", "DELAY MIX", coral),
          reverbSize (p.apvts, "reverbSize", "SIZE", coral), reverbDamping (p.apvts, "reverbDamping", "DAMPING", coral),
          reverbWidth (p.apvts, "reverbWidth", "WIDTH", coral), reverbMix (p.apvts, "reverbMix", "REVERB MIX", coral),
          filter2On (p.apvts, "filter2On", "F2"), distOn (p.apvts, "distOn", "DISTORTION"), delayOn (p.apvts, "delayOn", "DELAY"),
          delaySync (p.apvts, "delaySync", "SYNC"), delayPingPong (p.apvts, "delayPingPong", "PING-PONG"), reverbOn (p.apvts, "reverbOn", "REVERB"),
          filterType (p.apvts, "filterType", "", "Filter 1 type"), filter2Type (p.apvts, "filter2Type", "", "Filter 2 type"),
          filterRouting (p.apvts, "filterRouting", "ROUTING", "Filter routing"),
          routeA (p.apvts, "oscARoute", "A >", "Osc A route"), routeB (p.apvts, "oscBRoute", "B >", "Osc B route"),
          routeSub (p.apvts, "subRoute", ">", "Sub route"), routeNoise (p.apvts, "noiseRoute", ">", "Noise route"),
          distMode (p.apvts, "distMode", "MODE", "Distortion mode"), delayDivision (p.apvts, "delayDivision", "", "Delay division"),
          importA ("IMPORT WAV", [this] { importTable (0); }), importB ("B IMPORT WAV", [this] { importTable (1); }),
          filterTab1 ("FILTER 1", [this] { selectFilter (0); }), filterTab2 ("FILTER 2", [this] { selectFilter (1); }),
          env2Attack (p.apvts, "env2Attack", "E2 ATTACK", violet), env2Decay (p.apvts, "env2Decay", "E2 DECAY", violet),
          env2Sustain (p.apvts, "env2Sustain", "E2 SUSTAIN", violet), env2Release (p.apvts, "env2Release", "E2 RELEASE", violet),
          envTab1 ("ENV 1", [this] { selectEnvelope (0); }), envTab2 ("ENV 2", [this] { selectEnvelope (1); }), dragEnv2 ("DRAG ENV 2", 9),
          voiceMode (p.apvts, "voiceMode", "", "Voice mode"), glide (p.apvts, "glide", "GLIDE"),
          warpOnA (p.apvts, "oscAWarpOn", "WARP"), warpOnB (p.apvts, "oscBWarpOn", "B WARP"),
          warpA (p.apvts, "oscAWarp", "A WARP"), warpB (p.apvts, "oscBWarp", "B WARP", violet)
    {
        setOpaque (true);
        for (Component* c : std::initializer_list<Component*> { &wt, &env, &filter, &lfo, &wtPos, &level, &cutoff, &reso,
             &attack, &decay, &sustain, &release, &gain, &piano, &meter, &preset, &previous, &next, &flat, &stack,
             &fftView, &tableSelector, &tablePrevious, &tableNext, &octaveDown, &octaveUp,
             &wtB, &wtPosB, &levelB, &pitchA, &fineA, &panA, &pitchB, &fineB, &panB, &subLevel, &subOctave, &noiseLevel,
             &onA, &onB, &subOn, &noiseOn, &flatB, &stackB, &fftB, &selectorB, &previousB, &nextB,
             &voiceMode, &glide, &warpOnA, &warpOnB, &warpA, &warpB })
            addAndMakeVisible (c);
        for (int oscillator = 0; oscillator < 2; ++oscillator)
        {
            const String prefix = oscillator == 0 ? "oscA" : "oscB";
            const StringArray suffixes { "Unison", "Detune", "Spread", "Phase", "Random" };
            const StringArray captions { "VOICES", "DETUNE", "SPREAD", "PHASE", "RANDOM" };
            for (int i = 0; i < 5; ++i)
            {
                auto dial = std::make_unique<Dial> (p.apvts, prefix + suffixes[i], (oscillator == 0 ? "A " : "B ") + captions[i], oscillator == 0 ? mint : violet);
                addAndMakeVisible (*dial); unisonDials.push_back (std::move (dial));
            }
        }
        for (Component* c : std::initializer_list<Component*> { &cutoff2, &reso2, &distDrive, &distMix, &delayTime, &delayFeedback, &delayMix,
             &reverbSize, &reverbDamping, &reverbWidth, &reverbMix, &filter2On, &distOn, &delayOn, &delaySync, &delayPingPong, &reverbOn,
             &filterType, &filter2Type, &filterRouting, &routeA, &routeB, &routeSub, &routeNoise, &distMode, &delayDivision,
             &importA, &importB, &filterTab1, &filterTab2, &env2Attack, &env2Decay, &env2Sustain, &env2Release, &envTab1, &envTab2, &dragEnv2 })
            addChildComponent (c);
        addChildComponent (matrix);
        for (auto* child : getChildren())
            if (auto* dial = dynamic_cast<Dial*> (child))
            {
                for (auto id : bw::destinationIds())
                    if (dial->parameterId() == id)
                    {
                        dial->onModulationDrop = [this] (int source, const String& target) { assignModulation (source, target); };
                        modulationDials.push_back (dial);
                    }
            }
        const StringArray names { "SYNTH", "EFFECTS", "MATRIX", "GLOBAL" };
        for (int i = 0; i < names.size(); ++i)
        {
            tabs.push_back (std::make_unique<Action> (names[i], [this, i] { selectPage (i); }));
            tabs.back()->quiet = true;
            if (i == 0) tabs.back()->onModulationHover = [this] { selectPage (0); };
            addAndMakeVisible (*tabs.back());
        }
        for (int i = 0; i < fxNames.size(); ++i)
        {
            fxButtons.push_back (std::make_unique<Action> (fxNames[i], [this, i]
            {
                selectedFx = i;
                for (int j = 0; j < (int) fxButtons.size(); ++j) fxButtons[(size_t) j]->setSelected (i == j);
                updateVisibility();
                repaint();
            }));
            addChildComponent (*fxButtons.back());
        }
        fxButtons.front()->setSelected (true);
        addChildComponent (browser);
        addChildComponent (tableBrowser);
        tableAttachment = std::make_unique<ParameterAttachment> (*p.apvts.getParameter ("wtTable"), [this] (float value)
        {
            // The callback can run before APVTS updates its raw atomic value.
            // Use the delivered value, not a second read of the previous selection.
            const int index = jlimit (0, bw::WavetableBank::kCount - 1, roundToInt (value));
            tableSelector.setText (bw::WavetableBank::descriptions[(size_t) index].name);
            tableSelector.setTitle ("Choose wavetable");
            if (! browsingB) tableBrowser.setSelection (index);
            wt.repaint(); repaint();
        });
        attachmentB = std::make_unique<ParameterAttachment> (*p.apvts.getParameter ("wtTableB"), [this] (float value)
        {
            const int index = jlimit (0, bw::WavetableBank::kCount - 1, roundToInt (value));
            selectorB.setText (bw::WavetableBank::descriptions[(size_t) index].name);
            selectorB.setTitle ("Choose wavetable B");
            if (browsingB) tableBrowser.setSelection (index);
            wtB.repaint();
        });
        attachmentB->sendInitialUpdate();
        tableAttachment->sendInitialUpdate();
        setWaveViewB (false);
        setWaveView (false);
        selectPage (0);
        startTimerHz (12);
    }

    void resized() override
    {
        preset.setBounds (406, 25, 356, 36);
        previous.setBounds (366, 25, 32, 36); next.setBounds (770, 25, 32, 36);
        gain.setBounds (1040, 7, 88, 77); meter.setBounds (1140, 8, 58, 75);
        voiceMode.setBounds (846, 28, 104, 28); glide.setBounds (956, 7, 80, 77);
        for (int i = 0; i < (int) tabs.size(); ++i) tabs[(size_t) i]->setBounds (20 + i * 106, 94, 98, 31);
        tableSelector.setBounds (34, 177, 278, 27);
        flat.setBounds (322, 178, 38, 25); stack.setBounds (366, 178, 38, 25); fftView.setBounds (410, 178, 44, 25);
        tablePrevious.setBounds (264, 489, 88, 25); tableNext.setBounds (360, 489, 88, 25);
        wt.setBounds (34, 210, 420, 104);
        wtPos.setBounds (34, 320, 80, 79); level.setBounds (119, 320, 80, 79);
        pitchA.setBounds (204, 320, 80, 79); fineA.setBounds (289, 320, 80, 79); panA.setBounds (374, 320, 80, 79);
        onA.setBounds (382, 151, 72, 23); onB.setBounds (842, 151, 72, 23);
        selectorB.setBounds (494, 177, 260, 27);
        flatB.setBounds (762, 178, 44, 25); stackB.setBounds (812, 178, 44, 25); fftB.setBounds (862, 178, 52, 25);
        wtB.setBounds (494, 210, 420, 104);
        wtPosB.setBounds (494, 320, 80, 79); levelB.setBounds (579, 320, 80, 79);
        pitchB.setBounds (664, 320, 80, 79); fineB.setBounds (749, 320, 80, 79); panB.setBounds (834, 320, 80, 79);
        previousB.setBounds (714, 489, 96, 25); nextB.setBounds (816, 489, 98, 25);
        for (int oscillator = 0; oscillator < 2; ++oscillator)
            for (int i = 0; i < 5; ++i)
                unisonDials[(size_t) (oscillator * 5 + i)]->setBounds (34 + oscillator * 460 + i * 71, 404, 68, 79);
        warpA.setBounds (34 + 5 * 71, 404, 68, 79); warpB.setBounds (494 + 5 * 71, 404, 68, 79);
        warpOnA.setBounds (180, 151, 82, 23); warpOnB.setBounds (630, 151, 92, 23);
        filterTab1.setBounds (954, 178, 122, 25); filterTab2.setBounds (1084, 178, 122, 25);
        filter.setBounds (954, 210, 252, 150);
        filterType.setBounds (954, 366, 160, 24); filter2Type.setBounds (954, 366, 160, 24); filter2On.setBounds (1120, 366, 86, 24);
        cutoff.setBounds (960, 394, 110, 88); reso.setBounds (1080, 394, 110, 88);
        cutoff2.setBounds (960, 394, 110, 88); reso2.setBounds (1080, 394, 110, 88);
        filterRouting.setBounds (954, 484, 252, 24);
        routeA.setBounds (270, 151, 106, 23); routeB.setBounds (730, 151, 106, 23);
        importA.setBounds (34, 489, 104, 25); importB.setBounds (494, 489, 104, 25);
        routeSub.setBounds (954, 622, 86, 22); routeNoise.setBounds (954, 706, 86, 22);
        for (auto* toggle : { &distOn, &delayOn, &reverbOn }) toggle->setBounds (1036, 203, 150, 30);
        auto row = [] (std::initializer_list<Component*> dials) { int x = 346; for (auto* d : dials) { d->setBounds (x, 478, 110, 92); x += 140; } };
        row ({ &distDrive, &distMix }); row ({ &delayTime, &delayFeedback, &delayMix }); row ({ &reverbSize, &reverbDamping, &reverbWidth, &reverbMix });
        distMode.setBounds (346, 600, 170, 30);
        delaySync.setBounds (346, 600, 120, 30); delayDivision.setBounds (476, 600, 170, 30); delayPingPong.setBounds (656, 600, 150, 30);
        subOn.setBounds (954, 569, 86, 24); noiseOn.setBounds (954, 653, 86, 24);
        subLevel.setBounds (1044, 563, 78, 81); subOctave.setBounds (1126, 563, 78, 81);
        noiseLevel.setBounds (1044, 651, 78, 81);
        env.setBounds (34, 570, 420, 80);
        attack.setBounds (34, 682, 100, 83); decay.setBounds (140, 682, 100, 83);
        sustain.setBounds (246, 682, 100, 83); release.setBounds (352, 682, 100, 83);
        env2Attack.setBounds (34, 682, 100, 83); env2Decay.setBounds (140, 682, 100, 83);
        env2Sustain.setBounds (246, 682, 100, 83); env2Release.setBounds (352, 682, 100, 83);
        envTab1.setBounds (262, 538, 92, 24); envTab2.setBounds (360, 538, 92, 24);
        dragEnv2.setBounds (314, 654, 140, 22);
        lfo.setBounds (480, 528, 448, 270);
        matrix.setBounds (20, 144, 1200, 654);
        piano.setBounds (164, 831, 1042, 72);
        octaveDown.setBounds (34, 872, 28, 28); octaveUp.setBounds (116, 872, 28, 28);
        for (int i = 0; i < (int) fxButtons.size(); ++i) fxButtons[(size_t) i]->setBounds (38, 239 + i * 54, 242, 42);
        browser.setBounds (getLocalBounds());
        tableBrowser.setBounds (getLocalBounds());
    }

    void paint (Graphics& g) override
    {
        g.fillAll (background);
        g.setGradientFill (ColourGradient (Colour (0xff202b2c), 0, 0, background, 650, 160, false));
        g.fillRect (0, 0, designWidth, 86);
        g.setColour (border); g.drawHorizontalLine (85, 0, designWidth); g.drawHorizontalLine (133, 20, 1220);
        Path logo;
        logo.startNewSubPath (24, 46); logo.lineTo (31, 46); logo.lineTo (37, 28); logo.lineTo (45, 58);
        logo.lineTo (53, 34); logo.lineTo (59, 46); logo.lineTo (67, 46);
        curve (g, logo, mint, 2.5f);
        label (g, "borato", { 82, 18, 155, 32 }, 29, text, Justification::centredLeft, true);
        label (g, "W A V E   /   WAVETABLE SYNTH", { 84, 52, 242, 18 }, 9, muted);
        label (g, "FACTORY / " + String (currentPatch >= 0 ? bw::factoryPresets()[(size_t) currentPatch].category : "USER"), { 406, 9, 356, 13 }, 8, muted);
        label (g, "VOICE ENGINE", { 846, 10, 104, 15 }, 9, muted);
        badge (g, "BORATO WAVE  /  0.1", { 1060, 99, 160, 22 }, muted);
        label (g, page == 0 ? "SOUND STARTS HERE." : page == 1 ? "SHAPE THE SPACE." : page == 2 ? "MAKE CONNECTIONS." : "THE INSTRUMENT, AT A GLANCE.",
               { 532, 98, 480, 24 }, 10, faint, Justification::centredRight);
        if (page == 0) paintSynth (g);
        if (page == 1) paintEffects (g);

        if (page == 3) paintGlobal (g);
        surface (g, { 20, 810, 1200, 100 });
        label (g, "PERFORM", { 34, 824, 112, 19 }, 10, mint, Justification::centredLeft, true);
        label (g, "OCTAVE", { 34, 848, 112, 17 }, 9, muted);
        label (g, String (piano.octave()), { 65, 874, 48, 24 }, 12, text, Justification::centred);
        label (g, "CLICK KEYS TO PLAY  /  A W S E D F T G Y H U J K", { 164, 813, 700, 16 }, 8, muted);
        g.setColour (mint); g.fillEllipse (24, 924, 5, 5);
        label (g, status, { 36, 917, 800, 18 }, 9, mint);

        label (g, "DESIGNED FOR EXPLORATION", { 880, 917, 337, 18 }, 9, faint, Justification::centredRight);
    }

private:
    void paintSynth (Graphics& g)
    {
        section (g, { 20, 144, 448, 372 }, "OSCILLATOR A", "", mint);
        section (g, { 480, 144, 448, 372 }, "OSCILLATOR B", "", violet);
        label (g, framesText (false), { 146, 491, 112, 19 }, 10, mint);
        label (g, framesText (true), { 606, 491, 104, 19 }, 10, violet);
        section (g, { 940, 144, 280, 372 }, "FILTERS", "2 x SVF", coral);
        section (g, { 20, 528, 448, 270 }, "ENVELOPES", "", blue);
        section (g, { 940, 528, 280, 270 }, "SUB / NOISE", "MIX", mint);
        label (g, "SINE", { 954, 602, 86, 18 }, 10, muted);
        label (g, "WHITE", { 954, 686, 86, 18 }, 10, muted);
        label (g, "SOURCES > FILTERS > AMP > FX", { 954, 770, 252, 15 }, 9, faint);

    }
    void paintEffects (Graphics& g)
    {
        const bool live = selectedFx == 0 || selectedFx == 2 || selectedFx == 3;
        section (g, { 20, 144, 278, 654 }, "EFFECT RACK", "3 LIVE", coral);
        label (g, "Six ways to shape a sound.", { 38, 193, 242, 26 }, 14, muted);
        label (g, "DIST > DELAY > REVERB / FIXED ORDER", { 38, 590, 242, 18 }, 9, coral);
        label (g, "CHORUS, EQ, COMP: DESIGN PREVIEW", { 38, 610, 242, 18 }, 9, muted);
        section (g, { 310, 144, 910, 654 }, "EFFECT DESIGN", "LIVE", coral, ! live);
        const StringArray names { "Distortion", "Chorus", "Delay", "Reverb", "Equalizer", "Compressor" };
        const StringArray descriptions { "Harmonics with a little more character.", "A wider horizon for a single voice.", "A conversation between sound and time.",
                                         "Give every note a place to live.", "Sculpt the balance of your sound.", "Shape the movement of dynamics." };
        label (g, names[selectedFx], { 344, 197, 820, 45 }, 34, text);
        label (g, descriptions[selectedFx], { 346, 247, 810, 25 }, 14, muted);
        const Rectangle<int> graphArea (346, 296, 840, 166);
        surface (g, graphArea, inset); grid (g, graphArea.toFloat().reduced (20));
        Path path;
        for (int i = 0; i <= 300; ++i)
        {
            const float t = i / 300.0f;
            float y = 0;
            if (selectedFx == 0)
            {
                // Actual transfer curve of the current distortion settings.
                const float x = t * 2 - 1, mix = raw ("distMix");
                y = x + mix * (bw::shape (roundToInt (raw ("distMode")), x, Decibels::decibelsToGain (raw ("distDrive"))) - x);
            }
            else if (selectedFx == 1) y = std::sin (t * 20) * .65f;
            else if (selectedFx == 2) y = std::sin (t * 65) * std::exp (-t * 3);
            else if (selectedFx == 3) y = std::sin (t * 180) * std::sin (t * 71) * std::exp (-t * 3);
            else if (selectedFx == 4) { const float offset = (t - .6f) * 8; y = .7f * std::exp (-offset * offset) - .3f; }
            else y = t < .5f ? (t * 2 - 1) : (t - .5f) * .6f;
            Point<float> point (366 + t * 800, 379 - y * 59);
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        curve (g, path, coral);
        const std::array<StringArray, 6> parameters {{ { "DRIVE", "TONE", "MIX" }, { "RATE", "DEPTH", "MIX" }, { "TIME", "FEEDBACK", "MIX" },
                                                     { "SIZE", "DECAY", "MIX" }, { "FREQUENCY", "GAIN", "WIDTH" }, { "THRESHOLD", "RATIO", "MAKEUP" } }};
        if (live)
        {
            label (g, "20 ms SAFE BYPASS  /  TAILS CONTINUE AFTER NOTE OFF", { 346, 660, 820, 20 }, 10, muted);
            return;
        }
        for (int i = 0; i < 3; ++i) ghostDial (g, { 487.0f + i * 278, 519 }, parameters[(size_t) selectedFx][i]);
        badge (g, "DESIGN PREVIEW", { 346, 598, 140, 26 }, coral);
        label (g, "This processor will be connected in a future version.", { 504, 598, 682, 26 }, 12, muted);
    }
    void paintGlobal (Graphics& g)
    {
        section (g, { 20, 144, 1200, 654 }, "GLOBAL", "ENGINE", mint);
        label (g, "An instrument made to evolve.", { 48, 193, 1120, 40 }, 29, text);
        const StringArray titles { "VOICE ENGINE", "WAVETABLE", "INTERFACE" };
        const StringArray values { "16 voices", "8 tables", "100% vector" };
        const StringArray details { "Poly, mono or legato / glide / bend +/-2 st", "Dual oscillator / sub / white noise", "Custom controls / scalable drawing" };
        for (int i = 0; i < 3; ++i)
        {
            Rectangle<int> r (48 + i * 386, 261, 362, 132);
            surface (g, r, inset);
            label (g, titles[i], r.reduced (18).removeFromTop (20), 10, mint);
            label (g, values[i], { r.getX() + 18, r.getY() + 42, 326, 36 }, 27, text);
            label (g, details[i], { r.getX() + 18, r.getY() + 91, 326, 23 }, 12, muted);
        }
        label (g, "PLAY & EXPLORE", { 48, 428, 520, 22 }, 11, mint, Justification::centredLeft, true);
        label (g, "Drag a knob vertically. Hold Shift for precise adjustments.", { 48, 463, 700, 26 }, 14, text);
        label (g, "Double click to reset. Use Tab and arrow keys to navigate controls.", { 48, 495, 800, 26 }, 14, text);
        label (g, "Click the keyboard, then play with A W S E D F T G Y H U J K.", { 48, 527, 800, 26 }, 14, text);
        label (g, "The host saves your sound. Save your own presets from the patch library in the top bar.", { 48, 559, 960, 26 }, 14, text);
        badge (g, "VERSION 0.1", { 48, 617, 120, 25 }, mint);
        label (g, "Custom interface / first edition", { 188, 617, 650, 25 }, 12, muted);
    }
    void selectPage (int index)
    {
        page = index;
        for (int i = 0; i < (int) tabs.size(); ++i) tabs[(size_t) i]->setSelected (page == i);
        for (Component* c : std::initializer_list<Component*> { &wt, &env, &filter, &lfo, &wtPos, &level, &cutoff, &reso,
             &attack, &decay, &sustain, &release, &flat, &stack, &fftView, &tableSelector, &tablePrevious, &tableNext,
             &wtB, &wtPosB, &levelB, &pitchA, &fineA, &panA, &pitchB, &fineB, &panB, &subLevel, &subOctave, &noiseLevel,
             &onA, &onB, &subOn, &noiseOn, &flatB, &stackB, &fftB, &selectorB, &previousB, &nextB,
             &warpOnA, &warpOnB, &warpA, &warpB }) c->setVisible (page == 0);
        for (auto& fx : fxButtons) fx->setVisible (page == 1);
        for (auto& dial : unisonDials) dial->setVisible (page == 0);
        matrix.setVisible (page == 2);
        updateVisibility();
        repaint();
    }
    void updateVisibility()
    {
        const bool synth = page == 0, fx = page == 1;
        using List = std::initializer_list<Component*>;
        for (auto* c : List { &importA, &importB, &routeA, &routeB, &routeSub, &routeNoise, &filterRouting, &filterTab1, &filterTab2 }) c->setVisible (synth);
        for (auto* c : List { &filterType, &cutoff, &reso }) c->setVisible (synth && filterPage == 0);
        for (auto* c : List { &envTab1, &envTab2 }) c->setVisible (synth);
        for (auto* c : List { &attack, &decay, &sustain, &release }) c->setVisible (synth && envelopePage == 0);
        for (auto* c : List { &env2Attack, &env2Decay, &env2Sustain, &env2Release, &dragEnv2 }) c->setVisible (synth && envelopePage == 1);
        envTab1.setSelected (envelopePage == 0); envTab2.setSelected (envelopePage == 1);
        env.setEnvelopeIndex (envelopePage);
        for (auto* c : List { &filter2Type, &filter2On, &cutoff2, &reso2 }) c->setVisible (synth && filterPage == 1);
        for (auto* c : List { &distOn, &distMode, &distDrive, &distMix }) c->setVisible (fx && selectedFx == 0);
        for (auto* c : List { &delayOn, &delaySync, &delayDivision, &delayPingPong, &delayTime, &delayFeedback, &delayMix }) c->setVisible (fx && selectedFx == 2);
        for (auto* c : List { &reverbOn, &reverbSize, &reverbDamping, &reverbWidth, &reverbMix }) c->setVisible (fx && selectedFx == 3);
        filterTab1.setSelected (filterPage == 0); filterTab2.setSelected (filterPage == 1);
        filter.setFilterIndex (filterPage);
    }
    void selectFilter (int index) { filterPage = index; updateVisibility(); repaint(); }
    void selectEnvelope (int index) { envelopePage = index; updateVisibility(); repaint(); }
    float raw (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    String framesText (bool b) const
    {
        const auto user = processor.usingUserTable (b) ? processor.userTable (b) : nullptr;
        return user != nullptr ? "USER / " + String (user->numFrames) + " FR" : String (processor.currentWavetable (b).numFrames) + " FRAMES";
    }
    void showStatus (const String& message)
    {
        status = message;
        statusUntil = Time::getMillisecondCounter() + 5000;
        repaint (0, 914, designWidth, 26);
    }
    void setParameter (const char* id, float plain)
    {
        auto* param = processor.apvts.getParameter (id);
        param->beginChangeGesture(); param->setValueNotifyingHost (param->convertTo0to1 (plain)); param->endChangeGesture();
    }
    void importTable (int b)
    {
        chooser = std::make_unique<FileChooser> ("Import wavetable", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, b] (const FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == File()) return;
            const auto error = processor.importWavetable (b, file);
            if (error.isNotEmpty()) { showStatus (error.toUpperCase()); return; }
            setParameter (b ? "oscBUser" : "oscAUser", 1);
            showStatus ("IMPORTED " + file.getFileName().toUpperCase() + " / " + String (processor.userTable (b)->numFrames) + " FRAMES");
            timerCallback(); repaint();
        });
    }
    static Array<borato::PresetInfo> userPresets()
    {
        borato::PresetIndex index;
        index.rebuild ({ WaveProcessor::presetDirectory() }, "*.bwpreset", [] (const File& file)
        {
            auto info = borato::PresetIndex::readFromPath (file);
            // ponytail: parses the whole file (embedded tables included); read only the root tag if libraries grow large.
            if (const auto xml = parseXML (file))
            {
                info.category = xml->getStringAttribute ("presetCategory", "USER");
                info.description = xml->getStringAttribute ("presetDescription");
                info.tags = StringArray::fromTokens (xml->getStringAttribute ("presetTags"), false);
            }
            return info;
        });
        auto presets = index.search ({});
        std::sort (presets.begin(), presets.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
        return presets;
    }
    void savePresetAs()
    {
        const auto folder = WaveProcessor::presetDirectory();
        folder.createDirectory();
        chooser = std::make_unique<FileChooser> ("Save preset", folder.getChildFile (File::createLegalFileName (preset.getText()) + ".bwpreset"), "*.bwpreset");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == File()) return;
            file = file.withFileExtension ("bwpreset");
            if (! processor.savePreset (file)) { showStatus ("COULD NOT SAVE " + file.getFullPathName().toUpperCase()); return; }
            currentPatch = -1;
            preset.setText (file.getFileNameWithoutExtension());
            browser.setUserPresets (userPresets());
            showStatus ("SAVED " + file.getFileNameWithoutExtension().toUpperCase());
            repaint();
        });
    }
    void openPresetFile()
    {
        chooser = std::make_unique<FileChooser> ("Open preset", WaveProcessor::presetDirectory(), "*.bwpreset");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (const auto file = fc.getResult(); file != File()) loadUserPreset (file);
        });
    }
    void loadUserPreset (const File& file)
    {
        piano.releaseNotes();
        if (! processor.loadPreset (file)) { showStatus ("NOT A BORATO WAVE PRESET / " + file.getFileName().toUpperCase()); return; }
        // The timer still recognises a factory patch saved under another name.
        currentPatch = -1;
        preset.setText (file.getFileNameWithoutExtension());
        showBrowser (false);
        showStatus ("LOADED " + file.getFileNameWithoutExtension().toUpperCase());
        timerCallback(); repaint();
    }
    void assignModulation (int source, const String& target)
    {
        int destination = -1;
        for (size_t i = 0; i < bw::destinationIds().size(); ++i)
            if (target == bw::destinationIds()[i]) destination = (int) i;
        if (destination < 0) return;
        for (int i = 1; i <= 16; ++i)
        {
            const String prefix = "mod" + String (i);
            if (processor.apvts.getRawParameterValue (prefix + "Enabled")->load() >= .5f
                && processor.apvts.getRawParameterValue (prefix + "Source")->load() != 0) continue;
            auto set = [&] (const String& suffix, float value)
            {
                auto* param = processor.apvts.getParameter (prefix + suffix);
                param->beginChangeGesture(); param->setValueNotifyingHost (param->convertTo0to1 (value)); param->endChangeGesture();
            };
            set ("Enabled", 0); set ("Source", (float) source); set ("Destination", (float) destination);
            set ("Amount", .2f); set ("Bipolar", source <= 2 ? 1.0f : 0.0f); set ("Enabled", 1);
            selectPage (2); matrix.selectSlot (i - 1);
            status = "ROUTE " + String (i) + " CREATED / ADJUST DEPTH";
            statusUntil = Time::getMillisecondCounter() + 4000;
            timerCallback(); repaint(); return;
        }
        status = "MATRIX FULL / DISABLE A ROUTE BEFORE ADDING ANOTHER";
        statusUntil = Time::getMillisecondCounter() + 5000;
        repaint();
    }
    void setWaveView (bool isFlat) { wt.setFlat (isFlat); flat.setSelected (isFlat); stack.setSelected (! isFlat); fftView.setSelected (false); }
    void setWaveViewB (bool isFlat) { wtB.setFlat (isFlat); flatB.setSelected (isFlat); stackB.setSelected (! isFlat); fftB.setSelected (false); }
    void changeTable (int index, bool b = false)
    {
        // Choosing a factory table leaves the imported one loaded but unused.
        if (raw (b ? "oscBUser" : "oscAUser") >= .5f) setParameter (b ? "oscBUser" : "oscAUser", 0);
        (b ? attachmentB : tableAttachment)->setValueAsCompleteGesture ((float) index);
        timerCallback();
    }
    void loadPatch (int index)
    {
        piano.releaseNotes();
        processor.loadFactoryPreset (index);
        currentPatch = index;
        browser.setCurrent (index);
        preset.setText (bw::factoryPresets()[(size_t) index].name);
        repaint();
    }
    // Arrows walk the factory presets of the browser's current category and search.
    void stepPreset (int step)
    {
        auto order = browser.visibleFactory();
        if (order.empty()) for (int i = 0; i < (int) bw::factoryPresets().size(); ++i) order.push_back (i);
        const auto found = std::find (order.begin(), order.end(), currentPatch);
        const int n = (int) order.size();
        const int position = found == order.end() ? (step > 0 ? -1 : 0) : (int) (found - order.begin());
        loadPatch (order[(size_t) ((position + step + n) % n)]);
    }
    void showBrowser (bool show)
    {
        if (show) browser.setUserPresets (userPresets());
        setOverlay (show ? &browser : nullptr);
        if (show) { browser.toFront (false); browser.focusSearch(); }
        else preset.grabKeyboardFocus();
    }
    void showTableBrowser (bool show, bool b = false)
    {
        setOverlay (show ? &tableBrowser : nullptr);
        if (show) { browsingB = b; tableBrowser.setOscillatorB (b); tableBrowser.setSelection (processor.selectedTable (b)); tableBrowser.toFront (false); tableBrowser.focusSearch(); }
        else (browsingB ? selectorB : tableSelector).grabKeyboardFocus();
    }
    void setOverlay (Component* overlay)
    {
        piano.releaseNotes();
        for (auto* child : getChildren()) child->setEnabled (overlay == nullptr || child == overlay);
        browser.setVisible (overlay == &browser);
        tableBrowser.setVisible (overlay == &tableBrowser);
    }
    bool keyPressed (const KeyPress& k) override
    {
        if (k == KeyPress::escapeKey && browser.isVisible()) { showBrowser (false); return true; }
        if (k == KeyPress::escapeKey && tableBrowser.isVisible()) { showTableBrowser (false); return true; }
        return false;
    }
    void timerCallback() override
    {
        for (auto* dial : modulationDials)
        {
            float low = 0, high = 0;
            for (int route = 1; route <= 16; ++route)
            {
                const String prefix = "mod" + String (route);
                auto v = [&] (const char* suffix) { return processor.apvts.getRawParameterValue (prefix + suffix)->load(); };
                if (v ("Enabled") < .5f || v ("Source") == 0) continue;
                const int target = jlimit (0, bw::kDestinations - 1, roundToInt (v ("Destination")));
                if (dial->parameterId() != bw::destinationIds()[(size_t) target]) continue;
                const float amount = v ("Amount");
                low += v ("Bipolar") >= .5f ? -std::abs (amount) : jmin (0.0f, amount);
                high += v ("Bipolar") >= .5f ? std::abs (amount) : jmax (0.0f, amount);
            }
            dial->setModulationRange (low, high);
        }
        for (int b = 0; b < 2; ++b)
        {
            auto& selector = b == 1 ? selectorB : tableSelector;
            // getValue() is current even before the APVTS raw value catches up.
            auto* table = processor.apvts.getParameter (b == 1 ? "wtTableB" : "wtTable");
            const int index = jlimit (0, bw::WavetableBank::kCount - 1, roundToInt (table->convertFrom0to1 (table->getValue())));
            const String name = processor.usingUserTable (b == 1) ? processor.userTableName (b == 1) : String (bw::WavetableBank::descriptions[(size_t) index].name);
            if (selector.getText() != name)
            {
                selector.setText (name);
                selector.setTitle (b == 1 ? "Choose wavetable B" : "Choose wavetable");
                if (page == 0) repaint (20, 480, 920, 36);
            }
        }
        const std::array<const char*, 6> fxEnables { "distOn", nullptr, "delayOn", "reverbOn", nullptr, nullptr };
        for (size_t i = 0; i < fxEnables.size(); ++i)
            if (fxEnables[i] != nullptr)
                if (const auto caption = fxNames[(int) i] + (raw (fxEnables[i]) >= .5f ? "   ON" : ""); fxButtons[i]->getText() != caption)
                    fxButtons[i]->setText (caption);
        if (page == 1 && selectedFx == 0) repaint (346, 296, 840, 166);
        if (statusUntil != 0 && Time::getMillisecondCounter() > statusUntil)
        { status = "READY / DRAG A SOURCE ONTO A DIAL TO MODULATE"; statusUntil = 0; repaint (0, 914, designWidth, 26); }
        const int match = processor.matchingFactoryPreset();
        if (match != currentPatch)
        {
            currentPatch = match;
            browser.setCurrent (match);
            preset.setText (match >= 0 ? bw::factoryPresets()[(size_t) match].name : "Custom patch");
            repaint (0, 0, designWidth, 86);
        }
    }
    WaveProcessor& processor;
    Graph wt, wtB, env, filter;
    LfoPanel lfo;
    MatrixPanel matrix;
    Dial wtPos, level, wtPosB, levelB, pitchA, fineA, panA, pitchB, fineB, panB, subLevel, subOctave, noiseLevel;
    ParameterToggle onA, onB, subOn, noiseOn;
    Dial cutoff, reso, attack, decay, sustain, release, gain;
    Piano piano;
    Meter meter;
    Action preset, previous, next, flat, stack, fftView, tableSelector, tablePrevious, tableNext, flatB, stackB, fftB, selectorB, previousB, nextB, octaveDown, octaveUp;
    std::vector<std::unique_ptr<Action>> tabs, fxButtons;
    std::vector<std::unique_ptr<Dial>> unisonDials;
    std::vector<Dial*> modulationDials;
    String status = "READY";
    uint32 statusUntil = 0;
    Browser browser;
    WavetableBrowser tableBrowser;
    Dial cutoff2, reso2, distDrive, distMix, delayTime, delayFeedback, delayMix, reverbSize, reverbDamping, reverbWidth, reverbMix;
    ParameterToggle filter2On, distOn, delayOn, delaySync, delayPingPong, reverbOn;
    ParameterChoice filterType, filter2Type, filterRouting, routeA, routeB, routeSub, routeNoise, distMode, delayDivision;
    Action importA, importB, filterTab1, filterTab2;
    Dial env2Attack, env2Decay, env2Sustain, env2Release;
    Action envTab1, envTab2;
    SourceHandle dragEnv2;
    ParameterChoice voiceMode;
    Dial glide;
    ParameterToggle warpOnA, warpOnB;
    Dial warpA, warpB;
    const StringArray fxNames { "01    DISTORTION", "02    CHORUS", "03    DELAY", "04    REVERB", "05    EQUALIZER", "06    COMPRESSOR" };
    std::unique_ptr<FileChooser> chooser;
    std::unique_ptr<ParameterAttachment> tableAttachment, attachmentB;
    bool browsingB = false;
    int page = 0, selectedFx = 0, currentPatch = 0, filterPage = 0, envelopePage = 0;
};

WaveEditor::WaveEditor (WaveProcessor& p) : AudioProcessorEditor (p), ui (std::make_unique<Interface> (p))
{
    addAndMakeVisible (*ui);
    setResizable (true, false);
    setResizeLimits (992, 752, 1736, 1316);
    if (auto* constraints = getConstrainer()) constraints->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth, designHeight);
}
WaveEditor::~WaveEditor() = default;
void WaveEditor::resized()
{
    ui->setBounds (0, 0, designWidth, designHeight);
    ui->setTransform (AffineTransform::scale ((float) getWidth() / designWidth, (float) getHeight() / designHeight));
}
