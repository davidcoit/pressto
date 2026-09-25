#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr float dbToGainFactor = 0.11512925464970229f; // ln(10) / 20

float dbToGain(float db) noexcept { return std::exp(db * dbToGainFactor); }

// ---- Parameter text formatting / parsing -----------------------------------

juce::String formatDb(float v, bool showSign)
{
    return (showSign && v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB";
}

juce::String formatTime(float ms)
{
    if (ms < 1.0f)
        return juce::String(ms, 2) + " ms";
    if (ms < 10.0f)
        return juce::String(ms, 1) + " ms";
    if (ms < 1000.0f)
        return juce::String(juce::roundToInt(ms)) + " ms";
    return juce::String(ms / 1000.0f, 2) + " s";
}

float parseTime(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const auto value = t.getFloatValue();
    const bool seconds = t.endsWith("s") && ! t.endsWith("ms");
    return seconds ? value * 1000.0f : value;
}

juce::String formatRatio(float r)
{
    return (r < 10.0f ? juce::String(r, 1) : juce::String(juce::roundToInt(r))) + ":1";
}

juce::String formatHz(float hz)
{
    return hz <= 20.5f ? juce::String("Off") : juce::String(juce::roundToInt(hz)) + " Hz";
}

float parseHz(const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.startsWith("off"))
        return 20.0f;
    const auto value = t.getFloatValue();
    return t.endsWith("khz") || t.endsWith("k") ? value * 1000.0f : value;
}

juce::NormalisableRange<float> skewedRange(float min, float max, float interval, float centre)
{
    juce::NormalisableRange<float> range(min, max, interval);
    range.setSkewForCentre(centre);
    return range;
}

std::unique_ptr<juce::AudioParameterFloat> makeFloat(const char* id,
                                                     const juce::String& name,
                                                     juce::NormalisableRange<float> range,
                                                     float defaultValue,
                                                     const juce::String& label,
                                                     std::function<juce::String(float)> toText,
                                                     std::function<float(const juce::String&)> fromText = nullptr)
{
    auto attributes = juce::AudioParameterFloatAttributes()
                          .withLabel(label)
                          .withStringFromValueFunction([toText](float v, int) { return toText(v); });
    if (fromText)
        attributes = attributes.withValueFromStringFunction(fromText);

    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
}
} // namespace

