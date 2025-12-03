#include "PluginEditor.h"

PresstoAudioProcessorEditor::PresstoAudioProcessorEditor(PresstoAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      audioProcessor(p),
      inputMeter([this] { return audioProcessor.getInputMeterDb(); }, "Input", -60.0f, 6.0f),
      outputMeter([this] { return audioProcessor.getOutputMeterDb(); }, "Output", -60.0f, 6.0f),
      grMeter([this] { return audioProcessor.getGainReductionDb(); }, "Gain Reduction", 0.0f, 30.0f)
{
    setSize(820, 500);

    const auto configureAll = [this](juce::Slider& slider, juce::Label& label, const juce::String& name)
    {
        configureSlider(slider);
        addAndMakeVisible(slider);

        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.attachToComponent(&slider, false);
    };

    configureAll(inputSlider, inputLabel, "Input");
    configureAll(thresholdSlider, thresholdLabel, "Threshold");
    configureAll(ratioSlider, ratioLabel, "Ratio");
    configureAll(kneeSlider, kneeLabel, "Knee");
    configureAll(attackSlider, attackLabel, "Attack");
    configureAll(releaseSlider, releaseLabel, "Release");
    configureAll(makeupSlider, makeupLabel, "Makeup");

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(grMeter);

    auto& apvts = audioProcessor.getApvts();
    inputAttachment = std::make_unique<SliderAttachment>(apvts, "input", inputSlider);
    thresholdAttachment = std::make_unique<SliderAttachment>(apvts, "threshold", thresholdSlider);
    ratioAttachment = std::make_unique<SliderAttachment>(apvts, "ratio", ratioSlider);
    kneeAttachment = std::make_unique<SliderAttachment>(apvts, "knee", kneeSlider);
    attackAttachment = std::make_unique<SliderAttachment>(apvts, "attack", attackSlider);
    releaseAttachment = std::make_unique<SliderAttachment>(apvts, "release", releaseSlider);
    makeupAttachment = std::make_unique<SliderAttachment>(apvts, "makeup", makeupSlider);
}

void PresstoAudioProcessorEditor::configureSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setColour(juce::Slider::thumbColourId, juce::Colours::orange);
    slider.setColour(juce::Slider::trackColourId, juce::Colours::goldenrod);
    slider.setColour(juce::Slider::backgroundColourId, juce::Colours::black);
}

void PresstoAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient gradient(juce::Colour(0xff101018), bounds.getBottomLeft(),
                                  juce::Colour(0xff060608), bounds.getTopRight(), false);
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.drawText("Pressto Compressor", 16, 8, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void PresstoAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(16);
    bounds.removeFromTop(36); // header padding

    auto meterColumn = bounds.removeFromRight(150);
    auto sliderArea = bounds;

    auto knobArea = sliderArea.removeFromTop(300);
    auto topRow = knobArea.removeFromTop(150);
    auto bottomRow = knobArea;

    const int topSlotWidth = topRow.getWidth() / 4;
    const int bottomSlotWidth = bottomRow.getWidth() / 3;

    auto placeControl = [](juce::Component& comp, juce::Rectangle<int>& area, int slotWidth)
    {
        auto slot = area.removeFromLeft(slotWidth);
        comp.setBounds(slot.reduced(12));
    };

    auto topRowCopy = topRow;
    placeControl(inputSlider, topRowCopy, topSlotWidth);
    placeControl(thresholdSlider, topRowCopy, topSlotWidth);
    placeControl(ratioSlider, topRowCopy, topSlotWidth);
    placeControl(kneeSlider, topRowCopy, topSlotWidth);

    auto bottomRowCopy = bottomRow;
    placeControl(attackSlider, bottomRowCopy, bottomSlotWidth);
    placeControl(releaseSlider, bottomRowCopy, bottomSlotWidth);
    placeControl(makeupSlider, bottomRowCopy, bottomSlotWidth);

    auto meterArea = meterColumn.reduced(6);
    const int meterHeight = meterArea.getHeight() / 3;
    auto columnCopy = meterArea;

    inputMeter.setBounds(columnCopy.removeFromTop(meterHeight).reduced(4));
    outputMeter.setBounds(columnCopy.removeFromTop(meterHeight).reduced(4));
    grMeter.setBounds(columnCopy.reduced(4));
}
