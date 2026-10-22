#include "SamplePanel.h"

namespace bw::ui
{
using namespace juce;
namespace
{
const Rectangle<int> waveform { 20, 96, 1160, 180 };
}

SamplePanel::SamplePanel (WaveProcessor& p)
    : addZoneButton ("ADD ZONE", [this] { addZone (); }), addLayerButton ("ADD LAYER", [this] { addLayer (); }), processor (p),
      load ("LOAD SAMPLE", [this] { choose (false); }),
      relink ("RELINK", [this] { choose (true); }),
      cancel ("CANCEL", [this] { processor.cancelSampleLoad (); status = "SAMPLE LOAD CANCELLED"; repaint (); }),
      clear ("CLEAR", [this] { processor.clearSample (); status.clear (); repaint (); }),
      enabled (p.apvts, "sampleOn", "SAMPLE"),
      loop (p.apvts, "sampleLoop", "LOOP"),
      route (p.apvts, "sampleRoute", "ROUTE", "Sample route"),
      mode (p.apvts, "sampleMode", "MODE", "Sample playback mode"),
      level (p.apvts, "sampleLevel", "SAMPLE LEVEL"),
      pitch (p.apvts, "samplePitch", "SAMPLE PITCH"),
      root (p.apvts, "sampleRoot", "ROOT NOTE"),
      pan (p.apvts, "samplePan", "SAMPLE PAN"),
      start (p.apvts, "sampleStart", "SAMPLE START", coral),
      end (p.apvts, "sampleEnd", "SAMPLE END", coral),
      density (p.apvts, "grainDensity", "DENSITY"), size (p.apvts, "grainSize", "GRAIN SIZE"),
      position (p.apvts, "grainPosition", "POSITION"), scan (p.apvts, "grainScan", "SCAN RATE"),
      spread (p.apvts, "grainSpreadPos", "POS SPREAD"), grainPitch (p.apvts, "grainPitch", "GRAIN PITCH"),
      seed (p.apvts, "grainSeed", "GRAIN SEED")
{
    setTitle ("Sample source");
    for (Component* c : std::initializer_list<Component*> { &load, &relink, &cancel, &clear, &route, &enabled, &loop, &level, &pitch, &root, &pan, &start, &end, &mode, &density, &size, &position, &scan, &spread, &grainPitch, &seed })
        addAndMakeVisible (c);
    relink.setEnabled (processor.sampleMissing ());
    cancel.setEnabled (processor.sampleLoading ());
    addAndMakeVisible (zoneChoice); addAndMakeVisible (addZoneButton); addAndMakeVisible (addLayerButton);
    addAndMakeVisible (velocityLaw); addAndMakeVisible (variantChoice);
    velocityLaw.addItem ("LINEAR", 1); velocityLaw.addItem ("EQUAL POWER", 2);
    velocityLaw.setTitle ("Velocity crossfade law"); velocityLaw.onChange = [this] { editZone (); };
    variantChoice.setTitle ("Round robin variant");
    variantChoice.onChange = [this] { selectedVariant = variantChoice.getSelectedItemIndex (); revision = ~0u; timerCallback (); };
    zoneChoice.setTitle ("Sample zone");
    zoneChoice.onChange = [this] { selectedZone = zoneChoice.getSelectedItemIndex (); revision = ~0u; timerCallback (); };
    for (auto* slider : { &lowKey, &highKey, &zoneRoot, &lowVelocity, &highVelocity, &velocityFade, &variantCount })
    {
        addAndMakeVisible (slider);
        slider->setRange (0, 127, 1);
        slider->setSliderStyle (Slider::IncDecButtons);
        slider->setTextBoxStyle (Slider::TextBoxLeft, false, 60, 24);
        slider->onValueChange = [this] { editZone (); };
    }
    lowVelocity.setRange (1, 127, 1); highVelocity.setRange (1, 127, 1); velocityFade.setRange (0, 63, 1); variantCount.setRange (1, 4, 1);
    lowVelocity.setTitle ("Velocity low"); highVelocity.setTitle ("Velocity high"); velocityFade.setTitle ("Velocity crossfade"); variantCount.setTitle ("Round robin count");
    lowKey.setTitle ("Zone low key"); highKey.setTitle ("Zone high key"); zoneRoot.setTitle ("Zone root key");
    timerCallback ();
    startTimerHz (15);
}

void SamplePanel::resized ()
{
    load.setBounds (20, 52, 120, 28);
    relink.setBounds (146, 52, 74, 28);
    cancel.setBounds (226, 52, 74, 28);
    clear.setBounds (306, 52, 70, 28);
    route.setBounds (808, 52, 124, 28);
    enabled.setBounds (940, 52, 116, 28);
    loop.setBounds (1064, 52, 116, 28);
    mode.setBounds (500, 10, 200, 28);
    zoneChoice.setBounds (20, 490, 190, 28); addZoneButton.setBounds (220, 490, 105, 28); addLayerButton.setBounds (335, 490, 105, 28);
    lowKey.setBounds (490, 490, 100, 28); highKey.setBounds (650, 490, 100, 28); zoneRoot.setBounds (810, 490, 100, 28);
    variantChoice.setBounds (940, 490, 125, 28); variantCount.setBounds (1080, 490, 100, 28);
    lowVelocity.setBounds (90, 540, 115, 28); highVelocity.setBounds (290, 540, 115, 28);
    velocityFade.setBounds (500, 540, 115, 28); velocityLaw.setBounds (640, 540, 170, 28);
    int x = 20;
    for (auto* dial : { &level, &pitch, &root, &pan, &start, &end }) { dial->setBounds (x, 300, 100, 88); x += 112; }
    x = 20;
    for (auto* dial : { &density, &size, &position, &scan, &spread, &grainPitch, &seed }) { dial->setBounds (x, 395, 100, 88); x += 112; }

}

void SamplePanel::paint (Graphics& g)
{
    surface (g, getLocalBounds ());
    g.setColour (mint);
    g.fillRoundedRectangle (14, 16, 4, 13, 2);
    label (g, "SAMPLE", { 27, 10, 400, 24 }, 13, text, Justification::centredLeft, true);

    const int routeIdx = jlimit (0, 2, roundToInt (processor.apvts.getRawParameterValue ("sampleRoute")->load ()));
    const char* routeBadge = routeIdx == 1 ? "> FILTER 2" : (routeIdx == 2 ? "> DIRECT" : "> FILTER 1");
    badge (g, routeBadge, { getWidth () - 86, 13, 72, 20 }, routeIdx == 2 ? coral : mint);

    const auto sample = displayedSample ();
    const bool loading = processor.sampleLoading ();
    const bool missing = processor.sampleMissing () || (processor.sampleZonesMapped () && ! displayedSample ());
    const float progress = jlimit (0.0f, 1.0f, processor.sampleLoadProgress ());

    const auto bank = processor.sampleBank ();
    const String selectedName = bank && selectedZone >= 0 && selectedZone < (int) bank->zones.size ()
        ? bank->zones[(size_t) selectedZone].pathForVariant (jlimit (0, bank->zones[(size_t) selectedZone].numVariants - 1, selectedVariant)).getFileNameWithoutExtension () : processor.sampleName ();
    const String heading = loading ? "LOADING... " + String (roundToInt (progress * 100.0f)) + "%"
                         : missing ? "MISSING RR " + String (selectedVariant + 1) + " / " + selectedName
                         : sample != nullptr ? selectedName + "  /  " + String (sample->isStereo () ? "STEREO" : "MONO") + "  /  "
                                                   + String ((double) sample->numFrames () / sample->rate, 2) + " s  /  "
                                                   + String (sample->rate / 1000.0, 1) + " kHz"
                                             : String ("NO SAMPLE / LOAD A WAV, AIFF OR FLAC FILE");
    label (g, heading, { 386, 52, 412, 28 }, 11, (missing || loading) ? coral : muted);

    label (g, "LOW", { 445, 490, 45, 28 }, 11, muted);
    label (g, "HIGH", { 600, 490, 50, 28 }, 11, muted);
    label (g, "ROOT", { 760, 490, 50, 28 }, 11, muted);
    label (g, "RR SLOTS", { 1080, 470, 100, 16 }, 10, muted);
    label (g, "VEL LOW", { 20, 540, 70, 28 }, 11, muted); label (g, "VEL HIGH", { 220, 540, 70, 28 }, 11, muted);
    label (g, "VEL XFADE", { 420, 540, 80, 28 }, 11, muted);
    const float expected = processor.apvts.getRawParameterValue ("grainDensity")->load () * processor.apvts.getRawParameterValue ("grainSize")->load () * .001f;
    label (g, "GRAINS: " + String (processor.grainDisplay.count.load ()) + "/32  / TOTAL " + String (processor.activeGrains.load ()) + "/512", { 820, 400, 360, 22 }, 11, mint);
    label (g, "EXPECTED: " + String (expected, 1) + (expected > 32 || processor.grainPoolSaturated.load () ? " / POOL FULL" : ""), { 820, 428, 360, 22 }, 11, expected > 32 || processor.grainPoolSaturated.load () ? coral : muted);
    if (bank && selectedZone >= 0 && selectedZone < (int) bank->zones.size ())
    {
        const int active = processor.samplePlayback.lastVariant[(size_t) selectedZone].load (std::memory_order_relaxed);
        label (g, "ACTIVE RR: " + (active < 0 ? String ("NONE") : String (active + 1)), { 840, 540, 340, 28 }, 11, mint);
    }
    if (bank)
        for (int note = 0; note < 128; ++note)
        {
            const int zone = bank->findZone (note);
            const float x = 20.0f + 1160.0f * note / 128.0f;
            const bool active = displayedZone >= 0 && displayedZone < (int) bank->zones.size ()
                && note >= bank->zones[(size_t) displayedZone].lowKey && note <= bank->zones[(size_t) displayedZone].highKey;
            const bool selected = selectedZone >= 0 && selectedZone < (int) bank->zones.size ()
                && note >= bank->zones[(size_t) selectedZone].lowKey && note <= bank->zones[(size_t) selectedZone].highKey;
            g.setColour (zone < 0 ? inset : active ? coral : mint.withAlpha (selected ? .8f : .3f));
            g.fillRect (x, 590.0f, 1160.0f / 128.0f - 1.0f, 30.0f);
        }
    label (g, "KEY MAP 0 - 127 / ACTIVE ZONE " + (displayedZone < 0 ? String ("NONE") : String (displayedZone + 1)), { 20, 622, 500, 20 }, 11, muted);
    surface (g, waveform, inset);
    const auto r = waveform.toFloat ().reduced (10);
    if (! overview.empty ())
    {
        const float a = std::min (shown[0], shown[1]), b = std::max (shown[0], shown[1]);
        g.setColour (coral.withAlpha (.08f));
        g.fillRect (r.withX (r.getX () + a * r.getWidth ()).withWidth ((b - a) * r.getWidth ()));
        g.setColour (mint.withAlpha (.8f));
        if (! overviewRight.empty ())
        {
            const float halfH = r.getHeight () * 0.5f;
            const float topCentre = r.getY () + halfH * 0.5f;
            const float botCentre = r.getY () + halfH * 1.5f;
            for (size_t x = 0; x < overview.size (); ++x)
            {
                const float hl = overview[x] * halfH * 0.45f;
                const float hr = overviewRight[x] * halfH * 0.45f;
                const int px = (int) (r.getX () + (float) x);
                g.drawVerticalLine (px, topCentre - hl, topCentre + hl);
                g.drawVerticalLine (px, botCentre - hr, botCentre + hr);
            }
            label (g, "L", { (int) r.getX () + 6, (int) r.getY () + 4, 20, 14 }, 9, muted);
            label (g, "R", { (int) r.getX () + 6, (int) (r.getY () + halfH) + 4, 20, 14 }, 9, muted);
        }
        else
        {
            for (size_t x = 0; x < overview.size (); ++x)
            {
                const float h = overview[x] * r.getHeight () * .5f;
                g.drawVerticalLine ((int) (r.getX () + (float) x), r.getCentreY () - h, r.getCentreY () + h);
            }
        }
        g.setColour (coral);
        for (float edge : { a, b }) g.fillRect (r.getX () + edge * r.getWidth () - 1, r.getY (), 2.0f, r.getHeight ());
        label (g, shown[2] >= .5f ? "LOOP: START > END, CROSSFADED" : "ONE-SHOT: PLAYS START > END", { 30, 104, 400, 16 }, 9, coral);
    }

    if (loading)
    {
        const Rectangle<float> barArea { r.getCentreX () - 160.0f, r.getBottom () - 24.0f, 320.0f, 8.0f };
        g.setColour (inset.brighter (0.2f));
        g.fillRoundedRectangle (barArea, 4.0f);
        g.setColour (mint);
        g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth () * progress), 4.0f);
    }

    if (processor.grainDisplay.count.load (std::memory_order_relaxed) > 0)
        for (const auto& grain : processor.grainDisplay.grains)
        {
            const float amp = grain.amp.load (std::memory_order_relaxed);
            if (amp <= 0) continue;
            const float x = r.getX () + jlimit (0.0f, 1.0f, grain.normPos.load (std::memory_order_relaxed)) * r.getWidth ();
            g.setColour (coral.withAlpha (jlimit (0.0f, 1.0f, amp))); g.drawVerticalLine (roundToInt (x), r.getY (), r.getBottom ());
        }
    const String err = processor.sampleLoadError ().toUpperCase ();
    const String msg = status.isNotEmpty () ? status : err;
    label (g, msg.isNotEmpty () ? msg : "Root note plays the file at its original pitch. Start and end move with the next loop pass or note.",
           { 700, 305, 480, 20 }, 11, msg.isNotEmpty () ? coral : muted);
    label (g, "Saved presets embed a portable sample container; missing legacy paths can be relinked.", { 700, 331, 480, 20 }, 11, muted);
}

