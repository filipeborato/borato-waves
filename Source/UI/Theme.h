#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace bw::ui
{
inline const juce::Colour background { 0xff101416 }, panel { 0xff191f22 }, inset { 0xff111719 },
    border { 0xff303b3e }, text { 0xffe5eeeb }, muted { 0xff91a4a5 }, faint { 0xff586b6e },
    mint { 0xff8de5c1 }, blue { 0xff8bbde8 }, coral { 0xffeda88b }, violet { 0xffb5a1dd };

inline void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                   float size = 12.0f, juce::Colour colour = text,
                   juce::Justification align = juce::Justification::centredLeft, bool bold = false)
{
    g.setColour (colour);
    g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    g.drawText (s, r, align, true);
}
inline void surface (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour colour = panel)
{
    g.setColour (colour);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
}
inline void badge (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c = mint)
{
    g.setColour (c.withAlpha (0.09f));
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    label (g, s, r, 10.0f, c, juce::Justification::centred, true);
}
inline void grid (juce::Graphics& g, juce::Rectangle<float> r, int columns = 8, int rows = 4)
{
    g.setColour (border.withAlpha (0.5f));
    for (int x = 0; x <= columns; ++x)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * x / columns), r.getY(), r.getBottom());
    for (int y = 0; y <= rows; ++y)
        g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * y / rows), r.getX(), r.getRight());
}
inline void curve (juce::Graphics& g, const juce::Path& path, juce::Colour c, float width = 1.8f)
{
    g.setColour (c.withAlpha (0.07f));
    g.strokePath (path, juce::PathStrokeType (width + 6.0f));
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}
