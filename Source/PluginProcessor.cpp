#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto memoryParam = "memory";
constexpr auto hazeParam = "haze";
constexpr auto recallParam = "recall";
constexpr auto distanceParam = "distance";
constexpr auto dreamParam = "dream";
constexpr auto afterimageParam = "afterimage";
constexpr auto focusParam = "focus";
constexpr auto driftParam = "drift";
constexpr auto presenceParam = "presence";
constexpr auto outputParam = "output";
constexpr auto mixParam = "mix";
constexpr auto stateVersion = 1;

float clamp01 (float value) noexcept { return juce::jlimit (0.0f, 1.0f, value); }
}

LonglandDeadEndAudioProcessor::LonglandDeadEndAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& value : fieldAges) value.store (0.0f);
    for (auto& value : fieldSignificance) value.store (0.0f);
    for (auto& value : fieldOpacity) value.store (0.0f);
    for (auto& value : fieldPulse) value.store (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout LonglandDeadEndAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto percent = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);

    layout.add (std::make_unique<P> (memoryParam, "Memory", juce::NormalisableRange<float> (0.5f, 300.0f, 0.01f, 0.35f), 120.0f));
    layout.add (std::make_unique<P> (hazeParam, "Haze", percent, 0.46f));
    layout.add (std::make_unique<P> (recallParam, "Recall", percent, 0.55f));
    layout.add (std::make_unique<P> (distanceParam, "Distance", percent, 0.52f));
    layout.add (std::make_unique<P> (dreamParam, "Dream", percent, 0.22f));
    layout.add (std::make_unique<P> (afterimageParam, "Afterimage", percent, 0.50f));
    layout.add (std::make_unique<P> (focusParam, "Focus", percent, 0.75f));
    layout.add (std::make_unique<P> (driftParam, "Drift", percent, 0.12f));
    layout.add (std::make_unique<P> (presenceParam, "Presence", percent, 0.72f));
    layout.add (std::make_unique<P> (outputParam, "Output", juce::NormalisableRange<float> (0.5f, 2.0f, 0.001f), 1.35f));
    layout.add (std::make_unique<P> (mixParam, "Mix", juce::NormalisableRange<float> (0.0f, 2.5f, 0.001f), 1.35f));
    return layout;
}

void LonglandDeadEndAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = juce::jmax (sampleRate, 8000.0);
    maxBlockSize = samplesPerBlock;
    memoryCapacitySamples = juce::jmax (1, (int) std::ceil (currentSampleRate * maxMemorySeconds));
    activeMemorySamples = juce::jmax (1, (int) std::llround (currentSampleRate * 120.0));
    rollingMemory.setSize (2, memoryCapacitySamples, false, true, true);
    rollingMemory.clear();
    clearRuntimeState();
    memorySeconds.store (120.0f, std::memory_order_relaxed);
    nextRecallEligibleSample = (int64_t) (currentSampleRate * 0.75);
}

void LonglandDeadEndAudioProcessor::releaseResources()
{
    rollingMemory.setSize (0, 0);
}

bool LonglandDeadEndAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo()
        ? (mainIn == mainOut || (mainIn == juce::AudioChannelSet::disabled() && mainOut == juce::AudioChannelSet::stereo()))
        : false;
}

