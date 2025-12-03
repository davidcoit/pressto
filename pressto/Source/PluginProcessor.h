#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

class PresstoAudioProcessor : public juce::AudioProcessor
{
public:
    PresstoAudioProcessor();
    ~PresstoAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

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

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

    float getInputMeterDb() const noexcept { return inputMeterDb.load(); }
    float getOutputMeterDb() const noexcept { return outputMeterDb.load(); }
    float getGainReductionDb() const noexcept { return gainReductionDb.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    float computeGainReductionDb(float inputDb) const noexcept;
    float timeToCoefficient(float timeMs) const noexcept;
    void updateMeters(float inputPeak, float outputPeak, float gainReductionPeak) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* inputParam { nullptr };
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* ratioParam { nullptr };
    std::atomic<float>* attackParam { nullptr };
    std::atomic<float>* releaseParam { nullptr };
    std::atomic<float>* kneeParam { nullptr };
    std::atomic<float>* makeupParam { nullptr };

    std::vector<float> gainSmoothDb;
    double currentSampleRate { 44100.0 };

    std::atomic<float> inputMeterDb { -60.0f };
    std::atomic<float> outputMeterDb { -60.0f };
    std::atomic<float> gainReductionDb { 0.0f };

    const float minMeterDb { -60.0f };
    const float maxReductionDb { 48.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresstoAudioProcessor)
};
