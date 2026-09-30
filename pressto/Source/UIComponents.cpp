#include "UIComponents.h"
#include "PresstoLookAndFeel.h"

#include <cmath>

namespace
{
float gainToDb(float gain, float floorDb)
{
    return gain > 0.0f ? juce::jmax(floorDb, 20.0f * std::log10(gain)) : floorDb;
}

void drawWell(juce::Graphics& g, juce::Rectangle<float> area, float radius = 6.0f)
{
    g.setColour(Palette::well);
    g.fillRoundedRectangle(area, radius);
    g.setColour(Palette::panelEdge);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
}

void drawCaption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                 juce::Justification just = juce::Justification::centred, juce::Colour colour = Palette::textSecondary)
{
    g.setColour(colour);
    g.setFont(Fonts::make(11.0f, true).withExtraKerningFactor(0.12f));
    g.drawText(text, area, just, false);
}

juce::String formatDbShort(float db)
{
    return db <= -59.9f ? juce::String("-inf") : juce::String(db, 1);
}
} // namespace

//==============================================================================
Knob::Knob(juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
           const juce::String& captionText, const juce::String& tooltip, bool bipolar)
    : caption(captionText.toUpperCase())
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setMouseDragSensitivity(220);
    slider.setTooltip(tooltip);
    slider.setTitle(captionText);
    slider.getProperties().set(PresstoLookAndFeel::bipolarProperty, bipolar);
    addAndMakeVisible(slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterID, slider);

    if (auto* param = state.getParameter(parameterID))
        slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
}

void Knob::paint(juce::Graphics& g)
{
    drawCaption(g, caption, getLocalBounds().removeFromTop(16).toFloat());
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(18);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, area.getWidth(), 18);
    slider.setBounds(area);
}

//==============================================================================
LevelMeter::LevelMeter(juce::String captionText, bool scaleLeft)
    : caption(std::move(captionText)), scaleOnLeft(scaleLeft)
{
    setTooltip("Peak level (dBFS). The number shows the highest peak; click to reset it.");
}

void LevelMeter::update(const std::array<float, 2>& linearPeaks, float dt)
{
    constexpr float fallDbPerSecond = 24.0f;
    constexpr float holdSeconds = 1.5f;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float db = gainToDb(linearPeaks[ch], minDb);
        levelDb[ch] = db >= levelDb[ch] ? db : juce::jmax(db, levelDb[ch] - fallDbPerSecond * dt);

        holdAge[ch] += dt;
        if (db >= holdDb[ch])
        {
            holdDb[ch] = db;
            holdAge[ch] = 0.0f;
        }
        else if (holdAge[ch] > holdSeconds)
        {
            holdDb[ch] = levelDb[ch]; // after the hold, the tick falls with the bar
        }

        maxPeakDb = juce::jmax(maxPeakDb, db);
        if (linearPeaks[ch] >= 1.0f)
            clipAge = 0.0f;
    }
    clipAge += dt;
}

void LevelMeter::mouseDown(const juce::MouseEvent&)
{
    maxPeakDb = minDb;
    clipAge = 1.0e9f;
    holdDb = { minDb, minDb };
    repaint();
}

