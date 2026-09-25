#include "PluginEditor.h"

namespace
{
constexpr float headerHeight = 58.0f;
constexpr int margin = 18;
}

PresstoCanvas::PresstoCanvas(PresstoAudioProcessor& p)
    : processor(p),
      threshold(p.getApvts(), ParamIDs::threshold, "Threshold",
                "Level above which gain reduction starts (the SCL-1's \"dynamic threshold\")."),
      ratio(p.getApvts(), ParamIDs::ratio, "Ratio",
            "Amount of reduction above the threshold, from 1:1 (none) to 20:1 (limiting)."),
      knee(p.getApvts(), ParamIDs::knee, "Knee",
           "Width of the soft knee in dB (the SCL-1's \"static threshold\"). Wider is gentler and less audible."),
      attack(p.getApvts(), ParamIDs::attack, "Attack",
             "How quickly gain reduction responds to rising level. The SCL-1 range is 0.1 to 5 ms."),
      release(p.getApvts(), ParamIDs::release, "Release",
              "How quickly gain reduction recovers after a peak."),
      program(p.getApvts(), ParamIDs::program, "Program",
              "Program-dependent release. Higher values make the gain reduction settle at the average level of "
              "the program instead of releasing fully after every peak: transients recover fast and sustained "
              "passages stay steady. 0 % gives a plain peak compressor."),
      scHpf(p.getApvts(), ParamIDs::scHpf, "SC HPF",
            "High-pass filter on the detector only. Stops bass from driving the compressor (and pumping). "
            "Turn fully left for Off."),
      input(p.getApvts(), ParamIDs::input, "Input", "Gain before the compressor. Drives the signal harder into the threshold.", true),
      makeup(p.getApvts(), ParamIDs::makeup, "Makeup", "Output gain applied after the compressor and the dry/wet mix.", true),
      mix(p.getApvts(), ParamIDs::mix, "Mix", "Blend of compressed and uncompressed signal (parallel compression)."),
      transfer([&p](float inDb) { return p.getStaticReductionDb(inDb); })
{
    for (auto* k : { &threshold, &ratio, &knee, &attack, &release, &program, &scHpf, &input, &makeup, &mix })
        addAndMakeVisible(*k);

    linkButton.setClickingTogglesState(true);
    linkButton.setColour(juce::TextButton::buttonOnColourId, Palette::link);
    linkButton.setTooltip("Stereo: both channels share one detector so the image never shifts. "
                          "Off: dual-mono, each channel is compressed independently.");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, Palette::bypass);
    bypassButton.setTooltip("Bypass (click-free, also available as the host's bypass).");
    addAndMakeVisible(linkButton);
    addAndMakeVisible(bypassButton);

    auto& apvts = p.getApvts();
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ParamIDs::link, linkButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ParamIDs::bypass, bypassButton);

    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);
    addAndMakeVisible(grMeter);
    addAndMakeVisible(transfer);
    addAndMakeVisible(history);

    setSize(baseWidth, baseHeight);
}

void PresstoCanvas::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);

    // Header.
    g.setColour(Palette::panel);
    g.fillRect(header);
    g.setColour(Palette::panelEdge);
    g.fillRect(header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);

    auto titleArea = header.reduced((float) margin, 0.0f);
    g.setColour(Palette::textPrimary);
    g.setFont(Fonts::make(22.0f, true).withExtraKerningFactor(0.18f));
    const auto titleWidth = 132.0f;
    g.drawText("PRESSTO", titleArea.removeFromLeft(titleWidth), juce::Justification::centredLeft, false);

    g.setColour(Palette::accent);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ titleArea.getX() + 2.0f, header.getCentreY() + 5.0f }));
    g.setColour(Palette::textSecondary);
    g.setFont(Fonts::make(11.0f, true).withExtraKerningFactor(0.14f));
    g.drawText("PROGRAM-DEPENDENT COMPRESSOR", titleArea.withTrimmedLeft(14.0f).withTrimmedTop(3.0f),
               juce::Justification::centredLeft, false);

    // Knob panel.
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(knobPanel, 8.0f);
    g.setColour(Palette::panelEdge);
    g.drawRoundedRectangle(knobPanel.reduced(0.5f), 8.0f, 1.0f);

    const float midY = knobPanel.getCentreY();
    g.setColour(Palette::panelEdge.withAlpha(0.7f));
    g.fillRect(knobPanel.getX() + 16.0f, midY, knobPanel.getWidth() - 32.0f, 1.0f);
}

