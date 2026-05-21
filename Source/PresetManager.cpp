#ifndef PRESET_MANAGER_CPP_INCLUDED
#define PRESET_MANAGER_CPP_INCLUDED

#include "PresetManager.h"
#include "PluginProcessor.h"
#include "ParallelSplitProcessor.h"

static constexpr float kPresetGainMinDb = -100.0f;
static constexpr float kPresetGainMaxDb =   24.0f;

static float clampPresetKnobDb(float value)
{
    return juce::jlimit(kPresetGainMinDb, kPresetGainMaxDb, value);
}

static juce::String normalisePresetPluginKey(juce::String text)
{
    return text.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
               .toLowerCase();
}

// ==============================================================================
// This file implements full host state serialization:
// - Master chain
// - Parallel Split blocks
// - Binary state of hosted plugins
// - User presets and default presets
// ==============================================================================

// Constructor
PresetManager::PresetManager(PluginProcessor& p) : processor(p) {}

juce::String PresetManager::serializePluginState(juce::AudioProcessor* proc)
{
    if (!proc) return {};
    juce::MemoryBlock mb;
    try
    {
        proc->getStateInformation(mb);
    }
    catch (...)
    {
        juce::Logger::writeToLog("PresetManager: getStateInformation fallito per " + proc->getName());
    }
    if (mb.getSize() == 0) return {};
    return mb.toBase64Encoding();
}

static juce::String serializePluginProgramState(juce::AudioProcessor* proc)
{
    if (!proc) return {};
    juce::MemoryBlock mb;
    try
    {
        proc->getCurrentProgramStateInformation(mb);
    }
    catch (...)
    {
        juce::Logger::writeToLog("PresetManager: getCurrentProgramStateInformation fallito per " + proc->getName());
    }

    if (mb.getSize() == 0) return {};
    return mb.toBase64Encoding();
}

bool PresetManager::applyPluginState(juce::AudioProcessor* proc,
                                      const juce::String& base64State)
{
    if (!proc || base64State.isEmpty()) return false;
    juce::MemoryBlock mb;
    if (!mb.fromBase64Encoding(base64State)) return false;
    try
    {
        proc->setStateInformation(mb.getData(), (int)mb.getSize());
    }
    catch (...)
    {
        juce::Logger::writeToLog("PresetManager: setStateInformation fallito per " + proc->getName());
        return false;
    }
    return true;
}

static bool applyPluginProgramState(juce::AudioProcessor* proc,
                                    const juce::String& base64State)
{
    if (!proc || base64State.isEmpty()) return false;
    juce::MemoryBlock mb;
    if (!mb.fromBase64Encoding(base64State)) return false;
    try
    {
        proc->setCurrentProgramStateInformation(mb.getData(), (int)mb.getSize());
    }
    catch (...)
    {
        juce::Logger::writeToLog("PresetManager: setCurrentProgramStateInformation fallito per " + proc->getName());
        return false;
    }
    return true;
}

// ==============================================================================
// Paths
// ==============================================================================

juce::File PresetManager::getPresetsFolder()
{
    juce::File folder = juce::File::getSpecialLocation(
                            juce::File::userApplicationDataDirectory)
                        .getChildFile("Hostr")
                        .getChildFile("Presets");
    folder.createDirectory();
    return folder;
}

juce::File PresetManager::getDefaultPresetFile()
{
    juce::File dir = juce::File::getSpecialLocation(
                         juce::File::userApplicationDataDirectory)
                     .getChildFile("Hostr");
    dir.createDirectory();
    return dir.getChildFile("default.hostrpreset");
}

// ==============================================================================
// Serialization — builds the complete XML preset
// ==============================================================================

