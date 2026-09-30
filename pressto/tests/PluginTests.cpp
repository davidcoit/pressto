// Integration checks for the processor + editor, run without a host:
//  - every knob and button in the editor is wired to the parameter it claims,
//  - bypass / mix / gain / link behave as documented on real audio,
//  - state save + restore round-trips.
// Pass `--snapshot <dir>` to also render the editor to PNG files.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

template <typename T>
void collect(juce::Component& root, juce::Array<T*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(child))
            out.add(t);
        collect(*child, out);
    }
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 480;

// Stereo 1 kHz tone, `db` dBFS peak, with a different level on the right when asked.
void fillTone(juce::AudioBuffer<float>& buffer, double& phase, float leftDb, float rightDb)
{
    const float l = juce::Decibels::decibelsToGain(leftDb, -200.0f);
    const float r = juce::Decibels::decibelsToGain(rightDb, -200.0f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float s = (float) std::sin(phase);
        phase += juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        buffer.setSample(0, i, s * l);
        buffer.setSample(1, i, s * r);
    }
}

// Runs `seconds` of tone, returns the output peak of the final block per channel.
std::array<float, 2> runTone(PresstoAudioProcessor& p, float seconds, float leftDb, float rightDb,
                             juce::AudioBuffer<float>* lastInput = nullptr, juce::AudioBuffer<float>* lastOutput = nullptr)
{
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const int blocks = juce::jmax(1, (int) (seconds * sampleRate / blockSize));

    for (int b = 0; b < blocks; ++b)
    {
        fillTone(buffer, phase, leftDb, rightDb);
        if (lastInput != nullptr && b == blocks - 1)
            lastInput->makeCopyOf(buffer);
        p.processBlock(buffer, midi);
    }

    if (lastOutput != nullptr)
        lastOutput->makeCopyOf(buffer);

    return { buffer.getMagnitude(0, 0, blockSize), buffer.getMagnitude(1, 0, blockSize) };
}

