#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaDelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaDelayEditor (VegaDelayProcessor&);
    ~VegaDelayEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1000, H = 560;
    class Main;
private:
    void timerCallback() override;
    VegaDelayProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
