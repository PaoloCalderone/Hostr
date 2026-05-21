#ifndef PARALLEL_SPLIT_PROCESSOR_CPP_INCLUDED
#define PARALLEL_SPLIT_PROCESSOR_CPP_INCLUDED

#include "ParallelSplitProcessor.h"
#include "PluginSearchDialog.h"

static constexpr float kParallelProcessorGainMinDb = -100.0f;
static constexpr float kParallelProcessorGainMaxDb =   24.0f;
static constexpr float kParallelProcessorMinusInfThresholdDb = -99.9f;

static float clampParallelProcessorKnobDb(float value)
{
    return juce::jlimit(kParallelProcessorGainMinDb, kParallelProcessorGainMaxDb, value);
}

static float parallelProcessorKnobDbToGain(float db)
{
    return db <= kParallelProcessorMinusInfThresholdDb ? 0.0f : juce::Decibels::decibelsToGain(db);
}

static float calculateParallelBlockPeakDb(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return -100.0f;

    const auto* data = buffer.getReadPointer(channel);
    if (data == nullptr)
        return -100.0f;

    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        peak = juce::jmax(peak, std::abs(data[i]));

    return juce::jlimit(-100.0f, 12.0f,
        juce::Decibels::gainToDecibels(peak, -100.0f));
}

static void destroyParallelHostedPluginWindow(std::unique_ptr<juce::DocumentWindow>& window)
{
    if (window == nullptr)
        return;

    window->setVisible(false);
    PluginSearchDialog::dismissActive();
    window->clearContentComponent();
    window.reset();
}

// ==============================================================================
// This file implements the DSP and runtime of the Parallel Split block:
// - processing of the chains
// - final summation
// - latency compensation
// - plugin management in the parallel chains
// ==============================================================================

class ParallelPluginWindow : public juce::DocumentWindow,
                             private juce::ComponentListener
{
public:
    ParallelPluginWindow(const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(ResizableWindow::backgroundColourId),
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setAlwaysOnTop(true);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(26);
        hostedEditor = editor;
        editorSupportsResize = editor->isResizable();
        editor->addComponentListener(this);
        setContentOwned(editor, true);
        const int editorWidth  = juce::jmax(1, editor->getWidth());
        const int editorHeight = juce::jmax(1, editor->getHeight());
        setResizable(editorSupportsResize, editorSupportsResize);
        setFullScreen(false);
        setContentComponentSize(editorWidth, editorHeight);
        updateResizeLimitsForCurrentEditorSize(editorWidth, editorHeight);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        toFront(true);
    }

    ~ParallelPluginWindow() override
    {
        if (auto* editor = hostedEditor.getComponent())
            editor->removeComponentListener(this);
    }

    void closeButtonPressed() override
    {
        PluginSearchDialog::dismissActive();
        setVisible(false);
    }

private:
    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override
    {
        if (wasResized && &component == hostedEditor.getComponent())
            syncWindowToEditorSize();
    }

    void childBoundsChanged(juce::Component* child) override
    {
        if (child == hostedEditor.getComponent())
        {
            syncWindowToEditorSize();
            return;
        }

        juce::ResizableWindow::childBoundsChanged(child);
    }

    void syncWindowToEditorSize()
    {
        if (isSyncingEditorSize)
            return;

        if (auto* editor = hostedEditor.getComponent())
        {
            const int editorWidth  = juce::jmax(1, editor->getWidth());
            const int editorHeight = juce::jmax(1, editor->getHeight());

            const juce::ScopedValueSetter<bool> syncing(isSyncingEditorSize, true);
            if (! editorSupportsResize)
                setResizeLimits(1, 1, 8192, 8192);

            setContentComponentSize(editorWidth, editorHeight);
            updateResizeLimitsForCurrentEditorSize(editorWidth, editorHeight);
        }
    }

    void updateResizeLimitsForCurrentEditorSize(int editorWidth, int editorHeight)
    {
        if (editorSupportsResize)
        {
            const double aspect = editorHeight > 0 ? (double)editorWidth / (double)editorHeight : 1.0;
            if (auto* constrainer = getConstrainer())
                constrainer->setFixedAspectRatio(aspect);

            setResizeLimits(juce::jmax(80, editorWidth / 3),
                            juce::jmax(60, editorHeight / 3),
                            8192, 8192);
        }
        else
        {
            setResizeLimits(getWidth(), getHeight(), getWidth(), getHeight());
        }
    }

    juce::Component::SafePointer<juce::AudioProcessorEditor> hostedEditor;
    bool editorSupportsResize = false;
    bool isSyncingEditorSize = false;
};

