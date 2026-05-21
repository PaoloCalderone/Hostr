// Aggregator: Include all custom .cpp files here so they are
// compiled into all targets (Standalone, VST3, AU) without
// having to manually configure Compile Sources in Xcode.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParallelSplitProcessor.h"
#include "PresetManager.h"

#include "ParallelSplitProcessor.cpp"
#include "ParallelSplitComponent.cpp"
#include "PluginSearchDialog.cpp"
#include "PresetManager.cpp"

static constexpr float kProcessorGainMinDb = -100.0f;
static constexpr float kProcessorGainMaxDb =   24.0f;
static constexpr float kProcessorMinusInfThresholdDb = -99.9f;

static float clampProcessorKnobDb(float value)
{
    return juce::jlimit(kProcessorGainMinDb, kProcessorGainMaxDb, value);
}

static float processorKnobDbToGain(float db)
{
    return db <= kProcessorMinusInfThresholdDb ? 0.0f : juce::Decibels::decibelsToGain(db);
}

static juce::NormalisableRange<float> processorKnobDbParameterRange()
{
    return { kProcessorGainMinDb, kProcessorGainMaxDb,
             [](float start, float end, float normalised)
             {
                 juce::ignoreUnused(start, end);
                 normalised = juce::jlimit(0.0f, 1.0f, normalised);
                 if (normalised <= 0.03f)
                     return juce::jmap(normalised, 0.0f, 0.03f, kProcessorGainMinDb, -48.0f);
                 if (normalised <= 0.25f)
                     return juce::jmap(normalised, 0.03f, 0.25f, -48.0f, -24.0f);
                 if (normalised <= 0.50f)
                     return juce::jmap(normalised, 0.25f, 0.50f, -24.0f, 0.0f);
                 return juce::jmap(normalised, 0.50f, 1.0f, 0.0f, kProcessorGainMaxDb);
             },
             [](float start, float end, float value)
             {
                 juce::ignoreUnused(start, end);
                 value = juce::jlimit(kProcessorGainMinDb, kProcessorGainMaxDb, value);
                 if (value <= -48.0f)
                     return juce::jmap(value, kProcessorGainMinDb, -48.0f, 0.0f, 0.03f);
                 if (value <= -24.0f)
                     return juce::jmap(value, -48.0f, -24.0f, 0.03f, 0.25f);
                 if (value <= 0.0f)
                     return juce::jmap(value, -24.0f, 0.0f, 0.25f, 0.50f);
                 return juce::jmap(value, 0.0f, kProcessorGainMaxDb, 0.50f, 1.0f);
             },
             [](float start, float end, float value)
             {
                 return juce::jlimit(start, end, std::round(value * 10.0f) / 10.0f);
             } };
}

static float calculateBlockPeakDb(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return -100.0f;

    const auto* channelData = buffer.getReadPointer(channel);
    if (channelData == nullptr)
        return -100.0f;

    float peak = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        peak = juce::jmax(peak, std::abs(channelData[sample]));

    return juce::jlimit(-100.0f, 12.0f,
        juce::Decibels::gainToDecibels(peak, -100.0f));
}

static juce::File getHostrAppDataDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Hostr");
    dir.createDirectory();
    return dir;
}

static juce::File getKnownPluginsFile()
{
    return getHostrAppDataDirectory().getChildFile("Hostr_InstalledPlugins.xml");
}

static juce::File getScanCrashListFile()
{
    return getHostrAppDataDirectory().getChildFile("Hostr_ScanCrashBlacklist.txt");
}

static juce::File getScanInProgressFile()
{
    return getHostrAppDataDirectory().getChildFile("Hostr_ScanInProgress.txt");
}

static juce::StringArray readStringListFile(const juce::File& file)
{
    juce::StringArray items;

    if (!file.existsAsFile())
        return items;

    items.addLines(file.loadFileAsString());
    items.trim();
    items.removeEmptyStrings();
    items.removeDuplicates(false);
    return items;
}

static void writeStringListFile(const juce::File& file, juce::StringArray items)
{
    items.trim();
    items.removeEmptyStrings();
    items.removeDuplicates(false);

    if (items.isEmpty())
    {
        file.deleteFile();
        return;
    }

    file.replaceWithText(items.joinIntoString("\n"), false, false, "\n");
}

static bool pathContainsIgnoreCase(const juce::String& path, const juce::String& text)
{
    return path.replaceCharacter('\\', '/').containsIgnoreCase(text);
}

static bool isMacWavesPayloadPath(const juce::String& path)
{
   #if JUCE_MAC
    return pathContainsIgnoreCase(path, "/Applications/Waves/Plug-Ins V");
   #else
    juce::ignoreUnused(path);
    return false;
   #endif
}

static bool enumeratePluginTypesOnMessageThread(juce::AudioPluginFormat& fmt,
                                                const juce::String& filePath,
                                                juce::OwnedArray<juce::PluginDescription>& results)
{
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        try
        {
            fmt.findAllTypesForFile(results, filePath);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;
    bool success = false;

    const bool queued = juce::MessageManager::callAsync([&fmt, filePath, &results, &mutex, &condition, &finished, &success]() mutable
    {
        try
        {
            fmt.findAllTypesForFile(results, filePath);
            success = true;
        }
        catch (...)
        {
            success = false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
        }

        condition.notify_one();
    });

    if (! queued)
        return false;

    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&finished]() { return finished; });
    return success;
}

static void addVst3MetadataDescription(const juce::File& file,
                                       const juce::String& shortName,
                                       std::function<void(juce::PluginDescription)> addDescription)
{
    juce::File moduleInfo = file.getChildFile("Contents/moduleinfo.json");
    juce::File infoPlist  = file.getChildFile("Contents/Info.plist");

    juce::String plugName, mfName, categoryName;

    if (moduleInfo.exists())
    {
        juce::String json = moduleInfo.loadFileAsString();
        auto extractJson = [&](const juce::String& src, const juce::String& key) -> juce::String
        {
            juce::String search = "\"" + key + "\"";
            int idx = src.indexOf(search);
            if (idx < 0) return {};
            int colon = src.indexOf(idx, ":");
            if (colon < 0) return {};
            int q1 = src.indexOf(colon, "\"");
            if (q1 < 0) return {};
            int q2 = src.indexOf(q1 + 1, "\"");
            if (q2 < 0) return {};
            return src.substring(q1 + 1, q2);
        };
        auto extractJsonArray = [&](const juce::String& src, const juce::String& key) -> juce::StringArray
        {
            juce::StringArray values;
            juce::String search = "\"" + key + "\"";
            int idx = src.indexOf(search);
            if (idx < 0) return values;
            int colon = src.indexOf(idx, ":");
            if (colon < 0) return values;
            int open = src.indexOf(colon, "[");
            if (open < 0) return values;
            int close = src.indexOf(open, "]");
            if (close < 0) return values;

            juce::String arrayText = src.substring(open + 1, close);
            int pos = 0;
            for (;;)
            {
                int q1 = arrayText.indexOf(pos, "\"");
                if (q1 < 0) break;
                int q2 = arrayText.indexOf(q1 + 1, "\"");
                if (q2 < 0) break;
                values.add(arrayText.substring(q1 + 1, q2));
                pos = q2 + 1;
            }

            values.trim();
            values.removeEmptyStrings();
            return values;
        };

        plugName = extractJson(json, "Name");
        mfName   = extractJson(json, "Vendor");

        auto subCategories = extractJsonArray(json, "Sub Categories");
        if (subCategories.isEmpty())
        {
            auto singleCategory = extractJson(json, "Sub Categories");
            if (singleCategory.isNotEmpty())
                subCategories.addTokens(singleCategory, "|;/,", {});
        }

        if (subCategories.isEmpty())
        {
            auto category = extractJson(json, "Category");
            if (category.isNotEmpty())
                subCategories.add(category);
        }

        subCategories.trim();
        subCategories.removeEmptyStrings();
        categoryName = subCategories.joinIntoString("|");
    }
    else if (infoPlist.exists())
    {
        if (auto xml = juce::XmlDocument::parse(infoPlist))
        {
            auto* dict = xml->getFirstChildElement();
            if (dict)
            {
                auto* k = dict->getFirstChildElement();
                while (k)
                {
                    auto* v = k->getNextElement();
                    if (v)
                    {
                        if (k->getAllSubText() == "CFBundleName")                plugName = v->getAllSubText();
                        if (k->getAllSubText() == "CFBundleGetInfoString")        mfName   = v->getAllSubText();
                        if (k->getAllSubText() == "AudioUnit manufacturer code")  mfName   = v->getAllSubText();
                    }
                    k = v ? v->getNextElement() : nullptr;
                }
            }
        }
    }

    if (plugName.isEmpty())
        plugName = shortName;

    juce::PluginDescription desc;
    desc.name              = plugName;
    desc.manufacturerName  = mfName;
    desc.pluginFormatName  = "VST3";
    desc.fileOrIdentifier  = file.getFullPathName();
    desc.category          = categoryName.isEmpty() ? "Effect" : categoryName;
    desc.isInstrument      = desc.category.containsIgnoreCase("Instrument")
                          || desc.category.containsIgnoreCase("Synth");
    desc.numInputChannels  = 2;
    desc.numOutputChannels = 2;
    desc.uniqueId          = plugName.hashCode();
    addDescription(desc);
}

static void addVst2MetadataDescription(const juce::File& file,
                                       const juce::String& shortName,
                                       std::function<void(juce::PluginDescription)> addDescription)
{
    juce::PluginDescription desc;
    desc.name              = shortName.isNotEmpty() ? shortName : file.getFileNameWithoutExtension();
    desc.pluginFormatName  = "VST";
    desc.fileOrIdentifier  = file.getFullPathName();
    desc.category          = "Effect";
    desc.isInstrument      = false;
    desc.numInputChannels  = 2;
    desc.numOutputChannels = 2;
    desc.uniqueId          = desc.fileOrIdentifier.hashCode();
    addDescription(desc);
}

static int fourCharCodeFromString(const juce::String& text)
{
    if (text.length() < 4)
        return text.hashCode();

    return ((int)(juce::juce_wchar)text[0] << 24)
         | ((int)(juce::juce_wchar)text[1] << 16)
         | ((int)(juce::juce_wchar)text[2] << 8)
         |  (int)(juce::juce_wchar)text[3];
}

static juce::String auCategoryFromType(const juce::String& type)
{
    if (type.equalsIgnoreCase("aumu")) return "Instrument";
    if (type.equalsIgnoreCase("aumf")) return "Music Effect";
    if (type.equalsIgnoreCase("aufx")) return "Effect";
    return "Effect";
}

static juce::String auIdentifierSectionFromType(const juce::String& type)
{
    if (type.equalsIgnoreCase("aumu")) return "Generators";
    if (type.equalsIgnoreCase("aumf")) return "MusicDevices";
    return "Effects";
}

