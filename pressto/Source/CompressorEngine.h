#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Framework-independent compressor core. Kept free of JUCE so the DSP can be
// unit-tested offline (see tests/EngineTests.cpp).
//
// Topology: feed-forward, log-domain gain computer driving a clean digital VCA.
//
//   in -> [sidechain HPF] -> |x| -> (stereo link: max) -> dB -> soft-knee gain
//   computer -> program-dependent ballistics -> gain applied to the audio path
//
// The ballistics are modelled on the idea behind the Charter Oak SCL-1's
// "parabolic average charge" rectifier: a fast peak stage catches transients
// but releases *towards a slowly moving average of the program's gain
// reduction* instead of all the way back to unity. Short transients therefore
// recover quickly while sustained material is held at a steady, ripple-free
// level, which is what keeps gain reduction inaudible. The average stage
// charges faster the further it is below the demand (a parabolic charge
// curve), so large sustained level changes are followed promptly.
class CompressorEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr float maxReductionDb = 40.0f;

    struct Settings
    {
        float thresholdDb = -18.0f;
        float ratio = 4.0f;
        float kneeDb = 6.0f;
        float attackMs = 3.0f;
        float releaseMs = 150.0f;
        float program = 0.6f;       // 0 = plain peak release, 1 = full average-hold
        float sidechainHpfHz = 0.0f; // <= 20 Hz disables the filter
        bool stereoLink = true;
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        // Hold the peak for slightly longer than one period of full-wave-rectified
        // bass (>= ~35 Hz) so the gain does not ripple inside each waveform
        // cycle; this is what keeps low-frequency distortion down.
        holdSamples = static_cast<int>(std::lround(0.015 * sampleRate));
        reset();
        setSettings(settings, true);
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};
        for (auto& f : hpf)
            f.reset();
    }

    void setSettings(const Settings& s, bool force = false) noexcept
    {
        const auto differs = [](float a, float b) { return a < b || a > b; };
        const bool timingChanged = force
                                || differs(s.attackMs, settings.attackMs)
                                || differs(s.releaseMs, settings.releaseMs);
        const bool filterChanged = force || differs(s.sidechainHpfHz, settings.sidechainHpfHz);

        settings = s;
        settings.ratio = std::max(1.0f, settings.ratio);
        settings.kneeDb = std::max(0.0f, settings.kneeDb);
        settings.program = std::clamp(settings.program, 0.0f, 1.0f);

        if (timingChanged)
        {
            const float releaseMs = std::max(1.0f, settings.releaseMs);
            attackCoeff = timeToCoeff(std::max(0.01f, settings.attackMs));
            peakReleaseCoeff = timeToCoeff(releaseMs);
            // The average stage integrates over a window a few times longer than
            // the peak release, bounded so it neither chases the waveform nor
            // turns into a slow automatic gain control.
            avgChargeCoeff = timeToCoeff(std::clamp(releaseMs * 4.0f, 80.0f, 1200.0f));
            avgReleaseCoeff = timeToCoeff(std::clamp(releaseMs * 5.0f, 250.0f, 4000.0f));
        }

        if (filterChanged)
        {
            const bool wasEnabled = hpfEnabled;
            hpfEnabled = settings.sidechainHpfHz > 20.0f;
            if (hpfEnabled)
                for (auto& f : hpf)
                {
                    if (! wasEnabled)
                        f.reset();
                    f.setHighPass(settings.sidechainHpfHz, sampleRate);
                }
        }
    }

    const Settings& getSettings() const noexcept { return settings; }

    // Static curve: gain reduction in dB (>= 0) for a detector level in dBFS.
    static float staticReduction(const Settings& s, float levelDb) noexcept
    {
        const float over = levelDb - s.thresholdDb;
        const float knee = std::max(0.0f, s.kneeDb);
        const float slope = 1.0f - 1.0f / std::max(1.0f, s.ratio);
        float reduction = 0.0f;

        if (knee > 0.0f && 2.0f * std::abs(over) <= knee)
        {
            const float k = over + 0.5f * knee;
            reduction = slope * k * k / (2.0f * knee);
        }
        else if (over > 0.0f)
        {
            reduction = slope * over;
        }

        return std::min(reduction, maxReductionDb);
    }

    float computeStaticReduction(float levelDb) const noexcept { return staticReduction(settings, levelDb); }

    // Processes one frame (one sample per channel) of sidechain signal and
    // writes the gain reduction to apply to each channel, in dB (>= 0).
    void processFrame(const float* sidechain, int numChannels, float* reductionDbOut) noexcept
    {
        numChannels = std::min(numChannels, maxChannels);
        std::array<float, maxChannels> level {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float x = sidechain[ch];
            if (hpfEnabled)
                x = hpf[(size_t) ch].process(x);
            level[(size_t) ch] = std::abs(x);
        }

        if (settings.stereoLink && numChannels > 1)
        {
            const float linked = *std::max_element(level.begin(), level.begin() + numChannels);
            const float gr = runBallistics(state[0], demandFor(linked));
            state[1] = state[0];
            for (int ch = 0; ch < numChannels; ++ch)
                reductionDbOut[ch] = gr;
            return;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            reductionDbOut[ch] = runBallistics(state[(size_t) ch], demandFor(level[(size_t) ch]));
    }

private:
    struct DetectorState
    {
        float peak = 0.0f;
        float average = 0.0f;
        int hold = 0;
    };

    // RBJ biquad high-pass, transposed direct form II.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() noexcept { z1 = z2 = 0.0f; }

        void setHighPass(float freq, double fs) noexcept
        {
            const double w0 = 2.0 * 3.14159265358979323846 * std::min<double>(freq, 0.45 * fs) / fs;
            const double cosw = std::cos(w0);
            const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
            const double a0 = 1.0 + alpha;
            b0 = (float) ((1.0 + cosw) * 0.5 / a0);
            b1 = (float) (-(1.0 + cosw) / a0);
            b2 = b0;
            a1 = (float) (-2.0 * cosw / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        float process(float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    float demandFor(float linearLevel) const noexcept
    {
        const float levelDb = 20.0f * std::log10(std::max(linearLevel, 1.0e-6f));
        return computeStaticReduction(levelDb);
    }

    float runBallistics(DetectorState& s, float demand) const noexcept
    {
        // Average stage: follows the demand, charging along a parabolic curve
        // (faster the further below it is) and discharging slowly.
        if (demand > s.average)
        {
            const float diff = demand - s.average;
            const float rate = std::min(1.0f, (1.0f - avgChargeCoeff) * std::min(3.0f, 1.0f + diff / 10.0f));
            s.average += rate * diff;
        }
        else
        {
            s.average = demand + avgReleaseCoeff * (s.average - demand);
        }

        const float floor = std::max(demand, s.average * settings.program);

        // Peak stage: attack, hold (see prepare()), then release towards the floor.
        if (demand > s.peak)
        {
            s.peak = demand + attackCoeff * (s.peak - demand);
            s.hold = holdSamples;
        }
        else if (s.hold > 0)
        {
            --s.hold;
        }
        else
        {
            s.peak = floor + peakReleaseCoeff * (s.peak - floor);
        }

        s.peak = std::max(s.peak, s.average * settings.program);

        // Snap inaudible residue to exactly unity gain (also avoids denormals).
        if (s.peak < 1.0e-4f)
            s.peak = 0.0f;
        if (s.average < 1.0e-4f)
            s.average = 0.0f;

        return std::min(s.peak, maxReductionDb);
    }

    float timeToCoeff(float ms) const noexcept
    {
        return (float) std::exp(-1.0 / (0.001 * (double) ms * sampleRate));
    }

    Settings settings;
    double sampleRate = 44100.0;
    int holdSamples = 0;

    float attackCoeff = 0.0f;
    float peakReleaseCoeff = 0.0f;
    float avgChargeCoeff = 0.0f;
    float avgReleaseCoeff = 0.0f;

    bool hpfEnabled = false;
    std::array<Biquad, maxChannels> hpf;
    std::array<DetectorState, maxChannels> state;
};