static void connectParallelGraphAudioNodes(juce::AudioProcessorGraph& graph,
                                           juce::AudioProcessorGraph::Node& src,
                                           juce::AudioProcessorGraph::Node& dst)
{
    const auto srcChannels = juce::jmax(1, src.getProcessor()->getTotalNumOutputChannels());
    const auto dstChannels = juce::jmax(1, dst.getProcessor()->getTotalNumInputChannels());

    if (srcChannels >= 2 && dstChannels == 1)
    {
        graph.addConnection({ { src.nodeID, 0 }, { dst.nodeID, 0 } });
        return;
    }

    if (srcChannels == 1 && dstChannels >= 2)
    {
        graph.addConnection({ { src.nodeID, 0 }, { dst.nodeID, 0 } });
        graph.addConnection({ { src.nodeID, 0 }, { dst.nodeID, 1 } });
        return;
    }

    for (int ch = 0; ch < juce::jmin(srcChannels, dstChannels, 2); ++ch)
        graph.addConnection({ { src.nodeID, ch }, { dst.nodeID, ch } });
}

class NestedParallelSplitGraphProcessor : public juce::AudioProcessor
{
public:
    explicit NestedParallelSplitGraphProcessor(ParallelSplitProcessor* split)
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          splitProcessor(split) {}

    const juce::String getName() const override { return "Parallel Split"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        if (splitProcessor != nullptr)
            splitProcessor->prepareToPlay(sampleRate, samplesPerBlock);
    }
    void releaseResources() override
    {
        if (splitProcessor != nullptr)
            splitProcessor->releaseResources();
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        if (splitProcessor != nullptr)
            splitProcessor->processBlock(buffer, midiMessages);
    }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    ParallelSplitProcessor* splitProcessor = nullptr;
};

ParallelSplitProcessor::ParallelSplitProcessor()
{
    ensureChainCount(1);
    chains[0].muted = false;
}

ParallelSplitProcessor::~ParallelSplitProcessor()
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        closeAllPluginWindows();
    }
    else
    {
        juce::MessageManagerLock mmLock;
        if (mmLock.lockWasGained())
            closeAllPluginWindows();
    }

    // Release while nested split owners are still alive. The chain graphs may call NestedParallelSplitGraphProcessor::releaseResources()
    // Which needs those raw split pointers to remain valid.
    releaseResources();

    for (int c = 0; c < getNumChains(); ++c)
        if (chains[(size_t)c].chainGraph)
            chains[(size_t)c].chainGraph->clear();

    for (int c = 0; c < getNumChains(); ++c)
        for (auto& s : chains[(size_t)c].slots)
        {
            destroyParallelHostedPluginWindow(s.editorWindow);
            s.node = nullptr;
            s.parallelProcessor = nullptr;
            s.parallelSplitOwner.reset();
        }

    for (int c = 0; c < getNumChains(); ++c)
        chains[(size_t)c].chainGraph.reset();
}

ParallelSplitProcessor::ParallelChainData* ParallelSplitProcessor::addChain()
{
    if (!canAddChain()) return nullptr;
    ensureChainCount(getNumChains() + 1);
    return &chains.back();
}

