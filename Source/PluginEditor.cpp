#include "PluginEditor.h"

namespace
{
juce::Colour ink()       { return juce::Colour (0xffe6e2dc); }
juce::Colour muted()     { return juce::Colour (0xff8d918f); }
juce::Colour warm()      { return juce::Colour (0xffe7aa76); }
juce::Colour field()     { return juce::Colour (0xff151918); }
juce::Colour background(){ return juce::Colour (0xff0c0f0f); }
}

void LonglandDeadEndAudioProcessorEditor::Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto dialArea = bounds.reduced (3.0f, 16.0f);
    const auto diameter = juce::jmin (dialArea.getWidth(), dialArea.getHeight());
    const auto dial = dialArea.withSizeKeepingCentre (diameter, diameter);
    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.43f;
    const auto start = juce::MathConstants<float>::pi * 1.25f;
    const auto end = juce::MathConstants<float>::pi * 2.75f;
    const auto value = (float) getValue();
    const auto p = (float) value;

    g.setColour (juce::Colour (0xff252b29));
    g.drawEllipse (dial.reduced (2.0f), 2.0f);
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff343d39));
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + (end - start) * p, true);
    g.setColour (warm().withAlpha (0.95f));
    g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ink());
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (centre));
    g.setColour (background());
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (centre));
    g.setColour (ink());
    g.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
    g.drawText (label, getLocalBounds().removeFromTop (15), juce::Justification::centred, false);
    g.setColour (muted());
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (getTextFromValue (getValue()), getLocalBounds().removeFromBottom (17), juce::Justification::centred, false);
}

void LonglandDeadEndAudioProcessorEditor::MemoryField::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (field());
    g.fillRoundedRectangle (area, 9.0f);
    g.setColour (juce::Colour (0xff26302d));
    g.drawRoundedRectangle (area.reduced (0.5f), 9.0f, 1.0f);

    const auto lineY = area.getCentreY() + 4.0f;
    g.setColour (juce::Colour (0xff38443f));
    g.drawLine (area.getX() + 26.0f, lineY, area.getRight() - 26.0f, lineY, 1.0f);
    g.setColour (muted());
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("PAST", (int) area.getX() + 20, (int) area.getBottom() - 22, 50, 14, juce::Justification::left, false);
    g.drawText ("NOW", (int) area.getRight() - 58, (int) area.getBottom() - 22, 40, 14, juce::Justification::right, false);

    for (int i = 0; i < LonglandDeadEndAudioProcessor::fieldNodeCount; ++i)
    {
        const auto node = processor.getFieldNode (i);
        if (node.opacity <= 0.001f)
            continue;
        const auto x = area.getX() + 28.0f + (area.getWidth() - 56.0f) * (1.0f - node.age);
        const auto y = lineY + std::sin ((float) i * 2.7f) * area.getHeight() * 0.18f;
        const auto size = 2.5f + node.significance * 6.0f;
        const auto glow = node.pulse * 0.55f;
        g.setColour (warm().withAlpha (node.opacity * 0.14f + glow * 0.1f));
        g.fillEllipse (juce::Rectangle<float> (size * 3.6f, size * 3.6f).withCentre (juce::Point<float> (x, y)));
        g.setColour (warm().withAlpha (node.opacity * 0.72f + glow * 0.28f));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (juce::Point<float> (x, y)));
    }
}

