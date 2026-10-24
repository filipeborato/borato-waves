#include "SamplePanel.h"

namespace bw::ui
{
using namespace juce;
namespace
{
const Rectangle<int> waveform { 20, 96, 1160, 180 };
}

SamplePanel::SamplePanel (WaveProcessor& p)
    : addZoneButton ("ADD ZONE", [this] { addZone (); }), addLayerButton ("ADD LAYER", [this] { addLayer (); }),
      sliceButton ("SLICE", [this] { toggleSlices (); }), processor (p),
      load ("LOAD SAMPLE", [this] { choose (false); }),
      relink ("RELINK", [this] { choose (true); }),
      cancel ("CANCEL", [this] { processor.cancelSampleLoad (); status = "SAMPLE LOAD CANCELLED"; repaint (); }),
      clear ("CLEAR", [this] { processor.clearSample (); status.clear (); repaint (); }),
      enabled (p.apvts, "sampleOn", "SAMPLE"),
      loop (p.apvts, "sampleLoop", "LOOP"),
      freeze (p.apvts, "spectralFreeze", "FREEZE"),
      loopLink (p.apvts, "sampleLoopLink", "LINK"),
      jumpStart (p.apvts, "grainJumpStart", "JUMP"), grainLoop (p.apvts, "grainLoop", "G-LOOP"),
      oscA (p.apvts, "oscAOn", "OSC A"), oscB (p.apvts, "oscBOn", "OSC B"),
      route (p.apvts, "sampleRoute", "ROUTE", "Sample route"),
      mode (p.apvts, "sampleMode", "MODE", "Sample playback mode"),
      loopMode (p.apvts, "sampleLoopMode", "LOOP", "Sample loop mode"),
      direction (p.apvts, "sampleDirection", "DIR", "Sample playback direction"),
      densityMode (p.apvts, "grainDensityMode", "DENSITY", "Grain density mode"),
      densityDivision (p.apvts, "grainDensityDivision", "EVERY", "Grain density division"),
      lengthMode (p.apvts, "grainLengthMode", "LENGTH", "Grain length mode"),
      lengthDivision (p.apvts, "grainLengthDivision", "FOR", "Grain length division"),
      distribution (p.apvts, "grainDistribution", "SPREAD", "Grain batch distribution"),
      level (p.apvts, "sampleLevel", "SAMPLE LEVEL"),
      pitch (p.apvts, "samplePitch", "SAMPLE PITCH"),
      root (p.apvts, "sampleRoot", "ROOT NOTE"),
      pan (p.apvts, "samplePan", "SAMPLE PAN"),
      start (p.apvts, "sampleStart", "SAMPLE START", coral),
      end (p.apvts, "sampleEnd", "SAMPLE END", coral),
      loopStart (p.apvts, "sampleLoopStart", "LOOP START"), loopEnd (p.apvts, "sampleLoopEnd", "LOOP END"),
      loopFade (p.apvts, "sampleLoopXfade", "LOOP XFADE"), fadeIn (p.apvts, "sampleFadeIn", "FADE IN"),
      fadeOut (p.apvts, "sampleFadeOut", "FADE OUT"),
      density (p.apvts, "grainDensity", "DENSITY"), size (p.apvts, "grainSize", "GRAIN SIZE"),
      position (p.apvts, "grainPosition", "POSITION"), scan (p.apvts, "grainScan", "SCAN RATE"),
      spread (p.apvts, "grainSpreadPos", "POS SPREAD"), grainPitch (p.apvts, "grainPitch", "GRAIN PITCH"),
      jitter (p.apvts, "grainJitter", "JITTER"), grainPan (p.apvts, "grainPan", "PAN SPREAD"),
      seed (p.apvts, "grainSeed", "GRAIN SEED"),
      spectralPosition (p.apvts, "spectralPos", "SPECTRAL POS"), spectralSpeed (p.apvts, "spectralSpeed", "SPEED"),
      spectralPitch (p.apvts, "spectralPitch", "SPECTRAL PITCH"),
      overlap (p.apvts, "grainOverlap", "OVERLAP"), lengthPercent (p.apvts, "grainLengthPercent", "LENGTH %"),
      batch (p.apvts, "grainBatch", "PER EVENT"), keyTrack (p.apvts, "grainKeyTrack", "KEY TRACK"),
      grainsPage ("GRAINS", [this] { showTiming = false; updatePages (); }),
      timingPage ("TIMING", [this] { showTiming = true; updatePages (); })
{
    setTitle ("Sample source");
    for (Component* c : std::initializer_list<Component*> { &load, &relink, &cancel, &clear, &route, &enabled, &loop, &level, &pitch, &root, &pan, &start, &end, &loopStart, &loopEnd, &loopFade, &fadeIn, &fadeOut, &loopLink, &loopMode, &direction, &mode, &oscA, &oscB })
        addAndMakeVisible (c);
    for (Component* c : std::initializer_list<Component*> { &freeze, &spectralPosition, &spectralSpeed, &spectralPitch, &overlap, &lengthPercent, &batch, &keyTrack,
                                                            &densityMode, &densityDivision, &lengthMode, &lengthDivision, &distribution, &jumpStart, &grainLoop,
                                                            &grainsPage, &timingPage, &density, &size, &position, &scan, &spread, &grainPitch, &jitter, &grainPan, &seed })
        addChildComponent (c);
    relink.setEnabled (processor.sampleMissing ());
    cancel.setEnabled (processor.sampleLoading ());
    addAndMakeVisible (zoneChoice); addAndMakeVisible (addZoneButton); addAndMakeVisible (addLayerButton);
    addAndMakeVisible (velocityLaw); addAndMakeVisible (variantChoice);
    addAndMakeVisible (sliceButton); addAndMakeVisible (sliceRoot); addAndMakeVisible (sliceMode);
    sliceRoot.setRange (0, 127, 1); sliceRoot.setValue (60, dontSendNotification);
    sliceRoot.setSliderStyle (Slider::IncDecButtons); sliceRoot.setTextBoxStyle (Slider::TextBoxLeft, false, 50, 22);
    sliceRoot.setTitle ("Slice root note"); sliceRoot.onValueChange = [this] { if (! updatingZones) commitSlices (sliceMarkers ()); };
    sliceMode.addItem ("SINGLE SLICE", 1); sliceMode.addItem ("PLAY TO END", 2); sliceMode.setSelectedItemIndex (0, dontSendNotification);
    sliceMode.setTitle ("Slice playback"); sliceMode.onChange = [this] { if (! updatingZones) commitSlices (sliceMarkers ()); };
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
    oscA.setBounds (130, 10, 100, 28); oscB.setBounds (236, 10, 100, 28);
    mode.setBounds (500, 10, 200, 28);
    loopMode.setBounds (710, 10, 170, 28);
    direction.setBounds (890, 10, 130, 28);
    zoneChoice.setBounds (20, 490, 190, 28); addZoneButton.setBounds (220, 490, 105, 28); addLayerButton.setBounds (335, 490, 105, 28);
    lowKey.setBounds (490, 490, 100, 28); highKey.setBounds (650, 490, 100, 28); zoneRoot.setBounds (810, 490, 100, 28);
    variantChoice.setBounds (940, 490, 125, 28); variantCount.setBounds (1080, 490, 100, 28);
    lowVelocity.setBounds (90, 540, 115, 28); highVelocity.setBounds (290, 540, 115, 28);
    velocityFade.setBounds (500, 540, 115, 28); velocityLaw.setBounds (640, 540, 170, 28);
    sliceButton.setBounds (600, 624, 90, 26); sliceRoot.setBounds (750, 624, 110, 26); sliceMode.setBounds (870, 624, 150, 26);
    int x = 20;
    for (auto* dial : { &level, &pitch, &root, &pan, &start, &end, &loopStart, &loopEnd, &loopFade, &fadeIn, &fadeOut }) { dial->setBounds (x, 300, 92, 88); x += 98; }
    loopLink.setBounds (x, 330, 1180 - x, 28);
    x = 20;
    for (auto* dial : { &density, &size, &position, &spread, &scan, &grainPitch, &jitter, &grainPan, &seed }) { dial->setBounds (x, 395, 96, 88); x += 104; }
    x = 20;
    for (auto* dial : { &spectralPosition, &spectralSpeed, &spectralPitch }) { dial->setBounds (x, 395, 100, 88); x += 112; }
    freeze.setBounds (x + 6, 425, 116, 28);
    x = 20;
    for (auto* dial : { &overlap, &lengthPercent, &batch, &keyTrack }) { dial->setBounds (x, 395, 100, 88); x += 112; }
    densityMode.setBounds (480, 395, 150, 26); densityDivision.setBounds (640, 395, 160, 26);
    lengthMode.setBounds (480, 427, 150, 26); lengthDivision.setBounds (640, 427, 160, 26);
    distribution.setBounds (480, 459, 150, 26); jumpStart.setBounds (640, 459, 78, 26); grainLoop.setBounds (722, 459, 78, 26);
    grainsPage.setBounds (960, 460, 106, 26); timingPage.setBounds (1074, 460, 106, 26);

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
    if (shownMode == kSampleModeSpectral)
    {
        using State = WaveProcessor::SpectralState;
        const auto state = processor.spectralState ();
        const String stateText = state == State::ready ? "SPECTRAL: READY"
                          : state == State::missing ? "SPECTRAL: MISSING SAMPLE"
                          : state == State::refused ? "SPECTRAL: REFUSED"
                          : "SPECTRAL: ANALYZING... " + String (roundToInt (processor.spectralProgress () * 100)) + "%";
        label (g, stateText, { 600, 400, 580, 22 }, 11, state == State::ready ? mint : coral);
        const String error = processor.spectralError ().toUpperCase ();
        label (g, error.isNotEmpty () ? error : "FFT " + String (bw::kSpectralFftSize) + " / HOP " + String (bw::kSpectralHop) + " / LATENCY "
                   + String (bw::kSpectralLatencySamples) + " SAMPLES / PREVIOUS ANALYSIS PLAYS UNTIL READY",
               { 600, 428, 580, 22 }, 11, error.isNotEmpty () ? coral : muted);
    }
    else if (shownMode == kSampleModeGranular)
    {
        // The same resolution the engine uses, at the root note: effective event rate, length and occupancy.
        auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load (); };
        bw::GrainControls c;
        c.density = value ("grainDensity"); c.sizeMs = value ("grainSize");
        c.densityMode = (bw::GrainDensityMode) jlimit (0, 2, roundToInt (value ("grainDensityMode")));
        c.lengthMode = (bw::GrainLengthMode) jlimit (0, 2, roundToInt (value ("grainLengthMode")));
        c.densityBeats = bw::divisionBeats (roundToInt (value ("grainDensityDivision")));
        c.lengthBeats = bw::divisionBeats (roundToInt (value ("grainLengthDivision")));
        c.overlap = value ("grainOverlap"); c.lengthPercent = value ("grainLengthPercent"); c.batch = roundToInt (value ("grainBatch"));
        const auto timing = bw::resolveGrainTiming (c, processor.currentBpm.load ());
        const double expected = timing.eventHz * timing.batch * timing.lengthMs * .001;
        const bool full = expected > 32 || processor.grainPoolSaturated.load ();
        label (g, "GRAINS " + String (processor.grainDisplay.count.load ()) + "/32  TOTAL " + String (processor.activeGrains.load ()) + "/512", { 960, 396, 220, 18 }, 11, mint);
        label (g, String (timing.eventHz, 1) + " EVENTS/S x" + String (timing.batch) + "  " + String (timing.lengthMs, 0) + " MS", { 960, 416, 220, 18 }, 11, muted);
        label (g, "OVERLAP " + String (expected, 1) + (full ? "  POOL FULL" : ""), { 960, 436, 220, 18 }, 11, full ? coral : muted);
    }
    else
    {
        label (g, "MODE SAMPLE plays the region with the loop and fades above.", { 20, 405, 900, 22 }, 12, muted);
        label (g, "Switch MODE (top) to GRANULAR for a grain cloud or SPECTRAL for stretch / freeze; their controls appear here.", { 20, 431, 900, 22 }, 12, muted);
    }
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
    label (g, "ROOT", { 700, 624, 48, 26 }, 11, muted);
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
        // The inner loop window, as the player derives it (linked points are fractions of the region).
        auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load (); };
        const bool linked = value ("sampleLoopLink") >= .5f;
        float la = std::min (value ("sampleLoopStart"), value ("sampleLoopEnd")), lb = std::max (value ("sampleLoopStart"), value ("sampleLoopEnd"));
        if (linked) { la = a + la * (b - a); lb = a + lb * (b - a); }
        la = jlimit (a, b, la); lb = jlimit (a, b, lb);
        if (shown[2] >= .5f)
        {
            g.setColour (mint.withAlpha (.12f));
            g.fillRect (r.withX (r.getX () + la * r.getWidth ()).withWidth ((lb - la) * r.getWidth ()));
            g.setColour (mint);
            for (float edge : { la, lb }) g.fillRect (r.getX () + edge * r.getWidth () - 1, r.getY (), 2.0f, r.getHeight ());
        }
        const auto modes = bw::loopModeNames ();
        const String loopText = shown[2] >= .5f ? "LOOP " + modes[jlimit (0, 3, roundToInt (value ("sampleLoopMode")))].toUpperCase () + (linked ? " / LINKED" : " / FILE POINTS")
                                                : String ("ONE-SHOT");
        label (g, loopText + (value ("sampleDirection") >= .5f ? " / REVERSE" : " / FORWARD"), { 30, 104, 500, 16 }, 9, coral);
    }

    if (loading)
    {
        const Rectangle<float> barArea { r.getCentreX () - 160.0f, r.getBottom () - 24.0f, 320.0f, 8.0f };
        g.setColour (inset.brighter (0.2f));
        g.fillRoundedRectangle (barArea, 4.0f);
        g.setColour (mint);
        g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth () * progress), 4.0f);
    }

    if (shownMode == kSampleModeGranular && ! overview.empty ())
    {
        // Where new grains start: POSITION inside the region, with its +/- spread band. Click or drag to move it.
        auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load (); };
        const float a = std::min (shown[0], shown[1]), b = std::max (shown[0], shown[1]);
        const float at = a + jlimit (0.0f, 1.0f, value ("grainPosition")) * (b - a), band = jlimit (0.0f, 1.0f, value ("grainSpreadPos")) * (b - a);
        const float lo = jlimit (a, b, at - band), hi = jlimit (a, b, at + band);
        g.setColour (mint.withAlpha (.15f));
        g.fillRect (r.getX () + lo * r.getWidth (), r.getY (), (hi - lo) * r.getWidth (), r.getHeight ());
        g.setColour (mint);
        g.fillRect (r.getX () + at * r.getWidth () - 1.5f, r.getY (), 3.0f, r.getHeight ());
        label (g, "POSITION: CLICK OR DRAG", { roundToInt (r.getX () + at * r.getWidth ()) + 5, (int) r.getY () + 20, 160, 14 }, 9, mint);
    }
    if (processor.grainDisplay.count.load (std::memory_order_relaxed) > 0)
        for (const auto& grain : processor.grainDisplay.grains)
        {
            const float amp = grain.amp.load (std::memory_order_relaxed);
            if (amp <= 0) continue;
            const float x = r.getX () + jlimit (0.0f, 1.0f, grain.normPos.load (std::memory_order_relaxed)) * r.getWidth ();
            g.setColour (coral.withAlpha (jlimit (0.0f, 1.0f, amp))); g.drawVerticalLine (roundToInt (x), r.getY (), r.getBottom ());
        }
    if (const float at = processor.spectralCursor.load (std::memory_order_relaxed); at >= 0)
    {
        g.setColour (coral);
        g.fillRect (r.getX () + at * r.getWidth () - 1, r.getY (), 2.0f, r.getHeight ());
    }
    if (const auto markers = sliceMarkers (); ! markers.empty () && sample != nullptr)
    {
        g.setColour (Colours::yellow.withAlpha (.85f));
        for (size_t i = 0; i < markers.size (); ++i)
        {
            const float x = r.getX () + (float) markers[i] / (float) sample->numFrames () * r.getWidth ();
            g.fillRect (x - 1, r.getY (), 2.0f, r.getHeight ());
            label (g, String ((int) sliceRoot.getValue () + (int) i), { (int) x + 3, (int) r.getBottom () - 16, 40, 14 }, 9, Colours::yellow);
        }
        label (g, "SLICES: DOUBLE-CLICK ADDS / DRAG MOVES / RIGHT-CLICK REMOVES", { 640, 104, 540, 16 }, 9, Colours::yellow);
    }
    const String err = processor.sampleLoadError ().toUpperCase ();
    const String msg = status.isNotEmpty () ? status : err;
    auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load (); };
    const bool sampleOn = value ("sampleOn") >= .5f;
    // Why nothing is heard, ahead of the generic hint.
    const String warning = sample == nullptr && ! loading ? (shownMode == kSampleModeGranular ? "GRAINS NEED A SAMPLE: LOAD ONE" : "")
                         : ! sampleOn ? "SAMPLE IS OFF: TURN SAMPLE ON TO HEAR IT"
                         : value ("sampleLevel") <= 0 ? "SAMPLE LEVEL IS 0" : "";
    label (g, msg.isNotEmpty () ? msg : warning.isNotEmpty () ? warning : String ("Root note plays the file at its original pitch. Start and end move with the next loop pass or note."),
           { 20, 279, 600, 18 }, 10, msg.isNotEmpty () || warning.isNotEmpty () ? coral : muted);
    const String sfz = processor.sfzDiagnostics ().toUpperCase ();
    // Every enabled source sums into its route: the sample (or its grains) plays on top of the oscillators.
    StringArray sounding;
    if (value ("oscAOn") >= .5f) sounding.add ("OSC A");
    if (value ("oscBOn") >= .5f) sounding.add ("OSC B");
    if (value ("subOn") >= .5f) sounding.add ("SUB");
    if (value ("noiseOn") >= .5f) sounding.add ("NOISE");
    if (sampleOn) sounding.add (shownMode == kSampleModeGranular ? "GRAINS" : shownMode == kSampleModeSpectral ? "SPECTRAL" : "SAMPLE");
    label (g, sfz.isNotEmpty () ? "SFZ: " + sfz : "SOUNDING: " + (sounding.isEmpty () ? String ("NOTHING") : sounding.joinIntoString (" + ")) + "  /  SAMPLE " + String (routeBadge),
           { 640, 279, 540, 18 }, 10, sfz.contains ("UNSUPPORTED") || sfz.contains ("MISSING") ? coral : mint);
}