float LevelMeter::dbToY(float db, juce::Rectangle<float> area) const
{
    return juce::jmap(juce::jlimit(minDb, maxDb, db), minDb, maxDb, area.getBottom(), area.getY());
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawCaption(g, caption, area.removeFromTop(16.0f));

    auto readout = area.removeFromBottom(22.0f);
    const bool clipped = clipAge < 2.0f || maxPeakDb > 0.0f;
    g.setColour(clipped ? Palette::bypass : Palette::textPrimary);
    g.setFont(Fonts::make(12.0f));
    g.drawText(formatDbShort(maxPeakDb), readout, juce::Justification::centred, false);

    area.removeFromBottom(4.0f);
    auto scale = scaleOnLeft ? area.removeFromLeft(22.0f) : area.removeFromRight(22.0f);
    auto well = area;
    drawWell(g, well, 4.0f);

    auto bars = well.reduced(4.0f, 4.0f);
    const float gap = 3.0f;
    const float barWidth = (bars.getWidth() - gap) * 0.5f;

    const auto gradient = [&]
    {
        juce::ColourGradient grad(Palette::levelLow, 0.0f, bars.getBottom(), Palette::levelHigh, 0.0f, bars.getY(), false);
        grad.addColour(juce::jmap(-12.0f, minDb, maxDb, 0.0f, 1.0f), Palette::levelLow);
        grad.addColour(juce::jmap(-3.0f, minDb, maxDb, 0.0f, 1.0f), Palette::levelMid);
        grad.addColour(juce::jmap(0.0f, minDb, maxDb, 0.0f, 1.0f), Palette::levelHigh);
        return grad;
    }();

    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float>(bars.getX() + (float) ch * (barWidth + gap), bars.getY(), barWidth, bars.getHeight());
        g.setColour(Palette::grid.withAlpha(0.6f));
        g.fillRoundedRectangle(bar, 2.0f);

        const float top = dbToY(levelDb[ch], bar);
        if (levelDb[ch] > minDb)
        {
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(bar.withTop(top), 2.0f);
        }

        if (holdDb[ch] > minDb)
        {
            const float y = dbToY(holdDb[ch], bar);
            g.setColour(holdDb[ch] > 0.0f ? Palette::levelHigh : Palette::textPrimary.withAlpha(0.8f));
            g.fillRect(bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
        }
    }

    // 0 dBFS line across the bars.
    g.setColour(Palette::textSecondary.withAlpha(0.35f));
    g.fillRect(bars.getX(), dbToY(0.0f, bars), bars.getWidth(), 1.0f);

    g.setFont(Fonts::make(10.0f));
    for (int db : { 6, 0, -6, -12, -24, -36, -48, -60 })
    {
        const float y = dbToY((float) db, bars);
        g.setColour(db == 0 ? Palette::textSecondary : Palette::textDim);
        g.drawText(db > 0 ? "+" + juce::String(db) : juce::String(db), scale.withY(y - 6.0f).withHeight(12.0f),
                   juce::Justification::centred, false);
    }
}

//==============================================================================
GainReductionMeter::GainReductionMeter()
{
    setTooltip("Gain reduction (dB). The number shows the deepest reduction recently; click to reset it.");
}

void GainReductionMeter::setChannelLabels(const juce::String& first, const juce::String& second)
{
    if (labels[0] != first || labels[1] != second)
    {
        labels = { first, second };
        repaint();
    }
}

void GainReductionMeter::update(const std::array<float, 2>& reductionDb, float dt)
{
    constexpr float returnDbPerSecond = 30.0f;
    constexpr float holdSeconds = 2.0f;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float db = split() ? reductionDb[ch] : juce::jmax(reductionDb[0], reductionDb[1]);
        displayDb[ch] = db >= displayDb[ch] ? db : juce::jmax(db, displayDb[ch] - returnDbPerSecond * dt);

        holdAge[ch] += dt;
        if (db >= holdDb[ch])
        {
            holdDb[ch] = db;
            holdAge[ch] = 0.0f;
        }
        else if (holdAge[ch] > holdSeconds)
        {
            holdDb[ch] = displayDb[ch];
        }
    }
}

void GainReductionMeter::mouseDown(const juce::MouseEvent&)
{
    holdDb = displayDb;
    holdAge = {};
    repaint();
}

void GainReductionMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawCaption(g, "GR", area.removeFromTop(16.0f));

    const float maxHold = juce::jmax(holdDb[0], holdDb[1]);
    auto readout = area.removeFromBottom(22.0f);
    g.setColour(maxHold > 0.05f ? Palette::accent : Palette::textPrimary);
    g.setFont(Fonts::make(12.0f));
    g.drawText(maxHold > 0.05f ? "-" + juce::String(maxHold, 1) : juce::String("0.0"), readout, juce::Justification::centred, false);

    area.removeFromBottom(4.0f);
    drawWell(g, area, 4.0f);
    auto inner = area.reduced(4.0f);

    const int numBars = split() ? 2 : 1;
    const float gap = 3.0f;
    const float barWidth = (inner.getWidth() - gap * (float) (numBars - 1)) / (float) numBars;
    const auto toY = [&](float db) { return inner.getY() + juce::jlimit(0.0f, 1.0f, db / rangeDb) * inner.getHeight(); };

    for (int b = 0; b < numBars; ++b)
    {
        const auto ch = (size_t) b;
        const auto bar = juce::Rectangle<float>(inner.getX() + (float) b * (barWidth + gap), inner.getY(), barWidth, inner.getHeight());

        g.setColour(Palette::grid.withAlpha(0.6f));
        g.fillRoundedRectangle(bar, 2.0f);

        if (displayDb[ch] > 0.01f)
        {
            g.setGradientFill(juce::ColourGradient(Palette::accent.withAlpha(0.85f), 0.0f, bar.getY(),
                                                   Palette::accent.darker(0.5f), 0.0f, bar.getBottom(), false));
            g.fillRoundedRectangle(bar.withBottom(toY(displayDb[ch])), 2.0f);
        }

        if (holdDb[ch] > 0.05f)
        {
            g.setColour(Palette::textPrimary.withAlpha(0.8f));
            g.fillRect(bar.getX(), toY(holdDb[ch]) - 1.0f, bar.getWidth(), 2.0f);
        }

        if (split())
        {
            g.setColour(Palette::textSecondary);
            g.setFont(Fonts::make(10.0f, true));
            g.drawText(labels[ch], bar.withTop(bar.getBottom() - 14.0f), juce::Justification::centred, false);
        }
    }

    g.setColour(Palette::textDim.withAlpha(0.5f));
    for (float db : { 3.0f, 6.0f, 12.0f, 18.0f })
        g.fillRect(inner.getX(), toY(db), inner.getWidth(), 1.0f);
}

//==============================================================================
TransferDisplay::TransferDisplay(std::function<float(float)> staticReductionDb)
    : curve(std::move(staticReductionDb))
{
    setTooltip("Transfer curve: input level (horizontal) against output level before makeup (vertical). "
               "The dot shows the live operating point, including the effect of attack and release.");
}

void TransferDisplay::update(float inputDb, float reductionDb, float dt)
{
    // Smooth the dot so it glides rather than jitters.
    const float k = 1.0f - std::exp(-dt / 0.05f);
    dotInDb += k * (juce::jlimit(minDb, maxDb + 6.0f, inputDb) - dotInDb);
    dotGrDb += k * (reductionDb - dotGrDb);
}

void TransferDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawWell(g, bounds);

    const auto plot = bounds.reduced(10.0f).withTrimmedTop(12.0f).withTrimmedLeft(10.0f);
    const auto toX = [&](float db) { return juce::jmap(db, minDb, maxDb, plot.getX(), plot.getRight()); };
    const auto toY = [&](float db) { return juce::jmap(db, minDb, maxDb, plot.getBottom(), plot.getY()); };

    g.setFont(Fonts::make(9.5f));
    for (float db = -48.0f; db <= -12.0f; db += 12.0f)
    {
        g.setColour(Palette::grid);
        g.fillRect(toX(db), plot.getY(), 1.0f, plot.getHeight());
        g.fillRect(plot.getX(), toY(db), plot.getWidth(), 1.0f);
        g.setColour(Palette::textDim);
        g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(toX(db) - 14.0f, plot.getBottom() - 12.0f, 28.0f, 12.0f),
                   juce::Justification::centred, false);
    }

    g.setColour(Palette::textDim.withAlpha(0.6f));
    juce::Path unity;
    unity.startNewSubPath(toX(minDb), toY(minDb));
    unity.lineTo(toX(maxDb), toY(maxDb));
    const float dashes[] { 3.0f, 4.0f };
    juce::PathStrokeType(1.0f).createDashedStroke(unity, unity, dashes, 2);
    g.fillPath(unity);

    juce::Path line;
    const int steps = juce::jmax(2, (int) plot.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float in = juce::jmap((float) i, 0.0f, (float) steps, minDb, maxDb);
        const float out = in - (curve ? curve(in) : 0.0f);
        const juce::Point<float> p(toX(in), toY(out));
        if (i == 0)
            line.startNewSubPath(p);
        else
            line.lineTo(p);
    }

    juce::Path fill(line);
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(Palette::accent.withAlpha(0.14f), 0.0f, plot.getY(),
                                           Palette::accent.withAlpha(0.0f), 0.0f, plot.getBottom(), false));
    g.fillPath(fill);

    g.saveState();
    g.reduceClipRegion(plot.expanded(2.0f).toNearestInt());
    g.setColour(Palette::accent);
    g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (dotInDb > minDb + 0.5f)
    {
        const juce::Point<float> dot(toX(dotInDb), toY(dotInDb - dotGrDb));
        g.setColour(Palette::textPrimary.withAlpha(0.18f));
        g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(dot));
        g.setColour(Palette::textPrimary);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(dot));
    }
    g.restoreState();

    drawCaption(g, "TRANSFER", bounds.reduced(10.0f, 6.0f).removeFromTop(12.0f), juce::Justification::centredLeft);
}