void setParam(PresstoAudioProcessor& p, const char* id, float value)
{
    auto* param = p.getApvts().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

void resetParams(PresstoAudioProcessor& p)
{
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost(param->getDefaultValue());
}

float db(float gain) { return juce::Decibels::gainToDecibels(gain, -200.0f); }

bool writePng(juce::Component& c, float scale, const juce::File& file)
{
    const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out(file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::File snapshotDir;
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String(argv[i]) == "--snapshot")
            snapshotDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[i + 1]);

    PresstoAudioProcessor processor;
    processor.setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    // ---- 1. Every control in the editor drives its parameter ---------------
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
        check(editor != nullptr, "editor is created");

        juce::Array<juce::Slider*> sliders;
        juce::Array<juce::TextButton*> buttons;
        collect(*editor, sliders);
        collect(*editor, buttons);
        check(sliders.size() == 10, "editor exposes 10 knobs (found " + juce::String(sliders.size()) + ")");
        check(buttons.size() == 3, "editor exposes 3 buttons (found " + juce::String(buttons.size()) + ")");

        juce::StringArray covered;
        for (auto* slider : sliders)
        {
            juce::RangedAudioParameter* moved = nullptr;
            juce::Array<float> before;
            for (auto* param : processor.getParameters())
                before.add(param->getValue());

            // Move to whichever end of the range it is not already at.
            const bool atMax = slider->getValue() >= slider->getMaximum() - 1.0e-6;
            const double target = atMax ? slider->getMinimum() : slider->getMaximum();
            slider->setValue(target, juce::sendNotificationSync);

            int changed = 0;
            for (int i = 0; i < processor.getParameters().size(); ++i)
                if (std::abs(processor.getParameters()[i]->getValue() - before[i]) > 1.0e-6f)
                {
                    ++changed;
                    moved = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[i]);
                }

            const bool ok = changed == 1 && moved != nullptr && std::abs(moved->getValue() - (atMax ? 0.0f : 1.0f)) < 1.0e-4f
                         && moved->getName(64).equalsIgnoreCase(slider->getTitle());
            check(ok, "knob '" + slider->getTitle() + "' -> parameter '" + (moved ? moved->getName(64) : "none") + "'");
            if (moved != nullptr)
                covered.addIfNotAlreadyThere(moved->getParameterID());

            // Double-click value = parameter default.
            if (moved != nullptr)
                check(std::abs(slider->getDoubleClickReturnValue() - moved->convertFrom0to1(moved->getDefaultValue())) < 1.0e-3,
                      "  double-click on '" + slider->getTitle() + "' resets to default");

            // The value box shows the parameter's own text.
            if (moved != nullptr)
                check(slider->getTextFromValue(slider->getValue()) == moved->getCurrentValueAsText(),
                      "  value box shows '" + slider->getTextFromValue(slider->getValue()) + "'");
        }

        for (auto* button : buttons)
        {
            juce::Array<float> before;
            for (auto* param : processor.getParameters())
                before.add(param->getValue());

            button->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

            juce::String changedName;
            int changed = 0;
            for (int i = 0; i < processor.getParameters().size(); ++i)
                if (std::abs(processor.getParameters()[i]->getValue() - before[i]) > 0.5f)
                {
                    ++changed;
                    changedName = processor.getParameters()[i]->getName(64);
                }
            check(changed == 1, "button '" + button->getButtonText() + "' toggles '" + changedName + "'");
            button->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }

        check(covered.size() == 10, "all 10 continuous parameters have a knob");

        // Typed values with units are understood.
        for (auto* slider : sliders)
        {
            if (slider->getTitle() == "Release")
                check(std::abs(slider->getValueFromText("1.2 s") - 1200.0) < 1.0 && std::abs(slider->getValueFromText("80 ms") - 80.0) < 0.5,
                      "Release accepts '1.2 s' and '80 ms'");
            if (slider->getTitle() == "SC HPF")
                check(slider->getValueFromText("off") < 20.5 && std::abs(slider->getValueFromText("120 Hz") - 120.0) < 0.5,
                      "SC HPF accepts 'off' and '120 Hz'");
            if (slider->getTitle() == "Ratio")
                check(std::abs(slider->getValueFromText("8:1") - 8.0) < 0.01, "Ratio accepts '8:1'");
        }
        // Mid/side disables Stereo Link (it doesn't apply) and splits the GR meter.
        {
            juce::Array<PresstoCanvas*> canvases;
            collect(*editor, canvases);
            juce::TextButton* linkButton = nullptr;
            juce::TextButton* msButton = nullptr;
            for (auto* b : buttons)
            {
                if (b->getButtonText() == "STEREO LINK") linkButton = b;
                if (b->getButtonText() == "MID/SIDE") msButton = b;
            }
            check(canvases.size() == 1 && linkButton != nullptr && msButton != nullptr, "found canvas, link and mid/side buttons");
            if (canvases.size() == 1 && linkButton != nullptr && msButton != nullptr)
            {
                setParam(processor, ParamIDs::midSide, 1.0f);
                canvases[0]->refreshMeters(1.0f / 60.0f);
                check(msButton->isEnabled() && msButton->getToggleState() && ! linkButton->isEnabled(),
                      "mid/side ON (stereo): button lit, Stereo Link disabled");
                setParam(processor, ParamIDs::midSide, 0.0f);
                canvases[0]->refreshMeters(1.0f / 60.0f);
                check(linkButton->isEnabled(), "mid/side OFF: Stereo Link enabled again");
            }
        }

        processor.editorBeingDeleted(editor.get()); // as a host wrapper would
    }

    // ---- 2. Audio behaviour -------------------------------------------------
    resetParams(processor);
    processor.prepareToPlay(sampleRate, blockSize);
    {
        // Defaults: -18 dB threshold, 4:1, 6 dB knee. A 0 dBFS tone is 18 dB over -> ~13.5 dB GR.
        const auto out = runTone(processor, 2.0f, 0.0f, 0.0f);
        check(std::abs(db(out[0]) + 13.5f) < 0.6f, "0 dBFS tone at defaults comes out at ~-13.5 dBFS (" + juce::String(db(out[0]), 2) + ")");

        setParam(processor, ParamIDs::makeup, 6.0f);
        const auto withMakeup = runTone(processor, 1.0f, 0.0f, 0.0f);
        check(std::abs(db(withMakeup[0]) - db(out[0]) - 6.0f) < 0.1f, "makeup +6 dB raises output by 6 dB");
        setParam(processor, ParamIDs::makeup, 0.0f);

        setParam(processor, ParamIDs::input, -24.0f);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto quiet = runTone(processor, 1.0f, 0.0f, 0.0f);
        check(std::abs(db(quiet[0]) + 24.0f) < 0.1f, "input -24 dB puts the tone under threshold (unity path)");
        setParam(processor, ParamIDs::input, 0.0f);

        setParam(processor, ParamIDs::mix, 0.0f);
        const auto dry = runTone(processor, 1.0f, -3.0f, -3.0f);
        check(std::abs(db(dry[0]) + 3.0f) < 0.05f, "mix 0 % passes the dry signal");
        setParam(processor, ParamIDs::mix, 100.0f);

        setParam(processor, ParamIDs::bypass, 1.0f);
        juce::AudioBuffer<float> in, outBuf;
        setParam(processor, ParamIDs::input, 12.0f);
        setParam(processor, ParamIDs::makeup, 12.0f);
        runTone(processor, 0.5f, -6.0f, -6.0f, &in, &outBuf);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                maxDiff = juce::jmax(maxDiff, std::abs(in.getSample(ch, i) - outBuf.getSample(ch, i)));
        check(maxDiff < 1.0e-6f, "bypass is bit-transparent (ignores input/makeup)");
        check(processor.getBypassParameter() == processor.getApvts().getParameter(ParamIDs::bypass), "host bypass maps to the Bypass parameter");
        resetParams(processor);

        // Stereo link: loud left, quiet right.
        setParam(processor, ParamIDs::link, 1.0f);
        const auto linked = runTone(processor, 1.5f, 0.0f, -30.0f);
        setParam(processor, ParamIDs::link, 0.0f);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto dual = runTone(processor, 1.5f, 0.0f, -30.0f);
        check(db(linked[1]) < -40.0f && std::abs(db(dual[1]) + 30.0f) < 0.1f,
              "link ON ducks the quiet side with the loud one (" + juce::String(db(linked[1]), 1) + " dB); OFF leaves it alone ("
                  + juce::String(db(dual[1]), 1) + " dB)");
        resetParams(processor);

        const auto readings = processor.takeMeterReadings();
        check(readings.gainReductionDb > 1.0f && readings.inputPeak[0] > 0.5f, "meters receive input level and gain reduction");
        const auto again = processor.takeMeterReadings();
        check(again.gainReductionDb <= 0.0f && again.inputPeak[0] <= 0.0f, "meter readings reset after being taken");
    }

    // ---- 2b. Mid/side --------------------------------------------------------
    {
        // Renders a mid tone at midDb and a side tone at sideDb (L = M + S, R = M - S).
        // Returns the peak of the decoded mid and side output over the final block, and
        // the largest second difference |x[n] - 2x[n-1] + x[n-2]| of the left output after the first
        // second. For low tones that is tiny, while any discontinuity (a click) shows up at full size.
        struct MsResult { float mid, side, maxCurvature; };
        const auto runMs = [](PresstoAudioProcessor& p, float seconds, float midDb, float sideDb,
                              std::function<void(int block)> beforeBlock = nullptr,
                              double midHz = 440.0, double sideHz = 1000.0)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            juce::MidiBuffer midi;
            const float m = juce::Decibels::decibelsToGain(midDb, -200.0f);
            const float sd = juce::Decibels::decibelsToGain(sideDb, -200.0f);
            const int blocks = (int) (seconds * sampleRate / blockSize);
            MsResult r { 0.0f, 0.0f, 0.0f };
            float prev1 = 0.0f, prev2 = 0.0f;

            for (int b = 0; b < blocks; ++b)
            {
                if (beforeBlock)
                    beforeBlock(b);
                for (int i = 0; i < blockSize; ++i)
                {
                    const double t = (double) (b * blockSize + i) / sampleRate;
                    const float mid = m * (float) std::sin(juce::MathConstants<double>::twoPi * midHz * t);
                    const float side = sd * (float) std::sin(juce::MathConstants<double>::twoPi * sideHz * t);
                    buffer.setSample(0, i, mid + side);
                    buffer.setSample(1, i, mid - side);
                }
                p.processBlock(buffer, midi);

                r.mid = r.side = 0.0f;
                for (int i = 0; i < blockSize; ++i)
                {
                    const float l = buffer.getSample(0, i), rr = buffer.getSample(1, i);
                    r.mid = juce::jmax(r.mid, std::abs(0.5f * (l + rr)));
                    r.side = juce::jmax(r.side, std::abs(0.5f * (l - rr)));
                    if (b >= 100) // ignore the first second while the detector settles
                        r.maxCurvature = juce::jmax(r.maxCurvature, std::abs(l - 2.0f * prev1 + prev2));
                    prev2 = prev1;
                    prev1 = l;
                }
            }
            return r;
        };

        // Loud, wide side content over a quiet centre.
        resetParams(processor);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto lr = runMs(processor, 1.5f, -30.0f, -2.0f);

        setParam(processor, ParamIDs::midSide, 1.0f);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto ms = runMs(processor, 1.5f, -30.0f, -2.0f);
        const auto msMeters = [&] { processor.takeMeterReadings(); runMs(processor, 0.1f, -30.0f, -2.0f); return processor.takeMeterReadings(); }();

        check(db(lr.mid) < -36.0f, "L/R mode: loud side content also ducks the centre (" + juce::String(db(lr.mid), 1) + " dB)");
        check(std::abs(db(ms.mid) + 30.0f) < 0.2f && db(ms.side) < -8.0f,
              "M/S mode: centre untouched (" + juce::String(db(ms.mid), 1) + " dB), side compressed (" + juce::String(db(ms.side), 1) + " dB)");
        check(msMeters.channelReductionDb[0] < 0.5f && msMeters.channelReductionDb[1] > 5.0f,
              "M/S meters: M " + juce::String(msMeters.channelReductionDb[0], 1) + " dB, S " + juce::String(msMeters.channelReductionDb[1], 1) + " dB GR");

        // Mono content (L == R): M/S gives the same result as linked L/R.
        juce::AudioBuffer<float> inBuf, outLR, outMS;
        setParam(processor, ParamIDs::midSide, 0.0f);
        processor.prepareToPlay(sampleRate, blockSize);
        runTone(processor, 1.0f, 0.0f, 0.0f, &inBuf, &outLR);
        setParam(processor, ParamIDs::midSide, 1.0f);
        processor.prepareToPlay(sampleRate, blockSize);
        runTone(processor, 1.0f, 0.0f, 0.0f, &inBuf, &outMS);
        float monoDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                monoDiff = juce::jmax(monoDiff, std::abs(outLR.getSample(ch, i) - outMS.getSample(ch, i)));
        check(monoDiff < 1.0e-5f, "mono content: M/S output matches linked L/R (max diff " + juce::String(monoDiff, 8) + ")");

        // Switching modes while playing is click-free (20 ms crossfade between warm detectors).
        // A hard switch would make the centre's gain jump by ~10 dB. 58.5 Hz puts both
        // switch points mid-waveform rather than on a zero crossing.
        resetParams(processor);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto steady = runMs(processor, 3.0f, -20.0f, -2.0f, nullptr, 58.5, 90.0);
        processor.prepareToPlay(sampleRate, blockSize);
        const auto switched = runMs(processor, 3.0f, -20.0f, -2.0f, [&](int block)
        {
            if (block == 150)
                setParam(processor, ParamIDs::midSide, 1.0f);
            if (block == 225)
                setParam(processor, ParamIDs::midSide, 0.0f);
        }, 58.5, 90.0);
        check(switched.maxCurvature < steady.maxCurvature * 10.0f, // a hard switch measures ~2500x
              "switching M/S on and off mid-stream doesn't click (peak curvature " + juce::String(switched.maxCurvature, 6)
                  + " vs steady " + juce::String(steady.maxCurvature, 6) + ")");
        resetParams(processor);

        // Mono track: the switch has no effect.
        PresstoAudioProcessor monoProc;
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        check(monoProc.setBusesLayout(monoLayout), "mono layout accepted");
        const auto runMono = [&](bool midSideOn)
        {
            setParam(monoProc, ParamIDs::midSide, midSideOn ? 1.0f : 0.0f);
            monoProc.prepareToPlay(sampleRate, blockSize);
            juce::AudioBuffer<float> b(1, blockSize);
            juce::MidiBuffer midi;
            for (int blk = 0; blk < 100; ++blk)
            {
                for (int i = 0; i < blockSize; ++i)
                    b.setSample(0, i, (float) std::sin(juce::MathConstants<double>::twoPi * 1000.0 * (blk * blockSize + i) / sampleRate));
                monoProc.processBlock(b, midi);
            }
            return b;
        };
        const auto monoOff = runMono(false);
        const auto monoOn = runMono(true);
        float monoTrackDiff = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            monoTrackDiff = juce::jmax(monoTrackDiff, std::abs(monoOff.getSample(0, i) - monoOn.getSample(0, i)));
        check(monoTrackDiff <= 0.0f && monoProc.getNumProcessedChannels() == 1, "mono track: Mid/Side has no effect");
    }

    // ---- 3. State round-trip -----------------------------------------------
    {
        setParam(processor, ParamIDs::threshold, -31.0f);
        setParam(processor, ParamIDs::release, 800.0f);
        setParam(processor, ParamIDs::link, 0.0f);
        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PresstoAudioProcessor other;
        other.setStateInformation(state.getData(), (int) state.getSize());
        const auto* t = other.getApvts().getRawParameterValue(ParamIDs::threshold);
        const auto* r = other.getApvts().getRawParameterValue(ParamIDs::release);
        const auto* l = other.getApvts().getRawParameterValue(ParamIDs::link);
        check(std::abs(t->load() + 31.0f) < 0.05f && std::abs(r->load() - 800.0f) < 0.6f && l->load() < 0.5f, "state save/restore round-trips");

        other.setStateInformation("garbage", 7);
        check(std::abs(t->load() + 31.0f) < 0.05f, "invalid state data is ignored");
    }

    // ---- 4. Optional snapshots ----------------------------------------------
    if (snapshotDir != juce::File())
    {
        snapshotDir.createDirectory();
        resetParams(processor);
        processor.prepareToPlay(sampleRate, blockSize);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
        juce::Array<PresstoCanvas*> canvases;
        collect(*editor, canvases);
        check(canvases.size() == 1, "editor hosts the scaled canvas");
        auto* canvas = canvases.getFirst();

        // Drum-ish material: decaying bursts over a sustained bed, to animate the meters.
        juce::AudioBuffer<float> buffer(2, 800);
        juce::MidiBuffer midi;
        double phase = 0.0;
        juce::Random rng(1);
        for (int frame = 0; frame < 360; ++frame)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const int n = frame * 800 + i;
                const float env = std::exp(-(float) (n % 24000) / 2500.0f);
                const float bed = (frame / 120) % 2 == 0 ? 0.25f : 0.06f;
                const float s = (float) std::sin(phase) * bed + (rng.nextFloat() * 2.0f - 1.0f) * env * 0.9f;
                phase += juce::MathConstants<double>::twoPi * 110.0 / sampleRate;
                buffer.setSample(0, i, s);
                buffer.setSample(1, i, s * 0.8f);
            }
            processor.processBlock(buffer, midi);
            canvas->refreshMeters(1.0f / 60.0f);
        }

        check(writePng(*editor, 1.0f, snapshotDir.getChildFile("editor.png")), "wrote editor.png");
        editor->setSize(PresstoCanvas::baseWidth * 3 / 2, PresstoCanvas::baseHeight * 3 / 2);
        check(writePng(*editor, 1.0f, snapshotDir.getChildFile("editor-150.png")), "wrote editor-150.png (resized 150 %)");

        // Mid/side with wide material.
        editor->setSize(PresstoCanvas::baseWidth, PresstoCanvas::baseHeight);
        setParam(processor, ParamIDs::midSide, 1.0f);
        for (int frame = 0; frame < 240; ++frame)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const int n = frame * 800 + i;
                const float env = std::exp(-(float) (n % 24000) / 2500.0f);
                const float mid = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.5f;
                const float side = 0.6f * (float) std::sin(juce::MathConstants<double>::twoPi * 330.0 * n / sampleRate);
                buffer.setSample(0, i, mid + side);
                buffer.setSample(1, i, mid - side);
            }
            processor.processBlock(buffer, midi);
            canvas->refreshMeters(1.0f / 60.0f);
        }
        check(writePng(*editor, 1.0f, snapshotDir.getChildFile("editor-midside.png")), "wrote editor-midside.png");
        processor.editorBeingDeleted(editor.get());
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