void SamplePanel::timerCallback ()
{
    repaint (); // Grain positions change even when parameters and assets do not.
    if (const int current = roundToInt (processor.apvts.getRawParameterValue ("sampleMode")->load ()); current != shownMode)
    {
        shownMode = current;
        updatePages ();
    }
    updateEnables ();
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

void SamplePanel::updatePages ()
{
    const bool granular = shownMode == kSampleModeGranular, timing = granular && showTiming;
    for (Component* c : std::initializer_list<Component*> { &density, &size, &position, &scan, &spread, &grainPitch, &jitter, &grainPan, &seed }) c->setVisible (granular && ! timing);
    for (Component* c : std::initializer_list<Component*> { &overlap, &lengthPercent, &batch, &keyTrack, &densityMode, &densityDivision,
                                                            &lengthMode, &lengthDivision, &distribution, &jumpStart, &grainLoop }) c->setVisible (timing);
    for (Component* c : std::initializer_list<Component*> { &freeze, &spectralPosition, &spectralSpeed, &spectralPitch }) c->setVisible (shownMode == kSampleModeSpectral);
    for (auto* page : { &grainsPage, &timingPage }) page->setVisible (granular);
    grainsPage.setSelected (! timing); timingPage.setSelected (timing);
    repaint ();
}

// Controls that cannot change what is heard right now are dimmed and locked. Read every tick, so mode changes from
// the host or a preset show at once.
void SamplePanel::updateEnables ()
{
    auto enable = [] (Component& c, bool on) { c.setEnabled (on); c.setAlpha (on ? 1.0f : .3f); };
    auto value = [this] (const char* id) { return roundToInt (processor.apvts.getRawParameterValue (id)->load ()); };
    const bool mapped = processor.sampleZonesMapped (), engine = shownMode != kSampleModePlayback;
    // Zones carry their own loop window, mode and direction; the grain and spectral engines read only the region.
    for (Component* c : std::initializer_list<Component*> { &loopStart, &loopEnd, &loopLink, &loopMode, &direction }) enable (*c, ! mapped && ! engine);
    for (Component* c : std::initializer_list<Component*> { &loopFade, &fadeIn, &fadeOut }) enable (*c, ! engine);
    enable (loop, ! mapped && shownMode != kSampleModeGranular);
    const int rateMode = value ("grainDensityMode"), sizeMode = value ("grainLengthMode");
    enable (density, rateMode == 0 || (rateMode == 2 && sizeMode == 2)); // Percent measures the Free interval
    enable (densityDivision, rateMode == 1);
    enable (overlap, rateMode == 2);
    enable (size, sizeMode == 0);
    enable (lengthDivision, sizeMode == 1);
    enable (lengthPercent, sizeMode == 2);
    enable (distribution, value ("grainBatch") > 1);
}

void SamplePanel::setGrainPosition (int x)
{
    const auto r = waveform.toFloat ().reduced (10);
    const float a = std::min (shown[0], shown[1]), b = std::max (shown[0], shown[1]);
    const float at = ((float) x - r.getX ()) / r.getWidth ();
    if (auto* parameter = processor.apvts.getParameter ("grainPosition"); parameter && b > a)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (jlimit (0.0f, 1.0f, (at - a) / (b - a))));
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
    root.setEnabled (! mapped); start.setEnabled (! mapped); end.setEnabled (! mapped);
    zoneChoice.clear (dontSendNotification);
    const auto bank = processor.sampleBank ();
    for (auto* slider : { &lowKey, &highKey, &zoneRoot, &lowVelocity, &highVelocity, &velocityFade, &variantCount }) slider->setEnabled (bank != nullptr);
    const bool sliced = bank && bank->sliced;
    sliceButton.setText (sliced ? "UNSLICE" : "SLICE"); sliceButton.setEnabled (bank != nullptr);
    sliceRoot.setEnabled (sliced); sliceMode.setEnabled (sliced);
    if (sliced) { sliceRoot.setValue (bank->slices.root, dontSendNotification); sliceMode.setSelectedItemIndex (bank->slices.playToEnd ? 1 : 0, dontSendNotification); }
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
    auto edited = *bank; edited.sliced = false; // hand-edited zones are no longer derived from the markers
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
    auto edited = *bank; edited.sliced = false;
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
    auto edited = *bank; edited.sliced = false; auto& old = edited.zones[(size_t) selectedZone];
    if (old.lowVel >= old.highVel) return;
    auto added = old; old.highVel = (old.lowVel + old.highVel) / 2; added.lowVel = old.highVel + 1;
    edited.zones.push_back (std::move (added)); status = processor.setSampleBank (edited).toUpperCase ();
    if (status.isEmpty ()) selectedZone = (int) edited.zones.size () - 1;
    revision = ~0u; timerCallback ();
}

