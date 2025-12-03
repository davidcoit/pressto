#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class MeterComponent : public juce::Component, private juce::Timer
{
public:
    MeterComponent(std::function<float()> valueProvider,
                   juce::String labelText,
                   float minValueDb,
                   float maxValueDb,
                   bool invert = false);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    std::function<float()> provider;
    juce::String label;
    float minValue { -60.0f };
    float maxValue { 6.0f };
    bool inverted { false };

    float currentValue { 0.0f };
};