juce::XmlElement* PresetManager::createPresetXml(const juce::String& presetName) const
{
    auto* root = new juce::XmlElement("HostrPreset");
    root->setAttribute("name", presetName);
    root->setAttribute("version", 2);
    root->setAttribute("product", PluginProcessor::productName);
    root->setAttribute("presetFormat", "hostrpreset");

    auto* product = root->createNewChildElement("Product");
    product->setAttribute("name", PluginProcessor::productName);
    product->setAttribute("slogan", PluginProcessor::productSlogan);
    product->setAttribute("presetPromise", PluginProcessor::productPresetPromise);
    product->setAttribute("cloud", "none");
    product->setAttribute("accountRequired", 0);
    product->setAttribute("ecosystemLockIn", 0);

    // Master section
    auto* master = root->createNewChildElement("MasterSection");
    master->setAttribute("inputGainDb",    (double)clampPresetKnobDb(processor.inputGain.load()));
    master->setAttribute("masterVolumeDb", (double)clampPresetKnobDb(
        juce::Decibels::gainToDecibels(processor.masterVolume.load(), kPresetGainMinDb)));
    master->setAttribute("editorZoom",     (double)processor.getEditorZoomScale());

    auto* macrosEl = root->createNewChildElement("MacroControls");
    for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
    {
        auto* macroEl = macrosEl->createNewChildElement("Macro");
        macroEl->setAttribute("index", i);
        macroEl->setAttribute("name", processor.getMacroName(i));
        macroEl->setAttribute("value", (double)processor.getMacroValue(i));
    }

    auto* mappingsEl = root->createNewChildElement("MacroMappings");
    int mappingIndex = 0;
    for (const auto& mapping : processor.getMacroMappings())
    {
        auto* mappingEl = mappingsEl->createNewChildElement("Mapping");
        mappingEl->setAttribute("index", mappingIndex++);
        mappingEl->setAttribute("macroIndex", mapping.macroIndex);
        mappingEl->setAttribute("scope", mapping.scope == PluginProcessor::MacroTargetScope::MasterSlot
                                      ? "MasterSlot" : "ParallelSlot");
        mappingEl->setAttribute("masterSlot", mapping.masterSlot);
        mappingEl->setAttribute("chainIndex", mapping.chainIndex);
        mappingEl->setAttribute("parallelSlot", mapping.parallelSlot);
        mappingEl->setAttribute("parameterIndex", mapping.parameterIndex);
        mappingEl->setAttribute("targetPath", mapping.targetPath);
        mappingEl->setAttribute("pluginName", mapping.pluginName);
        mappingEl->setAttribute("parameterName", mapping.parameterName);
        mappingEl->setAttribute("targetMin", (double)mapping.targetMin);
        mappingEl->setAttribute("targetMax", (double)mapping.targetMax);
        mappingEl->setAttribute("inverted", mapping.inverted ? 1 : 0);
        mappingEl->setAttribute("enabled", mapping.enabled ? 1 : 0);
    }

    auto* manifestEl = root->createNewChildElement("PluginManifest");

    // Slots
    auto* slotsEl = root->createNewChildElement("Slots");

    for (int i = 0; i < 8; ++i)
    {
        const auto& slot = processor.pluginSlots[i];
        if (!slot.isValid) continue;

        auto* slotEl = slotsEl->createNewChildElement("Slot");
        slotEl->setAttribute("index",    i);
        slotEl->setAttribute("bypassed", slot.bypassed ? 1 : 0);

        if (slot.type == PluginProcessor::SlotType::Plugin)
        {
            slotEl->setAttribute("type", "Plugin");
            slotEl->setAttribute("pluginName", slot.name);
            juce::PluginDescription liveDesc;
            if (slot.node != nullptr)
                if (auto* instance = dynamic_cast<juce::AudioPluginInstance*>(slot.node->getProcessor()))
                    instance->fillInPluginDescription(liveDesc);
            const auto manufacturer = liveDesc.manufacturerName.isNotEmpty() ? liveDesc.manufacturerName : juce::String{};
            const auto category = liveDesc.category.isNotEmpty() ? liveDesc.category : juce::String{};

            auto* pluginEl = manifestEl->createNewChildElement("Plugin");
            pluginEl->setAttribute("location", "Master Slot " + juce::String(i + 1));
            pluginEl->setAttribute("name", slot.name);
            pluginEl->setAttribute("format", slot.pluginFormat);
            pluginEl->setAttribute("fileOrIdentifier", slot.fileOrIdentifier);
            pluginEl->setAttribute("uid", slot.uniqueId);
            pluginEl->setAttribute("manufacturer", manufacturer);
            pluginEl->setAttribute("category", category);

            if (slot.pluginFormat.isNotEmpty() || slot.fileOrIdentifier.isNotEmpty() || slot.uniqueId != 0)
            {
                slotEl->setAttribute("format",           slot.pluginFormat);
                slotEl->setAttribute("fileOrIdentifier", slot.fileOrIdentifier);
                slotEl->setAttribute("uid",              slot.uniqueId);
                slotEl->setAttribute("manufacturer",     manufacturer);
                slotEl->setAttribute("category",         category);

                if (slot.node != nullptr)
                {
                    juce::String state = serializePluginState(slot.node->getProcessor());
                    if (state.isNotEmpty())
                    {
                        auto* stateEl = slotEl->createNewChildElement("PluginState");
                        stateEl->addTextElement(state);
                    }

                    juce::String programState = serializePluginProgramState(slot.node->getProcessor());
                    if (programState.isNotEmpty())
                    {
                        auto* stateEl = slotEl->createNewChildElement("PluginProgramState");
                        stateEl->addTextElement(programState);
                    }
                }
            }
        }
        else if (slot.type == PluginProcessor::SlotType::ParallelSplit
                 && slot.parallelProcessor)
        {
            slotEl->setAttribute("type", "ParallelSplit");

            std::function<void(juce::XmlElement&, const ParallelSplitProcessor&)> serializeSplit;
            serializeSplit = [&](juce::XmlElement& parent, const ParallelSplitProcessor& split)
            {
                for (int c = 0; c < split.getNumChains(); ++c)
                {
                    const auto& chain = split.getChain(c);
                    auto* chainEl = parent.createNewChildElement("Chain");
                    chainEl->setAttribute("id",          c == 0 ? "chain0" : (c == 1 ? "chain1" : ("chain" + juce::String(c + 1))));
                    chainEl->setAttribute("index",       c);
                    chainEl->setAttribute("name",        chain.name);
                    chainEl->setAttribute("muted",       chain.muted  ? 1 : 0);
                    chainEl->setAttribute("solo",        chain.solo   ? 1 : 0);
                    chainEl->setAttribute("inputGainDb", (double)clampPresetKnobDb(chain.inputGainDb));
                    chainEl->setAttribute("outputVolDb", (double)clampPresetKnobDb(chain.outputVolDb));

                    for (int s = 0; s < 8; ++s)
                    {
                        const auto& cslot = chain.slots[s];
                        if (!cslot.valid) continue;

                        auto* cslotEl = chainEl->createNewChildElement("Slot");
                        cslotEl->setAttribute("index",      s);
                        cslotEl->setAttribute("bypassed",   cslot.bypassed ? 1 : 0);
                        cslotEl->setAttribute("pluginName", cslot.name);

                        if (cslot.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit
                            && cslot.parallelProcessor)
                        {
                            cslotEl->setAttribute("type", "ParallelSplit");
                            serializeSplit(*cslotEl, *cslot.parallelProcessor);
                            continue;
                        }

                        cslotEl->setAttribute("type", "Plugin");
                        juce::PluginDescription liveDesc;
                        if (cslot.node != nullptr)
                            if (auto* instance = dynamic_cast<juce::AudioPluginInstance*>(cslot.node->getProcessor()))
                                instance->fillInPluginDescription(liveDesc);
                        const auto manufacturer = liveDesc.manufacturerName.isNotEmpty() ? liveDesc.manufacturerName : juce::String{};
                        const auto category = liveDesc.category.isNotEmpty() ? liveDesc.category : juce::String{};
                        auto* pluginEl = manifestEl->createNewChildElement("Plugin");
                        pluginEl->setAttribute("location", "Master Slot " + juce::String(i + 1)
                                                   + " / Chain " + juce::String(c + 1)
                                                   + " / Slot " + juce::String(s + 1));
                        pluginEl->setAttribute("name", cslot.name);
                        pluginEl->setAttribute("format", cslot.pluginFormat);
                        pluginEl->setAttribute("fileOrIdentifier", cslot.fileOrIdentifier);
                        pluginEl->setAttribute("uid", cslot.uniqueId);
                        pluginEl->setAttribute("manufacturer", manufacturer);
                        pluginEl->setAttribute("category", category);

                        if (cslot.pluginFormat.isNotEmpty() || cslot.fileOrIdentifier.isNotEmpty() || cslot.uniqueId != 0)
                        {
                            cslotEl->setAttribute("format",           cslot.pluginFormat);
                            cslotEl->setAttribute("fileOrIdentifier", cslot.fileOrIdentifier);
                            cslotEl->setAttribute("uid",              cslot.uniqueId);
                            cslotEl->setAttribute("manufacturer",     manufacturer);
                            cslotEl->setAttribute("category",         category);

                            if (cslot.node != nullptr)
                            {
                                juce::String state = serializePluginState(cslot.node->getProcessor());
                                if (state.isNotEmpty())
                                {
                                    auto* stateEl = cslotEl->createNewChildElement("PluginState");
                                    stateEl->addTextElement(state);
                                }

                                juce::String programState = serializePluginProgramState(cslot.node->getProcessor());
                                if (programState.isNotEmpty())
                                {
                                    auto* stateEl = cslotEl->createNewChildElement("PluginProgramState");
                                    stateEl->addTextElement(programState);
                                }
                            }
                        }
                    }
                }
            };

            serializeSplit(*slotEl, *slot.parallelProcessor);
        }
    }

    return root;
}

