#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include "VendorCache.h"

// ==============================================================================
// Plugin search popup:
// - Group plugins by manufacturer, category, or format
// - Show Instruments/Effects subfolders in manufacturer view
// - Real time filtering
// - Show AU/VST3/VST2 format
// ==============================================================================
class PluginSearchDialog : public juce::Component,
                           public juce::TextEditor::Listener,
                           public juce::ComboBox::Listener,
                           public juce::ListBoxModel,
                           public juce::KeyListener
{
public:
    std::function<void(const juce::PluginDescription&)> onPluginSelected;

    PluginSearchDialog(const juce::Array<juce::PluginDescription>& allPlugins);
    ~PluginSearchDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void textEditorTextChanged(juce::TextEditor&) override;
    void comboBoxChanged(juce::ComboBox*) override;

    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;

    static void show(juce::Component* target,
                     const juce::Array<juce::PluginDescription>& plugins,
                     std::function<void(const juce::PluginDescription&)> callback);
    static void dismissActive();

private:
    enum class BrowserMode { Vendor = 1, Category, Format };

    struct Row
    {
        enum class Kind { Group, Plugin };
        Kind         kind       = Kind::Plugin;
        juce::String label;
        juce::String groupKey;
        juce::String formatName;
        int          pluginIndex = -1;
        bool         collapsed   = false;
        int          depth       = 0;
    };

    juce::TextEditor searchBox;
    juce::ComboBox   modeBox;
    juce::ListBox    treeList;

    juce::Array<juce::PluginDescription> allPlugins;
    juce::Array<Row>                     rows;
    juce::StringArray                    collapsedGroups;
    BrowserMode                          browserMode = BrowserMode::Format;
    bool                                 isFiltering = false;
    std::shared_ptr<VendorCache>         vendorCache;

    void rebuildRows();
    // Convert the clicked line into a PluginDescription and invoke the callback.
    void selectAndLoad(int row);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSearchDialog)
};