void LonglandDeadEndAudioProcessor::clearRuntimeState()
{
    totalSamples = 0;
    writePosition = 0;
    activeEventStart = -1;
    activeEventLastNonSilent = -1;
    lastRecallSample = std::numeric_limits<int64_t>::min();
    previousRms = 0.0f;
    previousHighRatio = 0.0f;
    silenceSamples = 0.0f;
    eventRmsSum = eventCentroidSum = eventLowSum = eventMidSum = eventHighSum = 0.0f;
    eventHarmonicitySum = eventTransientSum = 0.0f;
    eventDescriptorBlocks = 0;
    randomState = 0x9e3779b9u;
    recallResidueL = recallResidueR = 0.0f;

    for (auto& event : events)
        event = {};

    for (auto& voice : voices)
        voice = {};

    storedEventCount.store (0, std::memory_order_relaxed);
    bestSimilarity.store (0.0f, std::memory_order_relaxed);
    bestAge.store (0.0f, std::memory_order_relaxed);
    recallVoiceCount.store (0, std::memory_order_relaxed);
    recallAttemptCount.store (0, std::memory_order_relaxed);
    recallStartCount.store (0, std::memory_order_relaxed);
    bufferUsage.store (0.0f, std::memory_order_relaxed);

    for (int i = 0; i < fieldNodeCount; ++i)
    {
        fieldAges[(size_t) i].store (0.0f, std::memory_order_relaxed);
        fieldSignificance[(size_t) i].store (0.0f, std::memory_order_relaxed);
        fieldOpacity[(size_t) i].store (0.0f, std::memory_order_relaxed);
        fieldPulse[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
}

void LonglandDeadEndAudioProcessor::writeInputToMemory (const juce::AudioBuffer<float>& input)
{
    const auto samples = input.getNumSamples();
    const auto channels = juce::jmin (2, input.getNumChannels());
    if (memoryCapacitySamples <= 0 || samples <= 0)
        return;

    for (int i = 0; i < samples; ++i)
    {
        const auto destination = (writePosition + i) % memoryCapacitySamples;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* source = channels > 0 ? input.getReadPointer (juce::jmin (ch, channels - 1)) : nullptr;
            rollingMemory.setSample (ch, destination, source != nullptr ? source[i] : 0.0f);
        }
    }

    writePosition = (writePosition + samples) % memoryCapacitySamples;
}

void LonglandDeadEndAudioProcessor::updateEventDetector (const juce::AudioBuffer<float>& input, int numSamples)
{
    if (numSamples <= 0 || input.getNumChannels() == 0)
        return;

    const auto* left = input.getReadPointer (0);
    const auto* right = input.getNumChannels() > 1 ? input.getReadPointer (1) : left;
    double sumSquares = 0.0;
    double low = 0.0, mid = 0.0, high = 0.0, diffEnergy = 0.0;
    float blockPeak = 0.0f;
    float previousMono = left[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mono = 0.5f * (left[i] + right[i]);
        const auto absSample = std::abs (mono);
        const auto difference = std::abs (mono - previousMono);
        previousMono = mono;
        sumSquares += (double) mono * mono;
        diffEnergy += (double) difference * difference;
        blockPeak = juce::jmax (blockPeak, absSample);

        const auto lowPart = std::abs (mono + 0.95f * (i > 0 ? left[i - 1] : mono)) * 0.5f;
        const auto highPart = difference;
        const auto midPart = juce::jmax (0.0f, absSample - 0.45f * highPart - 0.25f * lowPart);
        low += lowPart;
        mid += midPart;
        high += highPart;
    }

    const auto blockRms = (float) std::sqrt (sumSquares / (double) numSamples);
    const auto lowEnergy = (float) (low / (double) numSamples);
    const auto midEnergy = (float) (mid / (double) numSamples);
    const auto highEnergy = (float) (high / (double) numSamples);
    const auto totalEnergy = juce::jmax (1.0e-5f, lowEnergy + midEnergy + highEnergy);
    const auto highRatio = highEnergy / totalEnergy;
    const auto centroid = clamp01 (0.12f * lowEnergy / totalEnergy + 0.45f * midEnergy / totalEnergy + 0.92f * highEnergy / totalEnergy);
    const auto harmonicity = clamp01 (1.0f - (float) std::sqrt (diffEnergy / (double) numSamples) / juce::jmax (0.0001f, blockRms * 2.0f));
    const auto transient = clamp01 (std::abs (blockRms - previousRms) / juce::jmax (0.002f, previousRms + 0.002f)
                                    + std::abs (highRatio - previousHighRatio) * 1.5f);
    const auto dt = (float) numSamples;
    const auto silenceThreshold = 0.0007f;
    const auto isSilent = blockRms < silenceThreshold && blockPeak < 0.0025f;
    // Sustained material still needs to become recallable while it is playing;
    // a shorter fallback segment avoids waiting for a silence boundary forever.
    const auto maxEventSamples = (int64_t) std::llround (currentSampleRate * 1.5);
    const auto minEventSamples = (int64_t) std::llround (currentSampleRate * 0.25);
    const auto onset = blockRms > silenceThreshold * 2.0f && transient > 0.18f;

    if (activeEventStart < 0)
    {
        if (! isSilent)
        {
            activeEventStart = totalSamples;
            activeEventLastNonSilent = totalSamples + numSamples;
            eventRmsSum = blockRms;
            eventCentroidSum = centroid;
            eventLowSum = lowEnergy;
            eventMidSum = midEnergy;
            eventHighSum = highEnergy;
            eventHarmonicitySum = harmonicity;
            eventTransientSum = transient + (onset ? 0.25f : 0.0f);
            eventDescriptorBlocks = 1;
        }
    }
    else
    {
        if (! isSilent)
        {
            activeEventLastNonSilent = totalSamples + numSamples;
            eventRmsSum += blockRms;
            eventCentroidSum += centroid;
            eventLowSum += lowEnergy;
            eventMidSum += midEnergy;
            eventHighSum += highEnergy;
            eventHarmonicitySum += harmonicity;
            eventTransientSum += transient;
            ++eventDescriptorBlocks;
        }

        silenceSamples = isSilent ? silenceSamples + dt : 0.0f;
        const auto duration = totalSamples + numSamples - activeEventStart;
        const auto shouldFinish = (silenceSamples >= currentSampleRate * 0.12)
                               || duration >= maxEventSamples;
        if (shouldFinish)
        {
            const auto end = juce::jmax (activeEventStart + minEventSamples, activeEventLastNonSilent);
            if (end - activeEventStart >= minEventSamples)
                finaliseActiveEvent (end);
            activeEventStart = -1;
            activeEventLastNonSilent = -1;
            silenceSamples = 0.0f;
        }
    }

    previousRms = previousRms * 0.78f + blockRms * 0.22f;
    previousHighRatio = previousHighRatio * 0.78f + highRatio * 0.22f;
}

void LonglandDeadEndAudioProcessor::finaliseActiveEvent (int64_t endSample)
{
    int slot = -1;
    float weakest = std::numeric_limits<float>::max();
    for (int i = 0; i < maxEvents; ++i)
    {
        if (! events[(size_t) i].valid)
        {
            slot = i;
            break;
        }

        const auto readableSamples = activeMemorySamples > 0 ? activeMemorySamples : memoryCapacitySamples;
        const auto age = (float) (totalSamples - events[(size_t) i].end) / (float) juce::jmax (1, readableSamples);
        if (age > 0.98f || events[(size_t) i].significance < weakest)
        {
            weakest = events[(size_t) i].significance;
            slot = i;
        }
    }

    if (slot < 0 || eventDescriptorBlocks <= 0)
        return;

    auto& event = events[(size_t) slot];
    const auto count = (float) eventDescriptorBlocks;
    event.start = activeEventStart;
    event.end = endSample;
    event.descriptor.rms = eventRmsSum / count;
    event.descriptor.centroid = eventCentroidSum / count;
    event.descriptor.low = eventLowSum / count;
    event.descriptor.mid = eventMidSum / count;
    event.descriptor.high = eventHighSum / count;
    event.descriptor.harmonicity = eventHarmonicitySum / count;
    event.descriptor.transient = eventTransientSum / count;

    const auto loudnessNovelty = clamp01 (event.descriptor.rms / juce::jmax (0.01f, previousRms) - 0.55f);
    const auto spectralNovelty = clamp01 (std::abs (event.descriptor.centroid - previousHighRatio * 0.8f) * 1.4f);
    const auto shape = clamp01 (event.descriptor.transient * 0.7f + event.descriptor.harmonicity * 0.3f);
    event.significance = clamp01 (0.20f + 0.42f * loudnessNovelty + 0.24f * spectralNovelty + 0.20f * shape);
    event.lastRecall = std::numeric_limits<int64_t>::min();
    event.valid = true;

    int countValid = 0;
    for (const auto& candidate : events)
        countValid += candidate.valid ? 1 : 0;
    storedEventCount.store (countValid, std::memory_order_relaxed);
}

bool LonglandDeadEndAudioProcessor::eventIsReadable (const MemoryEvent& event, int64_t now) const noexcept
{
    const auto readableSamples = activeMemorySamples > 0 ? activeMemorySamples : memoryCapacitySamples;
    return event.valid && event.end > event.start && event.start >= now - readableSamples + 4
        && event.end < now - (int64_t) std::llround (currentSampleRate * 0.3);
}

float LonglandDeadEndAudioProcessor::descriptorSimilarity (const Descriptor& a, const Descriptor& b) const noexcept
{
    const auto distance = std::abs (a.rms - b.rms) * 1.5f
                        + std::abs (a.centroid - b.centroid) * 0.9f
                        + std::abs (a.low - b.low) * 1.2f
                        + std::abs (a.mid - b.mid) * 1.0f
                        + std::abs (a.high - b.high) * 1.1f
                        + std::abs (a.harmonicity - b.harmonicity) * 0.55f;
    return clamp01 (1.0f - distance * 2.4f);
}

int LonglandDeadEndAudioProcessor::chooseRecallEvent (const Descriptor& current, float recall, float distance, int64_t now)
{
    float scores[maxEvents] {};
    float best = 0.0f;
    float oldest = 0.0f;
    int bestIndex = -1;

    for (int i = 0; i < maxEvents; ++i)
    {
        const auto& event = events[(size_t) i];
        if (! eventIsReadable (event, now))
            continue;

        const auto readableSamples = activeMemorySamples > 0 ? activeMemorySamples : memoryCapacitySamples;
        const auto age = clamp01 ((float) (now - event.end) / (float) juce::jmax (1, readableSamples));
        const auto targetAge = 0.03f + distance * 0.82f;
        const auto agePreference = clamp01 (1.0f - std::abs (age - targetAge) * (1.5f + distance));
        const auto secondsSinceRecall = event.lastRecall == std::numeric_limits<int64_t>::min()
                                      ? 999.0f
                                      : (float) (now - event.lastRecall) / (float) currentSampleRate;
        const auto repeatPenalty = clamp01 (secondsSinceRecall / 4.0f);
        const auto similarity = descriptorSimilarity (current, event.descriptor);
        const auto score = similarity * (0.42f + 0.34f * agePreference) * (0.45f + 0.55f * event.significance)
                         * (0.35f + 0.65f * repeatPenalty);
        scores[i] = score;

        if (score > best)
        {
            best = score;
            bestIndex = i;
            oldest = age;
        }
    }

    bestSimilarity.store (best, std::memory_order_relaxed);
    bestAge.store (oldest, std::memory_order_relaxed);

    const auto recallChance = 0.35f + clamp01 (recall) * 0.65f;
    if (bestIndex < 0 || best < 0.10f || random01() > recallChance)
        return -1;

    // A small amount of weighted choice keeps recall human rather than deterministic,
    // while the score still decides which memories are eligible.
    const auto threshold = best * (0.76f + random01() * 0.18f);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < maxEvents; ++i)
            if (scores[i] >= threshold && (pass == 1 || random01() > 0.35f))
                return i;

    return bestIndex;
}

