#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"

class PresstoAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PresstoAudioProcessorEditor(PresstoAudioProcessor&);
    ~PresstoAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void configureSlider(juce::Slider& slider);

    PresstoAudioProcessor& audioProcessor;

    juce::Slider inputSlider;
    juce::Slider thresholdSlider;
    juce::Slider ratioSlider;
    juce::Slider kneeSlider;
    juce::Slider attackSlider;
    juce::Slider releaseSlider;
    juce::Slider makeupSlider;

    juce::Label inputLabel;
    juce::Label thresholdLabel;
    juce::Label ratioLabel;
    juce::Label kneeLabel;
    juce::Label attackLabel;
    juce::Label releaseLabel;
    juce::Label makeupLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<SliderAttachment> ratioAttachment;
    std::unique_ptr<SliderAttachment> kneeAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> makeupAttachment;

    MeterComponent inputMeter;
    MeterComponent outputMeter;
    MeterComponent grMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresstoAudioProcessorEditor)
};