static void addAudioUnitDescriptionsFromPlist(const juce::File& component,
                                              std::function<void(juce::PluginDescription)> addDescription)
{
    const auto plist = component.getChildFile("Contents/Info.plist");
    if (! plist.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(plist);
    if (xml == nullptr)
        return;

    auto* plistDict = xml->getFirstChildElement();
    if (plistDict == nullptr)
        return;

    for (auto* key = plistDict->getFirstChildElement(); key != nullptr; key = key->getNextElement())
    {
        if (key->getTagName() != "key" || key->getAllSubText() != "AudioComponents")
            continue;

        auto* array = key->getNextElement();
        if (array == nullptr || array->getTagName() != "array")
            return;

        for (auto* componentDict = array->getFirstChildElement(); componentDict != nullptr; componentDict = componentDict->getNextElement())
        {
            if (componentDict->getTagName() != "dict")
                continue;

            juce::String name, type, subtype, manufacturer, version;
            for (auto* itemKey = componentDict->getFirstChildElement(); itemKey != nullptr; itemKey = itemKey->getNextElement())
            {
                if (itemKey->getTagName() != "key")
                    continue;

                auto* value = itemKey->getNextElement();
                if (value == nullptr)
                    continue;

                const auto field = itemKey->getAllSubText();
                if (field == "name") name = value->getAllSubText();
                else if (field == "type") type = value->getAllSubText();
                else if (field == "subtype") subtype = value->getAllSubText();
                else if (field == "manufacturer") manufacturer = value->getAllSubText();
                else if (field == "version") version = value->getAllSubText();
            }

            if (name.isEmpty() || type.length() < 4 || subtype.length() < 4 || manufacturer.length() < 4)
                continue;

            juce::String pluginName = name;
            juce::String manufacturerName = manufacturer;
            if (name.contains(":"))
            {
                manufacturerName = name.upToFirstOccurrenceOf(":", false, false).trim();
                pluginName = name.fromFirstOccurrenceOf(":", false, false).trim();
            }

            juce::PluginDescription desc;
            desc.name = pluginName;
            desc.descriptiveName = name;
            desc.manufacturerName = manufacturerName;
            desc.pluginFormatName = "AudioUnit";
            desc.category = auCategoryFromType(type);
            desc.isInstrument = type.equalsIgnoreCase("aumu");
            desc.numInputChannels = 2;
            desc.numOutputChannels = 2;
            desc.fileOrIdentifier = "AudioUnit:" + auIdentifierSectionFromType(type)
                                  + "/" + type + "," + subtype + "," + manufacturer;
            desc.uniqueId = fourCharCodeFromString(subtype);
            desc.version = version;
            addDescription(desc);
        }

        return;
    }
}

// ==============================================================================
// Implementations:
// - the main audio graph of the master chain
// - loading/removing hosted plugins
// - managing Parallel Split blocks
// - scanning the plugin library
// - saving/restoring the DAW-side state
// ==============================================================================

class ParallelSplitGraphProcessor : public juce::AudioProcessor
{
public:
    explicit ParallelSplitGraphProcessor(ParallelSplitProcessor* split)
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          splitProcessor(split) {}

    const juce::String getName() const override { return "Parallel Split"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        setPlayConfigDetails(getTotalNumInputChannels(), getTotalNumOutputChannels(),
                             sampleRate, samplesPerBlock);

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
        return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
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

class PluginWindow : public juce::DocumentWindow,
                     private juce::ComponentListener
{
public:
    PluginWindow(const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel()
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

    ~PluginWindow() override
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

static void destroyHostedPluginWindow(std::unique_ptr<juce::DocumentWindow>& window)
{
    if (window == nullptr)
        return;

    window->setVisible(false);
    PluginSearchDialog::dismissActive();
    window->clearContentComponent();
    window.reset();
}

class ScopedProcessingSuspender
{
public:
    explicit ScopedProcessingSuspender(PluginProcessor& p) : processor(p)
    {
        processor.beginGraphMutation();
    }

    ~ScopedProcessingSuspender()
    {
        processor.endGraphMutation();
    }

private:
    PluginProcessor& processor;
};

void PluginProcessor::beginGraphMutation()
{
    if (graphMutationDepth++ == 0)
        graphMutationLock.enter();
}

void PluginProcessor::endGraphMutation()
{
    jassert(graphMutationDepth > 0);
    if (graphMutationDepth <= 0)
        return;

    if (--graphMutationDepth == 0)
        graphMutationLock.exit();
}

// Rebuilds a more reliable PluginDescription from the saved metadata,
// useful when AU/VST3 requires a more precise description upon load.
static juce::PluginDescription canonicalizeDescriptionForLoad(
    juce::AudioPluginFormatManager& fm,
    const juce::KnownPluginList& knownPluginList,
    const juce::PluginDescription& desc);

static void connectMainGraphAudioNodes(juce::AudioProcessorGraph& graph,
                                       juce::AudioProcessorGraph::Node& src,
                                       juce::AudioProcessorGraph::Node& dst)
{
    const auto srcChannels = juce::jmax(1, src.getProcessor()->getTotalNumOutputChannels());
    const auto dstChannels = juce::jmax(1, dst.getProcessor()->getTotalNumInputChannels());

    if (srcChannels >= 2 && dstChannels == 1)
    {
        graph.addConnection({ { src.nodeID, 0 }, { dst.nodeID, 0 } });
        graph.addConnection({ { src.nodeID, 1 }, { dst.nodeID, 0 } });
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

static void addExistingSearchPathIfMissing(juce::FileSearchPath& paths, const juce::File& file)
{
    if (! file.exists())
        return;

    for (int i = 0; i < paths.getNumPaths(); ++i)
        if (paths[i] == file)
            return;

    paths.add(file);
}

static void addExistingSearchPathIfMissing(juce::FileSearchPath& paths, const juce::String& path)
{
    if (path.isNotEmpty())
        addExistingSearchPathIfMissing(paths, juce::File(path));
}

static juce::String envVar(const juce::String& name)
{
    return juce::SystemStats::getEnvironmentVariable(name, {});
}

static void addExistingChildSearchPathIfMissing(juce::FileSearchPath& paths,
                                                const juce::String& parentPath,
                                                const juce::String& childPath)
{
    if (parentPath.isNotEmpty())
        addExistingSearchPathIfMissing(paths, juce::File(parentPath).getChildFile(childPath));
}

// Builds the actual search paths for each format, adding
// the standard system directories when JUCE does not include them.
static juce::FileSearchPath buildSearchPathsForFormat(juce::AudioPluginFormat& format)
{
    juce::FileSearchPath paths = format.getDefaultLocationsToSearch();

   #if JUCE_MAC
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();

    if (format.getName() == "VST3")
    {
        addExistingSearchPathIfMissing(paths, "/Library/Audio/Plug-Ins/VST3");
        addExistingSearchPathIfMissing(paths, home + "/Library/Audio/Plug-Ins/VST3");
    }
    else if (format.getName() == "AudioUnit")
    {
        addExistingSearchPathIfMissing(paths, "/Library/Audio/Plug-Ins/Components");
        addExistingSearchPathIfMissing(paths, home + "/Library/Audio/Plug-Ins/Components");
        addExistingSearchPathIfMissing(paths, "/Applications/Waves");
        addExistingSearchPathIfMissing(paths, "/Applications/Waves/WaveShells V16");
    }
    else if (format.getName() == "VST")
    {
        addExistingSearchPathIfMissing(paths, "/Library/Audio/Plug-Ins/VST");
        addExistingSearchPathIfMissing(paths, home + "/Library/Audio/Plug-Ins/VST");
    }
   #elif JUCE_WINDOWS
    const auto commonProgramFiles = envVar("CommonProgramFiles");
    const auto commonProgramFilesX86 = envVar("CommonProgramFiles(x86)");
    const auto programFiles = envVar("ProgramFiles");
    const auto localAppData = envVar("LOCALAPPDATA");

    if (format.getName() == "VST3")
    {
        addExistingChildSearchPathIfMissing(paths, commonProgramFiles, "VST3");
        addExistingChildSearchPathIfMissing(paths, commonProgramFilesX86, "VST3");
        addExistingChildSearchPathIfMissing(paths, localAppData, "Programs/Common/VST3");
    }
    else if (format.getName() == "VST")
    {
        addExistingChildSearchPathIfMissing(paths, programFiles, "VstPlugins");
        addExistingChildSearchPathIfMissing(paths, programFiles, "Steinberg/VstPlugins");
    }
   #elif JUCE_LINUX
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    if (format.getName() == "VST3")
    {
        addExistingSearchPathIfMissing(paths, home.getChildFile(".vst3"));
        addExistingSearchPathIfMissing(paths, "/usr/local/lib/vst3");
        addExistingSearchPathIfMissing(paths, "/usr/lib/vst3");
    }
    else if (format.getName() == "VST")
    {
        addExistingSearchPathIfMissing(paths, home.getChildFile(".vst"));
        addExistingSearchPathIfMissing(paths, "/usr/local/lib/vst");
        addExistingSearchPathIfMissing(paths, "/usr/lib/vst");
    }
   #endif

    return paths;
}

static juce::File getHostrSettingsFile()
{
    return getHostrAppDataDirectory().getChildFile("settings.xml");
}

struct StoredEditorSettings
{
    float zoom = 1.25f;
    int skinIndex = 0;
};

static StoredEditorSettings loadStoredEditorSettings()
{
    if (auto xml = juce::XmlDocument::parse(getHostrSettingsFile()))
    {
        StoredEditorSettings settings;
        settings.zoom = juce::jlimit(0.75f, 2.00f, (float)xml->getDoubleAttribute("editorZoom", 1.25));
        settings.skinIndex = hostr::clampSkinIndex(xml->getIntAttribute("skinIndex", 0));
        return settings;
    }

    return {};
}

static void saveStoredEditorSettings(float zoom, int skinIndex)
{
    juce::XmlElement xml("HostrSettings");
    xml.setAttribute("editorZoom", (double)juce::jlimit(0.75f, 2.00f, zoom));
    xml.setAttribute("skinIndex", hostr::clampSkinIndex(skinIndex));
    xml.writeTo(getHostrSettingsFile());
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < macroControlCount; ++i)
    {
        macroNames[(size_t)i] = "M" + juce::String(i + 1);
        auto* parameter = new juce::AudioParameterFloat(
            juce::ParameterID("macro" + juce::String(i + 1), 1),
            macroNames[(size_t)i],
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.0f);
        macroParameters[(size_t)i] = parameter;
        addParameter(parameter);
    }

    inputGainParameter = new juce::AudioParameterFloat(
        juce::ParameterID("masterInputGainDb", 1),
        "Master Input Gain",
        processorKnobDbParameterRange(),
        0.0f);
    addParameter(inputGainParameter);

    masterVolumeParameter = new juce::AudioParameterFloat(
        juce::ParameterID("masterOutputGainDb", 1),
        "Master Output Gain",
        processorKnobDbParameterRange(),
        0.0f);
    addParameter(masterVolumeParameter);

    for (int i = 0; i < 8; ++i)
    {
        auto* parameter = new juce::AudioParameterBool(
            juce::ParameterID("masterSlot" + juce::String(i + 1) + "On", 1),
            "Master Slot " + juce::String(i + 1) + " On",
            true);
        masterSlotBypassParameters[(size_t)i] = parameter;
        addParameter(parameter);
    }

    const auto storedEditorSettings = loadStoredEditorSettings();
    editorZoomScale.store(storedEditorSettings.zoom);
    editorSkinIndex.store(storedEditorSettings.skinIndex);
    hostr::setActiveSkinIndex(storedEditorSettings.skinIndex);
    inputGainLinear.setCurrentAndTargetValue(1.0f);
    masterVolumeLinear.setCurrentAndTargetValue(1.0f);
    for (auto& smoothed : macroSmoothedValues)
        smoothed.setCurrentAndTargetValue(0.0f);
    formatManager.addDefaultFormats();

    auto formats = formatManager.getFormats();
    juce::Logger::writeToLog("=== Available Plugin Formats ===");
    for (auto* format : formats)
    {
        if (format == nullptr)
            continue;
        juce::Logger::writeToLog("Format: " + format->getName());
        auto searchPath = format->getDefaultLocationsToSearch();
        juce::Logger::writeToLog("  Default search paths: " + juce::String(searchPath.getNumPaths()));
    }

    mainGraph = std::make_unique<juce::AudioProcessorGraph>();

    // Carica la lista plugin nota da disco. Il rename a Hostr non deve svuotare
    // la cache dei plugin scansionati: i preset cambiano formato, la lista plugin no.
    auto appDataDir = getHostrAppDataDirectory();
    auto legacyAppDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    juce::Array<juce::File> pluginListCandidates;
    pluginListCandidates.add(getKnownPluginsFile());
    pluginListCandidates.add(appDataDir.getChildFile("HostPlugin_InstalledPlugins.xml"));
    pluginListCandidates.add(appDataDir.getChildFile("StudioVerse_InstalledPlugins.xml"));
    pluginListCandidates.add(legacyAppDataDir.getChildFile("Hostr_InstalledPlugins.xml"));
    pluginListCandidates.add(legacyAppDataDir.getChildFile("HostPlugin_InstalledPlugins.xml"));
    pluginListCandidates.add(legacyAppDataDir.getChildFile("StudioVerse_InstalledPlugins.xml"));

    for (const auto& savedListFile : pluginListCandidates)
    {
        if (!savedListFile.existsAsFile())
            continue;

        if (auto xml = juce::XmlDocument::parse(savedListFile))
        {
            knownPluginList.recreateFromXml(*xml);
            break;
        }
    }

    // Sanitize the list: remove entries with empty or invalid critical fields.
    // Corrupt entries cause a crash in AudioUnitPluginFormat::fileMightContain
    // which dereferences the internal fileOrIdentifier pointer without a null check.
    {
        juce::Array<juce::PluginDescription> toRemove;
        for (const auto& d : knownPluginList.getTypes())
        {
            if (d.name.isEmpty()
                || d.fileOrIdentifier.isEmpty()
                || d.pluginFormatName.isEmpty())
            {
                toRemove.add(d);
                continue;
            }

            // Waves payload bundles are not directly loadable plugins.
            // However, AudioUnit entries for Waves can be useful to expose
            // AU-based plugins; keep AudioUnit entries and only remove other
            // bundle-typed entries.
            if (isMacWavesPayloadPath(d.fileOrIdentifier)
                && d.fileOrIdentifier.endsWithIgnoreCase(".bundle")
                && d.pluginFormatName != "AudioUnit")
            {
                toRemove.add(d);
            }
        }
        for (const auto& d : toRemove)
        {
            knownPluginList.removeType(d);
            juce::Logger::writeToLog("PluginProcessor: rimossa entry corrotta dalla lista: "
                                     + d.name + " [" + d.pluginFormatName + "]");
        }
    }

    {
        auto inProgressEntries = readStringListFile(getScanInProgressFile());
        if (!inProgressEntries.isEmpty())
        {
            auto blacklistedEntries = readStringListFile(getScanCrashListFile());
            for (const auto& entry : inProgressEntries)
                if (!blacklistedEntries.contains(entry))
                    blacklistedEntries.add(entry);

            writeStringListFile(getScanCrashListFile(), blacklistedEntries);
            getScanInProgressFile().deleteFile();

            scanRecoveryMessage = "A previous plugin scan was interrupted by a plugin crash. "
                                  "The problematic plugin has been skipped so the rest of the library can still be scanned.";

            if (!inProgressEntries.isEmpty())
            {
                scanRecoveryMessage << "\n\nSkipped on this machine:";
                for (const auto& entry : inProgressEntries)
                    scanRecoveryMessage << "\n- " << entry;
            }
        }
    }

    // Lightweight cache based on the known list:
    // No filesystem scans when the user opens Search Plugin.
    rebuildLoadablePluginCache();
    loadablePluginCacheInitialized = true;
    // Construct the PresetManager (after all members are initialized)
    presetManager = std::make_unique<PresetManager>(*this);
    startTimerHz(30);
}

PluginProcessor::~PluginProcessor()
{
    saveStoredEditorSettings(getEditorZoomScale(), getEditorSkinIndex());
    stopTimer();
    shouldStopScanning = true;
    PluginSearchDialog::dismissActive();

    if (scanThread && scanThread->joinable())
        scanThread->join();

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

    releaseResources();

    mainGraph.reset();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mainGraph->setPlayConfigDetails(getMainBusNumInputChannels(), getMainBusNumOutputChannels(),
                                    sampleRate, samplesPerBlock);
    mainGraph->prepareToPlay(sampleRate, samplesPerBlock);

    inputGain.store(clampProcessorKnobDb(inputGain.load()));
    const auto masterDb = clampProcessorKnobDb(juce::Decibels::gainToDecibels(masterVolume.load(), kProcessorGainMinDb));
    masterVolume.store(processorKnobDbToGain(masterDb));
    inputGainLinear.reset(sampleRate, 0.02);
    masterVolumeLinear.reset(sampleRate, 0.02);
    inputGainLinear.setCurrentAndTargetValue(processorKnobDbToGain(inputGain.load()));
    masterVolumeLinear.setCurrentAndTargetValue(masterVolume.load());
    for (int i = 0; i < macroControlCount; ++i)
    {
        macroSmoothedValues[(size_t)i].reset(sampleRate, 0.02);
        macroSmoothedValues[(size_t)i].setCurrentAndTargetValue(getMacroValue(i));
    }

    // Prepare all ParallelSplits
    for (auto& slot : pluginSlots)
    {
        if (slot.isValid && slot.type == SlotType::ParallelSplit && slot.parallelProcessor)
            slot.parallelProcessor->prepareToPlay(sampleRate, samplesPerBlock);
    }

    // First call: full graph init (clears everything, sets up IO nodes).
    // Subsequent calls: just reconfigure without wiping loaded plugin nodes.
    if (!defaultPresetLoaded)
        initialiseGraph();
    else
        reconfigureGraph();

    // Defer all state restoration to the message thread — the graph is now
    // initialized but audio callbacks may fire before the message thread runs,
    // which is safe since the graph passes audio through even with empty slots.
    if (!defaultPresetLoaded && presetManager)
    {
        defaultPresetLoaded = true;

        if (pendingStateData.getSize() > 0)
        {
            // DAW restore: apply the state that was stashed in setStateInformation.
            pendingStateApplyQueued = true;
            juce::MemoryBlock stateCopy (pendingStateData);
            pendingStateData.reset();
            juce::MessageManager::callAsync([this, stateCopy = std::move(stateCopy)]() mutable
            {
                pendingStateApplyQueued = false;
                if (presetManager)
                    if (auto xml = getXmlFromBinary(stateCopy.getData(), (int)stateCopy.getSize()))
                        presetManager->applyPresetXml(*xml);
            });
        }
        else
        {
            // No DAW state — try loading the default preset file.
            juce::MessageManager::callAsync([this]()
            {
                if (presetManager && presetManager->isApplyingPreset) return;
                if (presetManager)
                    presetManager->loadDefaultPreset();
            });
        }
    }
}

void PluginProcessor::releaseResources()
{
    mainGraph->releaseResources();

    for (auto& slot : pluginSlots)
    {
        if (slot.isValid && slot.type == SlotType::ParallelSplit && slot.parallelProcessor)
            slot.parallelProcessor->releaseResources();
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const bool bypassHostedGraph = isScanning() || isSuspended();

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    int inputNumChannels = juce::jmin(2, buffer.getNumChannels());
    for (int ch = 0; ch < inputNumChannels; ++ch)
        inputLevels[ch].store(calculateBlockPeakDb(buffer, ch), std::memory_order_relaxed);

    if (inputNumChannels == 1)
        inputLevels[1].store(inputLevels[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
    else if (inputNumChannels <= 0)
    {
        inputLevels[0].store(-100.0f, std::memory_order_relaxed);
        inputLevels[1].store(-100.0f, std::memory_order_relaxed);
    }

    bool hasLoadedSlots = false;
    for (const auto& slot : pluginSlots)
    {
        if (slot.isValid)
        {
            hasLoadedSlots = true;
            break;
        }
    }

    if (inputGainParameter != nullptr)
        inputGain.store(clampProcessorKnobDb(inputGainParameter->get()));
    if (masterVolumeParameter != nullptr)
        masterVolume.store(processorKnobDbToGain(clampProcessorKnobDb(masterVolumeParameter->get())));

    if (hasLoadedSlots)
        for (int i = 0; i < 8; ++i)
            if (auto* parameter = masterSlotBypassParameters[(size_t)i])
                if (pluginSlots[(size_t)i].bypassed == parameter->get())
                    setSlotBypassedInternal(i, !parameter->get(), false);

    const auto inputDb = clampProcessorKnobDb(inputGain.load());
    inputGain.store(inputDb);
    const float inputGainValue = processorKnobDbToGain(inputDb);
    inputGainLinear.setTargetValue(inputGainValue);
    if (std::abs(inputGainValue - 1.0f) > 0.000001f
        || std::abs(inputGainLinear.getCurrentValue() - inputGainValue) > 0.000001f)
    {
        const float inputGainStart = inputGainLinear.getCurrentValue();
        const float inputGainEnd   = inputGainLinear.skip(buffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, 0, buffer.getNumSamples(), inputGainStart, inputGainEnd);
    }

    juce::SpinLock::ScopedTryLockType graphLock(graphMutationLock);
    if (!bypassHostedGraph && graphLock.isLocked() && hasLoadedSlots && mainGraph)
        mainGraph->processBlock(buffer, midiMessages);

    const auto masterDb = clampProcessorKnobDb(juce::Decibels::gainToDecibels(masterVolume.load(), kProcessorGainMinDb));
    masterVolume.store(processorKnobDbToGain(masterDb));
    const float masterGainValue = masterVolume.load();
    masterVolumeLinear.setTargetValue(masterGainValue);
    if (std::abs(masterGainValue - 1.0f) > 0.000001f
        || std::abs(masterVolumeLinear.getCurrentValue() - masterGainValue) > 0.000001f)
    {
        const float masterGainStart = masterVolumeLinear.getCurrentValue();
        const float masterGainEnd   = masterVolumeLinear.skip(buffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGainRamp(ch, 0, buffer.getNumSamples(), masterGainStart, masterGainEnd);
    }

    // The output meter should show the actual post-master-volume peak:
    // same buffer that Hostr delivers to the DAW.
    int numChannels = juce::jmin(2, buffer.getNumChannels());
    for (int ch = 0; ch < numChannels; ++ch)
        outputLevels[ch].store(calculateBlockPeakDb(buffer, ch), std::memory_order_relaxed);

    if (numChannels == 1)
        outputLevels[1].store(outputLevels[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
    else if (numChannels <= 0)
    {
        outputLevels[0].store(-100.0f, std::memory_order_relaxed);
        outputLevels[1].store(-100.0f, std::memory_order_relaxed);
    }
}

bool PluginProcessor::hasParallelOutputOverload() const
{
    for (const auto& slot : pluginSlots)
        if (slot.isValid
            && slot.type == SlotType::ParallelSplit
            && slot.parallelProcessor != nullptr
            && slot.parallelProcessor->hasOutputOverload())
            return true;

    return false;
}

juce::AudioParameterFloat* PluginProcessor::getMacroParameter(int index) const
{
    if (index < 0 || index >= macroControlCount)
        return nullptr;

    return macroParameters[(size_t)index];
}

float PluginProcessor::getMacroValue(int index) const
{
    if (auto* parameter = getMacroParameter(index))
        return parameter->get();

    return 0.0f;
}

void PluginProcessor::setMacroValue(int index, float value)
{
    if (auto* parameter = getMacroParameter(index))
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, value);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(clamped);
        parameter->endChangeGesture();
    }
}

juce::String PluginProcessor::getMacroName(int index) const
{
    if (index < 0 || index >= macroControlCount)
        return {};

    return macroNames[(size_t)index];
}

void PluginProcessor::setMacroName(int index, const juce::String& name)
{
    if (index < 0 || index >= macroControlCount)
        return;

    macroNames[(size_t)index] = name.trim().isEmpty() ? ("Macro " + juce::String(index + 1)) : name.trim();
}

void PluginProcessor::clearMacroMappings()
{
    macroMappings.clear();
}

void PluginProcessor::setMacroMappings(std::vector<MacroMapping> mappings)
{
    macroMappings = std::move(mappings);
}

bool PluginProcessor::addMacroMapping(const MacroMapping& mapping)
{
    if (mapping.macroIndex < 0
        || mapping.macroIndex >= macroControlCount
        || mapping.parameterIndex < 0)
        return false;

    macroMappings.push_back(mapping);
    return true;
}

void PluginProcessor::removeMacroMapping(size_t index)
{
    if (index < macroMappings.size())
        macroMappings.erase(macroMappings.begin() + (std::ptrdiff_t)index);
}

juce::StringArray PluginProcessor::getMappableParametersForMasterSlot(int slotIndex) const
{
    juce::StringArray names;
    if (slotIndex < 0 || slotIndex >= 8)
        return names;

    const auto& slot = pluginSlots[(size_t)slotIndex];
    if (!slot.isValid || slot.type != SlotType::Plugin || slot.node == nullptr)
        return names;

    auto* proc = slot.node->getProcessor();
    if (proc == nullptr)
        return names;

    const auto& params = proc->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto* param = params[i];
        const auto label = param != nullptr ? param->getName(64) : juce::String();
        names.add(label.isEmpty() ? ("Parameter " + juce::String(i + 1)) : label);
    }

    return names;
}


static juce::String cleanProcessorMacroParameterName(juce::String name)
{
    name = name.trim();
    while (name.length() >= 3
           && name.startsWithIgnoreCase("Ch")
           && juce::CharacterFunctions::isDigit(name[2]))
    {
        int pos = 2;
        while (pos < name.length() && juce::CharacterFunctions::isDigit(name[pos]))
            ++pos;
        name = name.substring(pos).trimStart();
        if (name.startsWithChar((juce_wchar) 58) || name.startsWithChar((juce_wchar) 45) || name.startsWithChar((juce_wchar) 95))
            name = name.substring(1).trimStart();
    }
    return name;
}

bool PluginProcessor::mapMacroToMasterSlotParameter(int macroIndex, int slotIndex, int parameterIndex)
{
    if (slotIndex < 0 || slotIndex >= 8)
        return false;

    const auto& slot = pluginSlots[(size_t)slotIndex];
    if (!slot.isValid || slot.type != SlotType::Plugin || slot.node == nullptr)
        return false;

    auto* proc = slot.node->getProcessor();
    if (proc == nullptr || parameterIndex < 0 || parameterIndex >= proc->getParameters().size())
        return false;

    MacroMapping mapping;
    mapping.scope = MacroTargetScope::MasterSlot;
    mapping.macroIndex = macroIndex;
    mapping.masterSlot = slotIndex;
    mapping.parameterIndex = parameterIndex;
    mapping.pluginName = slot.name;
    mapping.parameterName = cleanProcessorMacroParameterName(proc->getParameters()[parameterIndex]->getName(64));
    return addMacroMapping(mapping);
}


static int parseMacroPathIndex(const juce::String& token, juce_wchar prefix)
{
    if (token.length() < 2 || token[0] != prefix)
        return -1;
    return token.substring(1).getIntValue();
}

static juce::AudioProcessor* resolveMacroTargetPath(
    const std::array<PluginProcessor::LoadedPluginInfo, 8>& masterSlots,
    const juce::String& targetPath)
{
    juce::StringArray tokens;
    tokens.addTokens(targetPath, "/", {});
    tokens.removeEmptyStrings();
    if (tokens.isEmpty())
        return nullptr;

    const int masterSlot = parseMacroPathIndex(tokens[0], 'M');
    if (masterSlot < 0 || masterSlot >= (int)masterSlots.size())
        return nullptr;

    const auto& master = masterSlots[(size_t)masterSlot];
    if (!master.isValid)
        return nullptr;

    if (tokens.size() == 1)
        return (master.type == PluginProcessor::SlotType::Plugin && master.node != nullptr)
            ? master.node->getProcessor() : nullptr;

    if (master.type != PluginProcessor::SlotType::ParallelSplit || master.parallelProcessor == nullptr)
        return nullptr;

    auto* split = master.parallelProcessor;
    for (int tokenIndex = 1; tokenIndex + 1 < tokens.size(); tokenIndex += 2)
    {
        const int chainIndex = parseMacroPathIndex(tokens[tokenIndex], 'C');
        const int slotIndex = parseMacroPathIndex(tokens[tokenIndex + 1], 'S');
        if (split == nullptr
            || chainIndex < 0
            || chainIndex >= split->getNumChains()
            || slotIndex < 0)
            return nullptr;

        const auto& chain = split->getChain(chainIndex);
        if (slotIndex >= (int)chain.slots.size())
            return nullptr;

        const auto& slot = chain.slots[(size_t)slotIndex];
        if (!slot.valid)
            return nullptr;

        const bool isLastPair = tokenIndex + 2 >= tokens.size();
        if (isLastPair)
            return (slot.type == ParallelSplitProcessor::ChainSlotType::Plugin && slot.node != nullptr)
                ? slot.node->getProcessor() : nullptr;

        if (slot.type != ParallelSplitProcessor::ChainSlotType::ParallelSplit
            || slot.parallelProcessor == nullptr)
            return nullptr;

        split = slot.parallelProcessor;
    }

    return nullptr;
}


static bool macroPathMatches(const juce::String& path, const juce::String& prefix, bool includeChildren)
{
    if (path == prefix)
        return true;
    return includeChildren && path.startsWith(prefix + "/");
}

static juce::String remapMacroPathPrefix(juce::String path,
                                          const juce::String& from,
                                          const juce::String& to)
{
    if (path == from)
        return to;
    if (path.startsWith(from + "/"))
        return to + path.substring(from.length());
    return path;
}

static bool findSplitPathRecursive(const ParallelSplitProcessor* current,
                                   const ParallelSplitProcessor* target,
                                   const juce::String& currentPath,
                                   juce::String& result)
{
    if (current == nullptr)
        return false;
    if (current == target)
    {
        result = currentPath;
        return true;
    }

    for (int chainIndex = 0; chainIndex < current->getNumChains(); ++chainIndex)
    {
        const auto& chain = current->getChain(chainIndex);
        for (int slotIndex = 0; slotIndex < (int)chain.slots.size(); ++slotIndex)
        {
            const auto& slot = chain.slots[(size_t)slotIndex];
            if (!slot.valid
                || slot.type != ParallelSplitProcessor::ChainSlotType::ParallelSplit
                || slot.parallelProcessor == nullptr)
                continue;

            const auto childPath = currentPath + "/C" + juce::String(chainIndex) + "/S" + juce::String(slotIndex);
            if (findSplitPathRecursive(slot.parallelProcessor, target, childPath, result))
                return true;
        }
    }

    return false;
}

juce::String PluginProcessor::getMacroTargetPathForSplit(const ParallelSplitProcessor* split) const
{
    if (split == nullptr)
        return {};

    for (int masterSlot = 0; masterSlot < 8; ++masterSlot)
    {
        const auto& slot = pluginSlots[(size_t)masterSlot];
        if (!slot.isValid
            || slot.type != SlotType::ParallelSplit
            || slot.parallelProcessor == nullptr)
            continue;

        const auto rootPath = "M" + juce::String(masterSlot);
        juce::String result;
        if (findSplitPathRecursive(slot.parallelProcessor, split, rootPath, result))
            return result;
    }

    return {};
}

void PluginProcessor::removeMacroMappingsForTargetPath(const juce::String& targetPath, bool includeChildren)
{
    if (targetPath.isEmpty())
        return;

    for (int i = (int)macroMappings.size(); --i >= 0;)
        if (macroPathMatches(macroMappings[(size_t)i].targetPath, targetPath, includeChildren))
            macroMappings.erase(macroMappings.begin() + i);
}

void PluginProcessor::remapMacroTargetPaths(const juce::String& firstPath,
                                             const juce::String& secondPath,
                                             bool swapTargets)
{
    if (firstPath.isEmpty() || secondPath.isEmpty() || firstPath == secondPath)
        return;

    const juce::String tempPrefix = "__HOSTR_TMP_MACRO_PATH__";
    for (auto& mapping : macroMappings)
    {
        if (swapTargets)
            mapping.targetPath = remapMacroPathPrefix(mapping.targetPath, firstPath, tempPrefix);
        else
            mapping.targetPath = remapMacroPathPrefix(mapping.targetPath, firstPath, secondPath);
    }

    if (swapTargets)
    {
        for (auto& mapping : macroMappings)
            mapping.targetPath = remapMacroPathPrefix(mapping.targetPath, secondPath, firstPath);
        for (auto& mapping : macroMappings)
            mapping.targetPath = remapMacroPathPrefix(mapping.targetPath, tempPrefix, secondPath);
    }
}

void PluginProcessor::removeInvalidMacroMappings()
{
    for (int i = (int)macroMappings.size(); --i >= 0;)
        if (resolveMacroTarget(macroMappings[(size_t)i]) == nullptr)
            macroMappings.erase(macroMappings.begin() + i);
}

juce::AudioProcessor* PluginProcessor::resolveMacroTarget(const MacroMapping& mapping) const
{
    if (mapping.targetPath.isNotEmpty())
        return resolveMacroTargetPath(pluginSlots, mapping.targetPath);

    if (mapping.scope == MacroTargetScope::MasterSlot)
    {
        if (mapping.masterSlot < 0 || mapping.masterSlot >= 8)
            return nullptr;

        const auto& slot = pluginSlots[(size_t)mapping.masterSlot];
        if (!slot.isValid || slot.type != SlotType::Plugin || slot.node == nullptr)
            return nullptr;

        return slot.node->getProcessor();
    }

    if (mapping.scope == MacroTargetScope::ParallelSlot)
    {
        if (mapping.masterSlot < 0 || mapping.masterSlot >= 8)
            return nullptr;

        const auto& splitSlot = pluginSlots[(size_t)mapping.masterSlot];
        if (!splitSlot.isValid
            || splitSlot.type != SlotType::ParallelSplit
            || splitSlot.parallelProcessor == nullptr
            || mapping.chainIndex < 0
            || mapping.chainIndex >= splitSlot.parallelProcessor->getNumChains())
            return nullptr;

        const auto& chain = splitSlot.parallelProcessor->getChain(mapping.chainIndex);
        if (mapping.parallelSlot < 0 || mapping.parallelSlot >= (int)chain.slots.size())
            return nullptr;

        const auto& slot = chain.slots[(size_t)mapping.parallelSlot];
        if (!slot.valid || slot.type != ParallelSplitProcessor::ChainSlotType::Plugin || slot.node == nullptr)
            return nullptr;

        return slot.node->getProcessor();
    }

    return nullptr;
}

void PluginProcessor::applyMacroMappings()
{
    std::array<float, macroControlCount> smoothedMacroValues {};
    const int smoothingSamples = juce::jmax(1, getBlockSize());
    for (int i = 0; i < macroControlCount; ++i)
    {
        auto& smoothed = macroSmoothedValues[(size_t)i];
        smoothed.setTargetValue(getMacroValue(i));
        smoothedMacroValues[(size_t)i] = smoothed.skip(smoothingSamples);
    }

    for (auto& mapping : macroMappings)
    {
        if (!mapping.enabled)
            continue;

        auto* target = resolveMacroTarget(mapping);
        if (target == nullptr)
            continue;

        const auto& params = target->getParameters();
        if (mapping.parameterIndex < 0 || mapping.parameterIndex >= params.size())
            continue;

        auto* parameter = params[mapping.parameterIndex];
        if (parameter == nullptr)
            continue;

        float macroValue = smoothedMacroValues[(size_t)mapping.macroIndex];
        if (mapping.inverted)
            macroValue = 1.0f - macroValue;

        const float lo = juce::jlimit(0.0f, 1.0f, mapping.targetMin);
        const float hi = juce::jlimit(0.0f, 1.0f, mapping.targetMax);
        const float mappedValue = lo + (hi - lo) * juce::jlimit(0.0f, 1.0f, macroValue);
        if (std::abs(parameter->getValue() - mappedValue) > 0.0001f)
            parameter->setValue(mappedValue);
    }
}

// ==============================================================================
// getStateInformation / setStateInformation — delegate to PresetManager
// Allow the host DAW to save/restore the plugin's state.
// ==============================================================================

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (!presetManager) return;

    std::unique_ptr<juce::XmlElement> xml(presetManager->createPresetXml(
        presetManager->getCurrentPresetName().isEmpty()
            ? "DAW State"
            : presetManager->getCurrentPresetName()));

    if (xml)
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (!presetManager || sizeInBytes <= 0) return;

    // Block completely if a preset restore is already in progress or queued.
    // Some AU plugins call setStateInformation on the host during instantiation;
    // ignore those calls entirely to prevent a second applyPresetXml run.
    if (presetManager->isApplyingPreset || pendingStateApplyQueued) return;

    if (defaultPresetLoaded)
    {
        // Graph is already live — apply on message thread, but only once.
        pendingStateApplyQueued = true;
        juce::MemoryBlock stateCopy (data, (size_t)sizeInBytes);
        juce::MessageManager::callAsync([this, stateCopy = std::move(stateCopy)]() mutable
        {
            pendingStateApplyQueued = false;
            if (presetManager)
                if (auto xml = getXmlFromBinary(stateCopy.getData(), (int)stateCopy.getSize()))
                    presetManager->applyPresetXml(*xml);
        });
    }
    else
    {
        // prepareToPlay hasn't fired yet — stash and apply there.
        pendingStateData.replaceAll(data, (size_t)sizeInBytes);
    }
}

// ==============================================================================
// GRAPH
// ==============================================================================

void PluginProcessor::initialiseGraph()
{
    // Wipe all node pointers from slots before clearing — mainGraph->clear()
    // destroys every node, so any Node::Ptr still held in pluginSlots would
    // dangle. Reset them first so nothing references dead memory.
    for (auto& slot : pluginSlots)
        slot.node = nullptr;
    inputNode  = nullptr;
    outputNode = nullptr;

    mainGraph->clear();
    inputNode = mainGraph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    outputNode = mainGraph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    updateGraphConnections();
}

void PluginProcessor::reconfigureGraph()
{
    // Do not reconfigure while a preset is being applied — applyPresetXml
    // is adding nodes and would race with this method clearing connections.
    if (presetManager && presetManager->isApplyingPreset) return;

    if (!inputNode)
        inputNode = mainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    if (!outputNode)
        outputNode = mainGraph->addNode(
            std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
                juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    updateGraphConnections();
}

void PluginProcessor::updateGraphConnections()
{
    if (!mainGraph || !inputNode || !outputNode) return;

    for (auto& connection : mainGraph->getConnections())
        mainGraph->removeConnection(connection);

    juce::AudioProcessorGraph::Node::Ptr lastNode = inputNode;

    for (int i = 0; i < 8; ++i)
    {
        if (pluginSlots[i].isValid)
        {
            if (pluginSlots[i].node)
            {
                connectMainGraphAudioNodes(*mainGraph, *lastNode, *pluginSlots[i].node);
                lastNode = pluginSlots[i].node;
            }
        }
    }

    connectMainGraphAudioNodes(*mainGraph, *lastNode, *outputNode);
}

// ==============================================================================
// PARALLEL SPLIT
// ==============================================================================

void PluginProcessor::createParallelSplit(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 8) return;
    int existingSplits = 0;
    for (const auto& slot : pluginSlots)
        if (slot.isValid && slot.type == SlotType::ParallelSplit)
            ++existingSplits;
    if (existingSplits >= ParallelSplitProcessor::maxChains)
        return;

    ScopedProcessingSuspender suspend(*this);

    if (pluginSlots[slotIndex].isValid)
        removePlugin(slotIndex);

    auto parallelProcessor = std::make_unique<ParallelSplitProcessor>();
    ParallelSplitProcessor* rawPtr = parallelProcessor.get();
    auto parallelNode = mainGraph->addNode(std::make_unique<ParallelSplitGraphProcessor>(rawPtr));

    if (getSampleRate() > 0 && getBlockSize() > 0)
        rawPtr->prepareToPlay(getSampleRate(), getBlockSize());

    pluginSlots[slotIndex].name                = "Parallel Split";
    pluginSlots[slotIndex].isValid             = true;
    pluginSlots[slotIndex].type                = SlotType::ParallelSplit;
    pluginSlots[slotIndex].node                = parallelNode;
    pluginSlots[slotIndex].parallelProcessor   = rawPtr;
    pluginSlots[slotIndex].parallelSplitOwner  = std::move(parallelProcessor);

    juce::Logger::writeToLog("Created ParallelSplit at slot " + juce::String(slotIndex));
    updateGraphConnections();
}

void PluginProcessor::removePluginFromParallelSplit(juce::AudioProcessorGraph::Node& chain, int slotIndex)
{
    juce::Logger::writeToLog("Remove plugin from parallel split at slot " + juce::String(slotIndex));
}

// ==============================================================================
// PLUGIN HOSTING
// ==============================================================================

void PluginProcessor::loadPlugin(const juce::PluginDescription& description, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 8)
    {
        juce::Logger::writeToLog("ERROR: Invalid slot index");
        return;
    }

    const auto loadDesc = canonicalizeDescriptionForLoad(formatManager, knownPluginList, description);
    juce::Logger::writeToLog("=== loadPlugin called ===");
    juce::Logger::writeToLog("Slot Index: " + juce::String(slotIndex));
    juce::Logger::writeToLog("Plugin: " + loadDesc.name);
    juce::Logger::writeToLog("File: " + loadDesc.fileOrIdentifier);

    juce::String error;
    std::unique_ptr<juce::AudioProcessor> instance;

    try
    {
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        const int bs    = getBlockSize()  > 0   ? getBlockSize()  : 512;
        instance = formatManager.createPluginInstance(loadDesc, sr, bs, error);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("EXCEPTION: " + juce::String(e.what()));
        error = juce::String(e.what());
    }
    catch (...)
    {
        juce::Logger::writeToLog("EXCEPTION: Unknown exception during plugin loading");
        error = "Unknown exception";
    }

    if (instance)
    {
        juce::Logger::writeToLog("SUCCESS: Plugin loaded");
        instance->enableAllBuses();

        ScopedProcessingSuspender suspend(*this);
        if (pluginSlots[slotIndex].isValid)
            removePlugin(slotIndex);

        auto node = mainGraph->addNode(std::move(instance));
        pluginSlots[slotIndex].name              = loadDesc.name;
        pluginSlots[slotIndex].pluginFormat      = loadDesc.pluginFormatName;
        pluginSlots[slotIndex].fileOrIdentifier  = loadDesc.fileOrIdentifier;
        pluginSlots[slotIndex].uniqueId          = loadDesc.uniqueId;
        pluginSlots[slotIndex].isValid           = true;
        pluginSlots[slotIndex].type              = SlotType::Plugin;
        pluginSlots[slotIndex].node              = node;
        pluginSlots[slotIndex].parallelProcessor = nullptr;
        updateGraphConnections();

        if (getSampleRate() > 0.0 && getBlockSize() > 0)
        {
            mainGraph->setPlayConfigDetails(getMainBusNumInputChannels(),
                                            getMainBusNumOutputChannels(),
                                            getSampleRate(), getBlockSize());
            mainGraph->prepareToPlay(getSampleRate(), getBlockSize());
        }

        showPluginGUI(slotIndex);
    }
    else
    {
        removeKnownPlugin(loadDesc);
        juce::Logger::writeToLog("ERROR: Failed to create plugin instance: " + error);
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Plugin Load Failed",
            "Could not load: " + loadDesc.name + "\n\nReason: " + error);
    }
}

void PluginProcessor::removePlugin(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 8) return;
    ScopedProcessingSuspender suspend(*this);
    auto& slot = pluginSlots[slotIndex];

    if (slot.isValid)
    {
        removeMacroMappingsForTargetPath("M" + juce::String(slotIndex),
                                      slot.type == SlotType::ParallelSplit);

        if (slot.type == SlotType::ParallelSplit && slot.parallelProcessor != nullptr)
            slot.parallelProcessor->closeAllPluginWindows();

        destroyHostedPluginWindow(slot.editorWindow);

        if (slot.node)
            mainGraph->removeNode(slot.node->nodeID);

        if (slot.type == SlotType::ParallelSplit)
        {
            slot.parallelSplitOwner.reset();
            slot.parallelProcessor = nullptr;
        }

        slot.isValid           = false;
        slot.name              = "";
        slot.pluginFormat      = "";
        slot.fileOrIdentifier  = "";
        slot.uniqueId          = 0;
        slot.node              = nullptr;
        slot.type              = SlotType::Empty;
        slot.parallelProcessor = nullptr;

        updateGraphConnections();
    }
}

void PluginProcessor::closeAllPluginWindows()
{
    for (auto& slot : pluginSlots)
    {
        if (slot.type == SlotType::ParallelSplit && slot.parallelProcessor != nullptr)
            slot.parallelProcessor->closeAllPluginWindows();

        destroyHostedPluginWindow(slot.editorWindow);
    }
}

void PluginProcessor::moveOrSwapPlugin(int sourceSlot, int targetSlot)
{
    if (sourceSlot < 0 || sourceSlot >= 8 || targetSlot < 0 || targetSlot >= 8) return;
    if (sourceSlot == targetSlot) return;

    ScopedProcessingSuspender suspend(*this);

    auto& source = pluginSlots[sourceSlot];
    auto& target = pluginSlots[targetSlot];

    if (!source.isValid) return;

    const auto sourcePath = "M" + juce::String(sourceSlot);
    const auto targetPath = "M" + juce::String(targetSlot);

    if (target.isValid)
    {
        remapMacroTargetPaths(sourcePath, targetPath, true);
        std::swap(source.name,               target.name);
        std::swap(source.pluginFormat,       target.pluginFormat);
        std::swap(source.fileOrIdentifier,   target.fileOrIdentifier);
        std::swap(source.uniqueId,           target.uniqueId);
        std::swap(source.isValid,            target.isValid);
        std::swap(source.bypassed,           target.bypassed);
        std::swap(source.type,               target.type);
        std::swap(source.node,               target.node);
        std::swap(source.editorWindow,       target.editorWindow);
        std::swap(source.parallelProcessor,  target.parallelProcessor);
        std::swap(source.parallelSplitOwner, target.parallelSplitOwner);
    }
    else
    {
        remapMacroTargetPaths(sourcePath, targetPath, false);

        target.name                = source.name;
        target.pluginFormat        = source.pluginFormat;
        target.fileOrIdentifier    = source.fileOrIdentifier;
        target.uniqueId            = source.uniqueId;
        target.node                = source.node;
        target.isValid             = true;
        target.bypassed            = source.bypassed;
        target.type                = source.type;
        target.editorWindow        = std::move(source.editorWindow);
        target.parallelProcessor   = source.parallelProcessor;
        target.parallelSplitOwner  = std::move(source.parallelSplitOwner);

        source.isValid             = false;
        source.name                = "";
        source.pluginFormat        = "";
        source.fileOrIdentifier    = "";
        source.uniqueId            = 0;
        source.node                = nullptr;
        source.bypassed            = false;
        source.type                = SlotType::Empty;
        source.parallelProcessor   = nullptr;
    }

    updateGraphConnections();
}

void PluginProcessor::showPluginGUI(int slotIndex)
{
    PluginSearchDialog::dismissActive();
    auto& slot = pluginSlots[slotIndex];
    if (slot.isValid && slot.node && slot.node->getProcessor()->hasEditor())
    {
        if (slot.editorWindow)
        {
            if (slot.editorWindow->isVisible())
            {
                slot.editorWindow->setVisible(false);
            }
            else
            {
                slot.editorWindow->setMinimised(false);
                slot.editorWindow->setAlwaysOnTop(true);
                slot.editorWindow->setVisible(true);
                slot.editorWindow->toFront(true);
            }
        }
        else
        {
            auto* editor = slot.node->getProcessor()->createEditor();
            if (editor)
                slot.editorWindow = std::make_unique<PluginWindow>(slot.name, editor);
        }
    }
}

// ==============================================================================
// BYPASS
// ==============================================================================

void PluginProcessor::setSlotBypassed(int slotIndex, bool bypassed)
{
    setSlotBypassedInternal(slotIndex, bypassed, true);
}

void PluginProcessor::setSlotBypassedInternal(int slotIndex, bool bypassed, bool notifyHost)
{
    if (slotIndex < 0 || slotIndex >= 8) return;
    pluginSlots[slotIndex].bypassed = bypassed;

    if (auto* parameter = masterSlotBypassParameters[(size_t)slotIndex])
    {
        const float wantedOn = bypassed ? 0.0f : 1.0f;
        if (notifyHost)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(wantedOn);
            parameter->endChangeGesture();
        }
        else
        {
            *parameter = !bypassed;
        }
    }

    if (pluginSlots[slotIndex].type == SlotType::Plugin
        && pluginSlots[slotIndex].node)
    {
        pluginSlots[slotIndex].node->setBypassed(bypassed);
    }
    else if (pluginSlots[slotIndex].type == SlotType::ParallelSplit
             && pluginSlots[slotIndex].parallelProcessor)
    {
        if (pluginSlots[slotIndex].node)
            pluginSlots[slotIndex].node->setBypassed(bypassed);
    }
}

bool PluginProcessor::isSlotBypassed(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= 8) return false;
    return pluginSlots[slotIndex].bypassed;
}

void PluginProcessor::setInputGainDb(float db, bool notifyHost)
{
    const float clamped = clampProcessorKnobDb(db);
    inputGain.store(clamped);
    if (inputGainParameter != nullptr)
    {
        const float norm = inputGainParameter->convertTo0to1(clamped);
        if (notifyHost)
        {
            inputGainParameter->beginChangeGesture();
            inputGainParameter->setValueNotifyingHost(norm);
            inputGainParameter->endChangeGesture();
        }
        else
        {
            juce::ignoreUnused(norm);
        }
    }
}

void PluginProcessor::setMasterVolumeDb(float db, bool notifyHost)
{
    const float clamped = clampProcessorKnobDb(db);
    masterVolume.store(processorKnobDbToGain(clamped));
    if (masterVolumeParameter != nullptr)
    {
        const float norm = masterVolumeParameter->convertTo0to1(clamped);
        if (notifyHost)
        {
            masterVolumeParameter->beginChangeGesture();
            masterVolumeParameter->setValueNotifyingHost(norm);
            masterVolumeParameter->endChangeGesture();
        }
        else
        {
            juce::ignoreUnused(norm);
        }
    }
}

float PluginProcessor::getMasterVolumeDb() const
{
    if (masterVolumeParameter != nullptr)
        return clampProcessorKnobDb(masterVolumeParameter->get());

    return clampProcessorKnobDb(juce::Decibels::gainToDecibels(masterVolume.load(), kProcessorGainMinDb));
}

// ==============================================================================
// Cross-chain moves
// Transfers a plugin between master and parallel chains.
// Strategy: Extracts the AudioProcessor from the source node via transferable ownership,
// adds it to the destination, then removes the empty source.
// ==============================================================================

// Helper: Safely create an instance using a validated copy of the description.
// Prevents crashes in AudioUnitPluginFormat::fileMightContain on corrupt entries.
static std::unique_ptr<juce::AudioProcessor> safeCreateInstance(
    juce::AudioPluginFormatManager& fm,
    const juce::PluginDescription* desc,
    double sr, int bs)
{
    if (!desc
        || desc->name.isEmpty()
        || desc->fileOrIdentifier.isEmpty()
        || desc->fileOrIdentifier.length() < 2
        || desc->pluginFormatName.isEmpty())
        return nullptr;

    juce::PluginDescription copy = *desc;
    juce::String error;
    std::unique_ptr<juce::AudioProcessor> instance;
    try { instance = fm.createPluginInstance(copy, sr, bs, error); }
    catch (...) {}
    return instance;
}

static const juce::PluginDescription* findKnownPluginDescription(
    const juce::KnownPluginList& knownPluginList,
    const juce::String& name,
    const juce::String& format,
    const juce::String& fileOrIdentifier,
    int uniqueId);

static juce::PluginDescription canonicalizeDescriptionForLoad(
    juce::AudioPluginFormatManager& fm,
    const juce::KnownPluginList& knownPluginList,
    const juce::PluginDescription& desc)
{
    juce::PluginDescription best = desc;

    if (desc.fileOrIdentifier.isEmpty() || desc.pluginFormatName.isEmpty())
        return best;

    // Enumerate real bundle paths before trusting the cache. The cache may
    // contain metadata-only VST3 descriptions that are useful for search but
    // not sufficient for createPluginInstance().
    for (auto* fmt : fm.getFormats())
    {
        if (fmt == nullptr || fmt->getName() != desc.pluginFormatName)
            continue;
        if (! desc.fileOrIdentifier.endsWithIgnoreCase(".vst3")
            && ! desc.fileOrIdentifier.endsWithIgnoreCase(".component"))
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        try
        {
            bool enumerated = false;
            if (juce::MessageManager::existsAndIsCurrentThread())
            {
                fmt->findAllTypesForFile(found, desc.fileOrIdentifier);
                enumerated = true;
            }
            else
            {
                enumerated = enumeratePluginTypesOnMessageThread(*fmt, desc.fileOrIdentifier, found);
            }

            if (! enumerated)
            {
                juce::Logger::writeToLog("canonicalizeDescriptionForLoad: enumeration failed for " + desc.fileOrIdentifier);
                return best;
            }
        }
        catch (...)
        {
            juce::Logger::writeToLog("canonicalizeDescriptionForLoad: scan fallito per "
                                     + desc.fileOrIdentifier);
            return best;
        }

        for (auto* candidate : found)
        {
            if (candidate == nullptr)
                continue;
            if (desc.uniqueId != 0 && candidate->uniqueId == desc.uniqueId)
                return *candidate;
            if (candidate->name == desc.name)
                return *candidate;
        }

        for (auto* candidate : found)
        {
            if (candidate == nullptr)
                continue;
            if (candidate->descriptiveName.isNotEmpty()
                && candidate->descriptiveName == desc.name)
                return *candidate;
        }

        if (found.size() > 0 && found[0] != nullptr)
            return *found[0];
    }

    if (const auto* knownDesc = findKnownPluginDescription(knownPluginList,
                                                            desc.name,
                                                            desc.pluginFormatName,
                                                            desc.fileOrIdentifier,
                                                            desc.uniqueId))
    {
        juce::Logger::writeToLog("canonicalizeDescriptionForLoad: found in cache for " + desc.name);
        return *knownDesc;
    }

    if (desc.fileOrIdentifier.endsWithIgnoreCase(".bundle"))
    {
        // If this is a Waves bundle, first try to resolve the plugin from the
        // already-known plugin cache to avoid an expensive file-system search
        // for WaveShell components which can look like a full scan to the user.
        if (desc.fileOrIdentifier.containsIgnoreCase("Waves"))
        {
            // Try exact match by fileOrIdentifier / uniqueId first
            if (const auto* known = findKnownPluginDescription(knownPluginList, desc.name, {}, desc.fileOrIdentifier, desc.uniqueId))
                return *known;

            // Fall back to a name-based match in the cache (cheaper than scanning disk)
            for (const auto& candidate : knownPluginList.getTypes())
            {
                if (candidate.name.compareIgnoreCase(desc.name) == 0)
                    return candidate;
            }

            // Only if the cache did not contain a matching entry, fall back to
            // enumerating WaveShell components on disk (legacy behaviour).
            for (auto* fmt : fm.getFormats())
            {
                if (fmt == nullptr) continue;
                const juce::String fName = fmt->getName();
                if (fName != "VST3" && fName != "AudioUnit")
                    continue;

                juce::FileSearchPath searchPaths = fmt->getDefaultLocationsToSearch();

                for (int pi = 0; pi < searchPaths.getNumPaths(); ++pi)
                {
                    juce::File root = searchPaths[pi];
                    if (! root.exists()) continue;

                    juce::Array<juce::File> shellFiles;
                    // find only shell plugin bundles for this format
                    juce::String pattern;
                    int findFlags = juce::File::findFiles;
                    if (fName == "VST3")
                    {
                        pattern = "*WaveShell*.vst3";
                        findFlags = juce::File::findDirectories;
                    }
                    else if (fName == "AudioUnit")
                    {
                        pattern = "*WaveShell*.component";
                        findFlags = juce::File::findDirectories;
                    }
                    else
                    {
                        pattern = "*WaveShell*";
                    }

                    root.findChildFiles(shellFiles, findFlags, true, pattern);

                    for (const auto& shell : shellFiles)
                    {
                        juce::Logger::writeToLog("canonicalize: checking shell: " + shell.getFullPathName());
                        juce::OwnedArray<juce::PluginDescription> shellDescs;
                        try
                        {
                            // Prefer enumeration on message thread for safety.
                            if (! juce::MessageManager::existsAndIsCurrentThread())
                                enumeratePluginTypesOnMessageThread(*fmt, shell.getFullPathName(), shellDescs);
                            else
                                fmt->findAllTypesForFile(shellDescs, shell.getFullPathName());
                        }
                        catch (...) { juce::Logger::writeToLog("canonicalize: findAllTypesForFile threw for " + shell.getFullPathName()); continue; }

                        for (auto* sd : shellDescs)
                        {
                            if (sd == nullptr) continue;
                            juce::Logger::writeToLog("canonicalize: shell exposes: '" + sd->name + "' | '" + sd->fileOrIdentifier + "'");
                            
                                // Prima: exact match sul name (case-insensitive)
                                if (sd->name.compareIgnoreCase(desc.name) == 0)
                            {
                                    juce::Logger::writeToLog("canonicalize: exact name match '" + desc.name + "' -> '" + sd->fileOrIdentifier + "'");
                                return *sd;
                            }
                        }
                        
                            // If no exact match is found, try matching on descriptiveName or uid
                            for (auto* sd : shellDescs)
                            {
                                if (sd == nullptr) continue;
                            
                                // Second: exact match on descriptiveName (for Waves bundles with various formats)
                                if (sd->descriptiveName.isNotEmpty() 
                                    && sd->descriptiveName.compareIgnoreCase(desc.name) == 0)
                                {
                                    juce::Logger::writeToLog("canonicalize: descriptiveName match '" + desc.name 
                                                           + "' -> '" + sd->fileOrIdentifier + "'");
                                    return *sd;
                                }
                            
                                // Third: fuzzy match on name (only if desc.name is a significant substring)
                                // E.g., "PAZ" -> "PAZ-Meters Stereo" OK, but not the other way around
                                const juce::String nameLower = desc.name.toLowerCase();
                                const juce::String sdNameLower = sd->name.toLowerCase();
                            
                                // Accept match if:
                                // - The file name is exactly the name + something
                                if (sdNameLower.startsWith(nameLower) 
                                    && sdNameLower.length() <= nameLower.length() + 20)
                                {
                                    juce::Logger::writeToLog("canonicalize: fuzzy prefix match '" + desc.name 
                                                           + "' -> '" + sd->fileOrIdentifier + "'");
                                    return *sd;
                                }
                            }
                        juce::Logger::writeToLog("canonicalize: no match in shell: " + shell.getFullPathName());
                    }
                }
            }
        }

        // Generic bundle fallback: look for a non-bundle known type with same name
        for (const auto& candidate : knownPluginList.getTypes())
        {
            if (candidate.name != desc.name)
                continue;

            if (candidate.fileOrIdentifier.isNotEmpty()
                && ! candidate.fileOrIdentifier.endsWithIgnoreCase(".bundle")
                && (candidate.pluginFormatName == "VST3" || candidate.pluginFormatName == "AudioUnit"))
                return candidate;
        }

        for (const auto& candidate : knownPluginList.getTypes())
        {
            if (candidate.name == desc.name
                && candidate.fileOrIdentifier.isNotEmpty()
                && ! candidate.fileOrIdentifier.endsWithIgnoreCase(".bundle"))
                return candidate;
        }
    }


    return best;
}

static bool resolvePluginDescription(const juce::KnownPluginList& knownPluginList,
                                     juce::AudioProcessor* liveProcessor,
                                     const juce::String& name,
                                     const juce::String& format,
                                     const juce::String& fileOrIdentifier,
                                     int uniqueId,
                                     juce::PluginDescription& out)
{
    if (auto* liveInstance = dynamic_cast<juce::AudioPluginInstance*>(liveProcessor))
    {
        liveInstance->fillInPluginDescription(out);
        if (out.name.isNotEmpty() && out.pluginFormatName.isNotEmpty() && out.fileOrIdentifier.isNotEmpty())
            return true;
    }

    if (const auto* desc = findKnownPluginDescription(knownPluginList, name, format, fileOrIdentifier, uniqueId))
    {
        out = *desc;
        return true;
    }

    return false;
}

void PluginProcessor::rebuildLoadablePluginCache()
{
    juce::Array<juce::PluginDescription> result;
    juce::StringArray seen;

    const auto snapshot = getKnownPluginDescriptions();
    auto addIfUnseen = [&result, &seen](const juce::PluginDescription& desc)
    {
        const auto key = desc.pluginFormatName + "|" + desc.fileOrIdentifier
                       + "|" + juce::String(desc.uniqueId) + "|" + desc.name;
        if (seen.contains(key))
            return;

        seen.add(key);
        result.add(desc);
    };

    for (const auto& desc : snapshot)
    {
        if (desc.pluginFormatName != "AudioUnit"
            && desc.pluginFormatName != "VST3"
            && desc.pluginFormatName != "VST")
            continue;

        addIfUnseen(desc);
    }

    std::sort(result.begin(), result.end(),
              [](const juce::PluginDescription& a, const juce::PluginDescription& b)
              {
                  const int byName = a.name.compareIgnoreCase(b.name);
                  if (byName != 0) return byName < 0;
                  const int byMaker = a.manufacturerName.compareIgnoreCase(b.manufacturerName);
                  if (byMaker != 0) return byMaker < 0;
                  return a.pluginFormatName.compareIgnoreCase(b.pluginFormatName) < 0;
              });

    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    loadablePluginCache = std::move(result);
    loadablePluginCacheDirty = false;
}

juce::Array<juce::PluginDescription> PluginProcessor::getLoadablePluginDescriptions()
{
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        if (loadablePluginCacheInitialized && !loadablePluginCacheDirty)
            return loadablePluginCache;
    }

    if (!loadablePluginCacheInitialized)
    {
        rebuildLoadablePluginCache();
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        loadablePluginCacheInitialized = true;
    }
    else if (loadablePluginCacheDirty)
    {
        // Cache marked dirty after first initialization; rebuild it
        rebuildLoadablePluginCache();
    }

    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return loadablePluginCache;
}

juce::Array<juce::PluginDescription> PluginProcessor::getKnownPluginDescriptions() const
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return knownPluginList.getTypes();
}

juce::PluginDescription PluginProcessor::canonicalizePluginDescriptionForLoad(const juce::PluginDescription& description)
{
    return canonicalizeDescriptionForLoad(formatManager, knownPluginList, description);
}

void PluginProcessor::removeKnownPlugin(const juce::PluginDescription& description)
{
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        knownPluginList.removeType(description);
        loadablePluginCacheDirty = true;
    }

    loadPluginMetadata();
}