void LonglandDeadEndAudioProcessor::maybeStartRecall (const Descriptor& current, int64_t now, float recall,
                                                       float distance, float dream, float drift, float afterimage,
                                                       float presence)
{
    if (recall <= 0.001f || presence <= 0.001f || now < nextRecallEligibleSample)
        return;

    const auto minimumGap = (int64_t) std::llround (currentSampleRate * (1.35 - recall * 0.55));
    if (lastRecallSample != std::numeric_limits<int64_t>::min()
        && now - lastRecallSample < minimumGap)
        return;

    const auto eventIndex = chooseRecallEvent (current, recall, distance, now);
    recallAttemptCount.fetch_add (1, std::memory_order_relaxed);
    if (eventIndex < 0)
        return;

    int voiceIndex = -1;
    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].active) { voiceIndex = i; break; }
    if (voiceIndex < 0)
    {
        voiceIndex = 0;
        for (int i = 1; i < maxVoices; ++i)
            if (voices[(size_t) i].gain < voices[(size_t) voiceIndex].gain)
                voiceIndex = i;
    }

    auto& voice = voices[(size_t) voiceIndex];
    auto& event = events[(size_t) eventIndex];
    const auto readableSamples = activeMemorySamples > 0 ? activeMemorySamples : memoryCapacitySamples;
    const auto age = clamp01 ((float) (now - event.end) / (float) juce::jmax (1, readableSamples));
    voice.active = true;
    voice.eventIndex = eventIndex;
    voice.reverse = random01() < (0.28f + 0.62f * clamp01 (afterimage));
    voice.position = voice.reverse ? (double) (event.end - event.start - 1) : 0.0;
    const auto playbackRate = voice.reverse ? (0.42 + 0.10 * (1.0 - afterimage))
                                            : (0.68 + 0.08 * (1.0 - dream));
    voice.increment = (voice.reverse ? -1.0 : 1.0) * (playbackRate + (random01() - 0.5) * drift * 0.03);
    voice.gain = 0.08f + 0.14f * event.significance;
    voice.age = age;
    voice.pan = (random01() - 0.5f) * (0.22f + dream * 0.48f);
    voice.lpL = voice.lpR = voice.previousL = voice.previousR = 0.0f;
    voice.phase = random01() * juce::MathConstants<float>::twoPi;
    event.lastRecall = now;
    recallStartCount.fetch_add (1, std::memory_order_relaxed);
    lastRecallSample = now;
    nextRecallEligibleSample = now + (int64_t) std::llround (currentSampleRate * (0.45 + random01() * 1.25));
}

