#pragma once

#include "PluginProcessor.h"

class LonglandDeadEndAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                   private juce::Timer
{
public:
    explicit LonglandDeadEndAudioProcessorEditor (LonglandDeadEndAudioProcessor&);
    ~LonglandDeadEndAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MemoryField final : public juce::Component
    {
    public:
        explicit MemoryField (LonglandDeadEndAudioProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
    private:
        LonglandDeadEndAudioProcessor& processor;
    } memoryField;

    class Knob final : public juce::Slider
    {
    public:
        Knob() { setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16); }
        void setLabel (const juce::String& text) { label = text; }
        void paint (juce::Graphics& g) override;

    private:
        juce::String label;
    };

    struct Control
    {
        juce::String id;
        juce::String label;
        std::unique_ptr<Knob> knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addControl (const juce::String& id, const juce::String& label);

    LonglandDeadEndAudioProcessor& processor;
    std::vector<Control> controls;
    juce::Label title;
    juce::Label subtitle;
    juce::Label debug;
    juce::Label preset;
    juce::ComboBox presetBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LonglandDeadEndAudioProcessorEditor)
};