static const juce::PluginDescription* findKnownPluginDescription(
    const juce::KnownPluginList& knownPluginList,
    const juce::String& name,
    const juce::String& format,
    const juce::String& fileOrIdentifier,
    int uniqueId)
{
    // Priority 0: Exact match su fileOrIdentifier + name + format
    // (important for Waves bundles that share the same fileOrIdentifier)
    if (fileOrIdentifier.isNotEmpty() && name.isNotEmpty())
    {
        for (const auto& d : knownPluginList.getTypes())
        {
            if (format.isNotEmpty() && d.pluginFormatName != format)
                continue;
            if (d.fileOrIdentifier == fileOrIdentifier && d.name == name)
                return &d;
        }
    }
    
    for (const auto& d : knownPluginList.getTypes())
    {
        if (format.isNotEmpty() && d.pluginFormatName != format)
            continue;
        if (fileOrIdentifier.isNotEmpty() && d.fileOrIdentifier == fileOrIdentifier && name.isEmpty())
            return &d;
    }

    for (const auto& d : knownPluginList.getTypes())
    {
        if (format.isNotEmpty() && d.pluginFormatName != format)
            continue;
        if (uniqueId != 0 && d.uniqueId == uniqueId)
            return &d;
    }

    for (const auto& d : knownPluginList.getTypes())
    {
        if (format.isNotEmpty() && d.pluginFormatName != format)
            continue;
        if (d.name == name)
            return &d;
    }

    return nullptr;
}