void ParallelSplitProcessor::removeChain(int index)
{
    if (!canRemoveChain(index)) return;

    auto clearChain = [](ParallelChainData& chain)
    {
        for (auto& slot : chain.slots)
        {
            if (slot.parallelProcessor != nullptr)
                slot.parallelProcessor->closeAllPluginWindows();
            destroyParallelHostedPluginWindow(slot.editorWindow);
        }

        if (chain.chainGraph)
        {
            chain.chainGraph->releaseResources();
            chain.chainGraph->clear();
        }

        for (auto& slot : chain.slots)
        {
            slot.node = nullptr;
            slot.parallelProcessor = nullptr;
            slot.parallelSplitOwner.reset();
            slot.name.clear();
            slot.pluginFormat.clear();
            slot.fileOrIdentifier.clear();
            slot.uniqueId = 0;
            slot.valid = false;
            slot.bypassed = false;
            slot.type = ChainSlotType::Empty;
        }

        chain.chainGraph.reset();
        chain.inputNode = nullptr;
        chain.outputNode = nullptr;
        chain.name.clear();
        chain.inputGainDb = 0.0f;
        chain.outputVolDb = 0.0f;
        chain.muted = true;
        chain.solo = false;
        chain.outLevelL = -100.0f;
        chain.outLevelR = -100.0f;
        chain.inLevelL = -100.0f;
        chain.inLevelR = -100.0f;
        chain.level = -100.0f;
        chain.inputGainLinear.setCurrentAndTargetValue(1.0f);
        chain.outputGainLinear.setCurrentAndTargetValue(1.0f);
    };

    clearChain(chains[(size_t)index]);
    chains.erase(chains.begin() + index);

    for (int i = 0; i < getNumChains(); ++i)
    {
        if (chains[(size_t)i].name.isEmpty())
            chains[(size_t)i].name = juce::String(i + 1);
        initializeChain(chains[(size_t)i]);
    }

    updateLatencyCompensation();
}

int ParallelSplitProcessor::getTotalParallelSplitCount() const
{
    int total = 1;
    for (int c = 0; c < getNumChains(); ++c)
        for (const auto& slot : chains[(size_t)c].slots)
            if (slot.valid
                && slot.type == ChainSlotType::ParallelSplit
                && slot.parallelProcessor)
                total += slot.parallelProcessor->getTotalParallelSplitCount();
    return total;
}

void ParallelSplitProcessor::ensureChainCount(int count)
{
    count = juce::jlimit(1, maxChains, count);
    while (getNumChains() < count)
        chains.emplace_back();

    delayBuffers.resize((size_t)getNumChains());
    delayPositions.resize((size_t)getNumChains(), 0);
    delayLengths.resize((size_t)getNumChains(), 0);

    for (int i = 0; i < getNumChains(); ++i)
    {
        auto& chain = chains[(size_t)i];
        if (chain.name.isEmpty())
            chain.name = juce::String(i + 1);
        chain.inputGainLinear.setCurrentAndTargetValue(
            parallelProcessorKnobDbToGain(chain.inputGainDb));
        chain.outputGainLinear.setCurrentAndTargetValue(
            parallelProcessorKnobDbToGain(chain.outputVolDb));
        initializeChain(chain);
        if (currentSampleRate > 0.0 && currentBlockSize > 0)
        {
            chain.chainGraph->setPlayConfigDetails(2, 2, currentSampleRate, currentBlockSize);
            chain.chainGraph->prepareToPlay(currentSampleRate, currentBlockSize);
        }
    }
}

void ParallelSplitProcessor::initializeChain(ParallelChainData& chain)
{
    if (!chain.chainGraph)
        chain.chainGraph = std::make_unique<juce::AudioProcessorGraph>();

    if (!chain.inputNode)
    {
        chain.inputNode = chain.chainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    }

    if (!chain.outputNode)
    {
        chain.outputNode = chain.chainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    }

    // Connect input → output immediately: audio passes even without plugins loaded
    updateChainConnections(chain);
}

void ParallelSplitProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    auto prepareSmoothing = [sampleRate](ParallelChainData& chain)
    {
        chain.inputGainDb = clampParallelProcessorKnobDb(chain.inputGainDb);
        chain.outputVolDb = clampParallelProcessorKnobDb(chain.outputVolDb);
        chain.inputGainLinear.reset(sampleRate, 0.02);
        chain.outputGainLinear.reset(sampleRate, 0.02);
        chain.inputGainLinear.setCurrentAndTargetValue(
            parallelProcessorKnobDbToGain(chain.inputGainDb));
        chain.outputGainLinear.setCurrentAndTargetValue(
            parallelProcessorKnobDbToGain(chain.outputVolDb));
    };

    for (int i = 0; i < getNumChains(); ++i)
    {
        auto& chain = chains[(size_t)i];
        prepareSmoothing(chain);
        initializeChain(chain);
        chain.chainGraph->setPlayConfigDetails(2, 2, sampleRate, samplesPerBlock);
        chain.chainGraph->prepareToPlay(sampleRate, samplesPerBlock);
    }

    updateLatencyCompensation();
}

void ParallelSplitProcessor::releaseResources()
{
    for (int i = 0; i < getNumChains(); ++i)
        if (chains[(size_t)i].chainGraph)
            chains[(size_t)i].chainGraph->releaseResources();

    for (auto& b : delayBuffers)
        b.setSize(0, 0);
}

// ==============================================================================
// Connections — always maintains input→output even if the chain is empty
// ==============================================================================

void ParallelSplitProcessor::updateChainConnections(ParallelChainData& chain)
{
    if (!chain.chainGraph) return;

    for (auto& c : chain.chainGraph->getConnections())
        chain.chainGraph->removeConnection(c);

    juce::AudioProcessorGraph::Node::Ptr last = chain.inputNode;
    for (int i = 0; i < 8; ++i)
    {
        auto& sl = chain.slots[i];
        if (!sl.valid || !sl.node || sl.bypassed) continue;
        connectParallelGraphAudioNodes(*chain.chainGraph, *last, *sl.node);
        last = sl.node;
    }
    // SEMPRE connette l'ultimo nodo valido all'output
    connectParallelGraphAudioNodes(*chain.chainGraph, *last, *chain.outputNode);

    if (currentSampleRate > 0)
        updateLatencyCompensation();
}

// ==============================================================================
// Latency compensation
// ==============================================================================

int ParallelSplitProcessor::getChainLatency(const ParallelChainData& chain) const
{
    int total = 0;
    for (int i = 0; i < 8; ++i)
    {
        const auto& sl = chain.slots[i];
        if (!sl.valid || !sl.node || sl.bypassed) continue;
        if (sl.type == ChainSlotType::ParallelSplit && sl.parallelProcessor)
        {
            int nestedMax = 0;
            for (int c = 0; c < sl.parallelProcessor->getNumChains(); ++c)
                nestedMax = juce::jmax(nestedMax,
                    sl.parallelProcessor->getChainLatency(sl.parallelProcessor->getChain(c)));
            total += nestedMax;
        }
        else if (auto* proc = sl.node->getProcessor())
            total += proc->getLatencySamples();
    }
    return total;
}

void ParallelSplitProcessor::updateLatencyCompensation()
{
    int maxLatency = 0;
    for (int i = 0; i < getNumChains(); ++i)
        maxLatency = juce::jmax(maxLatency, getChainLatency(chains[(size_t)i]));

    int channels = 2;

    auto allocDelay = [&](juce::AudioBuffer<float>& buf, int& pos, int len)
    {
        if (len > 0)
        {
            int sz = len + currentBlockSize + 1;
            buf.setSize(channels, sz, false, true, true);
            buf.clear();
            pos = 0;
        }
        else
        {
            buf.setSize(0, 0);
            pos = 0;
        }
    };

    delayBuffers.resize((size_t)getNumChains());
    delayPositions.resize((size_t)getNumChains(), 0);
    delayLengths.resize((size_t)getNumChains(), 0);

    for (int i = 0; i < getNumChains(); ++i)
    {
        delayLengths[(size_t)i] = maxLatency - getChainLatency(chains[(size_t)i]);
        allocDelay(delayBuffers[(size_t)i], delayPositions[(size_t)i], delayLengths[(size_t)i]);
    }
}

