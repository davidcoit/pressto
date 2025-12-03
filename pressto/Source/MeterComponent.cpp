#include "MeterComponent.h"

#include <utility>

MeterComponent::MeterComponent(std::function<float()> valueProvider,
                               juce::String labelText,
                               float minValueDb,
                               float maxValueDb,
                               bool invert)
    : provider(std::move(valueProvider)),
      label(std::move(labelText)),
      minValue(minValueDb),
      maxValue(maxValueDb),
      inverted(invert)
{
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void MeterComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colours::black.withAlpha(0.85f));

    auto titleArea = bounds.removeFromTop(20.0f);
    auto valueArea = bounds.removeFromBottom(24.0f);
    auto meterArea = bounds.reduced(4.0f);

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawFittedText(label, titleArea.toNearestInt(), juce::Justification::centred, 1);

    g.setFont(juce::Font(13.0f));
    juce::String valueText;
    valueText << juce::String(currentValue, 1) << " dB";
    g.drawFittedText(valueText, valueArea.toNearestInt(), juce::Justification::centred, 1);

    g.setColour(juce::Colours::darkgrey);
    g.fillRoundedRectangle(meterArea, 3.0f);
    g.setColour(juce::Colours::grey);
    g.drawRoundedRectangle(meterArea, 3.0f, 1.0f);

    const float range = juce::jmax(0.001f, maxValue - minValue);
    float normalised = (currentValue - minValue) / range;
    normalised = juce::jlimit(0.0f, 1.0f, normalised);
    if (inverted)
        normalised = 1.0f - normalised;

    auto fillArea = meterArea;
    const auto heightToRemove = (1.0f - normalised) * fillArea.getHeight();
    fillArea.removeFromTop(heightToRemove);

    juce::ColourGradient gradient(juce::Colours::chartreuse, fillArea.getX(), fillArea.getBottom(),
                                  juce::Colours::red, fillArea.getX(), fillArea.getY(), false);
    gradient.addColour(0.5, juce::Colours::yellow);

    g.setGradientFill(gradient);
    g.fillRoundedRectangle(fillArea, 3.0f);
}

void MeterComponent::resized()
{
}

void MeterComponent::timerCallback()
{
    if (provider)
        currentValue = provider();

    repaint();
}