void PluginProcessor::moveFromMasterToParallel(int masterSlot,
                                                ParallelSplitProcessor* destParallel,
                                                int destChainIndex, int destSlotIndex)
{
    if (masterSlot < 0 || masterSlot >= 8 || !destParallel) return;
    ScopedProcessingSuspender suspend(*this);
    auto& src = pluginSlots[masterSlot];
    if (!src.isValid || src.type != SlotType::Plugin || !src.node) return;

    auto& destChain = destParallel->getChain(destChainIndex);
    const auto sourcePath = "M" + juce::String(masterSlot);
    const auto destSplitPath = getMacroTargetPathForSplit(destParallel);
    const auto destPath = destSplitPath.isNotEmpty()
        ? destSplitPath + "/C" + juce::String(destChainIndex) + "/S" + juce::String(destSlotIndex)
        : juce::String();
    if (destPath.isNotEmpty() && destChain.slots[(size_t)destSlotIndex].valid)
        removeMacroMappingsForTargetPath(destPath,
            destChain.slots[(size_t)destSlotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit);

    // Save name and bypassed before removing
    juce::String name     = src.name;
    bool         bypassed = src.bypassed;

    // Save the plugin state BEFORE any modifications
    juce::MemoryBlock state;
    auto* srcProc = src.node->getProcessor();
    if (srcProc)
        srcProc->getStateInformation(state);

    juce::PluginDescription resolvedDesc;
    if (!resolvePluginDescription(knownPluginList, srcProc, src.name, src.pluginFormat,
                                  src.fileOrIdentifier, src.uniqueId, resolvedDesc))
        return;

    double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    int bs = getBlockSize() > 0 ? getBlockSize() : 512;

    auto instance = safeCreateInstance(formatManager, &resolvedDesc, sr, bs);
    if (!instance) return;

    if (state.getSize() > 0)
        instance->setStateInformation(state.getData(), (int)state.getSize());

    // Remove from master chain - null out references BEFORE removing node
    src.editorWindow.reset();

    // Remove the node from mainGraph - this destroys the AudioProcessor
    if (src.node)
        mainGraph->removeNode(src.node->nodeID);

    // Reset slot state AFTER removing from graph
    src.node = nullptr;
    src.isValid = false;
    src.name.clear();
    src.pluginFormat.clear();
    src.fileOrIdentifier.clear();
    src.uniqueId = 0;
    src.type = SlotType::Empty;

    updateGraphConnections();

    if (destPath.isNotEmpty())
        remapMacroTargetPaths(sourcePath, destPath, false);

    // Load the plugin in the parallel chain
    destParallel->loadPluginInChain(destChain, destSlotIndex, std::move(instance), name,
                                    resolvedDesc.pluginFormatName, resolvedDesc.fileOrIdentifier, resolvedDesc.uniqueId);
    destParallel->setSlotBypassed(destChain, destSlotIndex, bypassed);
}

void PluginProcessor::moveFromParallelToMaster(ParallelSplitProcessor* srcParallel,
                                                int srcChainIndex, int srcSlotIndex,
                                                int masterSlot)
{
    if (!srcParallel || masterSlot < 0 || masterSlot >= 8) return;
    ScopedProcessingSuspender suspend(*this);
    auto& srcChain = srcParallel->getChain(srcChainIndex);
    const auto sourceSplitPath = getMacroTargetPathForSplit(srcParallel);
    const auto sourcePath = sourceSplitPath.isNotEmpty()
        ? sourceSplitPath + "/C" + juce::String(srcChainIndex) + "/S" + juce::String(srcSlotIndex)
        : juce::String();
    const auto destPath = "M" + juce::String(masterSlot);

    auto& slot = srcChain.slots[srcSlotIndex];
    if (!slot.valid || !slot.node) return;
    if (slot.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit) return;

    juce::String name     = slot.name;
    bool         bypassed = slot.bypassed;

    // Save the plugin state BEFORE any modifications
    juce::MemoryBlock state;
    auto* srcProc = slot.node->getProcessor();
    if (srcProc)
        srcProc->getStateInformation(state);

    juce::PluginDescription resolvedDesc;
    if (!resolvePluginDescription(knownPluginList, srcProc, slot.name, slot.pluginFormat,
                                  slot.fileOrIdentifier, slot.uniqueId, resolvedDesc))
        return;

    double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    int bs = getBlockSize() > 0 ? getBlockSize() : 512;

    auto instance = safeCreateInstance(formatManager, &resolvedDesc, sr, bs);
    if (!instance) return;

    if (state.getSize() > 0)
        instance->setStateInformation(state.getData(), (int)state.getSize());

    if (pluginSlots[masterSlot].isValid)
        removeMacroMappingsForTargetPath(destPath, pluginSlots[masterSlot].type == SlotType::ParallelSplit);

    // Remove from parallel chain - null out editorWindow before removing node
    slot.editorWindow.reset();

    // Remove the node from the parallel chain - this destroys the AudioProcessor
    srcParallel->removePluginFromChain(srcChain, srcSlotIndex);

    // Remove any existing plugins in the target slot
    if (pluginSlots[masterSlot].isValid)
        removePlugin(masterSlot);

    if (sourcePath.isNotEmpty())
        remapMacroTargetPaths(sourcePath, destPath, false);

    // Add the new node to the master chain
    auto node = mainGraph->addNode(std::move(instance));

    // Configure the slot
    pluginSlots[masterSlot].name             = name;
    pluginSlots[masterSlot].pluginFormat     = resolvedDesc.pluginFormatName;
    pluginSlots[masterSlot].fileOrIdentifier = resolvedDesc.fileOrIdentifier;
    pluginSlots[masterSlot].uniqueId         = resolvedDesc.uniqueId;
    pluginSlots[masterSlot].isValid          = true;
    pluginSlots[masterSlot].type             = SlotType::Plugin;
    pluginSlots[masterSlot].node             = node;
    pluginSlots[masterSlot].bypassed         = bypassed;

    if (bypassed && node)
        node->setBypassed(true);

    updateGraphConnections();
}

void PluginProcessor::moveParallelToParallel(ParallelSplitProcessor* srcParallel,
                                              int srcChainIndex, int srcSlotIndex,
                                              ParallelSplitProcessor* dstParallel,
                                              int dstChainIndex, int dstSlotIndex)
{
    if (!srcParallel || !dstParallel) return;
    ScopedProcessingSuspender suspend(*this);

    const auto srcSplitPath = getMacroTargetPathForSplit(srcParallel);
    const auto dstSplitPath = getMacroTargetPathForSplit(dstParallel);
    const auto sourcePath = srcSplitPath.isNotEmpty()
        ? srcSplitPath + "/C" + juce::String(srcChainIndex) + "/S" + juce::String(srcSlotIndex)
        : juce::String();
    const auto destPath = dstSplitPath.isNotEmpty()
        ? dstSplitPath + "/C" + juce::String(dstChainIndex) + "/S" + juce::String(dstSlotIndex)
        : juce::String();

    // Case same processor and same chain → use moveOrSwapInChain
    if (srcParallel == dstParallel && srcChainIndex == dstChainIndex)
    {
        auto& chain = srcParallel->getChain(srcChainIndex);
        const bool dstValid = chain.slots[(size_t)dstSlotIndex].valid;
        if (sourcePath.isNotEmpty() && destPath.isNotEmpty())
            remapMacroTargetPaths(sourcePath, destPath, dstValid);
        srcParallel->moveOrSwapInChain(chain, srcSlotIndex, dstSlotIndex);
        return;
    }

    auto& srcChain = srcParallel->getChain(srcChainIndex);
    auto& dstChain = dstParallel->getChain(dstChainIndex);
    if (destPath.isNotEmpty() && dstChain.slots[(size_t)dstSlotIndex].valid)
        removeMacroMappingsForTargetPath(destPath,
            dstChain.slots[(size_t)dstSlotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit);

    auto& slot = srcChain.slots[srcSlotIndex];
    if (!slot.valid || !slot.node) return;

    if (slot.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit)
    {
        if (dstChain.slots[dstSlotIndex].valid)
            dstParallel->removePluginFromChain(dstChain, dstSlotIndex);

        if (sourcePath.isNotEmpty() && destPath.isNotEmpty())
            remapMacroTargetPaths(sourcePath, destPath, false);

        auto nestedOwner = std::move(slot.parallelSplitOwner);
        auto* nestedRaw = nestedOwner.get();
        if (slot.node)
            srcChain.chainGraph->removeNode(slot.node->nodeID);

        auto& dst = dstChain.slots[dstSlotIndex];
        dst.node = dstChain.chainGraph->addNode(std::make_unique<NestedParallelSplitGraphProcessor>(nestedRaw));
        dst.name = "Parallel Split";
        dst.valid = true;
        dst.bypassed = slot.bypassed;
        dst.type = ParallelSplitProcessor::ChainSlotType::ParallelSplit;
        dst.parallelProcessor = nestedRaw;
        dst.parallelSplitOwner = std::move(nestedOwner);

        slot.node = nullptr;
        slot.valid = false;
        slot.bypassed = false;
        slot.type = ParallelSplitProcessor::ChainSlotType::Empty;
        slot.parallelProcessor = nullptr;
        slot.name.clear();

        srcParallel->updateChainConnections(srcChain);
        dstParallel->updateChainConnections(dstChain);
        return;
    }

    juce::String name     = slot.name;
    bool         bypassed = slot.bypassed;

    // Save the plugin state BEFORE any modifications
    juce::MemoryBlock state;
    auto* srcProc = slot.node->getProcessor();
    if (srcProc)
        srcProc->getStateInformation(state);

    juce::PluginDescription resolvedDesc;
    if (!resolvePluginDescription(knownPluginList, srcProc, slot.name, slot.pluginFormat,
                                  slot.fileOrIdentifier, slot.uniqueId, resolvedDesc))
        return;

    double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    int bs = getBlockSize() > 0 ? getBlockSize() : 512;

    auto instance = safeCreateInstance(formatManager, &resolvedDesc, sr, bs);
    if (!instance) return;

    if (state.getSize() > 0)
        instance->setStateInformation(state.getData(), (int)state.getSize());

    if (sourcePath.isNotEmpty() && destPath.isNotEmpty())
        remapMacroTargetPaths(sourcePath, destPath, false);

    // Remove from parallel chain - null out editorWindow before removing node
    slot.editorWindow.reset();

    srcParallel->removePluginFromChain(srcChain, srcSlotIndex);

    // Load the plugin in the parallel chain
    dstParallel->loadPluginInChain(dstChain, dstSlotIndex, std::move(instance), name,
                                   resolvedDesc.pluginFormatName, resolvedDesc.fileOrIdentifier, resolvedDesc.uniqueId);
    dstParallel->setSlotBypassed(dstChain, dstSlotIndex, bypassed);
}

// ==============================================================================
// Scanning
// ==============================================================================

void PluginProcessor::startScanning(bool clearCacheFirst)
{
    if (isScanning()) return;
    scanFolderOverride.clear();

    if (clearCacheFirst)
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        knownPluginList.clear();
        getKnownPluginsFile().deleteFile();
        getScanCrashListFile().deleteFile();
        getScanInProgressFile().deleteFile();
    }

    shouldStopScanning  = false;
    scanProgress        = 0.0f;
    scanNewPluginsFound = 0;
    scanClearCache      = clearCacheFirst;
    scanCompletedWithError = false;
    isScanningFlag      = true;
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        scanCurrentName = {};
        scanErrorMessage = {};
    }
    getScanInProgressFile().deleteFile();

    scanThread = std::make_unique<std::thread>(&PluginProcessor::scanPluginsThread, this);
    startTimerHz(10);
}

