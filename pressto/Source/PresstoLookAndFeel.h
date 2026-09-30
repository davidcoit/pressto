#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
inline const juce::Colour background { 0xff0f1115 };
inline const juce::Colour panel { 0xff171a20 };
inline const juce::Colour panelEdge { 0xff252932 };
inline const juce::Colour well { 0xff0b0d10 };
inline const juce::Colour grid { 0xff222630 };
inline const juce::Colour textPrimary { 0xffe9e6df };
inline const juce::Colour textSecondary { 0xff8b909b };
inline const juce::Colour textDim { 0xff5a5f6a };
inline const juce::Colour accent { 0xfff0a53a };      // amber: controls + gain reduction
inline const juce::Colour link { 0xff58c4dc };        // teal: stereo link / levels
inline const juce::Colour midSide { 0xffb08cf0 };     // violet: mid/side
inline const juce::Colour bypass { 0xffe5534b };      // red: bypass / clip
inline const juce::Colour levelLow { 0xff4fd1a5 };
inline const juce::Colour levelMid { 0xffe8d25a };
inline const juce::Colour levelHigh { 0xffe5534b };
} // namespace Palette

namespace Fonts
{
juce::Font make(float height, bool bold = false);
}

class PresstoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Set on a Slider's properties to draw its value arc from the centre.
    static constexpr const char* bipolarProperty = "bipolar";

    PresstoLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;

private:
    juce::TextLayout layoutTooltip(const juce::String& text) const;
};