LonglandDeadEndAudioProcessorEditor::LonglandDeadEndAudioProcessorEditor (LonglandDeadEndAudioProcessor& p)
    : AudioProcessorEditor (&p), memoryField (p), processor (p)
{
    setResizable (true, true);
    setSize (760, 710);
    setOpaque (true);

    title.setText ("LONGLAND DEAD END", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, ink());
    title.setFont (juce::FontOptions (21.0f).withStyle ("bold"));
    addAndMakeVisible (title);

    subtitle.setText ("a processor for incomplete memories", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, muted());
    subtitle.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (subtitle);

    preset.addChildComponent (presetBox);
    presetBox.addItemList ({ "DEAD END", "EMPTY STREET", "SUMMER 1998", "I KNOW THIS PLACE", "LAST LIGHT", "HALF REMEMBERED" }, 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this] { processor.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    presetBox.setColour (juce::ComboBox::backgroundColourId, field());
    presetBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff26302d));
    presetBox.setColour (juce::ComboBox::textColourId, muted());
    addAndMakeVisible (presetBox);

    addAndMakeVisible (memoryField);
    addControl ("memory", "MEMORY");
    addControl ("haze", "HAZE");
    addControl ("recall", "RECALL");
    addControl ("distance", "DISTANCE");
    addControl ("dream", "DREAM");
    addControl ("afterimage", "AFTERIMAGE");
    addControl ("focus", "FOCUS");
    addControl ("drift", "DRIFT");
    addControl ("presence", "PRESENCE");
    addControl ("output", "OUTPUT");
    addControl ("mix", "MIX");

    debug.setColour (juce::Label::textColourId, muted());
    debug.setFont (juce::FontOptions (9.0f));
    debug.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (debug);
    // setSize() runs before the controls exist; lay them out once more now
    // so hosts that do not issue an initial resized() callback still show them.
    resized();
    startTimerHz (15);
}

void LonglandDeadEndAudioProcessorEditor::addControl (const juce::String& id, const juce::String& label)
{
    Control control;
    control.id = id;
    control.label = label;
    control.knob = std::make_unique<Knob>();
    control.knob->setLabel (label);
    control.knob->setRange (0.0, 1.0, 0.001);
    if (id == "memory")
        control.knob->setRange (0.5, 300.0, 0.01);
    control.knob->setDoubleClickReturnValue (true, id == "memory" ? 120.0 : 0.35);
    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, id, *control.knob);
    addAndMakeVisible (*control.knob);
    controls.push_back (std::move (control));
}

void LonglandDeadEndAudioProcessorEditor::timerCallback()
{
    memoryField.repaint();
    debug.setText (juce::String ("EVENTS ") + juce::String (processor.getStoredEventCount())
                   + "   MEMORY " + juce::String (processor.getMemorySeconds(), 0) + "s"
                   + "   BEST " + juce::String (processor.getBestSimilarity(), 2)
                   + "   AGE " + juce::String (processor.getBestAge(), 2)
                   + "   ACTIVE " + juce::String (processor.getRecallVoiceCount())
                   + "   RECALLS " + juce::String (processor.getRecallStartCount()), juce::dontSendNotification);
}

void LonglandDeadEndAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background());
    g.setColour (juce::Colour (0xff1d2421));
    g.drawLine (24.0f, 74.0f, (float) getWidth() - 24.0f, 74.0f, 1.0f);
    g.setColour (muted());
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("MEMORY FIELD", 26, 84, 180, 16, juce::Justification::left, false);
}

void LonglandDeadEndAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    title.setBounds (area.removeFromTop (30));
    subtitle.setBounds (area.removeFromTop (20));
    auto header = area.removeFromTop (24);
    presetBox.setBounds (header.removeFromRight (170).reduced (0, 2));
    memoryField.setBounds (area.removeFromTop (148));
    area.removeFromTop (18);

    auto knobArea = area.withTrimmedBottom (24);
    const auto knobW = knobArea.getWidth() / 3;
    const auto rows = juce::jmax (1, ((int) controls.size() + 2) / 3);
    const auto knobH = knobArea.getHeight() / rows;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const auto index = row * 3 + col;
            if (index < (int) controls.size())
                controls[(size_t) index].knob->setBounds (knobArea.getX() + col * knobW, knobArea.getY() + row * knobH, knobW, knobH);
        }
    debug.setBounds (area.getX(), area.getBottom() - 18, area.getWidth(), 18);
}
