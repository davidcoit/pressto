#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PresstoLookAndFeel.h"
#include "UIComponents.h"

// All controls live on a fixed-size canvas that is scaled as a whole when the
// editor is resized, so the layout stays pixel-consistent at any size.
class PresstoCanvas : public juce::Component
{
public:
    static constexpr int baseWidth = 940;
    static constexpr int baseHeight = 560;

    explicit PresstoCanvas(PresstoAudioProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

    void refreshMeters(float dtSeconds);

private:
    PresstoAudioProcessor& processor;

    Knob threshold, ratio, knee, attack, release;
    Knob program, scHpf, input, makeup, mix;

    juce::TextButton linkButton { "STEREO LINK" }, bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment, bypassAttachment;

    LevelMeter inputMeter { "IN", true }, outputMeter { "OUT", false };
    GainReductionMeter grMeter;
    TransferDisplay transfer;
    GainReductionHistory history;

    juce::Rectangle<float> knobPanel, header;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresstoCanvas)
};

class PresstoAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PresstoAudioProcessorEditor(PresstoAudioProcessor&);
    ~PresstoAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PresstoLookAndFeel lookAndFeel;
    PresstoCanvas canvas;
    juce::TooltipWindow tooltips { this, 600 };
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresstoAudioProcessorEditor)
};