float LonglandDeadEndAudioProcessor::readMemoryInterpolated (double absoluteSample, int channel) const noexcept
{
    if (memoryCapacitySamples <= 0)
        return 0.0f;

    const auto baseSample = (int64_t) std::floor (absoluteSample);
    const auto fraction = (float) (absoluteSample - (double) baseSample);
    const auto wrapped = (int) ((baseSample % memoryCapacitySamples + memoryCapacitySamples) % memoryCapacitySamples);
    const auto next = (wrapped + 1) % memoryCapacitySamples;
    const auto a = rollingMemory.getSample (juce::jmin (channel, rollingMemory.getNumChannels() - 1), wrapped);
    const auto b = rollingMemory.getSample (juce::jmin (channel, rollingMemory.getNumChannels() - 1), next);
    return a + (b - a) * fraction;
}

void LonglandDeadEndAudioProcessor::renderRecallVoices (juce::AudioBuffer<float>& output, int startSample,
                                                         int numSamples, float haze, float dream, float drift,
                                                         float afterimage, float presence, float mix)
{
    if (output.getNumChannels() == 0)
        return;

    const auto hazeAmount = clamp01 (haze);
    const auto dreamAmount = clamp01 (dream);
    const auto afterimageAmount = clamp01 (afterimage);
    const auto memoryMix = juce::jlimit (0.0f, 2.5f, mix);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float recalledL = 0.0f;
        float recalledR = 0.0f;

        for (auto& voice : voices)
        {
            if (! voice.active || voice.eventIndex < 0)
                continue;

            const auto& event = events[(size_t) voice.eventIndex];
            const auto length = juce::jmax<int64_t> (1, event.end - event.start);
            const auto progress = clamp01 ((float) voice.position / (float) length);
            const auto sourceOffset = voice.reverse ? (double) length - 1.0 - voice.position : voice.position;
            const auto sourceSample = (double) event.start + sourceOffset;
            auto left = readMemoryInterpolated (sourceSample, 0);
            auto right = readMemoryInterpolated (sourceSample, 1);

            // Older recollections lose high-frequency detail, stereo precision, and
            // temporal/pitch certainty. The dry signal never enters this path.
            const auto ageSoftness = clamp01 (voice.age * hazeAmount * 1.22f + dreamAmount * 0.22f);
            const auto lowPass = juce::jlimit (0.012f, 0.23f, 0.21f * (1.0f - ageSoftness) + 0.018f);
            voice.lpL += lowPass * (left - voice.lpL);
            voice.lpR += lowPass * (right - voice.lpR);
            left = voice.lpL;
            right = voice.lpR;

            const auto mono = 0.5f * (left + right);
            const auto stereoLoss = clamp01 (ageSoftness * 0.86f + dreamAmount * 0.22f);
            left += (mono - left) * stereoLoss;
            right += (mono - right) * stereoLoss;

            const auto smooth = juce::jlimit (0.02f, 0.42f, 0.32f - ageSoftness * 0.22f);
            voice.previousL += smooth * (left - voice.previousL);
            voice.previousR += smooth * (right - voice.previousR);
            left = voice.previousL;
            right = voice.previousR;

            const auto driftRatio = 1.0f + std::sin (voice.phase) * drift * (0.0015f + ageSoftness * 0.004f);
            voice.phase += 0.0007f * (1.0f + drift * 4.0f);
            voice.position += voice.increment * driftRatio;

            const auto edgeFade = voice.reverse
                                ? std::pow (juce::jmax (0.0f, 1.0f - progress), 0.72f)
                                : std::sin (juce::MathConstants<float>::pi * progress);
            const auto panL = 0.7071f * std::sqrt (1.0f - voice.pan);
            const auto panR = 0.7071f * std::sqrt (1.0f + voice.pan);
            const auto voiceGain = voice.gain * edgeFade * (1.0f - 0.18f * ageSoftness);
            recalledL += left * panL * voiceGain;
            recalledR += right * panR * voiceGain;

            if ((! voice.reverse && voice.position >= length) || (voice.reverse && voice.position <= 0.0))
                voice.active = false;
        }

        const auto tailDecay = 0.99986f - afterimageAmount * 0.00002f;
        recallResidueL = recallResidueL * tailDecay + recalledL * (0.00030f + afterimageAmount * 0.00075f);
        recallResidueR = recallResidueR * tailDecay + recalledR * (0.00030f + afterimageAmount * 0.00075f);
        const auto directMix = 0.62f - afterimageAmount * 0.22f;
        const auto outL = (recalledL * presence * directMix + recallResidueL * afterimageAmount * presence * 0.52f) * memoryMix;
        const auto outR = (recalledR * presence * directMix + recallResidueR * afterimageAmount * presence * 0.52f) * memoryMix;
        output.addSample (0, startSample + sample, outL);
        if (output.getNumChannels() > 1)
            output.addSample (1, startSample + sample, outR);
    }

    int active = 0;
    for (const auto& voice : voices)
        active += voice.active ? 1 : 0;
    recallVoiceCount.store (active, std::memory_order_relaxed);
}

void LonglandDeadEndAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels == 0)
        return;

    const auto memorySecondsValue = parameters.getRawParameterValue (memoryParam)->load();
    const auto haze = parameters.getRawParameterValue (hazeParam)->load();
    const auto recall = parameters.getRawParameterValue (recallParam)->load();
    const auto distance = parameters.getRawParameterValue (distanceParam)->load();
    const auto dream = parameters.getRawParameterValue (dreamParam)->load();
    const auto afterimage = parameters.getRawParameterValue (afterimageParam)->load();
    const auto focus = parameters.getRawParameterValue (focusParam)->load();
    const auto drift = parameters.getRawParameterValue (driftParam)->load();
    const auto presence = parameters.getRawParameterValue (presenceParam)->load();
    const auto outputGain = parameters.getRawParameterValue (outputParam)->load();
    const auto mix = parameters.getRawParameterValue (mixParam)->load();

    const auto effectiveMemorySamples = juce::jlimit<int64_t> ((int64_t) std::llround (currentSampleRate * 0.5),
                                                                (int64_t) memoryCapacitySamples,
                                                                (int64_t) std::llround (currentSampleRate * memorySecondsValue));
    activeMemorySamples = (int) effectiveMemorySamples;
    memorySeconds.store ((float) effectiveMemorySamples / (float) currentSampleRate, std::memory_order_relaxed);

    writeInputToMemory (buffer);
    updateEventDetector (buffer, numSamples);

    Descriptor current;
    {
        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
        double energy = 0.0, high = 0.0, low = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mono = 0.5f * (left[i] + right[i]);
            const auto previous = i > 0 ? 0.5f * (left[i - 1] + right[i - 1]) : mono;
            const auto diff = std::abs (mono - previous);
            energy += (double) mono * mono;
            high += diff;
            low += std::abs (mono);
        }
        current.rms = (float) std::sqrt (energy / (double) numSamples);
        current.high = clamp01 ((float) high / (float) numSamples * 3.0f);
        current.low = clamp01 ((float) low / (float) numSamples);
        current.mid = clamp01 (current.low - current.high * 0.35f);
        current.centroid = clamp01 (current.high * 0.84f + current.mid * 0.36f);
        current.harmonicity = clamp01 (1.0f - current.high);
        current.transient = clamp01 (std::abs (current.rms - previousRms) * 3.0f + current.high * 0.5f);
    }

    maybeStartRecall (current, totalSamples, recall, distance, dream, drift, afterimage, presence);

    const auto dryGain = 0.26f + 0.54f * clamp01 (focus);
    buffer.applyGain (dryGain);
    renderRecallVoices (buffer, 0, numSamples, haze, dream, drift, afterimage, presence, mix);
    buffer.applyGain (outputGain);

    // Recalled material can overlap the present. Keep the summed output below
    // digital full scale without hard-clipping bass transients.
    constexpr auto cleanPeak = 0.92f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto peak = std::abs (buffer.getSample (0, sample));
        if (numChannels > 1)
            peak = juce::jmax (peak, std::abs (buffer.getSample (1, sample)));

        if (peak > cleanPeak)
        {
            const auto scale = cleanPeak / peak;
            for (int channel = 0; channel < numChannels; ++channel)
                buffer.setSample (channel, sample, buffer.getSample (channel, sample) * scale);
        }
    }

    totalSamples += numSamples;
    publishFieldState (totalSamples);
    bufferUsage.store (memoryCapacitySamples > 0 ? juce::jlimit (0.0f, 1.0f, (float) totalSamples / (float) juce::jmax<int64_t> (1, effectiveMemorySamples)) : 0.0f,
                       std::memory_order_relaxed);
}

