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
        check(buttons.size() == 2, "editor exposes 2 buttons (found " + juce::String(buttons.size()) + ")");

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
        processor.editorBeingDeleted(editor.get());
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
