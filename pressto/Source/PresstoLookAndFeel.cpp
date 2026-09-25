#include "PresstoLookAndFeel.h"

juce::Font Fonts::make(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

PresstoLookAndFeel::PresstoLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, Palette::background);

    setColour(juce::Slider::textBoxTextColourId, Palette::textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, Palette::accent.withAlpha(0.35f));
    setColour(juce::Slider::rotarySliderFillColourId, Palette::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, Palette::grid);

    setColour(juce::Label::textColourId, Palette::textSecondary);
    setColour(juce::Label::textWhenEditingColourId, Palette::textPrimary);
    setColour(juce::Label::outlineWhenEditingColourId, Palette::accent.withAlpha(0.6f));
    setColour(juce::Label::backgroundWhenEditingColourId, Palette::well);
    setColour(juce::TextEditor::highlightColourId, Palette::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, Palette::textPrimary);
    setColour(juce::CaretComponent::caretColourId, Palette::accent);

    setColour(juce::TextButton::buttonColourId, Palette::panel);
    setColour(juce::TextButton::buttonOnColourId, Palette::accent);
    setColour(juce::TextButton::textColourOffId, Palette::textSecondary);
    setColour(juce::TextButton::textColourOnId, Palette::background);

    setColour(juce::TooltipWindow::backgroundColourId, Palette::panel.brighter(0.08f));
    setColour(juce::TooltipWindow::textColourId, Palette::textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, Palette::panelEdge.brighter(0.2f));
}

void PresstoLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const float outer = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcRadius = outer - 3.0f;
    const float arcWidth = juce::jmax(2.5f, outer * 0.085f);
    const float knobRadius = arcRadius - arcWidth - 4.0f;
    const bool active = slider.isEnabled();
    const bool hot = slider.isMouseOverOrDragging();

    // Track.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(Palette::grid);
    g.strokePath(track, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (from the start, or from the centre for bipolar controls).
    const auto valueAngle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = slider.getProperties().getWithDefault(bipolarProperty, false);
    const float originAngle = bipolar
        ? startAngle + (float) slider.valueToProportionOfLength(0.0) * (endAngle - startAngle)
        : startAngle;

    if (std::abs(valueAngle - originAngle) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          juce::jmin(originAngle, valueAngle), juce::jmax(originAngle, valueAngle), true);
        const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(active ? colour : colour.withSaturation(0.0f).withAlpha(0.4f));
        g.strokePath(arc, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body.
    const auto knob = juce::Rectangle<float>(knobRadius * 2.0f, knobRadius * 2.0f).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2e37), centre.x, knob.getY(),
                                           juce::Colour(0xff15171c), centre.x, knob.getBottom(), false));
    g.fillEllipse(knob);
    g.setColour(hot ? Palette::panelEdge.brighter(0.6f) : Palette::panelEdge.brighter(0.15f));
    g.drawEllipse(knob.reduced(0.5f), 1.0f);

    // Pointer.
    const auto direction = juce::Point<float>(std::sin(valueAngle), -std::cos(valueAngle));
    g.setColour(active ? Palette::textPrimary : Palette::textDim);
    g.drawLine(juce::Line<float>(centre + direction * (knobRadius * 0.35f), centre + direction * (knobRadius * 0.85f)),
               juce::jmax(2.0f, knobRadius * 0.09f));
}

juce::Label* PresstoLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(Fonts::make(13.0f));
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, Palette::textPrimary);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font PresstoLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

void PresstoLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                              bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = bounds.getHeight() * 0.5f;
    const auto onColour = button.findColour(juce::TextButton::buttonOnColourId);

    if (button.getToggleState())
    {
        g.setColour(down ? onColour.darker(0.2f) : (highlighted ? onColour.brighter(0.1f) : onColour));
        g.fillRoundedRectangle(bounds, radius);
    }
    else
    {
        g.setColour(highlighted ? Palette::panel.brighter(0.12f) : Palette::panel);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(highlighted ? Palette::panelEdge.brighter(0.5f) : Palette::panelEdge.brighter(0.2f));
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }
}

juce::Font PresstoLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return Fonts::make(juce::jmin(13.0f, (float) buttonHeight * 0.5f), true).withExtraKerningFactor(0.08f);
}

void PresstoLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool on = button.getToggleState();
    const auto bounds = button.getLocalBounds().toFloat();
    const float dot = bounds.getHeight() * 0.22f;
    const auto onColour = button.findColour(juce::TextButton::buttonOnColourId);

    // Status LED on the left, text centred in the remaining space.
    auto ledArea = juce::Rectangle<float>(dot, dot).withCentre({ bounds.getX() + bounds.getHeight() * 0.55f, bounds.getCentreY() });
    g.setColour(on ? Palette::background.withAlpha(0.75f) : Palette::textDim);
    g.fillEllipse(ledArea);

    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(on ? button.findColour(juce::TextButton::textColourOnId)
                   : (button.isMouseOver() ? onColour : button.findColour(juce::TextButton::textColourOffId)));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(bounds.getHeight() * 0.6f), juce::Justification::centred, false);
}

juce::TextLayout PresstoLookAndFeel::layoutTooltip(const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::topLeft);
    s.append(text, Fonts::make(13.0f), Palette::textPrimary);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths(s, 280.0f);
    return layout;
}

juce::Rectangle<int> PresstoLookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                                          juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip(tipText);
    const int w = (int) std::ceil(layout.getWidth()) + 20;
    const int h = (int) std::ceil(layout.getHeight()) + 14;

    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 12,
                                w, h)
        .constrainedWithin(parentArea);
}

void PresstoLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float>((float) width, (float) height);
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    layoutTooltip(text).draw(g, bounds.reduced(10.0f, 7.0f));
}