void LonglandDeadEndAudioProcessor::publishFieldState (int64_t now) noexcept
{
    int write = 0;
    for (int i = 0; i < maxEvents && write < fieldNodeCount; ++i)
    {
        const auto& event = events[(size_t) i];
        if (! eventIsReadable (event, now))
            continue;
        const auto readableSamples = activeMemorySamples > 0 ? activeMemorySamples : memoryCapacitySamples;
        const auto age = clamp01 ((float) (now - event.end) / (float) juce::jmax (1, readableSamples));
        fieldAges[(size_t) write].store (age, std::memory_order_relaxed);
        fieldSignificance[(size_t) write].store (event.significance, std::memory_order_relaxed);
        fieldOpacity[(size_t) write].store (clamp01 ((1.0f - age) * 0.82f + event.significance * 0.35f), std::memory_order_relaxed);
        const auto recalled = event.lastRecall != std::numeric_limits<int64_t>::min()
                            ? clamp01 (1.0f - (float) (now - event.lastRecall) / (float) currentSampleRate * 0.9f)
                            : 0.0f;
        fieldPulse[(size_t) write].store (recalled, std::memory_order_relaxed);
        ++write;
    }

    for (; write < fieldNodeCount; ++write)
    {
        fieldAges[(size_t) write].store (0.0f, std::memory_order_relaxed);
        fieldSignificance[(size_t) write].store (0.0f, std::memory_order_relaxed);
        fieldOpacity[(size_t) write].store (0.0f, std::memory_order_relaxed);
        fieldPulse[(size_t) write].store (0.0f, std::memory_order_relaxed);
    }
}