// ==============================================================================
// De-serialization — apply the XML to the PluginProcessor
// ==============================================================================

bool PresetManager::applyPresetXml(const juce::XmlElement& xml)
{
    // Re-entrancy guard
    if (isApplyingPreset) return false;
    isApplyingPreset = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard { isApplyingPreset };

    if (xml.getTagName() != "HostrPreset")
    {
        juce::Logger::writeToLog("PresetManager: tag radice non valido: " + xml.getTagName());
        return false;
    }

    // STEP 1: Notify the UI to destroy all ParallelSplit panels before removing processors.
    // This prevents a crash from dangling pointers.
    // Must occur on the message thread (here we're already on the message thread).
    juce::Component::SafePointer<juce::AudioProcessorEditor> editorSafe(
        processor.getActiveEditor());

    if (auto* editor = editorSafe.getComponent())
    {
    // Safe casting — the editor is always a PluginEditor in our targets.
    // Using dynamic_cast to avoid circular dependencies in the header.
    // The call is synchronous since we are already on the message thread.
        if (auto* pe = dynamic_cast<juce::AudioProcessorEditor*>(editor))
        {
            // To avoid the circular include, we use the onPrepareForPresetLoad callback.
            if (onPrepareForPresetLoad)
                onPrepareForPresetLoad();
        }
    }

    // STEP 2: Ensure IO nodes exist
    if (!processor.inputNode)
        processor.inputNode = processor.mainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    if (!processor.outputNode)
        processor.outputNode = processor.mainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    // STEP 3: Empty all existing slots
    for (int i = 7; i >= 0; --i)
        if (processor.pluginSlots[i].isValid)
            processor.removePlugin(i);

    // STEP 4: Master section
    if (auto* master = xml.getChildByName("MasterSection"))
    {
        processor.setInputGainDb(clampPresetKnobDb((float)master->getDoubleAttribute("inputGainDb", 0.0)), true);
        processor.setMasterVolumeDb(
            clampPresetKnobDb((float)master->getDoubleAttribute("masterVolumeDb", 0.0)), true);
        processor.setEditorZoomScale((float)master->getDoubleAttribute("editorZoom", processor.getEditorZoomScale()));
    }

    if (auto* macrosEl = xml.getChildByName("MacroControls"))
    {
        for (auto* macroEl : macrosEl->getChildIterator())
        {
            if (macroEl->getTagName() != "Macro")
                continue;

            const int index = macroEl->getIntAttribute("index", -1);
            if (index < 0 || index >= PluginProcessor::macroControlCount)
                continue;

            processor.setMacroName(index, macroEl->getStringAttribute("name"));
            processor.setMacroValue(index, (float)macroEl->getDoubleAttribute("value", 0.0));
        }
    }

    std::vector<PluginProcessor::MacroMapping> loadedMappings;
    if (auto* mappingsEl = xml.getChildByName("MacroMappings"))
    {
        for (auto* mappingEl : mappingsEl->getChildIterator())
        {
            if (mappingEl->getTagName() != "Mapping")
                continue;

            PluginProcessor::MacroMapping mapping;
            mapping.scope = mappingEl->getStringAttribute("scope") == "ParallelSlot"
                ? PluginProcessor::MacroTargetScope::ParallelSlot
                : PluginProcessor::MacroTargetScope::MasterSlot;
            mapping.macroIndex = mappingEl->getIntAttribute("macroIndex", 0);
            mapping.masterSlot = mappingEl->getIntAttribute("masterSlot", -1);
            mapping.chainIndex = mappingEl->getIntAttribute("chainIndex", -1);
            mapping.parallelSlot = mappingEl->getIntAttribute("parallelSlot", -1);
            mapping.parameterIndex = mappingEl->getIntAttribute("parameterIndex", -1);
            mapping.targetPath = mappingEl->getStringAttribute("targetPath");
            mapping.pluginName = mappingEl->getStringAttribute("pluginName");
            mapping.parameterName = mappingEl->getStringAttribute("parameterName");
            mapping.targetMin = (float)mappingEl->getDoubleAttribute("targetMin", 0.0);
            mapping.targetMax = (float)mappingEl->getDoubleAttribute("targetMax", 1.0);
            mapping.inverted = mappingEl->getBoolAttribute("inverted", false);
            mapping.enabled = mappingEl->getBoolAttribute("enabled", true);
            loadedMappings.push_back(mapping);
        }
    }

    // STEP 5: Helper to instantiate a plugin
    auto instantiatePlugin = [&](const juce::String& format,
                                  const juce::String& fileOrIdentifier,
                                  int uid,
                                  const juce::String& pluginName,
                                  const juce::String& manufacturer,
                                  const juce::String& base64State,
                                  const juce::String& base64ProgramState)
        -> std::unique_ptr<juce::AudioProcessor>
    {
        // Pre-validation: without format or name we cannot search
        if (format.isEmpty() && pluginName.isEmpty())
        {
            juce::Logger::writeToLog("PresetManager: skip slot senza formato/nome");
            return nullptr;
        }

        const auto knownPlugins = processor.getLoadablePluginDescriptions();
        const juce::PluginDescription* matchDesc = nullptr;

        const auto normalisedPresetName = normalisePresetPluginKey(pluginName);
        const auto normalisedPresetManufacturer = normalisePresetPluginKey(manufacturer);

        for (const auto& d : knownPlugins)
        {
            if ((d.name == pluginName || d.fileOrIdentifier == fileOrIdentifier) &&
                d.pluginFormatName == format)
            {
                matchDesc = &d;
                break;
            }
        }

        if (!matchDesc)
        {
            for (const auto& d : knownPlugins)
            {
                if (d.uniqueId == uid && d.pluginFormatName == format)
                {
                    matchDesc = &d;
                    break;
                }
            }
        }

        if (!matchDesc)
        {
            for (const auto& d : knownPlugins)
            {
                if (d.name.equalsIgnoreCase(pluginName))
                {
                    matchDesc = &d;
                    break;
                }
            }
        }

        if (!matchDesc)
        {
            auto compactName = pluginName.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").toLowerCase();
            for (const auto& d : knownPlugins)
            {
                const auto candidateName = d.name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").toLowerCase();
                if (compactName.isNotEmpty() && candidateName == compactName)
                {
                    matchDesc = &d;
                    break;
                }
            }
        }

        if (!matchDesc)
        {
            for (const auto& d : knownPlugins)
            {
                if (normalisePresetPluginKey(d.name) != normalisedPresetName)
                    continue;

                if (normalisedPresetManufacturer.isNotEmpty()
                    && normalisePresetPluginKey(d.manufacturerName) != normalisedPresetManufacturer)
                    continue;

                matchDesc = &d;
                break;
            }
        }

        if (!matchDesc)
        {
            for (const auto& d : knownPlugins)
            {
                if (normalisePresetPluginKey(d.name) == normalisedPresetName)
                {
                    matchDesc = &d;
                    break;
                }
            }
        }

        if (!matchDesc)
        {
            juce::Logger::writeToLog("PresetManager: plugin non trovato in lista - "
                                     + pluginName + " [" + format + "] " + fileOrIdentifier);
            return nullptr;
        }

        // Defensive copy: Check ALL critical fields before passing
        // the description to createPluginInstance.
        juce::PluginDescription descCopy = *matchDesc;

        if (descCopy.name.isEmpty()
            || descCopy.fileOrIdentifier.isEmpty()
            || descCopy.pluginFormatName.isEmpty())
        {
            juce::Logger::writeToLog("PresetManager: description con campi vuoti per "
                                     + pluginName + " — skip");
            return nullptr;
        }

        // Additional check: for VST3, the fileOrIdentifier must be a path
        // pointing to an existing file.
        if (descCopy.fileOrIdentifier.length() < 2)
        {
            juce::Logger::writeToLog("PresetManager: fileOrIdentifier troppo corto per "
                                     + pluginName + " — skip");
            return nullptr;
        }

        juce::String error;
        double sr  = processor.getSampleRate() > 0 ? processor.getSampleRate() : 44100.0;
        int    bs  = processor.getBlockSize()  > 0 ? processor.getBlockSize()  : 512;

        std::unique_ptr<juce::AudioProcessor> instance;
        try
        {
            instance = processor.formatManager.createPluginInstance(descCopy, sr, bs, error);
        }
        catch (const std::exception& ex)
        {
            juce::Logger::writeToLog("PresetManager: eccezione durante istanziazione di "
                                     + descCopy.name + ": " + ex.what());
            return nullptr;
        }
        catch (...)
        {
            juce::Logger::writeToLog("PresetManager: eccezione sconosciuta durante istanziazione di "
                                     + descCopy.name);
            return nullptr;
        }

        if (!instance)
        {
            juce::Logger::writeToLog("PresetManager: impossibile istanziare "
                                     + pluginName + ": " + error);
            return nullptr;
        }

        if (base64State.isNotEmpty())
            applyPluginState(instance.get(), base64State);
        if (base64ProgramState.isNotEmpty())
            applyPluginProgramState(instance.get(), base64ProgramState);

        return instance;
    };

    auto setMissingMasterSlot = [&](int slotIdx,
                                    const juce::String& pluginName,
                                    const juce::String& format,
                                    const juce::String& fileId,
                                    int uid)
    {
        auto& s = processor.pluginSlots[(size_t)slotIdx];
        s.name = pluginName.isNotEmpty() ? pluginName : "Missing Plugin";
        s.pluginFormat = format;
        s.fileOrIdentifier = fileId;
        s.uniqueId = uid;
        s.isValid = true;
        s.bypassed = true;
        s.type = PluginProcessor::SlotType::Plugin;
        s.node = nullptr;
        s.editorWindow.reset();
        s.parallelProcessor = nullptr;
        s.parallelSplitOwner.reset();
        processor.setSlotBypassedInternal(slotIdx, true, false);
    };

    auto setMissingParallelSlot = [&](ParallelSplitProcessor::ParallelChainData& chainData,
                                      int slotIdx,
                                      const juce::String& pluginName,
                                      const juce::String& format,
                                      const juce::String& fileId,
                                      int uid)
    {
        auto& s = chainData.slots[(size_t)slotIdx];
        s.name = pluginName.isNotEmpty() ? pluginName : "Missing Plugin";
        s.pluginFormat = format;
        s.fileOrIdentifier = fileId;
        s.uniqueId = uid;
        s.valid = true;
        s.bypassed = true;
        s.type = ParallelSplitProcessor::ChainSlotType::Plugin;
        s.node = nullptr;
        s.editorWindow.reset();
        s.parallelProcessor = nullptr;
        s.parallelSplitOwner.reset();
    };

    // STEP 6: Load the slots
    if (auto* slotsEl = xml.getChildByName("Slots"))
    {
        for (auto* slotEl : slotsEl->getChildIterator())
        {
            if (slotEl->getTagName() != "Slot") continue;

            int  slotIdx  = slotEl->getIntAttribute("index", -1);
            bool bypassed = slotEl->getBoolAttribute("bypassed", false);
            if (slotIdx < 0 || slotIdx >= 8) continue;

            juce::String type = slotEl->getStringAttribute("type");

            // STEP 7: Load plugin slot
            if (type == "Plugin")
            {
                juce::String format = slotEl->getStringAttribute("format");
                juce::String fileId = slotEl->getStringAttribute("fileOrIdentifier");
                int          uid    = slotEl->getIntAttribute("uid", 0);
                juce::String pname  = slotEl->getStringAttribute("pluginName");
                juce::String manufacturer = slotEl->getStringAttribute("manufacturer");
                juce::String state;
                if (auto* stEl = slotEl->getChildByName("PluginState"))
                    state = stEl->getAllSubText().trim();
                juce::String programState;
                if (auto* stEl = slotEl->getChildByName("PluginProgramState"))
                    programState = stEl->getAllSubText().trim();

                auto instance = instantiatePlugin(format, fileId, uid, pname, manufacturer, state, programState);
                if (instance)
                {
                    juce::PluginDescription loadedDesc;
                    if (auto* loadedInstance = dynamic_cast<juce::AudioPluginInstance*>(instance.get()))
                        loadedInstance->fillInPluginDescription(loadedDesc);
                    auto node = processor.mainGraph->addNode(std::move(instance));
                    auto& s   = processor.pluginSlots[slotIdx];
                    s.name             = loadedDesc.name.isNotEmpty() ? loadedDesc.name : pname;
                    s.pluginFormat     = loadedDesc.pluginFormatName.isNotEmpty() ? loadedDesc.pluginFormatName : format;
                    s.fileOrIdentifier = loadedDesc.fileOrIdentifier.isNotEmpty() ? loadedDesc.fileOrIdentifier : fileId;
                    s.uniqueId         = loadedDesc.uniqueId != 0 ? loadedDesc.uniqueId : uid;
                    s.isValid          = true;
                    s.type             = PluginProcessor::SlotType::Plugin;
                    s.node             = node;
                    processor.setSlotBypassedInternal(slotIdx, bypassed, true);
                    processor.updateGraphConnections();
                }
                else
                {
                    setMissingMasterSlot(slotIdx, pname, format, fileId, uid);
                }
            }
            // STEP 8: Load parallel split slot
            else if (type == "ParallelSplit")
            {
                processor.createParallelSplit(slotIdx);
                auto& s = processor.pluginSlots[slotIdx];
                processor.setSlotBypassedInternal(slotIdx, bypassed, true);

                if (!s.parallelProcessor) continue;

                std::function<void(const juce::XmlElement&, ParallelSplitProcessor&)> loadSplit;
                loadSplit = [&](const juce::XmlElement& splitEl, ParallelSplitProcessor& split)
                {
                    int maxIndex = -1;
                    for (auto* chainEl : splitEl.getChildIterator())
                    {
                        if (chainEl->getTagName() != "Chain") continue;
                        juce::String chainId = chainEl->getStringAttribute("id");
                        int idx = chainEl->getIntAttribute("index", -1);
                        if (idx < 0)
                        {
                            if (chainId == "chain0") idx = 0;
                            else if (chainId == "chain1") idx = 1;
                        }
                        if (idx >= 0) maxIndex = juce::jmax(maxIndex, idx);
                    }

                    const int desiredChainCount = juce::jmax(1, maxIndex + 1);

                    while (split.getNumChains() > desiredChainCount && split.canRemoveChain(split.getNumChains() - 1))
                        split.removeChain(split.getNumChains() - 1);

                    split.ensureChainCount(desiredChainCount);

                    for (auto* chainEl : splitEl.getChildIterator())
                    {
                        if (chainEl->getTagName() != "Chain") continue;

                        juce::String chainId = chainEl->getStringAttribute("id");
                        int chainIdx = chainEl->getIntAttribute("index", -1);
                        if (chainIdx < 0)
                        {
                            if (chainId == "chain0") chainIdx = 0;
                            else if (chainId == "chain1") chainIdx = 1;
                        }
                        if (chainIdx < 0 || chainIdx >= split.getNumChains()) continue;

                        auto* chainData = &split.getChain(chainIdx);

                        chainData->name        = chainEl->getStringAttribute("name", juce::String(chainIdx + 1));
                        chainData->muted       = chainEl->getBoolAttribute("muted", chainIdx > 0);
                        chainData->solo        = chainEl->getBoolAttribute("solo", false);
                        chainData->inputGainDb = clampPresetKnobDb((float)chainEl->getDoubleAttribute("inputGainDb", 0.0));
                        chainData->outputVolDb = clampPresetKnobDb((float)chainEl->getDoubleAttribute("outputVolDb", 0.0));

                        for (auto* cslotEl : chainEl->getChildIterator())
                        {
                            if (cslotEl->getTagName() != "Slot") continue;

                            int  csIdx  = cslotEl->getIntAttribute("index", -1);
                            bool csbyp  = cslotEl->getBoolAttribute("bypassed", false);
                            if (csIdx < 0 || csIdx >= 8) continue;

                            if (cslotEl->getStringAttribute("type") == "ParallelSplit")
                            {
                                split.createParallelSplitInChain(*chainData, csIdx);
                                auto& nested = chainData->slots[csIdx];
                                nested.bypassed = csbyp;
                                if (nested.parallelProcessor)
                                    loadSplit(*cslotEl, *nested.parallelProcessor);
                                continue;
                            }

                            juce::String fmt    = cslotEl->getStringAttribute("format");
                            juce::String fid    = cslotEl->getStringAttribute("fileOrIdentifier");
                            int          cuid   = cslotEl->getIntAttribute("uid", 0);
                            juce::String cpname = cslotEl->getStringAttribute("pluginName");
                            juce::String cmanufacturer = cslotEl->getStringAttribute("manufacturer");
                            juce::String cstate;
                            if (auto* stEl = cslotEl->getChildByName("PluginState"))
                                cstate = stEl->getAllSubText().trim();
                            juce::String cprogramState;
                            if (auto* stEl = cslotEl->getChildByName("PluginProgramState"))
                                cprogramState = stEl->getAllSubText().trim();

                            auto instance = instantiatePlugin(fmt, fid, cuid, cpname, cmanufacturer, cstate, cprogramState);
                            if (instance)
                            {
                                juce::PluginDescription loadedDesc;
                                if (auto* loadedInstance = dynamic_cast<juce::AudioPluginInstance*>(instance.get()))
                                    loadedInstance->fillInPluginDescription(loadedDesc);
                                split.loadPluginInChain(*chainData, csIdx,
                                                        std::move(instance),
                                                        loadedDesc.name.isNotEmpty() ? loadedDesc.name : cpname,
                                                        loadedDesc.pluginFormatName.isNotEmpty() ? loadedDesc.pluginFormatName : fmt,
                                                        loadedDesc.fileOrIdentifier.isNotEmpty() ? loadedDesc.fileOrIdentifier : fid,
                                                        loadedDesc.uniqueId != 0 ? loadedDesc.uniqueId : cuid);
                                split.setSlotBypassed(*chainData, csIdx, csbyp);
                            }
                            else
                            {
                                setMissingParallelSlot(*chainData, csIdx, cpname, fmt, fid, cuid);
                            }
                        }
                        split.updateChainConnections(*chainData);
                    }
                };

                loadSplit(*slotEl, *s.parallelProcessor);
            }
        }
    }

    processor.updateGraphConnections();
    processor.setMacroMappings(std::move(loadedMappings));

    currentPresetName = xml.getStringAttribute("name");
    juce::Logger::writeToLog("PresetManager: preset '" + currentPresetName + "' applicato.");
    if (onPresetApplied)
        onPresetApplied();
    return true;
}