void SamplePanel::timerCallback ()
{
    repaint (); // Grain positions change even when parameters and assets do not.
    const int active = processor.activeSampleZone.load (std::memory_order_relaxed);
    if (active != displayedZone) { displayedZone = active; repaint (); }
    const auto now = processor.sampleRevision.load (std::memory_order_relaxed);
    const bool loading = processor.sampleLoading ();
    const bool missing = processor.sampleMissing () || (processor.sampleZonesMapped () && ! displayedSample ());
    const float progress = processor.sampleLoadProgress ();
    std::array<float, 4> region { processor.apvts.getRawParameterValue ("sampleStart")->load (),
                                        processor.apvts.getRawParameterValue ("sampleEnd")->load (),
                                        processor.apvts.getRawParameterValue ("sampleLoop")->load (),
                                        processor.apvts.getRawParameterValue ("sampleRoute")->load () };
    if (processor.sampleZonesMapped ())
        if (const auto bank = processor.sampleBank (); bank && selectedZone >= 0 && selectedZone < (int) bank->zones.size ())
        { const auto& z = bank->zones[(size_t) selectedZone]; region[0] = z.start; region[1] = z.end; region[2] = z.loop ? 1.0f : 0.0f; }
    relink.setEnabled (missing);
    cancel.setEnabled (loading);
    if (now == revision && region == shown && loading == wasLoading && missing == wasMissing && std::abs (progress - lastProgress) < 1.0e-3f)
        return;
    if (now != revision)
    {
        refreshZones ();
        revision = now;
        overview.clear ();
        overviewRight.clear ();
        if (const auto sample = displayedSample ())
        {
            const int columns = waveform.getWidth () - 20;
            overview.assign ((size_t) columns, 0.0f);
            if (sample->isStereo ()) overviewRight.assign ((size_t) columns, 0.0f);
            for (int x = 0; x < columns; ++x)
            {
                const auto from = sample->numFrames () * (size_t) x / (size_t) columns;
                const auto to = sample->numFrames () * (size_t) (x + 1) / (size_t) columns;
                for (auto i = from; i < to; ++i)
                {
                    overview[(size_t) x] = std::max (overview[(size_t) x], std::abs (sample->data[i]));
                    if (sample->isStereo ())
                        overviewRight[(size_t) x] = std::max (overviewRight[(size_t) x], std::abs (sample->right[i]));
                }
            }
        }
    }
    shown = region;
    wasLoading = loading;
    wasMissing = missing;
    lastProgress = progress;
    repaint ();
}