void ParallelSplitProcessor::applyDelay(juce::AudioBuffer<float>& buf,
                                         juce::AudioBuffer<float>& delayBuf,
                                         int& pos, int len)
{
    if (len <= 0 || delayBuf.getNumSamples() == 0) return;

    int numCh  = juce::jmin(buf.getNumChannels(), delayBuf.getNumChannels());
    int numSmp = buf.getNumSamples();
    int bufLen = delayBuf.getNumSamples();

    juce::AudioBuffer<float> tmp(numCh, numSmp);

    for (int c = 0; c < numCh; ++c)
    {
        const float* in  = buf.getReadPointer(c);
        float*       db  = delayBuf.getWritePointer(c);
        float*       out = tmp.getWritePointer(c);

        int rp = (pos - len + bufLen) % bufLen;
        int wp = pos;
        for (int i = 0; i < numSmp; ++i)
        {
            out[i]   = db[rp];
            db[wp]   = in[i];
            rp = (rp + 1) % bufLen;
            wp = (wp + 1) % bufLen;
        }
    }
    pos = (pos + numSmp) % bufLen;

    for (int c = 0; c < numCh; ++c)
        buf.copyFrom(c, 0, tmp, c, 0, numSmp);
}

// ==============================================================================
// Plugin management
// Loading, removal, GUI and reordering of slots in a parallel chain.
// ==============================================================================

void ParallelSplitProcessor::loadPluginInChain(ParallelChainData& chain, int slot,
                                               std::unique_ptr<juce::AudioProcessor> proc,
                                               const juce::String& name,
                                               const juce::String& format,
                                               const juce::String& fileOrIdentifier,
                                               int uniqueId,
                                               bool openEditor)
{
    if (slot < 0 || slot >= 8 || !chain.chainGraph) return;

    if (proc)
    {
        proc->enableAllBuses();
    }

    auto& sl = chain.slots[slot];
    if (sl.valid)
        removePluginFromChain(chain, slot);

    sl.node = chain.chainGraph->addNode(std::move(proc));
    sl.name = name;
    sl.pluginFormat = format;
    sl.fileOrIdentifier = fileOrIdentifier;
    sl.uniqueId = uniqueId;
    sl.valid = true;
    sl.bypassed = false;
    sl.type = ChainSlotType::Plugin;
    sl.parallelProcessor = nullptr;
    sl.parallelSplitOwner.reset();
    updateChainConnections(chain);

    if (chain.chainGraph && currentSampleRate > 0.0 && currentBlockSize > 0)
    {
        chain.chainGraph->setPlayConfigDetails(2, 2, currentSampleRate, currentBlockSize);
        chain.chainGraph->prepareToPlay(currentSampleRate, currentBlockSize);
    }

    if (openEditor)
        showPluginGUI(chain, slot);
}

void ParallelSplitProcessor::removePluginFromChain(ParallelChainData& chain, int slot)
{
    if (slot < 0 || slot >= 8 || !chain.chainGraph) return;
    auto& sl = chain.slots[slot];

    if (sl.parallelProcessor != nullptr)
        sl.parallelProcessor->closeAllPluginWindows();

    // Reset editor window BEFORE removing node to prevent dangling pointer issues
    destroyParallelHostedPluginWindow(sl.editorWindow);

    if (sl.valid)
    {
        if (sl.node)
            chain.chainGraph->removeNode(sl.node->nodeID);

        // Thoroughly reset slot state after removal
        sl.node = nullptr;
        sl.valid = false;
        sl.bypassed = false;
        sl.type = ChainSlotType::Empty;
        sl.parallelProcessor = nullptr;
        sl.parallelSplitOwner.reset();
        sl.name.clear();
        sl.pluginFormat.clear();
        sl.fileOrIdentifier.clear();
        sl.uniqueId = 0;
    }

    updateChainConnections(chain);
}