// ==============================================================================
// Saving to file
// ==============================================================================

bool PresetManager::savePresetAs(const juce::String& presetName)
{
    if (presetName.isEmpty()) return false;

    juce::String safeName = presetName.replaceCharacters("\\/: *?\"<>|", "__________");
    juce::File file = getPresetsFolder().getChildFile(safeName + ".hostrpreset");

    std::unique_ptr<juce::XmlElement> xml(createPresetXml(presetName));
    if (!xml) return false;

    juce::String xmlText = xml->toString();
    bool ok = file.replaceWithText(xmlText);

    if (ok)
    {
        currentPresetName = presetName;
        juce::Logger::writeToLog("PresetManager: preset salvato -> " + file.getFullPathName());
    }
    else
    {
        juce::Logger::writeToLog("PresetManager: ERRORE scrittura -> " + file.getFullPathName());
    }
    return ok;
}

// ==============================================================================
// Loading from FileChooser
// ==============================================================================

void PresetManager::loadPresetFromFile(
    std::function<void(bool, const juce::String&, bool)> onComplete)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Apri Preset",
        getPresetsFolder(),
        "*.hostrpreset");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, onComplete = std::move(onComplete)](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty())
            {
                if (onComplete) onComplete(false, {}, true);
                return;
            }

            auto xml = juce::XmlDocument::parse(results[0]);
            if (!xml)
            {
                juce::Logger::writeToLog("PresetManager: impossibile parsare "
                                         + results[0].getFullPathName());
                // Errore reale di parsing — cancelled=false
                if (onComplete) onComplete(false, {}, false);
                return;
            }

            bool ok = applyPresetXml(*xml);
            // cancelled=false in entrambi i casi (successo o errore di apply)
            if (onComplete) onComplete(ok, ok ? currentPresetName : juce::String{}, false);
        });
}

