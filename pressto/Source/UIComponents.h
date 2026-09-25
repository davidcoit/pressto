#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <vector>

// Rotary control bound to a parameter, with a caption above and an editable
// value box below. Double-click returns to the parameter's default.
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
         const juce::String& caption, const juce::String& tooltip, bool bipolar = false);

    void paint(juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};

// Two-channel peak meter (dBFS) with peak-hold ticks and a clip indicator.
// Click to reset the held peak / clip.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter(juce::String caption, bool scaleOnLeft);

    void update(const std::array<float, 2>& linearPeaks, float dtSeconds);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 6.0f;

private:
    float dbToY(float db, juce::Rectangle<float> area) const;

    juce::String caption;
    bool scaleOnLeft;
    std::array<float, 2> levelDb { minDb, minDb };
    std::array<float, 2> holdDb { minDb, minDb };
    std::array<float, 2> holdAge {};
    float maxPeakDb = minDb;
    float clipAge = 1.0e9f;
};

// Vertical gain-reduction bar that grows downwards from 0 dB.
class GainReductionMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainReductionMeter();

    void update(float reductionDb, float dtSeconds);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr float rangeDb = 24.0f;

private:
    float displayDb = 0.0f;
    float holdDb = 0.0f;
    float holdAge = 0.0f;
};

// Static input/output transfer curve with a live operating-point marker.
class TransferDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TransferDisplay(std::function<float(float)> staticReductionDb);

    void update(float inputDb, float reductionDb, float dtSeconds);
    void paint(juce::Graphics&) override;

    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 0.0f;

private:
    std::function<float(float)> curve;
    float dotInDb = minDb;
    float dotGrDb = 0.0f;
};

// Scrolling history of input level (background) and gain reduction (foreground).
class GainReductionHistory : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainReductionHistory();

    void push(float inputDb, float reductionDb);
    void paint(juce::Graphics&) override;

    static constexpr float rangeDb = 18.0f;

private:
    static constexpr int length = 360; // ~6 s at 60 Hz
    std::vector<float> input, reduction;
    int writeIndex = 0;
};
