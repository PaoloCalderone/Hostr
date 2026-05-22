#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <thread>
#include "ParallelSplitProcessor.h"

// ==============================================================================
// The heart of the host plugin:
// - manages the master chain
// - loads and removes AU/VST3/VST2 plugins from slots
// - creates Parallel Split blocks
// - coordinates presets, plugin scan, and output meters
// ==============================================================================

// forward — avoid circular dependencies in the header
class PresetManager;

class PluginProcessor : public juce::AudioProcessor, public juce::Timer
{
public:
    static constexpr int macroControlCount = 8;
    static constexpr const char* productName = "Hostr";
    static constexpr const char* productSlogan = "Create complex parallel racks within a single insert, without cables, buses, or cluttered routing.";
    static constexpr const char* productPresetPromise = "Local, portable, transparent presets. No cloud, no account, no closed ecosystem.";

    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Hostr"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void loadPlugin(const juce::PluginDescription& description, int slotIndex);
    void removePlugin(int slotIndex);
    void moveOrSwapPlugin(int sourceSlot, int targetSlot);
    void showPluginGUI(int slotIndex);
    void createParallelSplit(int slotIndex);
    void setSlotBypassed(int slotIndex, bool bypassed);
    bool isSlotBypassed(int slotIndex) const;
    void setInputGainDb(float db, bool notifyHost = true);
    void setMasterVolumeDb(float db, bool notifyHost = true);
    float getMasterVolumeDb() const;
    void startScanningFolder(const juce::File& folderToScan);

    // Cross-chain moves
    void moveFromMasterToParallel(int masterSlot,
                                  ParallelSplitProcessor* destParallel,
                                  int destChainIndex, int destSlotIndex);
    void moveFromParallelToMaster(ParallelSplitProcessor* srcParallel,
                                  int srcChainIndex, int srcSlotIndex,
                                  int masterSlot);
    void moveParallelToParallel(ParallelSplitProcessor* srcParallel,
                                int srcChainIndex, int srcSlotIndex,
                                ParallelSplitProcessor* dstParallel,
                                int dstChainIndex, int dstSlotIndex);

    // Parallel Split Specific
    void removePluginFromParallelSplit(juce::AudioProcessorGraph::Node& chain, int slotIndex);

    void startScanning(bool clearCacheFirst = false);
    bool isScanning();
    float getScanProgress();
    juce::String getScanCurrentName();
    int  getNewPluginsFoundCount() const;
    int  getKnownPluginCount() const;
    bool didLastScanFinishWithError() const;
    juce::String getLastScanErrorMessage();
    juce::String getPendingScanRecoveryMessage();
    void clearPendingScanRecoveryMessage();
    juce::Array<juce::PluginDescription> getLoadablePluginDescriptions();
    juce::Array<juce::PluginDescription> getKnownPluginDescriptions() const;
    juce::PluginDescription canonicalizePluginDescriptionForLoad(const juce::PluginDescription& description);
    void removeKnownPlugin(const juce::PluginDescription& description);
    float getEditorZoomScale() const;
    void  setEditorZoomScale(float scale);
    int   getEditorSkinIndex() const;
    void  setEditorSkinIndex(int index);

    void timerCallback() override;
    void debugPrintPlugins();
    void beginGraphMutation();
    void endGraphMutation();

    // VU METERS
    float getOutputLevel(int channel) const
    {
        if (channel < 0 || channel >= 2) return -100.0f;
        return outputLevels[channel].load(std::memory_order_relaxed);
    }
    float getInputLevel(int channel) const
    {
        if (channel < 0 || channel >= 2) return -100.0f;
        return inputLevels[channel].load(std::memory_order_relaxed);
    }
    bool hasParallelOutputOverload() const;

    enum class SlotType { Empty, Plugin, ParallelSplit };

    enum class MacroTargetScope { MasterSlot, ParallelSlot };

    struct MacroMapping
    {
        MacroTargetScope scope = MacroTargetScope::MasterSlot;
        int macroIndex = 0;
        int masterSlot = -1;
        int chainIndex = -1;
        int parallelSlot = -1;
        int parameterIndex = -1;
        juce::String targetPath;
        juce::String pluginName;
        juce::String parameterName;
        float targetMin = 0.0f;
        float targetMax = 1.0f;
        bool inverted = false;
        bool enabled = true;
    };

    struct LoadedPluginInfo {
        juce::String name;
        juce::String pluginFormat;
        juce::String fileOrIdentifier;
        int uniqueId   = 0;
        bool isValid   = false;
        bool bypassed  = false;
        SlotType type  = SlotType::Empty;
        juce::AudioProcessorGraph::Node::Ptr node;
        std::unique_ptr<juce::DocumentWindow> editorWindow;
        ParallelSplitProcessor* parallelProcessor = nullptr;
        std::unique_ptr<ParallelSplitProcessor> parallelSplitOwner = nullptr;
    };