std::shared_ptr<const bw::Sample> SamplePanel::displayedSample () const
{
    if (const auto bank = processor.sampleBank (); bank && selectedZone >= 0 && selectedZone < (int) bank->zones.size ())
    {
        const auto& z = bank->zones[(size_t) selectedZone];
        const int v = jlimit (0, z.numVariants - 1, selectedVariant);
        if (z.variants[(size_t) v].missing) return {};
        return z.variants[(size_t) v].sample ? z.variants[(size_t) v].sample : v == 0 ? z.sample : nullptr;
    }
    return processor.sample ();
}

void SamplePanel::refreshZones ()
{
    const ScopedValueSetter<bool> guard (updatingZones, true);
    const bool mapped = processor.sampleZonesMapped ();
    root.setEnabled (! mapped); start.setEnabled (! mapped); end.setEnabled (! mapped); loop.setEnabled (! mapped);
    zoneChoice.clear (dontSendNotification);
    const auto bank = processor.sampleBank ();
    for (auto* slider : { &lowKey, &highKey, &zoneRoot, &lowVelocity, &highVelocity, &velocityFade, &variantCount }) slider->setEnabled (bank != nullptr);
    if (! bank) { addZoneButton.setEnabled (false); addLayerButton.setEnabled (false); velocityLaw.setEnabled (false); variantChoice.clear (dontSendNotification); return; }
    velocityLaw.setEnabled (true);
    for (size_t i = 0; i < bank->zones.size (); ++i)
        zoneChoice.addItem ("Zone " + String ((int) i + 1) + " / " + bank->zones[i].path.getFileNameWithoutExtension (), (int) i + 1);
    selectedZone = jlimit (0, (int) bank->zones.size () - 1, selectedZone);
    zoneChoice.setSelectedItemIndex (selectedZone, dontSendNotification);
    const auto& z = bank->zones[(size_t) selectedZone];
    lowKey.setValue (z.lowKey, dontSendNotification); highKey.setValue (z.highKey, dontSendNotification);
    zoneRoot.setValue (z.rootKey, dontSendNotification);
    lowVelocity.setValue (z.lowVel, dontSendNotification); highVelocity.setValue (z.highVel, dontSendNotification);
    velocityFade.setValue (z.crossfadeVel, dontSendNotification); velocityLaw.setSelectedItemIndex ((int) z.crossfadeLaw, dontSendNotification);
    variantCount.setValue (z.numVariants, dontSendNotification); variantChoice.clear (dontSendNotification);
    for (int v = 0; v < z.numVariants; ++v) variantChoice.addItem ("RR " + String (v + 1) + (z.sampleForVariant (v) ? "" : " / MISSING"), v + 1);
    selectedVariant = jlimit (0, z.numVariants - 1, selectedVariant); variantChoice.setSelectedItemIndex (selectedVariant, dontSendNotification);
    addLayerButton.setEnabled (bank->zones.size () < bw::kMaxSampleZones && z.lowVel < z.highVel);
    addZoneButton.setEnabled (bank->zones.size () < bw::kMaxSampleZones && z.highKey > z.lowKey);
}