std::vector<int> SamplePanel::sliceMarkers () const
{
    if (draggedIndex >= 0) return draggedMarkers;
    if (const auto bank = processor.sampleBank (); bank && bank->sliced) return bank->slices.markers;
    return {};
}

int SamplePanel::frameAt (int x) const
{
    const auto sample = processor.sample ();
    const auto r = waveform.toFloat ().reduced (10);
    return sample ? roundToInt (jlimit (0.0f, 1.0f, ((float) x - r.getX ()) / r.getWidth ()) * (float) sample->numFrames ()) : 0;
}

int SamplePanel::markerAt (int x) const
{
    const auto sample = processor.sample ();
    const auto markers = sliceMarkers ();
    const auto r = waveform.toFloat ().reduced (10);
    for (size_t i = 1; sample && i < markers.size (); ++i) // the marker at 0 is fixed
        if (std::abs (r.getX () + (float) markers[i] / (float) sample->numFrames () * r.getWidth () - (float) x) <= 5) return (int) i;
    return -1;
}

void SamplePanel::commitSlices (std::vector<int> markers)
{
    if (markers.empty ()) return;
    status = processor.setSlices (std::move (markers), (int) sliceRoot.getValue (), sliceMode.getSelectedItemIndex () == 1).toUpperCase ();
    revision = ~0u; timerCallback ();
}