void PluginProcessor::startScanningFolder(const juce::File& folderToScan)
{
    if (isScanning() || !folderToScan.exists())
        return;

    shouldStopScanning  = false;
    scanProgress        = 0.0f;
    scanNewPluginsFound = 0;
    scanClearCache      = false;
    scanCompletedWithError = false;
    scanFolderOverride  = folderToScan.getFullPathName();
    isScanningFlag      = true;
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        scanCurrentName = {};
        scanErrorMessage = {};
    }
    getScanInProgressFile().deleteFile();

    scanThread = std::make_unique<std::thread>(&PluginProcessor::scanPluginsThread, this);
    startTimerHz(10);
}

void PluginProcessor::scanPluginsThread()
{
    bool fullScan = scanClearCache.load();
    bool scanHadRecoverableErrors = false;
    juce::StringArray failedEntries;
    juce::StringArray crashBlacklistedEntries = readStringListFile(getScanCrashListFile());
    bool knownPluginListChanged = false;

    auto markScanFailure = [this, &scanHadRecoverableErrors, &failedEntries](const juce::String& item,
                                                                             const juce::String& reason)
    {
        scanHadRecoverableErrors = true;

        juce::String cleanItem = item.isNotEmpty() ? item : "Unknown plugin";
        juce::String cleanReason = reason.isNotEmpty() ? reason : "Unknown error";
        juce::String failureLine = cleanItem + " - " + cleanReason;

        juce::Logger::writeToLog("Scanner: failure: " + failureLine);

        if (failedEntries.contains(failureLine))
            return;

        if (failedEntries.size() < 5)
            failedEntries.add(failureLine);
    };

    auto exceptionToString = [](const std::exception& e) -> juce::String
    {
        return juce::String(e.what()).trim();
    };

    auto persistKnownPlugins = [this]()
    {
        auto xml = std::unique_ptr<juce::XmlElement>();
        {
            const juce::SpinLock::ScopedLockType lock(scanSpinLock);
            xml = knownPluginList.createXml();
            loadablePluginCacheDirty = true;
        }

        if (xml != nullptr)
            xml->writeTo(getKnownPluginsFile());
    };

    try
    {
        juce::Logger::writeToLog("Scan: started, fullScan=" + juce::String(fullScan ? "1" : "0")
                                 + " override='" + scanFolderOverride + "'");

        {
            const juce::SpinLock::ScopedLockType lock(scanSpinLock);
            scanProgress = 0.01f;
            scanCurrentName = "Preparing scan";
        }

        juce::Array<juce::AudioPluginFormat*> formats;
        for (auto* fmt : formatManager.getFormats())
        {
            const juce::String name = fmt->getName();
            if (name == "VST3" || name == "AudioUnit" || name == "VST")
                formats.add(fmt);
        }

        juce::Array<juce::File> filesToScan;
        juce::StringArray seenScanPaths;

        juce::StringArray knownPaths;
        if (!fullScan)
        {
            for (const auto& desc : knownPluginList.getTypes())
                knownPaths.add(desc.fileOrIdentifier);
        }

        for (auto* fmt : formats)
        {
            if (shouldStopScanning) break;

            juce::Logger::writeToLog("Scan: collecting candidates for format " + fmt->getName());

            juce::FileSearchPath paths = scanFolderOverride.isNotEmpty()
                           ? juce::FileSearchPath(scanFolderOverride)
                           : buildSearchPathsForFormat(*fmt);

       #if JUCE_MAC
        juce::Logger::writeToLog("Scan: WaveShell discovery is deferred to avoid UI-thread crashes");
       #endif

            juce::String extension;
            if      (fmt->getName() == "VST3")      extension = "*.vst3";
            else if (fmt->getName() == "AudioUnit") extension = "*.component";
            else if (fmt->getName() == "VST")       extension = "*.vst";
            else continue;

            for (int pi = 0; pi < paths.getNumPaths(); ++pi)
            {
                if (shouldStopScanning) break;
                juce::Array<juce::File> found;
                try
                {
                    const auto rootPath = paths[pi].getFullPathName();
                    if ((extension == "*.vst3" && rootPath.endsWithIgnoreCase(".vst3"))
                        || (extension == "*.component" && rootPath.endsWithIgnoreCase(".component"))
                        || (extension == "*.vst" && rootPath.endsWithIgnoreCase(".vst")))
                        found.add(paths[pi]);

                    paths[pi].findChildFiles(found,
                                             juce::File::findDirectories | juce::File::findFiles,
                                             true, extension);
                }
                catch (const std::exception& e)
                {
                    markScanFailure(paths[pi].getFullPathName(), exceptionToString(e));
                    continue;
                }
                catch (...)
                {
                    markScanFailure(paths[pi].getFullPathName(), "Could not read this search path");
                    continue;
                }

                for (const auto& f : found)
                {
                    const auto fullPath = f.getFullPathName();
                    const bool isWaveShellWrapper = fullPath.containsIgnoreCase("WaveShell")
                                                 && (fullPath.endsWithIgnoreCase(".vst3")
                                                     || fullPath.endsWithIgnoreCase(".component"));

                    if (isMacWavesPayloadPath(fullPath))
                        continue;
                    const bool isLegacyVst = fullPath.endsWithIgnoreCase(".vst");
                    if (!isLegacyVst && crashBlacklistedEntries.contains(fullPath))
                    {
                        markScanFailure(fullPath, "Skipped because it crashed a previous scan on this machine");
                        continue;
                    }

                    if (!fullScan && !isWaveShellWrapper && knownPaths.contains(f.getFullPathName()))
                        continue;
                    if (seenScanPaths.indexOf(fullPath, true) >= 0)
                        continue;
                    seenScanPaths.add(fullPath);
                    filesToScan.add(f);
                }
            }
        }

       #if JUCE_MAC
        {
            juce::StringArray waveShellRoots;
            waveShellRoots.add("/Library/Audio/Plug-Ins/VST3");
            waveShellRoots.add("/Library/Audio/Plug-Ins/Components");
            waveShellRoots.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                   .getChildFile("Library/Audio/Plug-Ins/VST3")
                                   .getFullPathName());
            waveShellRoots.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                   .getChildFile("Library/Audio/Plug-Ins/Components")
                                   .getFullPathName());
            waveShellRoots.add("/Applications/Waves/WaveShells V16");
            waveShellRoots.add("/Applications/Waves");

            for (const auto& rootPath : waveShellRoots)
            {
                juce::File root(rootPath);
                if (!root.exists())
                    continue;

                try
                {
                    juce::Array<juce::File> waveShells;
                    root.findChildFiles(waveShells, juce::File::findDirectories, false, "WaveShell*-VST3*.vst3");
                    root.findChildFiles(waveShells, juce::File::findDirectories, false, "WaveShell*.vst3");
                    root.findChildFiles(waveShells, juce::File::findDirectories, false, "WaveShell*-AU*.component");
                    root.findChildFiles(waveShells, juce::File::findDirectories, false, "WaveShell*.component");
                    root.findChildFiles(waveShells, juce::File::findDirectories, true, "WaveShell*-VST3*.vst3");
                    root.findChildFiles(waveShells, juce::File::findDirectories, true, "WaveShell*.vst3");
                    root.findChildFiles(waveShells, juce::File::findDirectories, true, "WaveShell*-AU*.component");
                    root.findChildFiles(waveShells, juce::File::findDirectories, true, "WaveShell*.component");

                    for (const auto& ws : waveShells)
                    {
                        const auto wsPath = ws.getFullPathName();
                        if (isMacWavesPayloadPath(wsPath))
                            continue;
                        if (crashBlacklistedEntries.contains(wsPath))
                        {
                            markScanFailure(wsPath, "Skipped because it crashed a previous scan on this machine");
                            continue;
                        }
                        if (seenScanPaths.indexOf(wsPath, true) >= 0)
                            continue;

                        seenScanPaths.add(wsPath);
                        filesToScan.add(ws);
                        juce::Logger::writeToLog("Scanner: added explicit WaveShell wrapper: " + wsPath);
                    }
                }
                catch (const std::exception& e)
                {
                    markScanFailure(rootPath, exceptionToString(e));
                }
                catch (...)
                {
                    markScanFailure(rootPath, "Could not enumerate Waves wrappers");
                }
            }
        }
       #endif

        int totalFiles = filesToScan.size();
        juce::Logger::writeToLog("Scan: candidate files to scan=" + juce::String(totalFiles));
        if (totalFiles == 0)
        {
            scanNewPluginsFound = 0;
            {
                const juce::SpinLock::ScopedLockType lock(scanSpinLock);
                scanProgress = 1.0f;
                scanCurrentName = {};
                scanCompletedWithError = scanHadRecoverableErrors;

                if (scanHadRecoverableErrors)
                {
                    scanErrorMessage = "No additional plugins were scanned because one or more entries are marked as unsafe on this machine.";

                    if (!failedEntries.isEmpty())
                    {
                        scanErrorMessage << "\n\nSkipped items:";
                        for (const auto& entry : failedEntries)
                            scanErrorMessage << "\n- " << entry;
                    }

                    scanErrorMessage << "\n\nYou can keep using the plugins already in the library and retry the scan later.";
                }
                else
                {
                    scanErrorMessage.clear();
                }
            }
            getScanInProgressFile().deleteFile();
            isScanningFlag = false;
            return;
        }

        int countBefore = 0;
        {
            const juce::SpinLock::ScopedLockType lock(scanSpinLock);
            countBefore = knownPluginList.getNumTypes();
        }
        int scanned = 0;
        int changesSincePersist = 0;

        for (const auto& file : filesToScan)
        {
            if (shouldStopScanning) break;

            const auto inProgressFile = getScanInProgressFile();
            struct InProgressFileClearer
            {
                juce::File file;
                ~InProgressFileClearer() { file.deleteFile(); }
            } inProgressFileClearer { inProgressFile };

            juce::String filePath  = file.getFullPathName();
            juce::String shortName = file.getFileNameWithoutExtension();
            juce::StringArray inProgressEntry;
            inProgressEntry.add(filePath);
            writeStringListFile(inProgressFile, inProgressEntry);

            if (shortName.equalsIgnoreCase(JucePlugin_Name))
            {
                ++scanned;
                juce::Logger::writeToLog("Skipped self plugin during scan: " + filePath);
                continue;
            }

            {
                const juce::SpinLock::ScopedLockType lock(scanSpinLock);
                scanCurrentName = shortName;
                scanProgress    = juce::jlimit(0.0f, 0.99f, (float) scanned / (float) totalFiles);
            }

            juce::Logger::writeToLog("Scan: file " + juce::String(scanned + 1) + "/" + juce::String(totalFiles)
                                     + " -> " + filePath);

            juce::AudioPluginFormat* matchFmt = nullptr;
            for (auto* fmt : formats)
            {
                if (filePath.endsWith(".vst3")      && fmt->getName() == "VST3")      { matchFmt = fmt; break; }
                if (filePath.endsWith(".component") && fmt->getName() == "AudioUnit") { matchFmt = fmt; break; }
                if (filePath.endsWith(".vst")       && fmt->getName() == "VST")       { matchFmt = fmt; break; }
            }

            bool loaded = false;
            auto addScannedDescription = [this, &loaded, &knownPluginListChanged](juce::PluginDescription desc)
            {
                desc.fileOrIdentifier = desc.fileOrIdentifier.trim();
                desc.pluginFormatName = desc.pluginFormatName.trim();
                desc.name             = desc.name.trim();

                juce::Logger::writeToLog("addScannedDescription: name='" + desc.name + "' format='" + desc.pluginFormatName + "' file='" + desc.fileOrIdentifier + "'");

                if (desc.name.isEmpty()
                    || desc.fileOrIdentifier.isEmpty()
                    || desc.pluginFormatName.isEmpty())
                    return;

                const juce::SpinLock::ScopedLockType lock(scanSpinLock);
                knownPluginList.addType(desc);
                loaded = true;
                knownPluginListChanged = true;
            };

            try
            {
                juce::Logger::writeToLog("Scanner: filePath='" + filePath + "' matchFmt='" + (matchFmt ? matchFmt->getName() : juce::String("<none>")) + "'");

                if (filePath.endsWithIgnoreCase(".bundle"))
                {
                    if (pathContainsIgnoreCase(filePath, "/Applications/Waves"))
                    {
                        ++scanned;
                        juce::Logger::writeToLog("Skipped Waves bundle path during scan: " + filePath);
                        continue;
                    }

                    juce::File moduleInfo = file.getChildFile("Contents/moduleinfo.json");
                    juce::File infoPlist  = file.getChildFile("Contents/Info.plist");

                    juce::String plugName, mfName;

                    if (moduleInfo.exists())
                    {
                        juce::String json = moduleInfo.loadFileAsString();
                        auto extractJson = [&](const juce::String& src, const juce::String& key) -> juce::String
                        {
                            juce::String search = "\"" + key + "\"";
                            int idx = src.indexOf(search);
                            if (idx < 0) return {};
                            int colon = src.indexOf(idx, ":");
                            if (colon < 0) return {};
                            int q1 = src.indexOf(colon, "\"");
                            if (q1 < 0) return {};
                            int q2 = src.indexOf(q1 + 1, "\"");
                            if (q2 < 0) return {};
                            return src.substring(q1 + 1, q2);
                        };

                        plugName = extractJson(json, "Name");
                        mfName   = extractJson(json, "Vendor");
                    }
                    else if (infoPlist.exists())
                    {
                        if (auto xml = juce::XmlDocument::parse(infoPlist))
                        {
                            auto* dict = xml->getFirstChildElement();
                            if (dict)
                            {
                                auto* k = dict->getFirstChildElement();
                                while (k)
                                {
                                    auto* v = k->getNextElement();
                                    if (v)
                                    {
                                        if (k->getAllSubText() == "CFBundleName")         plugName = v->getAllSubText();
                                        if (k->getAllSubText() == "CFBundleGetInfoString") mfName   = v->getAllSubText();
                                    }
                                    k = v ? v->getNextElement() : nullptr;
                                }
                            }
                        }
                    }

                    if (plugName.isEmpty())
                        plugName = shortName;

                    if (plugName.isNotEmpty())
                    {
                        juce::PluginDescription desc;
                        desc.name              = plugName;
                        desc.manufacturerName  = mfName;
                        desc.pluginFormatName  = "AudioUnit";
                        desc.fileOrIdentifier  = filePath;
                        desc.category          = "Effect";
                        desc.isInstrument      = false;
                        desc.numInputChannels  = 2;
                        desc.numOutputChannels = 2;
                        desc.uniqueId          = plugName.hashCode();
                        addScannedDescription(desc);
                    }

                    if (loaded && knownPluginListChanged)
                    {
                        if (++changesSincePersist >= 100)
                        {
                            persistKnownPlugins();
                            knownPluginListChanged = false;
                            changesSincePersist = 0;
                        }
                    }

                    ++scanned;
                    juce::Logger::writeToLog("Scanned Waves bundle: " + filePath);
                    continue;
                }

                if (matchFmt == nullptr)
                {
                    ++scanned;
                    continue;
                }

                if (!loaded && matchFmt->getName() == "VST")
                {
                    juce::Logger::writeToLog("Scanner: using metadata-only VST2 scan to avoid instantiating legacy plugins: " + filePath);
                    addVst2MetadataDescription(file, shortName, addScannedDescription);
                }

                if (!loaded && matchFmt->getName() == "AudioUnit")
                {
                    if (filePath.containsIgnoreCase("WaveShell")
                        && filePath.endsWithIgnoreCase(".component"))
                        juce::Logger::writeToLog("Scanner: using plist-only AU WaveShell scan to keep UI responsive: " + filePath);

                    addAudioUnitDescriptionsFromPlist(file, addScannedDescription);
                }

                if (!loaded && matchFmt->getName() == "VST3")
                {
                    juce::Logger::writeToLog("Scanner: using metadata-only VST3 scan to avoid instantiating plugins during rescan: " + filePath);
                    addVst3MetadataDescription(file, shortName, addScannedDescription);
                }

                juce::Logger::writeToLog((loaded ? "Scanned: " : "Skipped: ") + filePath);

                if (loaded && knownPluginListChanged)
                {
                    if (++changesSincePersist >= 100)
                    {
                        persistKnownPlugins();
                        knownPluginListChanged = false;
                        changesSincePersist = 0;
                    }
                }
            }
            catch (const std::exception& e)
            {
                markScanFailure(filePath, exceptionToString(e));
            }
            catch (...)
            {
                markScanFailure(filePath, "This plugin could not be scanned");
            }

            ++scanned;
            {
                const juce::SpinLock::ScopedLockType lock(scanSpinLock);
                scanProgress = juce::jlimit(0.0f, 0.99f, (float) scanned / (float) totalFiles);
            }
        }

        int countAfter = 0;
        {
            const juce::SpinLock::ScopedLockType lock(scanSpinLock);
            countAfter = knownPluginList.getNumTypes();
        }
        scanNewPluginsFound = countAfter - countBefore;

        if (knownPluginListChanged)
            persistKnownPlugins();
    }
    catch (const std::exception& e)
    {
        markScanFailure("Plugin scan", exceptionToString(e));
    }
    catch (...)
    {
        markScanFailure("Plugin scan", "Unexpected scan failure");
    }

    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        scanProgress = 1.0f;
        scanCurrentName = {};
        scanCompletedWithError = scanHadRecoverableErrors;

        if (scanHadRecoverableErrors)
        {
            scanErrorMessage = "Some plugins could not be scanned. The plugins found before the error remain available.";

            if (!failedEntries.isEmpty())
            {
                scanErrorMessage << "\n\nProblematic items:";
                for (const auto& entry : failedEntries)
                    scanErrorMessage << "\n- " << entry;

                if (failedEntries.size() >= 5)
                    scanErrorMessage << "\n- Additional failing items were omitted from this list.";
            }

            scanErrorMessage << "\n\nYou can retry the scan from the plugin menu.";
        }
        else
        {
            scanErrorMessage.clear();
        }
    }

    getScanInProgressFile().deleteFile();
    isScanningFlag = false;
}

