#include "Displays.h"

namespace bw::ui
{
using namespace juce;
Graph::Graph (WaveProcessor& p, Kind k, bool b) : processor (p), kind (k), oscillatorB (b)
{
    last.fill (-1);
    startTimerHz (30);
    if (kind == Kind::wavetable)
    {
        setTitle (oscillatorB ? "Wavetable B display" : "Wavetable display");
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
        positionAttachment = std::make_unique<ParameterAttachment> (*processor.apvts.getParameter (oscillatorB ? "wtPosB" : "wtPos"),
                                                                   [this] (float) { repaint(); });
        positionAttachment->sendInitialUpdate();
    }
}
Graph::~Graph() { if (scanning) positionAttachment->endGesture(); }
float Graph::value (const char* id) const
{
    if (oscillatorB && juce::String (id) == "wtPos") id = "wtPosB";
    if (oscillatorB && juce::String (id) == "wtTable") id = "wtTableB";
    if (oscillatorB && juce::String (id) == "oscAUser") id = "oscBUser";
    return processor.apvts.getRawParameterValue (id)->load();
}
Rectangle<float> Graph::plot() const { return getLocalBounds().toFloat().reduced (16, 25).withTrimmedBottom (5); }
void Graph::timerCallback()
{
    const std::array<float, 18> now { value ("env2Attack"), value ("env2Decay"), value ("env2Sustain"), value ("env2Release"), value ("wtPos"), value ("attack"), value ("decay"), value ("sustain"),
                                    value ("release"), value ("cutoff"), value ("reso"), value ("wtTable"),
                                    value ("cutoff2"), value ("reso2"), value ("filterType"), value ("filter2Type"),
                                    value ("oscAUser") + value ("filter2On") * 2, (float) processor.tableRevision.load() };
    if (now != last) { last = now; repaint(); }
}
void Graph::paint (Graphics& g)
{
    surface (g, getLocalBounds(), inset);
    const auto r = plot();
    grid (g, r, kind == Kind::filter ? 6 : 8);
    Path path;
    if (kind == Kind::wavetable)
    {
        const int table = processor.selectedTable (oscillatorB);
        // Hold the user table for the whole paint; a new import may replace it meanwhile.
        const auto user = processor.usingUserTable (oscillatorB) ? processor.userTable (oscillatorB) : nullptr;
        const auto& wt = user != nullptr ? *user : processor.wavetableBank[table];
        const String category = user != nullptr ? "User" : bw::WavetableBank::descriptions[(size_t) table].category;
        const String name = user != nullptr ? processor.userTableName (oscillatorB) : bw::WavetableBank::descriptions[(size_t) table].name;
        const float pos = value ("wtPos");
        if (spectrum)
        {
            // Display-only FFT: no allocation or FFT work is performed by the audio callback.
            std::array<float, bw::Wavetable::kSize * 2> bins {};
            for (int i = 0; i < bw::Wavetable::kSize; ++i) bins[(size_t) i] = wt.read ((double) i / bw::Wavetable::kSize, pos, 0);
            juce::dsp::FFT fft (bw::Wavetable::kOrder);
            fft.performFrequencyOnlyForwardTransform (bins.data());
            const float width = r.getWidth() / 64.0f;
            for (int h = 1; h <= 64; ++h)
            {
                const float magnitude = bins[(size_t) h] * 2 / bw::Wavetable::kSize;
                const float height = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (magnitude, -60.0f) + 60) / 60) * r.getHeight();
                g.setColour (mint.withAlpha (h % 2 == 0 ? .55f : .9f));
                g.fillRect (r.getX() + (h - 1) * width, r.getBottom() - height, width - 1, height);
            }
            label (g, "HARMONICS / 1 - 64", { 13, 7, 230, 14 }, 9, mint);
            label (g, "CURRENT FRAME  /  FULL-BAND TABLE  /  -60 dB FLOOR", { 13, getHeight() - 21, getWidth() - 26, 15 }, 9, faint);
            return;
        }
        const auto wave = [&] (float depth, float position)
        {
            Path p;
            const float width = flat ? r.getWidth() : r.getWidth() * 0.77f;
            const float amp = r.getHeight() * (flat ? 0.44f : 0.23f);
            const float x = r.getX() + (flat ? 0 : depth * r.getWidth() * 0.23f);
            const float y = flat ? r.getCentreY() : r.getBottom() - amp - depth * r.getHeight() * 0.51f;
            for (int i = 0; i <= 220; ++i)
            {
                const Point<float> pt (x + width * i / 220.0f, y - amp * wt.read (jmin (0.999999, i / 220.0), position, 0));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            return p;
        };
        if (! flat)
            for (int f = wt.numFrames - 1; f >= 0; --f)
            {
                const float d = f / (float) jmax (1, wt.numFrames - 1);
                g.setColour (mint.withAlpha (0.10f + 0.18f * (1 - d)));
                g.strokePath (wave (d, d), PathStrokeType (1.0f));
            }
        curve (g, wave (pos, pos), mint, 2.0f);
        label (g, category.toUpperCase() + " / MORPH", { 13, 7, 220, 14 }, 9, muted, Justification::centredLeft, true);
        label (g, "FRAME " + String (roundToInt (pos * (wt.numFrames - 1)) + 1).paddedLeft ('0', 2) + " / " + String (wt.numFrames),
               { getWidth() - 140, 7, 127, 14 }, 9, mint, Justification::centredRight);
        label (g, "DRAG TO SCAN  /  " + name.toUpperCase(), { 13, getHeight() - 21, getWidth() - 26, 15 }, 9, faint);
    }
    else if (kind == Kind::envelope)
    {
        const bool second = filterIndex == 1;
        const float a = std::sqrt (value (second ? "env2Attack" : "attack")), d = std::sqrt (value (second ? "env2Decay" : "decay")),
                    s = value (second ? "env2Sustain" : "sustain"), release = std::sqrt (value (second ? "env2Release" : "release"));
        const auto tint = second ? violet : blue;
        const float scale = r.getWidth() / (a + d + 0.65f + release);
        const float x1 = r.getX() + a * scale, x2 = x1 + d * scale, x3 = x2 + 0.65f * scale;
        const float sy = r.getBottom() - s * r.getHeight();
        path.startNewSubPath (r.getX(), r.getBottom());
        path.lineTo (x1, r.getY()); path.lineTo (x2, sy); path.lineTo (x3, sy); path.lineTo (r.getRight(), r.getBottom());
        auto fill = path; fill.closeSubPath();
        g.setGradientFill (ColourGradient (tint.withAlpha (0.22f), r.getTopLeft(), tint.withAlpha (0.01f), r.getBottomLeft(), false));
        g.fillPath (fill); curve (g, path, tint);
        for (auto p : { Point<float> (x1, r.getY()), Point<float> (x2, sy), Point<float> (x3, sy) })
        {
            g.setColour (inset); g.fillEllipse (p.x - 3, p.y - 3, 6, 6);
            g.setColour (tint); g.drawEllipse (p.x - 3, p.y - 3, 6, 6, 1);
        }
        label (g, second ? "ENV 2 / MODULATION" : "ENV 1 / AMPLITUDE", { 12, 5, 200, 15 }, 9, tint);
        label (g, "TIME / COMPRESSED", { getWidth() - 140, getHeight() - 20, 128, 15 }, 9, faint, Justification::centredRight);
    }
    else if (kind == Kind::filter)
    {
        // Exact digital response of the voice filter at the current sample rate.
        const bool second = filterIndex == 1;
        const double rate = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
        const double fc = jmin ((double) value (second ? "cutoff2" : "cutoff"), rate * 0.45), q = value (second ? "reso2" : "reso");
        const int mode = jlimit (0, 5, roundToInt (value (second ? "filter2Type" : "filterType")));
        const bool bypassed = second && value ("filter2On") < .5f;
        for (int i = 0; i <= 180; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 180.0);
            const double mag = bw::VoiceFilter::magnitude (mode, hz, fc, q, rate);
            const float db = (float) (20 * std::log10 (jmax (0.00001, mag)));
            const Point<float> p (r.getX() + r.getWidth() * i / 180.0f, r.getY() + jlimit (0.0f, 1.0f, (24 - db) / 72) * r.getHeight());
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        curve (g, path, bypassed ? faint : coral);
        label (g, String (second ? "FILTER 2 / " : "FILTER 1 / ") + bw::VoiceFilter::modeNames()[mode].toUpperCase()
                  + (bypassed ? "  /  BYPASSED" : "  /  DIGITAL RESPONSE"), { 12, 5, getWidth() - 24, 15 }, 9, muted);
        label (g, "20 Hz", { 12, getHeight() - 20, 65, 14 }, 9, faint);
        label (g, "20 kHz", { getWidth() - 77, getHeight() - 20, 65, 14 }, 9, faint, Justification::centredRight);
    }

}
void Graph::mouseDown (const MouseEvent& e)
{
    if (kind == Kind::wavetable && e.mods.isLeftButtonDown())
    {
        if (scanning) positionAttachment->endGesture();
        positionAttachment->beginGesture(); scanning = true; mouseDrag (e); return;
    }

}
void Graph::mouseDrag (const MouseEvent& e)
{
    if (scanning)
    {
        const auto r = plot();
        positionAttachment->setValueAsPartOfGesture (jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()));
        return;
    }

}
void Graph::mouseUp (const MouseEvent&)
{
    if (scanning) { positionAttachment->endGesture(); scanning = false; }
}
void Graph::mouseDoubleClick (const MouseEvent&)
{
    if (kind == Kind::wavetable)
    {
        if (scanning) { positionAttachment->endGesture(); scanning = false; }
        positionAttachment->setValueAsCompleteGesture (0.0f);
    }
}
void Meter::timerCallback()
{
    for (size_t i = 0; i < levels.size(); ++i)
        levels[i] = jmax (processor.outputPeak[i].load (std::memory_order_relaxed), levels[i] * 0.87f);
    repaint();
}
void Meter::paint (Graphics& g)
{
    label (g, "OUTPUT", { 0, 0, getWidth(), 15 }, 9, muted, Justification::centred);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = Decibels::gainToDecibels (levels[(size_t) ch], -60.0f);
        const int lit = roundToInt (jlimit (0.0f, 1.0f, (db + 60) / 60) * 18);
        for (int i = 0; i < 18; ++i)
        {
            g.setColour (i < lit ? (i > 15 ? coral : mint) : border);
            g.fillRect (ch * 9 + getWidth() / 2 - 8, 19 + (17 - i) * 3, 6, 2);
        }
    }
}
}
