#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <limits>
#include <vector>

// ==============================================================================
// It manages a parallel split block with independent chains.
// Each chain has plugin slots, gain, volume, mute/solo, and meters.
// This also includes final summing, latency compensation, and gain smoothing.
// ==============================================================================

class ParallelSplitProcessor
{
public:
    static constexpr int maxChains = std::numeric_limits<int>::max();

    enum class ChainSlotType { Empty, Plugin, ParallelSplit };

    struct AtomicFloatValue
    {
        AtomicFloatValue(float initial = 0.0f) noexcept : value(initial) {}
        AtomicFloatValue(const AtomicFloatValue& other) noexcept : value(other.load()) {}
        AtomicFloatValue(AtomicFloatValue&& other) noexcept : value(other.load()) {}

        AtomicFloatValue& operator=(const AtomicFloatValue& other) noexcept
        {
            store(other.load());
            return *this;
        }

        AtomicFloatValue& operator=(AtomicFloatValue&& other) noexcept
        {
            store(other.load());
            return *this;
        }

        AtomicFloatValue& operator=(float newValue) noexcept
        {
            store(newValue);
            return *this;
        }

        operator float() const noexcept { return load(); }
        float load() const noexcept { return value.load(std::memory_order_relaxed); }
        void store(float newValue) noexcept { value.store(newValue, std::memory_order_relaxed); }

    private:
        std::atomic<float> value;
    };

    // State of a single slot within a parallel chain.
    struct ChainSlotInfo
    {
        juce::AudioProcessorGraph::Node::Ptr  node;
        juce::String                          name;
        juce::String                          pluginFormat;
        juce::String                          fileOrIdentifier;
        int                                   uniqueId = 0;
        bool                                  valid    = false;
        bool                                  bypassed = false;
        ChainSlotType                         type     = ChainSlotType::Empty;
        ParallelSplitProcessor*               parallelProcessor = nullptr;
        std::unique_ptr<ParallelSplitProcessor> parallelSplitOwner;
        std::unique_ptr<juce::DocumentWindow> editorWindow;
    };

    // Complete state of a single parallel chain.
    struct ParallelChainData
    {
        juce::String name;
        std::unique_ptr<juce::AudioProcessorGraph> chainGraph;
        juce::AudioProcessorGraph::Node::Ptr inputNode;
        juce::AudioProcessorGraph::Node::Ptr outputNode;

        std::array<ChainSlotInfo, 8> slots;

        AtomicFloatValue inputGainDb { 0.0f };   // -inf .. +24 dB (-100 dB represents -inf)
        AtomicFloatValue outputVolDb { 0.0f };   // -inf .. +24 dB (-100 dB represents -inf)
        bool  muted       = false;
        bool  solo        = false;

    // Linearized and smoothed gains actually used in DSP.
        juce::LinearSmoothedValue<float> inputGainLinear;
        juce::LinearSmoothedValue<float> outputGainLinear;

        float outLevelL = -100.0f;
        float outLevelR = -100.0f;
        float inLevelL  = -100.0f;
        float inLevelR  = -100.0f;
        float level     = -100.0f;   // legacy pre-fader

        bool         pluginValid   (int i) const { return slots[i].valid;   }
        juce::String pluginName    (int i) const { return slots[i].name;    }
        bool         pluginBypassed(int i) const { return slots[i].bypassed;}
    };

    ParallelSplitProcessor(); // Builds a default active chain.
    ~ParallelSplitProcessor(); // Close the chain editor and graph in safe order.

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages); // Duplicate the input, process the branches, and add the final output.

    int getNumChains() const { return (int)chains.size(); }
    int getTotalParallelSplitCount() const;
    bool canAddChain() const { return getNumChains() < maxChains; }
    bool canRemoveChain(int index) const { return index >= 0 && index < getNumChains() && getNumChains() > 1; }
    ParallelChainData& getChain(int index) { return chains[(size_t)juce::jlimit(0, getNumChains() - 1, index)]; }
    const ParallelChainData& getChain(int index) const { return chains[(size_t)juce::jlimit(0, getNumChains() - 1, index)]; }
    ParallelChainData* addChain();
    void removeChain(int index);
    void ensureChainCount(int count);

    ParallelChainData& getLeftChain()  { return getChain(0); }
    ParallelChainData& getRightChain() { return getChain(1); }

    float getLeftLevel()  const { return getChain(0).level; }
    float getRightLevel() const { return getChain(1).level; }
    bool hasOutputOverload() const
    {
        for (int i = 0; i < getNumChains(); ++i)
        {
            const auto& chain = chains[(size_t)i];
            if (chain.outLevelL >= 0.0f || chain.outLevelR >= 0.0f)
                return true;

            for (const auto& slot : chain.slots)
                if (slot.valid
                    && slot.type == ChainSlotType::ParallelSplit
                    && slot.parallelProcessor != nullptr
                    && slot.parallelProcessor->hasOutputOverload())
                    return true;
        }

        return false;
    }

// Calculate the total latency of a chain in samples
    int  getChainLatency(const ParallelChainData& chain) const;

    void loadPluginInChain    (ParallelChainData& chain, int slot,
                               std::unique_ptr<juce::AudioProcessor> proc,
                               const juce::String& name,
                               const juce::String& format = {},
                               const juce::String& fileOrIdentifier = {},
                               int uniqueId = 0,
                               bool openEditor = false);
    void removePluginFromChain(ParallelChainData& chain, int slot);
    void closeAllPluginWindows();
    void createParallelSplitInChain(ParallelChainData& chain, int slot);
    void updateChainConnections(ParallelChainData& chain);
    void setSlotBypassed(ParallelChainData& chain, int slot, bool bypass);
    void showPluginGUI(ParallelChainData& chain, int slot);
    void moveOrSwapInChain(ParallelChainData& chain, int srcSlot, int dstSlot);

private:
    std::vector<ParallelChainData> chains;

    double currentSampleRate  = 44100.0;
    int    currentBlockSize   = 512;

    // Delay lines used to temporally realign branches.
    std::vector<juce::AudioBuffer<float>> delayBuffers;
    std::vector<int> delayPositions;
    std::vector<int> delayLengths;

    // Create the chain's internal graph and its IO nodes if missing.
    void initializeChain(ParallelChainData& chain);
    // Calculate the necessary delays to temporally realign the two branches.
    void updateLatencyCompensation();

    void processChainFull(ParallelChainData& chain,
                          const juce::AudioBuffer<float>& inputBuf,
                          juce::AudioBuffer<float>& outputBuf,
                          juce::MidiBuffer& midi);

    // Apply circular delay buffer to compensate latency difference
    void applyDelay(juce::AudioBuffer<float>& buf,
                    juce::AudioBuffer<float>& delayBuf,
                    int& delayPos, int delayLen);

    void calculateLevel(ParallelChainData& chain, const juce::AudioBuffer<float>& buf);
    void calculateInputLevels(ParallelChainData& chain, const juce::AudioBuffer<float>& buf);
    void calculateOutputLevels(ParallelChainData& chain, const juce::AudioBuffer<float>& buf);
};