void PluginProcessor::loadPluginMetadata()
{
    juce::File savedListFile = getKnownPluginsFile();

    auto xml = std::unique_ptr<juce::XmlElement>();
    {
        const juce::SpinLock::ScopedLockType lock(scanSpinLock);
        xml = knownPluginList.createXml();
        loadablePluginCacheDirty = true;
    }

    if (xml != nullptr)
        xml->writeTo(savedListFile);

    rebuildLoadablePluginCache();

    juce::Logger::writeToLog("Scan complete. Total plugins: "
        + juce::String(knownPluginList.getNumTypes())
        + ", new: " + juce::String(scanNewPluginsFound.load()));
}

bool PluginProcessor::isScanning()    { return isScanningFlag; }

float PluginProcessor::getScanProgress()
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return scanProgress;
}

juce::String PluginProcessor::getScanCurrentName()
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return scanCurrentName;
}

void PluginProcessor::debugPrintPlugins()
{
    const auto types = getKnownPluginDescriptions();
    juce::Logger::writeToLog("=== Known Plugins: " + juce::String(types.size()) + " ===");
    for (int i = 0; i < juce::jmin(10, types.size()); ++i)
        juce::Logger::writeToLog("  " + types[i].manufacturerName + " / " + types[i].name);
}

void PluginProcessor::timerCallback()
{
    if (!macroMappings.empty())
        applyMacroMappings();

    if (!isScanning() && scanThread && scanThread->joinable())
    {
        scanThread->join();
        loadPluginMetadata();
        startTimerHz(30);
    }
}