float LonglandDeadEndAudioProcessor::random01() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (float) (randomState & 0x00ffffffu) / 16777215.0f;
}

LonglandDeadEndAudioProcessor::FieldNode LonglandDeadEndAudioProcessor::getFieldNode (int index) const noexcept
{
    if (index < 0 || index >= fieldNodeCount)
        return {};
    return { fieldAges[(size_t) index].load (std::memory_order_relaxed),
             fieldSignificance[(size_t) index].load (std::memory_order_relaxed),
             fieldOpacity[(size_t) index].load (std::memory_order_relaxed),
             fieldPulse[(size_t) index].load (std::memory_order_relaxed) };
}

double LonglandDeadEndAudioProcessor::getTailLengthSeconds() const
{
    return 8.0;
}

juce::AudioProcessorEditor* LonglandDeadEndAudioProcessor::createEditor()
{
    return new LonglandDeadEndAudioProcessorEditor (*this);
}

void LonglandDeadEndAudioProcessor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, index);
    static const float presets[6][11] =
    {
        { 120.0f, 0.46f, 0.55f, 0.52f, 0.22f, 0.50f, 0.75f, 0.12f, 0.72f, 1.35f, 1.35f },
        { 180.0f, 0.48f, 0.13f, 0.86f, 0.22f, 0.25f, 0.88f, 0.07f, 0.34f, 1.20f, 1.10f },
        { 110.0f, 0.40f, 0.27f, 0.58f, 0.20f, 0.32f, 0.84f, 0.20f, 0.48f, 1.30f, 1.25f },
        { 150.0f, 0.24f, 0.48f, 0.42f, 0.12f, 0.36f, 0.78f, 0.05f, 0.56f, 1.35f, 1.35f },
        { 240.0f, 0.78f, 0.16f, 0.82f, 0.28f, 0.62f, 0.76f, 0.04f, 0.42f, 1.15f, 1.00f },
        { 150.0f, 0.55f, 0.19f, 0.64f, 0.82f, 0.40f, 0.80f, 0.11f, 0.34f, 1.25f, 1.20f }
    };
    const auto ids = { memoryParam, hazeParam, recallParam, distanceParam, dreamParam, afterimageParam, focusParam, driftParam, presenceParam, outputParam, mixParam };
    int i = 0;
    for (const auto id : ids)
        if (auto* value = parameters.getParameter (id))
            value->setValueNotifyingHost (value->convertTo0to1 (presets[currentProgram][i++]));
}

const juce::String LonglandDeadEndAudioProcessor::getProgramName (int index)
{
    static const char* names[] = { "DEAD END", "EMPTY STREET", "SUMMER 1998", "I KNOW THIS PLACE", "LAST LIGHT", "HALF REMEMBERED" };
    return names[juce::jlimit (0, getNumPrograms() - 1, index)];
}

void LonglandDeadEndAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destination);
}

void LonglandDeadEndAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LonglandDeadEndAudioProcessor();
}