// ==============================================================================
// Default preset
// ==============================================================================

bool PresetManager::saveAsDefault()
{
    juce::File file = getDefaultPresetFile();

    std::unique_ptr<juce::XmlElement> xml(createPresetXml(
        currentPresetName.isEmpty() ? "Default" : currentPresetName));
    if (!xml) return false;

    juce::String xmlText = xml->toString();
    bool ok = file.replaceWithText(xmlText);
    juce::Logger::writeToLog(ok
        ? "PresetManager: default salvato -> " + file.getFullPathName()
        : "PresetManager: ERRORE salvataggio default");
    return ok;
}

bool PresetManager::resetDefaultPreset()
{
    // Delete the default file (if it exists) so that the next time you start
    // the plugin starts with all the slots empty.
    juce::File file = getDefaultPresetFile();
    bool fileOk = true;
    if (file.exists())
    {
        fileOk = file.deleteFile();
        juce::Logger::writeToLog(fileOk
            ? "PresetManager: default eliminato"
            : "PresetManager: ERRORE eliminazione default");
    }

    // Clears the current session: applies an empty preset
    // with all free slots, so that the UI immediately reflects
    // the "no plugins loaded" state.
    if (fileOk)
    {
        // Constructs an empty XML and applies it
        juce::XmlElement emptyPreset("HostrPreset");
        emptyPreset.setAttribute("name",    "Empty");
        emptyPreset.setAttribute("version", 2);
        emptyPreset.setAttribute("product", PluginProcessor::productName);
        if (auto* master = emptyPreset.createNewChildElement("MasterSection"))
        {
            master->setAttribute("inputGainDb",    0.0);
            master->setAttribute("masterVolumeDb", 0.0);
        }
        emptyPreset.createNewChildElement("MacroControls");
        emptyPreset.createNewChildElement("MacroMappings");
        emptyPreset.createNewChildElement("PluginManifest");
        emptyPreset.createNewChildElement("Slots");

        applyPresetXml(emptyPreset);
        currentPresetName = "Default";
    }

    return fileOk;
}

