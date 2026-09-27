#pragma once

#include <JuceHeader.h>

class LonglandDeadEndAudioProcessor final : public juce::AudioProcessor
{
public:
    LonglandDeadEndAudioProcessor();
    ~LonglandDeadEndAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LONGLAND DEAD END"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;

    static constexpr int fieldNodeCount = 14;

    struct FieldNode
    {
        float age = 0.0f;
        float significance = 0.0f;
        float opacity = 0.0f;
        float pulse = 0.0f;
    };

    FieldNode getFieldNode (int index) const noexcept;
    int getStoredEventCount() const noexcept { return storedEventCount.load (std::memory_order_relaxed); }
    float getBestSimilarity() const noexcept { return bestSimilarity.load (std::memory_order_relaxed); }
    float getBestAge() const noexcept { return bestAge.load (std::memory_order_relaxed); }
    int getRecallVoiceCount() const noexcept { return recallVoiceCount.load (std::memory_order_relaxed); }
    int getRecallAttemptCount() const noexcept { return recallAttemptCount.load (std::memory_order_relaxed); }
    int getRecallStartCount() const noexcept { return recallStartCount.load (std::memory_order_relaxed); }
    float getBufferUsage() const noexcept { return bufferUsage.load (std::memory_order_relaxed); }
    float getMemorySeconds() const noexcept { return memorySeconds.load (std::memory_order_relaxed); }

private:
    struct Descriptor
    {
        float rms = 0.0f;
        float centroid = 0.0f;
        float low = 0.0f;
        float mid = 0.0f;
        float high = 0.0f;
        float harmonicity = 0.0f;
        float transient = 0.0f;
    };

    struct MemoryEvent
    {
        int64_t start = 0;
        int64_t end = 0;
        Descriptor descriptor;
        float significance = 0.0f;
        int64_t lastRecall = std::numeric_limits<int64_t>::min();
        bool valid = false;
    };

    struct RecallVoice
    {
        bool active = false;
        int eventIndex = -1;
        double position = 0.0;
        double increment = 1.0;
        float gain = 0.0f;
        float age = 0.0f;
        float pan = 0.0f;
        float lpL = 0.0f;
        float lpR = 0.0f;
        float previousL = 0.0f;
        float previousR = 0.0f;
        float phase = 0.0f;
        bool reverse = false;
    };

    static constexpr int maxEvents = 96;
    static constexpr int maxVoices = 4;
    static constexpr double maxMemorySeconds = 300.0;

    void clearRuntimeState();
    void writeInputToMemory (const juce::AudioBuffer<float>& input);
    void updateEventDetector (const juce::AudioBuffer<float>& input, int numSamples);
    void finaliseActiveEvent (int64_t endSample);
    bool eventIsReadable (const MemoryEvent& event, int64_t now) const noexcept;
    float descriptorSimilarity (const Descriptor&, const Descriptor&) const noexcept;
    int chooseRecallEvent (const Descriptor& current, float recall, float distance, int64_t now);
    void maybeStartRecall (const Descriptor& current, int64_t now, float recall, float distance,
                           float dream, float drift, float afterimage, float presence);
    float readMemoryInterpolated (double absoluteSample, int channel) const noexcept;
    void renderRecallVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples,
                             float haze, float dream, float drift, float afterimage,
                             float presence, float mix);
    void publishFieldState (int64_t now) noexcept;
    float random01() noexcept;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 0;
    int64_t totalSamples = 0;
    int memoryCapacitySamples = 0;
    int activeMemorySamples = 0;
    int writePosition = 0;
    int64_t activeEventStart = -1;
    int64_t activeEventLastNonSilent = -1;
    int64_t lastRecallSample = std::numeric_limits<int64_t>::min();
    float previousRms = 0.0f;
    float previousHighRatio = 0.0f;
    float silenceSamples = 0.0f;
    float eventRmsSum = 0.0f;
    float eventCentroidSum = 0.0f;
    float eventLowSum = 0.0f;
    float eventMidSum = 0.0f;
    float eventHighSum = 0.0f;
    float eventHarmonicitySum = 0.0f;
    float eventTransientSum = 0.0f;
    int64_t eventDescriptorBlocks = 0;
    int64_t nextRecallEligibleSample = 0;
    uint32_t randomState = 0x9e3779b9u;
    int currentProgram = 0;
    float recallResidueL = 0.0f;
    float recallResidueR = 0.0f;

    juce::AudioBuffer<float> rollingMemory;
    std::array<MemoryEvent, maxEvents> events;
    std::array<RecallVoice, maxVoices> voices;
    std::array<std::atomic<float>, fieldNodeCount> fieldAges {};
    std::array<std::atomic<float>, fieldNodeCount> fieldSignificance {};
    std::array<std::atomic<float>, fieldNodeCount> fieldOpacity {};
    std::array<std::atomic<float>, fieldNodeCount> fieldPulse {};
    std::atomic<int> storedEventCount { 0 };
    std::atomic<float> bestSimilarity { 0.0f };
    std::atomic<float> bestAge { 0.0f };
    std::atomic<int> recallVoiceCount { 0 };
    std::atomic<int> recallAttemptCount { 0 };
    std::atomic<int> recallStartCount { 0 };
    std::atomic<float> bufferUsage { 0.0f };
    std::atomic<float> memorySeconds { 120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LonglandDeadEndAudioProcessor)
};