    std::array<LoadedPluginInfo, 8> pluginSlots;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;

    std::atomic<float> inputLevels[2]  { -100.0f, -100.0f };
    std::atomic<float> outputLevels[2] { -100.0f, -100.0f };
    std::atomic<float> masterVolume { 1.0f };
    std::atomic<float> inputGain    { 0.0f };
    std::atomic<float> editorZoomScale { 1.25f };
    std::atomic<int>   editorSkinIndex { 0 };
    juce::String abSnapshotA;
    juce::String abSnapshotB;
    bool activeABSlotIsA = true;

    juce::AudioParameterFloat* getMacroParameter(int index) const;
    float getMacroValue(int index) const;
    void setMacroValue(int index, float value);
    juce::String getMacroName(int index) const;
    void setMacroName(int index, const juce::String& name);
    const std::vector<MacroMapping>& getMacroMappings() const { return macroMappings; }
    void clearMacroMappings();
    void setMacroMappings(std::vector<MacroMapping> mappings);
    bool addMacroMapping(const MacroMapping& mapping);
    void removeMacroMapping(size_t index);
    void removeInvalidMacroMappings();
    void removeMacroMappingsForTargetPath(const juce::String& targetPath, bool includeChildren);
    void remapMacroTargetPaths(const juce::String& firstPath, const juce::String& secondPath, bool swapTargets);
    juce::String getMacroTargetPathForSplit(const ParallelSplitProcessor* split) const;
    juce::StringArray getMappableParametersForMasterSlot(int slotIndex) const;
    bool mapMacroToMasterSlotParameter(int macroIndex, int slotIndex, int parameterIndex);

    // --- PRESET MANAGER ---
    std::unique_ptr<PresetManager> presetManager;

private:
    friend class PresetManager;
    friend class ScopedProcessingSuspender;

    // Main audio graph for the master chain.
    std::unique_ptr<juce::AudioProcessorGraph> mainGraph;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::LinearSmoothedValue<float> inputGainLinear;
    juce::LinearSmoothedValue<float> masterVolumeLinear;
    std::array<juce::LinearSmoothedValue<float>, macroControlCount> macroSmoothedValues;

    // Create IO node e stato base del grafo principale.
    void initialiseGraph();
    // Update graph configuration when slot content changes.
    void reconfigureGraph();
    void updateGraphConnections();
    // Thread worker for plugin scanning without blocking the editor.
    void scanPluginsThread();
    // Load plugin metadata from disk.
    void loadPluginMetadata();
    void rebuildLoadablePluginCache();
    void closeAllPluginWindows();

    std::unique_ptr<std::thread> scanThread;
    mutable juce::SpinLock scanSpinLock;

    std::atomic<bool>  isScanningFlag    { false };
    std::atomic<float> scanProgress      { 0.0f  };
    std::atomic<bool>  shouldStopScanning{ false  };
    std::atomic<int>   scanNewPluginsFound{ 0     };
    std::atomic<bool>  scanClearCache    { false  };
    std::atomic<bool>  scanCompletedWithError { false };
    juce::String scanCurrentName;
    juce::String scanErrorMessage;
    juce::String scanRecoveryMessage;
    int currentFormatIndex = 0;

    bool defaultPresetLoaded = false;

    // DAW State received before prepareToPlay
    juce::MemoryBlock pendingStateData;

    // Prevent duplicate queue of callAsync for the restore of the DAW state.
    bool pendingStateApplyQueued = false;
    int graphMutationDepth = 0;
    juce::SpinLock graphMutationLock;
    std::array<juce::AudioParameterFloat*, macroControlCount> macroParameters {};
    juce::AudioParameterFloat* inputGainParameter = nullptr;
    juce::AudioParameterFloat* masterVolumeParameter = nullptr;
    std::array<juce::AudioParameterBool*, 8> masterSlotBypassParameters {};
    std::array<juce::String, macroControlCount> macroNames {};
    std::vector<MacroMapping> macroMappings;
    juce::String scanFolderOverride;
    juce::Array<juce::PluginDescription> loadablePluginCache;
    bool loadablePluginCacheDirty = true;
    bool loadablePluginCacheInitialized = false;

    void applyMacroMappings();
    juce::AudioProcessor* resolveMacroTarget(const MacroMapping& mapping) const;
    void setSlotBypassedInternal(int slotIndex, bool bypassed, bool notifyHost);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