void SamplePanel::toggleSlices ()
{
    if (const auto bank = processor.sampleBank (); bank && bank->sliced) { processor.clearSlices (); status.clear (); }
    else commitSlices ({ 0 });
    revision = ~0u; timerCallback ();
}

void SamplePanel::mouseDown (const MouseEvent& e)
{
    if (! waveform.contains (e.getPosition ())) return;
    const int index = sliceMarkers ().empty () ? -1 : markerAt (e.x);
    if (index < 0)
    {
        if (shownMode != kSampleModeGranular || e.mods.isPopupMenu () || overview.empty ()) return;
        draggingPosition = true; // one gesture, one undo step, per drag
        processor.apvts.getParameter ("grainPosition")->beginChangeGesture ();
        setGrainPosition (e.x);
        return;
    }
    auto markers = sliceMarkers ();
    if (e.mods.isPopupMenu ()) { markers.erase (markers.begin () + index); commitSlices (std::move (markers)); return; }
    draggedMarkers = std::move (markers); draggedIndex = index;
}

void SamplePanel::mouseDrag (const MouseEvent& e)
{
    if (draggingPosition) { setGrainPosition (e.x); return; }
    if (draggedIndex < 0) return;
    draggedMarkers[(size_t) draggedIndex] = frameAt (e.x);
    repaint ();
}