void ParallelSplitProcessor::closeAllPluginWindows()
{
    for (auto& chain : chains)
    {
        for (auto& slot : chain.slots)
        {
            if (slot.parallelProcessor != nullptr)
                slot.parallelProcessor->closeAllPluginWindows();
            destroyParallelHostedPluginWindow(slot.editorWindow);
        }
    }
}

void ParallelSplitProcessor::createParallelSplitInChain(ParallelChainData& chain, int slot)
{
    if (slot < 0 || slot >= 8 || !chain.chainGraph) return;
    int chainSplitCount = 0;
    for (const auto& slotInfo : chain.slots)
        if (slotInfo.valid && slotInfo.type == ChainSlotType::ParallelSplit)
            ++chainSplitCount;
    if (chainSplitCount >= maxChains
        && chain.slots[(size_t)slot].type != ChainSlotType::ParallelSplit)
        return;

    if (chain.slots[slot].valid)
        removePluginFromChain(chain, slot);

    auto split = std::make_unique<ParallelSplitProcessor>();
    auto* raw = split.get();
    auto node = chain.chainGraph->addNode(std::make_unique<NestedParallelSplitGraphProcessor>(raw));

    if (currentSampleRate > 0.0 && currentBlockSize > 0)
        raw->prepareToPlay(currentSampleRate, currentBlockSize);

    auto& sl = chain.slots[slot];
    sl.node = node;
    sl.name = "Parallel Split";
    sl.pluginFormat.clear();
    sl.fileOrIdentifier.clear();
    sl.uniqueId = 0;
    sl.valid = true;
    sl.bypassed = false;
    sl.type = ChainSlotType::ParallelSplit;
    sl.parallelProcessor = raw;
    sl.parallelSplitOwner = std::move(split);

    updateChainConnections(chain);
}

void ParallelSplitProcessor::setSlotBypassed(ParallelChainData& chain, int slot, bool bypass)
{
    if (slot < 0 || slot >= 8) return;
    chain.slots[slot].bypassed = bypass;
    updateChainConnections(chain);
}

void ParallelSplitProcessor::showPluginGUI(ParallelChainData& chain, int slot)
{
    PluginSearchDialog::dismissActive();
    if (slot < 0 || slot >= 8) return;
    auto& sl = chain.slots[slot];
    if (!sl.valid || !sl.node || sl.type == ChainSlotType::ParallelSplit) return;
    auto* proc = sl.node->getProcessor();
    if (!proc || !proc->hasEditor()) return;
    if (sl.editorWindow)
    {
        if (sl.editorWindow->isVisible())
        {
            sl.editorWindow->setVisible(false);
        }
        else
        {
            sl.editorWindow->setMinimised(false);
            sl.editorWindow->setAlwaysOnTop(true);
            sl.editorWindow->setVisible(true);
            sl.editorWindow->toFront(true);
        }
        return;
    }
    if (auto* ed = proc->createEditor())
        sl.editorWindow = std::make_unique<ParallelPluginWindow>(sl.name, ed);
}

void ParallelSplitProcessor::moveOrSwapInChain(ParallelChainData& chain, int src, int dst)
{
    if (src < 0 || src >= 8 || dst < 0 || dst >= 8 || src == dst) return;
    if (!chain.slots[src].valid) return;
    if (chain.slots[dst].valid)
    {
        std::swap(chain.slots[src].node,             chain.slots[dst].node);
        std::swap(chain.slots[src].name,             chain.slots[dst].name);
        std::swap(chain.slots[src].pluginFormat,     chain.slots[dst].pluginFormat);
        std::swap(chain.slots[src].fileOrIdentifier, chain.slots[dst].fileOrIdentifier);
        std::swap(chain.slots[src].uniqueId,         chain.slots[dst].uniqueId);
        std::swap(chain.slots[src].valid,            chain.slots[dst].valid);
        std::swap(chain.slots[src].bypassed,         chain.slots[dst].bypassed);
        std::swap(chain.slots[src].editorWindow,     chain.slots[dst].editorWindow);
        std::swap(chain.slots[src].type,             chain.slots[dst].type);
        std::swap(chain.slots[src].parallelProcessor, chain.slots[dst].parallelProcessor);
        std::swap(chain.slots[src].parallelSplitOwner, chain.slots[dst].parallelSplitOwner);
    }
    else
    {
        chain.slots[dst] = std::move(chain.slots[src]);
        chain.slots[src].node = nullptr;
        chain.slots[src].name.clear();
        chain.slots[src].pluginFormat.clear();
        chain.slots[src].fileOrIdentifier.clear();
        chain.slots[src].uniqueId = 0;
        chain.slots[src].valid = false;
        chain.slots[src].bypassed = false;
        chain.slots[src].type = ChainSlotType::Empty;
        chain.slots[src].parallelProcessor = nullptr;
        chain.slots[src].parallelSplitOwner.reset();
        chain.slots[src].editorWindow.reset();
    }
    updateChainConnections(chain);
}