void SamplePanel::editZone ()
{
    if (updatingZones) return;
    const auto bank = processor.sampleBank ();
    if (! bank || selectedZone < 0 || selectedZone >= (int) bank->zones.size ()) return;
    auto edited = *bank;
    auto& z = edited.zones[(size_t) selectedZone];
    z.lowKey = (int) lowKey.getValue (); z.highKey = (int) highKey.getValue (); z.rootKey = (int) zoneRoot.getValue ();
    z.lowVel = (int) lowVelocity.getValue (); z.highVel = (int) highVelocity.getValue (); z.crossfadeVel = (int) velocityFade.getValue ();
    z.numVariants = (int) variantCount.getValue ();
    const auto law = (bw::VelocityCrossfadeLaw) velocityLaw.getSelectedItemIndex ();
    for (auto& zone : edited.zones) if (zone.lowKey == z.lowKey && zone.highKey == z.highKey) zone.crossfadeLaw = law;
    status = processor.setSampleBank (edited).toUpperCase ();
    const auto colour = status.isEmpty () ? mint : coral;
    lowKey.setColour (Slider::textBoxOutlineColourId, colour); highKey.setColour (Slider::textBoxOutlineColourId, colour);
    repaint ();
}

void SamplePanel::addZone ()
{
    const auto bank = processor.sampleBank ();
    if (! bank || bank->zones.size () >= bw::kMaxSampleZones) return;
    auto edited = *bank;
    const auto selected = edited.zones[(size_t) selectedZone];
    if (selected.lowKey >= selected.highKey) return;
    const int middle = (selected.lowKey + selected.highKey) / 2;
    const size_t originalSize = edited.zones.size ();
    for (size_t i = 0; i < originalSize; ++i)
        if (edited.zones[i].lowKey == selected.lowKey && edited.zones[i].highKey == selected.highKey)
        {
            auto added = edited.zones[i]; edited.zones[i].highKey = middle; added.lowKey = middle + 1;
            edited.zones.push_back (std::move (added));
        }
    status = processor.setSampleBank (edited).toUpperCase ();
    if (status.isEmpty ()) selectedZone = (int) edited.zones.size () - 1;
    revision = ~0u;
    refreshZones ();
    repaint ();
}

