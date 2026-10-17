#pragma once
#include "Controls.h"
#include <memory>
class WaveProcessor;
namespace bw::ui
{
class LfoPanel : public juce::Component
{
public:
    explicit LfoPanel (WaveProcessor&);
    ~LfoPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct Content;
    std::unique_ptr<Content> content;
};
class MatrixPanel : public juce::Component
{
public:
    explicit MatrixPanel (WaveProcessor&);
    ~MatrixPanel() override;
    void resized() override;
    void selectSlot (int zeroBased);
private:
    struct Content;
    std::unique_ptr<Content> content;
};
}

