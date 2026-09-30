#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

#include "CompressorEngine.h"

namespace ParamIDs
{
inline constexpr const char* input = "input";
inline constexpr const char* threshold = "threshold";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* knee = "knee";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* program = "program";
inline constexpr const char* scHpf = "schpf";
inline constexpr const char* makeup = "makeup";
inline constexpr const char* mix = "mix";
inline constexpr const char* link = "link";
inline constexpr const char* midSide = "midside";
inline constexpr const char* bypass = "bypass";
} // namespace ParamIDs

// Lock-free peak accumulator: the audio thread pushes maxima, the UI thread
// takes (and resets) the maximum seen since its last read, so no peaks are lost
// between repaints regardless of block size.
class PeakAccumulator
{
public:
    void push(float v) noexcept
    {
        auto current = value.load(std::memory_order_relaxed);
        while (v > current && ! value.compare_exchange_weak(current, v, std::memory_order_relaxed)) {}
    }

    float take() noexcept { return value.exchange(0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> value { 0.0f };
};

class PresstoAudioProcessor : public juce::AudioProcessor
{
public:
    PresstoAudioProcessor();
    ~PresstoAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

    struct MeterReadings
    {
        std::array<float, 2> inputPeak {};  // linear
        std::array<float, 2> outputPeak {}; // linear
        float gainReductionDb = 0.0f;       // max over both channels since last read
        std::array<float, 2> channelReductionDb {}; // per detector channel: L/R, or M/S in mid-side mode
    };

    // Called from the UI thread; returns the maxima since the previous call.
    MeterReadings takeMeterReadings() noexcept;

    // Number of channels on the main bus as last processed (1 = mono, 2 = stereo).
    int getNumProcessedChannels() const noexcept { return processedChannels.load(std::memory_order_relaxed); }

    // Static curve for the editor's transfer display (uses current parameters).
    float getStaticReductionDb(float inputDb) const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    CompressorEngine::Settings readSettings() const noexcept;
    void updateEngineSettings(bool force) noexcept;
    bool isMidSideRequested() const noexcept;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* inputParam {};
    std::atomic<float>* thresholdParam {};
    std::atomic<float>* ratioParam {};
    std::atomic<float>* kneeParam {};
    std::atomic<float>* attackParam {};
    std::atomic<float>* releaseParam {};
    std::atomic<float>* programParam {};
    std::atomic<float>* scHpfParam {};
    std::atomic<float>* makeupParam {};
    std::atomic<float>* mixParam {};
    std::atomic<float>* linkParam {};
    std::atomic<float>* midSideParam {};
    juce::AudioParameterBool* bypassParam {};

    // Both engines run on stereo input so that switching between L/R and M/S
    // crossfades between two warm detectors instead of clicking.
    CompressorEngine stereoEngine, midSideEngine;

    juce::SmoothedValue<float> inputGain, makeupGain, wetMix, processedAmount, midSideAmount;
    std::atomic<int> processedChannels { 2 };

    std::array<PeakAccumulator, 2> inputPeaks, outputPeaks;
    PeakAccumulator gainReductionPeak;
    std::array<PeakAccumulator, 2> channelReductionPeaks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresstoAudioProcessor)
};