void SamplePanel::mouseUp (const MouseEvent&)
{
    if (draggingPosition) { draggingPosition = false; processor.apvts.getParameter ("grainPosition")->endChangeGesture (); return; }
    if (draggedIndex < 0) return;
    auto markers = std::move (draggedMarkers);
    draggedIndex = -1; draggedMarkers.clear ();
    commitSlices (std::move (markers)); // one undo step per drag
}

void SamplePanel::mouseDoubleClick (const MouseEvent& e)
{
    if (! waveform.contains (e.getPosition ()) || sliceMarkers ().empty () || markerAt (e.x) >= 0) return;
    auto markers = sliceMarkers ();
    markers.push_back (frameAt (e.x));
    commitSlices (std::move (markers));
}

void SamplePanel::choose (bool isRelink)
{
    chooser = std::make_unique<FileChooser> (isRelink ? "Relink sample" : "Load sample",
                                             File::getSpecialLocation (File::userMusicDirectory), isRelink ? "*.wav;*.aif;*.aiff;*.flac" : "*.wav;*.aif;*.aiff;*.flac;*.sfz");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, isRelink] (const FileChooser& fc)
    {
        const auto file = fc.getResult ();
        if (file == File ()) return;
        status.clear ();
        if (! isRelink && file.hasFileExtension ("sfz"))
        {
            processor.startSfzImport (file);
            if (auto* on = processor.apvts.getParameter ("sampleOn"); on->getValue () < .5f)
            { on->beginChangeGesture (); on->setValueNotifyingHost (1); on->endChangeGesture (); }
        }
        else if (isRelink && ! processor.sampleZonesMapped ())
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
