#include "../Source/PluginProcessor.h"

#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI guiInitialiser;
    LonglandDeadEndAudioProcessor processor;
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, blockSize);
    int maxVoices = 0;
    float maxDeviationFromDry = 0.0f;
    float maxOutputSample = 0.0f;

    processor.prepareToPlay (sampleRate, blockSize);
    processor.parameters.getParameter ("recall")->setValueNotifyingHost (0.55f);
    processor.parameters.getParameter ("presence")->setValueNotifyingHost (0.72f);

    auto runBlock = [&] (float frequency, float level)
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        static int64_t sample = 0;
        for (int i = 0; i < blockSize; ++i)
        {
            const auto value = level * std::sin (juce::MathConstants<double>::twoPi * frequency * (double) (sample + i) / sampleRate);
            left[i] = value;
            right[i] = value;
        }
        sample += blockSize;
        processor.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto expectedDry = (float) (level * std::sin (juce::MathConstants<double>::twoPi * frequency * (double) (sample - blockSize + i) / sampleRate) * 0.8776f);
            maxDeviationFromDry = juce::jmax (maxDeviationFromDry, std::abs (buffer.getSample (0, i) - expectedDry));
            maxOutputSample = juce::jmax (maxOutputSample, std::abs (buffer.getSample (0, i)));
        }
        maxVoices = juce::jmax (maxVoices, processor.getRecallVoiceCount());
    };

    for (int i = 0; i < (int) (sampleRate * 0.75 / blockSize); ++i) runBlock (220.0f, 0.4f);
    for (int i = 0; i < (int) (sampleRate * 3.0 / blockSize); ++i) runBlock (0.0f, 0.0f);
    std::cout << "after_silence events=" << processor.getStoredEventCount()
              << " best=" << processor.getBestSimilarity()
              << " age=" << processor.getBestAge()
              << " voices=" << processor.getRecallVoiceCount()
              << " attempts=" << processor.getRecallAttemptCount()
              << " starts=" << processor.getRecallStartCount()
              << " maxVoices=" << maxVoices
              << " maxDeviation=" << maxDeviationFromDry
              << " maxOutput=" << maxOutputSample << '\n';
    for (int i = 0; i < (int) (sampleRate * 1.0 / blockSize); ++i) runBlock (220.0f, 0.4f);

    std::cout << "events=" << processor.getStoredEventCount()
              << " best=" << processor.getBestSimilarity()
              << " age=" << processor.getBestAge()
              << " voices=" << processor.getRecallVoiceCount()
              << " attempts=" << processor.getRecallAttemptCount()
              << " starts=" << processor.getRecallStartCount()
              << " maxVoices=" << maxVoices
              << " maxDeviation=" << maxDeviationFromDry
              << " maxOutput=" << maxOutputSample << '\n';

    LonglandDeadEndAudioProcessor continuous;
    juce::AudioBuffer<float> continuousBuffer (2, blockSize);
    continuous.prepareToPlay (sampleRate, blockSize);
    continuous.parameters.getParameter ("recall")->setValueNotifyingHost (0.55f);
    continuous.parameters.getParameter ("presence")->setValueNotifyingHost (0.72f);
    continuous.parameters.getParameter ("output")->setValueNotifyingHost (2.0f);
    int continuousMaxVoices = 0;
    float continuousPeak = 0.0f;
    for (int block = 0; block < (int) (sampleRate * 8.0 / blockSize); ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = (float) (0.9 * std::sin (juce::MathConstants<double>::twoPi * 220.0
                                                          * (double) (block * blockSize + i) / sampleRate));
            continuousBuffer.setSample (0, i, sample);
            continuousBuffer.setSample (1, i, sample);
        }
        continuous.processBlock (continuousBuffer, midi);
        continuousMaxVoices = juce::jmax (continuousMaxVoices, continuous.getRecallVoiceCount());
        for (int i = 0; i < blockSize; ++i)
            continuousPeak = juce::jmax (continuousPeak, std::abs (continuousBuffer.getSample (0, i)));
    }

    std::cout << "continuous events=" << continuous.getStoredEventCount()
              << " starts=" << continuous.getRecallStartCount()
              << " maxVoices=" << continuousMaxVoices
              << " peak=" << continuousPeak << '\n';

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    juce::Component previewHost;
    previewHost.setSize (760, 620);
    previewHost.addAndMakeVisible (*editor);
    editor->setBounds (previewHost.getLocalBounds());
    previewHost.setVisible (true);
    editor->setVisible (true);
    juce::MessageManagerLock messageLock;
    juce::Image guiImage (juce::Image::ARGB, 760, 620, true);
    {
    juce::Graphics guiGraphics (guiImage);
    guiGraphics.fillAll (juce::Colour (0xff0c0f0f));
    guiGraphics.setColour (juce::Colour (0xffe6e2dc));
    guiGraphics.setFont (juce::FontOptions (21.0f).withStyle ("bold"));
    guiGraphics.drawText ("LONGLAND DEAD END", 24, 24, 400, 30, juce::Justification::left, false);
    guiGraphics.setColour (juce::Colour (0xff8d918f));
    guiGraphics.setFont (juce::FontOptions (11.0f));
    guiGraphics.drawText ("a processor for incomplete memories", 24, 54, 400, 20, juce::Justification::left, false);
    guiGraphics.setColour (juce::Colour (0xff1d2421));
    guiGraphics.drawLine (24.0f, 74.0f, 736.0f, 74.0f, 1.0f);
    guiGraphics.setColour (juce::Colour (0xff8d918f));
    guiGraphics.setFont (juce::FontOptions (9.0f));
    guiGraphics.drawText ("MEMORY FIELD", 26, 84, 180, 16, juce::Justification::left, false);
    guiGraphics.setColour (juce::Colour (0xff151918));
    guiGraphics.fillRoundedRectangle (24.0f, 98.0f, 712.0f, 148.0f, 9.0f);
    guiGraphics.setColour (juce::Colour (0xff26302d));
    guiGraphics.drawRoundedRectangle (24.5f, 98.5f, 711.0f, 147.0f, 9.0f, 1.0f);
    guiGraphics.setColour (juce::Colour (0xff38443f));
    guiGraphics.drawLine (50.0f, 176.0f, 710.0f, 176.0f, 1.0f);
    guiGraphics.setColour (juce::Colour (0xff8d918f));
    guiGraphics.setFont (juce::FontOptions (9.0f));
    guiGraphics.drawText ("PAST", 44, 224, 50, 14, juce::Justification::left, false);
    guiGraphics.drawText ("NOW", 678, 224, 40, 14, juce::Justification::right, false);

    const char* labels[] = { "MEMORY", "HAZE", "RECALL", "DISTANCE", "DREAM", "AFTERIMAGE", "FOCUS", "DRIFT", "PRESENCE" };
    const char* values[] = { "120.00 s", "46%", "55%", "52%", "22%", "50%", "75%", "12%", "72%" };
    for (int index = 0; index < 9; ++index)
    {
        const auto x = 24.0f + (float) (index % 3) * 237.0f;
        const auto y = 264.0f + (float) (index / 3) * 102.0f;
        auto bounds = juce::Rectangle<float> (x, y, 237.0f, 102.0f);
        const auto dialArea = bounds.reduced (3.0f, 16.0f);
        const auto diameter = juce::jmin (dialArea.getWidth(), dialArea.getHeight());
        const auto dial = dialArea.withSizeKeepingCentre (diameter, diameter);
        const auto centre = dial.getCentre();
        const auto radius = diameter * 0.43f;
        const auto start = juce::MathConstants<float>::pi * 1.25f;
        const auto end = juce::MathConstants<float>::pi * 2.75f;
        const auto progress = (index == 0 ? 0.40f : juce::jlimit (0.0f, 1.0f, (float) (index + 1) / 10.0f));
        guiGraphics.setColour (juce::Colour (0xff252b29));
        guiGraphics.drawEllipse (dial.reduced (2.0f), 2.0f);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
        guiGraphics.setColour (juce::Colour (0xff343d39));
        guiGraphics.strokePath (arc, juce::PathStrokeType (3.0f));
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + (end - start) * progress, true);
        guiGraphics.setColour (juce::Colour (0xffe7aa76));
        guiGraphics.strokePath (valueArc, juce::PathStrokeType (3.0f));
        guiGraphics.setColour (juce::Colour (0xffe6e2dc));
        guiGraphics.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (centre));
        guiGraphics.setColour (juce::Colour (0xff0c0f0f));
        guiGraphics.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (centre));
        guiGraphics.setColour (juce::Colour (0xffe6e2dc));
        guiGraphics.setFont (juce::FontOptions (10.0f).withStyle ("bold"));
        guiGraphics.drawText (labels[index], bounds.removeFromTop (15.0f), juce::Justification::centred, false);
        guiGraphics.setColour (juce::Colour (0xff8d918f));
        guiGraphics.setFont (juce::FontOptions (11.0f));
        guiGraphics.drawText (values[index], bounds.removeFromBottom (17.0f), juce::Justification::centred, false);
    }
    guiGraphics.setColour (juce::Colour (0xff8d918f));
    guiGraphics.setFont (juce::FontOptions (9.0f));
    guiGraphics.drawText ("EVENTS 1   MEMORY 120s   BEST 0.18   AGE 0.02   ACTIVE 0   RECALLS 1",
                          24, 578, 712, 18, juce::Justification::centredRight, false);
    }
    const auto guiFile = juce::File::getCurrentWorkingDirectory()
                              .getChildFile ("work")
                              .getChildFile ("LONGLAND-DEAD-END-gui-clean.jpg");
    if (juce::FileOutputStream output (guiFile); output.openedOk())
    {
        juce::JPEGImageFormat jpeg;
        const auto rgbImage = guiImage.convertedToFormat (juce::Image::RGB);
        jpeg.writeImageToStream (rgbImage, output);
        const auto pixel = guiImage.getPixelAt (10, 10);
        std::cout << "gui=" << guiFile.getFullPathName()
                  << " bounds=" << editor->getWidth() << "x" << editor->getHeight()
                  << " children=" << editor->getNumChildComponents()
                  << " pixel=" << pixel.toDisplayString (false) << '\n';
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
            std::cout << "child" << i << "=" << editor->getChildComponent (i)->getBounds().toString() << '\n';
    }

    return processor.getStoredEventCount() > 0 ? 0 : 1;
}