// ==============================================================================
// Level METERS and block peak CALCULATION for the parallel chains
// ==============================================================================

void ParallelSplitProcessor::calculateLevel(ParallelChainData& chain,
                                            const juce::AudioBuffer<float>& buf)
{
    if (buf.getNumChannels() <= 0 || buf.getNumSamples() <= 0)
    {
        chain.level = chain.level * 0.85f + (-100.0f) * 0.15f;
        return;
    }

    float peak = 0.0f;
    const float* d = buf.getReadPointer(0);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        peak = juce::jmax(peak, std::abs(d[i]));
    const float db = juce::jlimit(-100.0f, 12.0f,
        juce::Decibels::gainToDecibels(peak, -100.0f));
    chain.level = chain.level * 0.70f + db * 0.30f;
}

void ParallelSplitProcessor::calculateOutputLevels(ParallelChainData& chain,
                                                   const juce::AudioBuffer<float>& buf)
{
    const int numChannels = juce::jmin(buf.getNumChannels(), 2);

    if (numChannels <= 0 || buf.getNumSamples() <= 0)
    {
        chain.outLevelL = -100.0f;
        chain.outLevelR = -100.0f;
        return;
    }

    for (int c = 0; c < numChannels; ++c)
    {
        const float db = calculateParallelBlockPeakDb(buf, c);
        if (c == 0) chain.outLevelL = db;
        else        chain.outLevelR = db;
    }
    if (buf.getNumChannels() == 1) chain.outLevelR = chain.outLevelL;
}

void ParallelSplitProcessor::calculateInputLevels(ParallelChainData& chain,
                                                  const juce::AudioBuffer<float>& buf)
{
    const int numChannels = juce::jmin(buf.getNumChannels(), 2);

    if (numChannels <= 0 || buf.getNumSamples() <= 0)
    {
        chain.inLevelL = -100.0f;
        chain.inLevelR = -100.0f;
        return;
    }

    for (int c = 0; c < numChannels; ++c)
    {
        const float db = calculateParallelBlockPeakDb(buf, c);
        if (c == 0) chain.inLevelL = db;
        else        chain.inLevelR = db;
    }

    if (buf.getNumChannels() == 1)
        chain.inLevelR = chain.inLevelL;
}

// ==============================================================================
// EXECUTE the processing of the two branches and generate the final output of the Parallel Split.
// ==============================================================================