PresstoAudioProcessor::PresstoAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    inputParam = apvts.getRawParameterValue(ParamIDs::input);
    thresholdParam = apvts.getRawParameterValue(ParamIDs::threshold);
    ratioParam = apvts.getRawParameterValue(ParamIDs::ratio);
    kneeParam = apvts.getRawParameterValue(ParamIDs::knee);
    attackParam = apvts.getRawParameterValue(ParamIDs::attack);
    releaseParam = apvts.getRawParameterValue(ParamIDs::release);
    programParam = apvts.getRawParameterValue(ParamIDs::program);
    scHpfParam = apvts.getRawParameterValue(ParamIDs::scHpf);
    makeupParam = apvts.getRawParameterValue(ParamIDs::makeup);
    mixParam = apvts.getRawParameterValue(ParamIDs::mix);
    linkParam = apvts.getRawParameterValue(ParamIDs::link);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(ParamIDs::bypass));
    jassert(bypassParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout PresstoAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto db = [](float v) { return formatDb(v, false); };
    const auto dbSigned = [](float v) { return formatDb(v, true); };
    const auto percent = [](float v) { return juce::String(juce::roundToInt(v)) + " %"; };

    layout.add(makeFloat(ParamIDs::input, "Input", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB", dbSigned));
    layout.add(makeFloat(ParamIDs::threshold, "Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, "dB", db));
    layout.add(makeFloat(ParamIDs::ratio, "Ratio", skewedRange(1.0f, 20.0f, 0.01f, 4.0f), 4.0f, ":1", formatRatio));
    layout.add(makeFloat(ParamIDs::knee, "Knee", { 0.0f, 24.0f, 0.1f }, 6.0f, "dB", db));
    layout.add(makeFloat(ParamIDs::attack, "Attack", skewedRange(0.1f, 50.0f, 0.01f, 3.0f), 3.0f, "ms", formatTime, parseTime));
    layout.add(makeFloat(ParamIDs::release, "Release", skewedRange(20.0f, 2000.0f, 1.0f, 200.0f), 150.0f, "ms", formatTime, parseTime));
    layout.add(makeFloat(ParamIDs::program, "Program", { 0.0f, 100.0f, 1.0f }, 60.0f, "%", percent));
    layout.add(makeFloat(ParamIDs::scHpf, "SC HPF", skewedRange(20.0f, 500.0f, 1.0f, 100.0f), 20.0f, "Hz", formatHz, parseHz));
    layout.add(makeFloat(ParamIDs::makeup, "Makeup", { -12.0f, 24.0f, 0.1f }, 0.0f, "dB", dbSigned));
    layout.add(makeFloat(ParamIDs::mix, "Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, "%", percent));

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParamIDs::link, 1 }, "Stereo Link", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    return layout;
}

CompressorEngine::Settings PresstoAudioProcessor::readSettings() const noexcept
{
    CompressorEngine::Settings s;
    s.thresholdDb = thresholdParam->load();
    s.ratio = ratioParam->load();
    s.kneeDb = kneeParam->load();
    s.attackMs = attackParam->load();
    s.releaseMs = releaseParam->load();
    s.program = programParam->load() * 0.01f;
    s.sidechainHpfHz = scHpfParam->load();
    s.stereoLink = linkParam->load() >= 0.5f;
    return s;
}

void PresstoAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    engine.setSettings(readSettings(), true);

    constexpr double smoothingSeconds = 0.02;
    for (auto* smoother : { &inputGain, &makeupGain, &wetMix, &processedAmount })
        smoother->reset(sampleRate, smoothingSeconds);

    inputGain.setCurrentAndTargetValue(dbToGain(inputParam->load()));
    makeupGain.setCurrentAndTargetValue(dbToGain(makeupParam->load()));
    wetMix.setCurrentAndTargetValue(mixParam->load() * 0.01f);
    processedAmount.setCurrentAndTargetValue(bypassParam->get() ? 0.0f : 1.0f);
}

void PresstoAudioProcessor::releaseResources()
{
    engine.reset();
}

bool PresstoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainOut == layouts.getMainInputChannelSet();
}

void PresstoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numInputs = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    const int numChannels = juce::jmin(numInputs, CompressorEngine::maxChannels);
    if (numChannels == 0 || numSamples == 0)
        return;

    engine.setSettings(readSettings());
    inputGain.setTargetValue(dbToGain(inputParam->load()));
    makeupGain.setTargetValue(dbToGain(makeupParam->load()));
    wetMix.setTargetValue(mixParam->load() * 0.01f);
    processedAmount.setTargetValue(bypassParam->get() ? 0.0f : 1.0f);

    std::array<float*, CompressorEngine::maxChannels> data {};
    for (int ch = 0; ch < numChannels; ++ch)
        data[(size_t) ch] = buffer.getWritePointer(ch);

    std::array<float, 2> inPeak {}, outPeak {};
    float grPeak = 0.0f;
    float trimmed[CompressorEngine::maxChannels] {};
    float reductionDb[CompressorEngine::maxChannels] {};

    for (int i = 0; i < numSamples; ++i)
    {
        const float inGain = inputGain.getNextValue();
        const float outGain = makeupGain.getNextValue();
        const float mix = wetMix.getNextValue();
        const float amount = processedAmount.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            trimmed[ch] = data[(size_t) ch][i] * inGain;

        engine.processFrame(trimmed, numChannels, reductionDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = data[(size_t) ch][i];
            const float compressed = trimmed[ch] * dbToGain(-reductionDb[ch]);
            const float processed = (trimmed[ch] + mix * (compressed - trimmed[ch])) * outGain;
            const float out = dry + amount * (processed - dry);
            data[(size_t) ch][i] = out;

            inPeak[(size_t) ch] = juce::jmax(inPeak[(size_t) ch], std::abs(trimmed[ch]));
            outPeak[(size_t) ch] = juce::jmax(outPeak[(size_t) ch], std::abs(out));
            grPeak = juce::jmax(grPeak, reductionDb[ch] * amount);
        }
    }

    // Mono: mirror onto both meter channels.
    if (numChannels == 1)
    {
        inPeak[1] = inPeak[0];
        outPeak[1] = outPeak[0];
    }

    for (size_t ch = 0; ch < 2; ++ch)
    {
        inputPeaks[ch].push(inPeak[ch]);
        outputPeaks[ch].push(outPeak[ch]);
    }
    gainReductionPeak.push(grPeak);
}

PresstoAudioProcessor::MeterReadings PresstoAudioProcessor::takeMeterReadings() noexcept
{
    MeterReadings r;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        r.inputPeak[ch] = inputPeaks[ch].take();
        r.outputPeak[ch] = outputPeaks[ch].take();
    }
    r.gainReductionDb = gainReductionPeak.take();
    return r;
}

float PresstoAudioProcessor::getStaticReductionDb(float inputDb) const noexcept
{
    return CompressorEngine::staticReduction(readSettings(), inputDb);
}

juce::AudioProcessorParameter* PresstoAudioProcessor::getBypassParameter() const
{
    return bypassParam;
}

juce::AudioProcessorEditor* PresstoAudioProcessor::createEditor()
{
    return new PresstoAudioProcessorEditor(*this);
}

void PresstoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PresstoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PresstoAudioProcessor();
}
