// Offline checks for CompressorEngine. Build with -DPRESSTO_BUILD_TESTS=ON and
// run `ctest` (or the PresstoEngineTests executable directly).

#include "../Source/CompressorEngine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const char* what, double value)
{
    std::printf("[%s] %-58s %12.6f\n", ok ? " ok " : "FAIL", what, value);
    if (! ok)
        ++failures;
}

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// Exact comparison without tripping -Wfloat-equal.
bool same(float a, float b) { return ! (a < b) && ! (a > b); }

// Runs a mono signal through the engine and returns the output.
std::vector<float> run(CompressorEngine& engine, const std::vector<float>& in, std::vector<float>* grOut = nullptr)
{
    std::vector<float> out(in.size());
    if (grOut)
        grOut->resize(in.size());

    for (size_t i = 0; i < in.size(); ++i)
    {
        float gr = 0.0f;
        engine.processFrame(&in[i], 1, &gr);
        out[i] = in[i] * dbToGain(-gr);
        if (grOut)
            (*grOut)[i] = gr;
    }
    return out;
}

std::vector<float> sine(double freq, double seconds, float amp)
{
    std::vector<float> s((size_t) (seconds * fs));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = amp * (float) std::sin(2.0 * pi * freq * (double) i / fs);
    return s;
}

// THD (harmonics 2..10) of the last `seconds` of a signal, via direct DFT bins.
double thdPercent(const std::vector<float>& x, double freq, double seconds)
{
    const size_t n = (size_t) (seconds * fs);
    const size_t start = x.size() - n;
    auto mag = [&](double f)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * pi * (double) i / (double) n); // Hann
            re += w * x[start + i] * std::cos(2.0 * pi * f * (double) i / fs);
            im += w * x[start + i] * std::sin(2.0 * pi * f * (double) i / fs);
        }
        return std::sqrt(re * re + im * im);
    };

    const double fundamental = mag(freq);
    double harmonics = 0.0;
    for (int h = 2; h <= 10; ++h)
        harmonics += std::pow(mag(freq * h), 2.0);
    return 100.0 * std::sqrt(harmonics) / fundamental;
}

// The original Pressto detector (per-sample |x|, branching attack/release on
// the gain, release slowed at deep reduction) — kept here for comparison.
std::vector<float> runLegacy(const std::vector<float>& in, float thr, float ratio, float knee, float atkMs, float relMs)
{
    CompressorEngine curve;
    CompressorEngine::Settings s;
    s.thresholdDb = thr; s.ratio = ratio; s.kneeDb = knee;
    curve.prepare(fs);
    curve.setSettings(s, true);

    const float a = (float) std::exp(-1.0 / (fs * atkMs * 0.001));
    const float r = 1.0f - (float) std::exp(-1.0 / (fs * relMs * 0.001));
    float gainDb = 0.0f;
    std::vector<float> out(in.size());

    for (size_t i = 0; i < in.size(); ++i)
    {
        const float levelDb = 20.0f * std::log10(std::abs(in[i]) + 1.0e-8f);
        const float target = -curve.computeStaticReduction(levelDb);
        if (target < gainDb)
            gainDb = a * (gainDb - target) + target;
        else
        {
            const float norm = std::min(1.0f, std::abs(gainDb) / 48.0f);
            const float shape = std::clamp(1.0f - norm * norm + 0.05f, 0.05f, 1.0f);
            gainDb += r * shape * (target - gainDb);
        }
        out[i] = in[i] * dbToGain(gainDb);
    }
    return out;
}

// Time for gain reduction to fall below `db` after the signal stops at `from`.
double recoveryMs(const std::vector<float>& gr, size_t from, float db)
{
    for (size_t i = from; i < gr.size(); ++i)
        if (gr[i] < db)
            return 1000.0 * (double) (i - from) / fs;
    return 1.0e9;
}
} // namespace