void ParallelSplitProcessor::processChainFull(ParallelChainData& chain,
                                              const juce::AudioBuffer<float>& inputBuf,
                                              juce::AudioBuffer<float>& outputBuf,
                                              juce::MidiBuffer& midi)
{
    int ch = inputBuf.getNumChannels(), smp = inputBuf.getNumSamples();
    outputBuf.setSize(ch, smp, false, true, true);
    outputBuf.clear();

    if (ch <= 0 || smp <= 0)
    {
        calculateInputLevels(chain, outputBuf);
        calculateLevel(chain, outputBuf);
        calculateOutputLevels(chain, outputBuf);
        return;
    }

    if (chain.muted)
    {
        chain.inLevelL  = chain.inLevelL  * 0.85f + (-100.0f) * 0.15f;
        chain.inLevelR  = chain.inLevelR  * 0.85f + (-100.0f) * 0.15f;
        chain.level     = chain.level * 0.85f + (-100.0f) * 0.15f;
        chain.outLevelL = chain.outLevelL * 0.85f + (-100.0f) * 0.15f;
        chain.outLevelR = chain.outLevelR * 0.85f + (-100.0f) * 0.15f;
        return;
    }

    juce::AudioBuffer<float> work(ch, smp);
    for (int c = 0; c < ch; ++c) work.copyFrom(c, 0, inputBuf, c, 0, smp);
    calculateInputLevels(chain, work);

    const auto inputDb = clampParallelProcessorKnobDb(chain.inputGainDb.load());
    chain.inputGainDb.store(inputDb);
    chain.inputGainLinear.setTargetValue(parallelProcessorKnobDbToGain(inputDb));
    const float inputGainStart = chain.inputGainLinear.getCurrentValue();
    const float inputGainEnd   = chain.inputGainLinear.skip(smp);
    for (int c = 0; c < ch; ++c)
        work.applyGainRamp(c, 0, smp, inputGainStart, inputGainEnd);

    if (chain.chainGraph) chain.chainGraph->processBlock(work, midi);
    calculateLevel(chain, work);

    const auto outputDb = clampParallelProcessorKnobDb(chain.outputVolDb.load());
    chain.outputVolDb.store(outputDb);
    chain.outputGainLinear.setTargetValue(parallelProcessorKnobDbToGain(outputDb));
    const float outputGainStart = chain.outputGainLinear.getCurrentValue();
    const float outputGainEnd   = chain.outputGainLinear.skip(smp);
    for (int c = 0; c < ch; ++c)
        work.applyGainRamp(c, 0, smp, outputGainStart, outputGainEnd);

    calculateOutputLevels(chain, work);

    for (int c = 0; c < ch; ++c) outputBuf.copyFrom(c, 0, work, c, 0, smp);
}

void ParallelSplitProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    int ch = buffer.getNumChannels(), smp = buffer.getNumSamples();

    if (ch <= 0 || smp <= 0)
    {
        for (int i = 0; i < getNumChains(); ++i)
        {
            calculateLevel(chains[(size_t)i], buffer);
            calculateOutputLevels(chains[(size_t)i], buffer);
        }
        return;
    }

    juce::AudioBuffer<float> snap(ch, smp);
    for (int c = 0; c < ch; ++c) snap.copyFrom(c, 0, buffer, c, 0, smp);

    std::vector<juce::AudioBuffer<float>> chainOut((size_t)getNumChains());
    bool anySolo = false;
    int activeCount = 0;
    for (int i = 0; i < getNumChains(); ++i)
    {
        auto& chain = chains[(size_t)i];
        processChainFull(chain, snap, chainOut[(size_t)i], midiMessages);
        applyDelay(chainOut[(size_t)i], delayBuffers[(size_t)i],
                   delayPositions[(size_t)i], delayLengths[(size_t)i]);
        if (chain.solo && !chain.muted) anySolo = true;
        if (!chain.muted) ++activeCount;
    }

    buffer.clear();

    if (anySolo)
    {
        for (int i = 0; i < getNumChains(); ++i)
            if (chains[(size_t)i].solo && !chains[(size_t)i].muted)
                for (int c = 0; c < ch; ++c)
                    buffer.addFrom(c, 0, chainOut[(size_t)i], c, 0, smp);
    }
    else
    {
        if (activeCount == 0)
        {
            for (int c = 0; c < ch; ++c)
                buffer.copyFrom(c, 0, snap, c, 0, smp);
            return;
        }

        for (int i = 0; i < getNumChains(); ++i)
            if (!chains[(size_t)i].muted)
                for (int c = 0; c < ch; ++c)
                    buffer.addFrom(c, 0, chainOut[(size_t)i], c, 0, smp);
    }
}

#endif // PARALLEL_SPLIT_PROCESSOR_CPP_INCLUDED