void PresstoCanvas::resized()
{
    auto bounds = getLocalBounds();
    header = bounds.removeFromTop((int) headerHeight).toFloat();

    auto headerControls = header.toNearestInt().reduced(margin, 15);
    bypassButton.setBounds(headerControls.removeFromRight(104));
    headerControls.removeFromRight(10);
    linkButton.setBounds(headerControls.removeFromRight(132));

    auto body = bounds.reduced(margin);
    body.removeFromTop(2);

    inputMeter.setBounds(body.removeFromLeft(62));
    body.removeFromLeft(14);
    outputMeter.setBounds(body.removeFromRight(62));
    body.removeFromRight(10);
    grMeter.setBounds(body.removeFromRight(36));
    body.removeFromRight(14);

    auto displays = body.removeFromTop(196);
    transfer.setBounds(displays.removeFromLeft(displays.getHeight()));
    displays.removeFromLeft(12);
    history.setBounds(displays);

    body.removeFromTop(14);
    knobPanel = body.toFloat();

    auto rows = body.reduced(8, 10);
    const int rowHeight = rows.getHeight() / 2;
    const auto layoutRow = [](juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int width = row.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
            k->setBounds(row.removeFromLeft(width).reduced(6, 2));
    };

    layoutRow(rows.removeFromTop(rowHeight).withTrimmedBottom(6), { &threshold, &ratio, &knee, &attack, &release });
    layoutRow(rows.withTrimmedTop(6), { &program, &scHpf, &input, &makeup, &mix });
}

void PresstoCanvas::refreshMeters(float dt)
{
    const auto readings = processor.takeMeterReadings();

    inputMeter.update(readings.inputPeak, dt);
    outputMeter.update(readings.outputPeak, dt);
    grMeter.update(readings.gainReductionDb, dt);

    const float inputPeak = juce::jmax(readings.inputPeak[0], readings.inputPeak[1]);
    const float inputDb = inputPeak > 0.0f ? 20.0f * std::log10(inputPeak) : -100.0f;
    transfer.update(inputDb, readings.gainReductionDb, dt);
    history.push(inputDb, readings.gainReductionDb);

    inputMeter.repaint();
    outputMeter.repaint();
    grMeter.repaint();
    transfer.repaint();
    history.repaint();
}

//==============================================================================
PresstoAudioProcessorEditor::PresstoAudioProcessorEditor(PresstoAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), canvas(p)
{
    // Attach the canvas first: setLookAndFeel() re-styles existing children
    // (e.g. rebuilds slider text boxes), but components added later are not.
    addAndMakeVisible(canvas);
    setLookAndFeel(&lookAndFeel);

    constexpr double aspect = (double) PresstoCanvas::baseWidth / PresstoCanvas::baseHeight;
    setResizable(true, true);
    setResizeLimits(PresstoCanvas::baseWidth * 3 / 4, PresstoCanvas::baseHeight * 3 / 4,
                    PresstoCanvas::baseWidth * 2, PresstoCanvas::baseHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio(aspect);

    setSize(PresstoCanvas::baseWidth, PresstoCanvas::baseHeight);

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

PresstoAudioProcessorEditor::~PresstoAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void PresstoAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
}

void PresstoAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) PresstoCanvas::baseWidth;
    canvas.setTransform(juce::AffineTransform::scale(scale));
    canvas.setBounds(0, 0, PresstoCanvas::baseWidth, PresstoCanvas::baseHeight);
}

void PresstoAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit(0.001, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    canvas.refreshMeters(dt);
}