int PluginProcessor::getNewPluginsFoundCount() const
{
    return scanNewPluginsFound.load();
}

int PluginProcessor::getKnownPluginCount() const
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return knownPluginList.getNumTypes();
}

bool PluginProcessor::didLastScanFinishWithError() const
{
    return scanCompletedWithError.load();
}

juce::String PluginProcessor::getLastScanErrorMessage()
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return scanErrorMessage;
}

juce::String PluginProcessor::getPendingScanRecoveryMessage()
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    return scanRecoveryMessage;
}

void PluginProcessor::clearPendingScanRecoveryMessage()
{
    const juce::SpinLock::ScopedLockType lock(scanSpinLock);
    scanRecoveryMessage.clear();
}

float PluginProcessor::getEditorZoomScale() const
{
    return juce::jlimit(0.75f, 2.00f, editorZoomScale.load());
}

void PluginProcessor::setEditorZoomScale(float scale)
{
    const float clamped = juce::jlimit(0.75f, 2.00f, scale);
    if (std::abs(editorZoomScale.load() - clamped) < 0.002f)
        return;

    editorZoomScale.store(clamped);
    saveStoredEditorSettings(clamped, getEditorSkinIndex());
}

int PluginProcessor::getEditorSkinIndex() const
{
    return hostr::clampSkinIndex(editorSkinIndex.load());
}

void PluginProcessor::setEditorSkinIndex(int index)
{
    const int clamped = hostr::clampSkinIndex(index);
    if (editorSkinIndex.load() == clamped)
        return;

    editorSkinIndex.store(clamped);
    hostr::setActiveSkinIndex(clamped);
    saveStoredEditorSettings(getEditorZoomScale(), clamped);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input  = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    const bool stereoEffect = input == juce::AudioChannelSet::stereo()
                           && output == juce::AudioChannelSet::stereo();
    const bool monoEffect   = input == juce::AudioChannelSet::mono()
                           && output == juce::AudioChannelSet::mono();
    const bool synthStyle   = input.isDisabled()
                           && output == juce::AudioChannelSet::stereo();

    return stereoEffect || monoEffect || synthStyle;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
