#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float meterCeilingDb = 6.0f;
}

PresstoAudioProcessor::PresstoAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
    , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    inputParam = apvts.getRawParameterValue("input");
    thresholdParam = apvts.getRawParameterValue("threshold");
    ratioParam = apvts.getRawParameterValue("ratio");
    attackParam = apvts.getRawParameterValue("attack");
    releaseParam = apvts.getRawParameterValue("release");
    kneeParam = apvts.getRawParameterValue("knee");
    makeupParam = apvts.getRawParameterValue("makeup");
}

void PresstoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    const auto numChannels = juce::jmax(1, getTotalNumOutputChannels());
    gainSmoothDb.assign(static_cast<size_t>(numChannels), 0.0f);
}

void PresstoAudioProcessor::releaseResources()
{
    std::fill(gainSmoothDb.begin(), gainSmoothDb.end(), 0.0f);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PresstoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (mainOut != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
}
#endif

void PresstoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear(channel, 0, numSamples);

    if (static_cast<int>(gainSmoothDb.size()) < totalNumOutputChannels)
        gainSmoothDb.resize((size_t) totalNumOutputChannels, 0.0f);

    const auto attackMs = attackParam->load();
    const auto releaseMs = releaseParam->load();
    const float inputTrim = juce::Decibels::decibelsToGain(inputParam->load());
    const float makeup = juce::Decibels::decibelsToGain(makeupParam->load());

    const float attackCoeff = timeToCoefficient(attackMs);
    const float releaseCoeff = timeToCoefficient(releaseMs);
    const float baseReleaseWeight = 1.0f - releaseCoeff;

    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float grPeak = 0.0f;

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto& gainDb = gainSmoothDb[(size_t) channel];

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float drySample = channelData[sample];
            const float trimmedSample = drySample * inputTrim;
            const float detectorInput = std::abs(trimmedSample) + 1.0e-8f;
            const float detectorDb = juce::Decibels::gainToDecibels(detectorInput);
            const float targetGainDb = computeGainReductionDb(detectorDb);

            if (targetGainDb < gainDb)
            {
                gainDb = attackCoeff * (gainDb - targetGainDb) + targetGainDb;
            }
            else
            {
                const float normalised = juce::jlimit(0.0f, 1.0f, std::abs(gainDb) / maxReductionDb);
                const float curve = 1.0f - (normalised * normalised);
                const float releaseShape = juce::jlimit(0.05f, 1.0f, curve + 0.05f);
                const float weight = baseReleaseWeight * releaseShape;
                gainDb += weight * (targetGainDb - gainDb);
            }

            const float gainLinear = juce::Decibels::decibelsToGain(gainDb);
            const float processed = trimmedSample * gainLinear * makeup;
            channelData[sample] = processed;

            inputPeak = juce::jmax(inputPeak, std::abs(trimmedSample));
            outputPeak = juce::jmax(outputPeak, std::abs(processed));
            grPeak = juce::jmax(grPeak, -gainDb);
        }
    }

    updateMeters(inputPeak, outputPeak, grPeak);
}

bool PresstoAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PresstoAudioProcessor::createEditor()
{
    return new PresstoAudioProcessorEditor(*this);
}

void PresstoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PresstoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr)
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout PresstoAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "input", "Input",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        nullptr));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        nullptr));

    juce::NormalisableRange<float> ratioRange { 1.0f, 20.0f, 0.01f, 0.35f };
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ratio", "Ratio",
        ratioRange, 4.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + ":1"; },
        nullptr));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float>(0.1f, 200.0f, 0.01f, 0.35f), 10.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; },
        nullptr));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float>(10.0f, 1500.0f, 0.1f, 0.35f), 250.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ms"; },
        nullptr));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "knee", "Knee",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        nullptr));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "makeup", "Makeup",
        juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 6.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " dB"; },
        nullptr));

    return { params.begin(), params.end() };
}

float PresstoAudioProcessor::computeGainReductionDb(float inputDb) const noexcept
{
    const float threshold = thresholdParam->load();
    const float ratio = juce::jmax(1.0f, ratioParam->load());
    const float knee = juce::jmax(0.0f, kneeParam->load());

    const float delta = inputDb - threshold;
    const float halfKnee = knee * 0.5f;
    float reduction = 0.0f;

    if (knee > 0.0f && delta > -halfKnee && delta < halfKnee)
    {
        const float kneePos = delta + halfKnee;
        const float output = inputDb + (1.0f / ratio - 1.0f) * (kneePos * kneePos) / (2.0f * knee);
        reduction = output - inputDb;
    }
    else if (delta >= halfKnee)
    {
        const float output = threshold + delta / ratio;
        reduction = output - inputDb;
    }

    return juce::jlimit(-maxReductionDb, 0.0f, reduction);
}

float PresstoAudioProcessor::timeToCoefficient(float timeMs) const noexcept
{
    const float time = juce::jmax(0.1f, timeMs);
    const auto samplesForTime = static_cast<float>(currentSampleRate) * (time * 0.001f);

    if (samplesForTime <= 0.0f)
        return 0.0f;

    return std::exp(-1.0f / samplesForTime);
}

void PresstoAudioProcessor::updateMeters(float inputPeak, float outputPeak, float gainReductionPeak) noexcept
{
    const auto safeInput = juce::Decibels::gainToDecibels(inputPeak, minMeterDb);
    const auto safeOutput = juce::Decibels::gainToDecibels(outputPeak, minMeterDb);

    inputMeterDb.store(juce::jlimit(minMeterDb, meterCeilingDb, safeInput));
    outputMeterDb.store(juce::jlimit(minMeterDb, meterCeilingDb, safeOutput));
    gainReductionDb.store(juce::jlimit(0.0f, maxReductionDb, gainReductionPeak));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PresstoAudioProcessor();
}