//==============================================================================
GainReductionHistory::GainReductionHistory()
    : input((size_t) length, -100.0f), reduction((size_t) length, 0.0f)
{
    setTooltip("Last few seconds of input level (grey) and gain reduction (amber). "
               "Look for smooth, steady reduction on sustained material and short dips on transients.");
}

void GainReductionHistory::push(float inputDb, float reductionDb)
{
    input[(size_t) writeIndex] = inputDb;
    reduction[(size_t) writeIndex] = reductionDb;
    writeIndex = (writeIndex + 1) % length;
}

void GainReductionHistory::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawWell(g, bounds);

    auto header = bounds.reduced(10.0f, 6.0f).removeFromTop(12.0f);
    const auto plot = bounds.reduced(10.0f).withTrimmedTop(12.0f).withTrimmedRight(18.0f);

    const auto xFor = [&](int i) { return plot.getX() + plot.getWidth() * (float) i / (float) (length - 1); };
    const auto grY = [&](float db) { return plot.getY() + juce::jlimit(0.0f, 1.0f, db / rangeDb) * plot.getHeight(); };
    const auto inY = [&](float db) { return juce::jmap(juce::jlimit(-60.0f, 0.0f, db), -60.0f, 0.0f, plot.getBottom(), plot.getY()); };

    g.setFont(Fonts::make(9.5f));
    for (float db = 3.0f; db < rangeDb; db += 3.0f)
    {
        g.setColour(Palette::grid);
        g.fillRect(plot.getX(), grY(db), plot.getWidth(), 1.0f);
        g.setColour(Palette::textDim);
        g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(plot.getRight() + 2.0f, grY(db) - 6.0f, 16.0f, 12.0f),
                   juce::Justification::centredRight, false);
    }

    juce::Path inPath, grPath, grFill;
    inPath.startNewSubPath(plot.getX(), plot.getBottom());
    grFill.startNewSubPath(plot.getX(), plot.getY());

    for (int i = 0; i < length; ++i)
    {
        const auto idx = (size_t) ((writeIndex + i) % length);
        const float x = xFor(i);
        inPath.lineTo(x, inY(input[idx]));
        const float y = grY(reduction[idx]);
        grFill.lineTo(x, y);
        if (i == 0)
            grPath.startNewSubPath(x, y);
        else
            grPath.lineTo(x, y);
    }

    inPath.lineTo(plot.getRight(), plot.getBottom());
    inPath.closeSubPath();
    g.setColour(Palette::textSecondary.withAlpha(0.16f));
    g.fillPath(inPath);

    grFill.lineTo(plot.getRight(), plot.getY());
    grFill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(Palette::accent.withAlpha(0.22f), 0.0f, plot.getY(),
                                           Palette::accent.withAlpha(0.02f), 0.0f, plot.getBottom(), false));
    g.fillPath(grFill);
    g.setColour(Palette::accent);
    g.strokePath(grPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawCaption(g, "GAIN REDUCTION", header, juce::Justification::centredLeft);
    const float current = reduction[(size_t) ((writeIndex + length - 1) % length)];
    drawCaption(g, (current > 0.05f ? "-" + juce::String(current, 1) : juce::String("0.0")) + " dB", header,
                juce::Justification::centredRight, current > 0.05f ? Palette::accent : Palette::textSecondary);
}