int main()
{
    CompressorEngine::Settings base;
    base.thresholdDb = -20.0f;
    base.ratio = 4.0f;
    base.kneeDb = 0.0f;
    base.attackMs = 3.0f;
    base.releaseMs = 100.0f;
    base.program = 0.6f;

    // 1. Static curve.
    {
        CompressorEngine e;
        e.prepare(fs);
        e.setSettings(base, true);
        check(std::abs(e.computeStaticReduction(-10.0f) - 7.5f) < 1e-4f, "hard knee: -10 dB in, 4:1 @ -20 -> 7.5 dB GR", e.computeStaticReduction(-10.0f));
        check(same(e.computeStaticReduction(-30.0f), 0.0f), "below threshold -> 0 dB GR", e.computeStaticReduction(-30.0f));

        auto soft = base;
        soft.kneeDb = 12.0f;
        e.setSettings(soft);
        const float atThr = e.computeStaticReduction(-20.0f);
        check(std::abs(atThr - 0.75f * 36.0f / 24.0f) < 1e-4f, "soft knee (12 dB) at threshold -> 1.125 dB GR", atThr);
        check(same(e.computeStaticReduction(-26.01f), 0.0f) && std::abs(e.computeStaticReduction(-14.0f) - 4.5f) < 1e-3f,
              "soft knee joins static curve at knee edges", e.computeStaticReduction(-14.0f));
    }

    // 2. Steady-state level and distortion on sustained low-frequency tone at ~15 dB GR.
    for (double freq : { 40.0, 100.0, 1000.0 })
    {
        const auto in = sine(freq, 3.0, 1.0f);

        CompressorEngine e;
        e.prepare(fs);
        e.setSettings(base, true);
        const auto out = run(e, in);
        const double thd = thdPercent(out, freq, 1.0);
        const auto legacy = runLegacy(in, base.thresholdDb, base.ratio, base.kneeDb, base.attackMs, base.releaseMs);
        const double thdLegacy = thdPercent(legacy, freq, 1.0);

        char label[128];
        std::snprintf(label, sizeof label, "%4.0f Hz: THD new (%%), legacy %.2f%%", freq, thdLegacy);
        check(thd < thdLegacy && thd < 0.1, label, thd);
    }

    // 3. Program-dependent release: a short transient recovers faster than a
    //    sustained passage at the same level.
    {
        const auto shortBurst = [&] { auto s = sine(1000.0, 0.02, 1.0f); s.resize((size_t) (2.0 * fs), 0.0f); return s; }();
        const auto longBurst = [&] { auto s = sine(1000.0, 1.5, 1.0f); s.resize((size_t) (3.5 * fs), 0.0f); return s; }();

        std::vector<float> grShort, grLong;
        CompressorEngine e;
        e.prepare(fs);
        e.setSettings(base, true);
        run(e, shortBurst, &grShort);
        e.reset();
        run(e, longBurst, &grLong);

        const double tShort = recoveryMs(grShort, (size_t) (0.02 * fs), 1.0f);
        const double tLong = recoveryMs(grLong, (size_t) (1.5 * fs), 1.0f);
        std::printf("       short-burst recovery %.1f ms, sustained recovery %.1f ms\n", tShort, tLong);
        check(tShort < tLong * 0.5, "transient recovers >2x faster than sustained (ms)", tShort);

        auto plain = base;
        plain.program = 0.0f;
        e.setSettings(plain);
        e.reset();
        run(e, longBurst, &grLong);
        check(recoveryMs(grLong, (size_t) (1.5 * fs), 1.0f) < tLong, "Program = 0 gives plain (faster) peak release", recoveryMs(grLong, (size_t) (1.5 * fs), 1.0f));
    }

    // 4. Stereo link.
    {
        CompressorEngine e;
        e.prepare(fs);
        auto s = base;
        s.stereoLink = true;
        e.setSettings(s, true);
        float gr[2] {};
        for (int i = 0; i < 4800; ++i)
        {
            const float frame[2] { (float) std::sin(2.0 * pi * 1000.0 * i / fs), 0.0f };
            e.processFrame(frame, 2, gr);
        }
        check(gr[1] > 10.0f && same(gr[0], gr[1]), "linked: silent channel follows loud channel GR", gr[1]);

        s.stereoLink = false;
        e.setSettings(s);
        e.reset();
        for (int i = 0; i < 4800; ++i)
        {
            const float frame[2] { (float) std::sin(2.0 * pi * 1000.0 * i / fs), 0.0f };
            e.processFrame(frame, 2, gr);
        }
        check(gr[0] > 10.0f && same(gr[1], 0.0f), "unlinked (dual mono): silent channel untouched", gr[1]);
    }

    // 5. Sidechain HPF reduces GR on sub-bass.
    {
        const auto in = sine(40.0, 1.0, 1.0f);
        std::vector<float> grOff, grOn;
        CompressorEngine e;
        e.prepare(fs);
        e.setSettings(base, true);
        run(e, in, &grOff);
        auto s = base;
        s.sidechainHpfHz = 150.0f;
        e.setSettings(s);
        e.reset();
        run(e, in, &grOn);
        check(grOn.back() < grOff.back() - 6.0f, "SC HPF @150 Hz cuts 40 Hz GR by >6 dB", grOff.back() - grOn.back());
    }

    // 6. Silence and very quiet input settle to exactly zero GR.
    {
        CompressorEngine e;
        e.prepare(fs);
        e.setSettings(base, true);
        std::vector<float> gr;
        auto in = sine(1000.0, 0.5, 1.0f);
        in.resize((size_t) (8.0 * fs), 0.0f);
        run(e, in, &gr);
        check(same(gr.back(), 0.0f) && std::isfinite(gr.back()), "returns to exactly 0 dB GR after silence", gr.back());
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