void SamplePanel::addLayer ()
{
    const auto bank = processor.sampleBank ();
    if (! bank || bank->zones.size () >= bw::kMaxSampleZones) return;
    auto edited = *bank; auto& old = edited.zones[(size_t) selectedZone];
    if (old.lowVel >= old.highVel) return;
    auto added = old; old.highVel = (old.lowVel + old.highVel) / 2; added.lowVel = old.highVel + 1;
    edited.zones.push_back (std::move (added)); status = processor.setSampleBank (edited).toUpperCase ();
    if (status.isEmpty ()) selectedZone = (int) edited.zones.size () - 1;
    revision = ~0u; timerCallback ();
}

void SamplePanel::choose (bool isRelink)
{
    chooser = std::make_unique<FileChooser> (isRelink ? "Relink sample" : "Load sample",
                                             File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, isRelink] (const FileChooser& fc)
    {
        const auto file = fc.getResult ();
        if (file == File ()) return;
        status.clear ();
        if (isRelink && ! processor.sampleZonesMapped ())
        {
            status = processor.relinkSample (file).toUpperCase ();
        }
        else
        {
            processor.startSampleLoad (file, {}, processor.sampleZonesMapped () ? selectedZone : -1, selectedVariant);
            if (auto* on = processor.apvts.getParameter ("sampleOn"); on->getValue () < .5f)
            {
                on->beginChangeGesture ();
                on->setValueNotifyingHost (1);
                on->endChangeGesture ();
            }
        }
        repaint ();
    });
}
}