bool PresetManager::loadDefaultPreset()
{
    juce::File file = getDefaultPresetFile();

    if (!file.exists())
    {
        juce::XmlElement emptyPreset("HostrPreset");
        emptyPreset.setAttribute("name",    "Default");
        emptyPreset.setAttribute("version", 2);
        emptyPreset.setAttribute("product", PluginProcessor::productName);
        auto* ms = emptyPreset.createNewChildElement("MasterSection");
        ms->setAttribute("inputGainDb",    0.0);
        ms->setAttribute("masterVolumeDb", 0.0);
        emptyPreset.createNewChildElement("MacroControls");
        emptyPreset.createNewChildElement("MacroMappings");
        emptyPreset.createNewChildElement("PluginManifest");
        emptyPreset.createNewChildElement("Slots");

        juce::Logger::writeToLog("PresetManager: default preset non trovato, creo 'Default' vuoto");
        file.replaceWithText(emptyPreset.toString());

        return applyPresetXml(emptyPreset);
    }

    auto xml = juce::XmlDocument::parse(file);
    if (!xml)
    {
        juce::Logger::writeToLog("PresetManager: impossibile parsare il default preset");
        return false;
    }

    return applyPresetXml(*xml);
}

#endif // PRESET_MANAGER_CPP_INCLUDED
