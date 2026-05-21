#ifndef PLUGIN_SEARCH_DIALOG_CPP_INCLUDED
#define PLUGIN_SEARCH_DIALOG_CPP_INCLUDED

#include "PluginSearchDialog.h"
#include "HostrSkin.h"
#include "VendorCache.h"

// ==============================================================================
// Implementation of the plugin search popup.
// It handles grouping, filtering, and selecting loadable plugins.
// ==============================================================================

namespace
{
constexpr int kDialogPadding   = 10;
constexpr int kSearchHeight    = 34;
constexpr int kModeHeight      = 28;
constexpr int kLegendHeight    = 28;
constexpr int kLegendGap       = 8;
constexpr int kListRowHeight   = 26;
constexpr int kDialogWidth     = 270;
constexpr int kDialogHeight    = 540;
constexpr int kLegendDotSize   = 8;
const juce::Colour kDialogBg   (0xff1a1a1a);
const juce::Colour kLegendBg   (0xff20242d);
const juce::Colour kLegendLine (0xff2e3745);
const juce::Colour kAudioUnitColour (0xff4a9eff);
const juce::Colour kVST3Colour      (0xffffcc33);
const juce::Colour kVST2Colour      (0xff9a63ff);
juce::Component::SafePointer<juce::CallOutBox> gActivePluginSearchCallout;

static juce::String pluginCacheFingerprint(const juce::Array<juce::PluginDescription>& plugins)
{
    juce::String fingerprint;

    for (const auto& plugin : plugins)
    {
        fingerprint << plugin.name << "|"
                    << plugin.manufacturerName << "|"
                    << plugin.pluginFormatName << "|"
                    << plugin.fileOrIdentifier << "|"
                    << plugin.uniqueId << "\n";
    }

    return fingerprint;
}

static std::shared_ptr<VendorCache> getCachedVendorCache(const juce::Array<juce::PluginDescription>& plugins)
{
    static juce::CriticalSection cacheLock;
    static juce::String cachedFingerprint;
    static std::shared_ptr<VendorCache> cachedVendorCache;

    const juce::ScopedLock lock(cacheLock);
    const auto fingerprint = pluginCacheFingerprint(plugins);

    if (cachedVendorCache == nullptr || cachedFingerprint != fingerprint)
    {
        auto rebuilt = std::make_shared<VendorCache>();
        rebuilt->buildCache(plugins);
        cachedVendorCache = std::move(rebuilt);
        cachedFingerprint = fingerprint;
    }

    return cachedVendorCache;
}
}

// ==============================================================================
// Search Dialog Plugin — DAW-style hierarchical browser + live search with dynamic cache
// ==============================================================================

static bool pluginIsInstrument(const juce::PluginDescription& d)
{
    return d.isInstrument
           || d.category.containsIgnoreCase("Instrument")
           || d.category.containsIgnoreCase("Synth")
           || d.category.containsIgnoreCase("Generator");
}

static juce::String pluginRoleLabel(const juce::PluginDescription& d)
{
    return pluginIsInstrument(d) ? "Virtual Instruments" : "Effects";
}

static juce::String displayFormatLabel(const juce::String& formatName)
{
    return formatName == "VST" ? "VST2" : formatName;
}

static juce::String normalisePluginCategory(const juce::PluginDescription& d)
{
    if (pluginIsInstrument(d))
        return {};

    auto haystack = (d.name + " " + d.descriptiveName + " " + d.category + " "
                    + d.manufacturerName + " " + d.fileOrIdentifier).toLowerCase();

    juce::StringArray tokens;
    tokens.addTokens(d.category, "|;/,", {});
    tokens.trim();
    tokens.removeEmptyStrings();

    for (auto token : tokens)
    {
        token = token.trim();
        if (token.isEmpty() || token.equalsIgnoreCase("Fx") || token.equalsIgnoreCase("Effect")
            || token.equalsIgnoreCase("Music Effect"))
            continue;

        haystack += " " + token.toLowerCase();
    }

    if (haystack.contains(" limiter") || haystack.contains("limit")
        || haystack.contains("maximizer") || haystack.contains("clipper"))
        return "Limiter";

    if (haystack.contains(" compressor") || haystack.contains("compressor")
        || haystack.contains("comp ") || haystack.contains(" comp")
        || haystack.contains("dynamics") || haystack.contains("gate")
        || haystack.contains("expander") || haystack.contains("de-esser")
        || haystack.contains("deesser") || haystack.contains("transient"))
        return "Compressors";

    if (haystack.contains(" equalizer") || haystack.contains("equalizer")
        || haystack.contains(" eq") || haystack.contains("eq ")
        || haystack.contains("-eq") || haystack.contains("filter")
        || haystack.contains("pultec") || haystack.contains("maag"))
        return "EQ";

    if (haystack.contains(" reverb") || haystack.contains("reverb")
        || haystack.contains("verb") || haystack.contains("room")
        || haystack.contains("plate") || haystack.contains("spring"))
        return "Reverb";

    if (haystack.contains(" delay") || haystack.contains("delay")
        || haystack.contains("echo") || haystack.contains("tape echo")
        || haystack.contains("bucket brigade"))
        return "Delay";

    if (haystack.contains(" distortion") || haystack.contains("distortion")
        || haystack.contains("saturation") || haystack.contains("saturat")
        || haystack.contains("drive") || haystack.contains("overdrive")
        || haystack.contains("fuzz") || haystack.contains("crusher")
        || haystack.contains("decapitator") || haystack.contains("clip"))
        return "Distortion";

    if (haystack.contains(" amplifier") || haystack.contains(" amp")
        || haystack.contains("amp ") || haystack.contains("cabinet")
        || haystack.contains("guitar") || haystack.contains("bass amp")
        || haystack.contains("ampeg") || haystack.contains("archetype")
        || haystack.contains("helix") || haystack.contains("marshall")
        || haystack.contains("soldano") || haystack.contains("fortin"))
        return "Amplifiers";

    return {};
}

static juce::String groupKey(const juce::String& prefix, const juce::String& label)
{
    return prefix + ":" + label.toLowerCase();
}

static int roleSortOrder(const juce::String& role)
{
    return role == "Virtual Instruments" ? 0 : 1;
}

PluginSearchDialog::PluginSearchDialog(const juce::Array<juce::PluginDescription>& plugins)
    : allPlugins(plugins)
{
    const auto& skin = hostr::currentSkin();

    searchBox.setTextToShowWhenEmpty("Search by name or manufacturer...", skin.mutedText.withAlpha(0.70f));
    searchBox.setFont(juce::Font(juce::FontOptions(14.0f)));
    searchBox.setColour(juce::TextEditor::backgroundColourId, skin.panelInset);
    searchBox.setColour(juce::TextEditor::textColourId,       skin.text);
    searchBox.setColour(juce::TextEditor::outlineColourId,    skin.border);
    searchBox.addListener(this);
    searchBox.addKeyListener(this);
    addAndMakeVisible(searchBox);

    modeBox.addItem("Vendor",   (int) BrowserMode::Vendor);
    modeBox.addItem("Category", (int) BrowserMode::Category);
    modeBox.addItem("Format",   (int) BrowserMode::Format);
    modeBox.setSelectedId((int) browserMode, juce::dontSendNotification);
    modeBox.setJustificationType(juce::Justification::centredLeft);
    modeBox.setColour(juce::ComboBox::backgroundColourId, skin.panelRaised);
    modeBox.setColour(juce::ComboBox::textColourId, skin.text.withAlpha(0.9f));
    modeBox.setColour(juce::ComboBox::outlineColourId, skin.border);
    modeBox.setColour(juce::ComboBox::arrowColourId, skin.accent);
    modeBox.addListener(this);
    modeBox.addKeyListener(this);
    addAndMakeVisible(modeBox);

    treeList.setModel(this);
    treeList.setRowHeight(kListRowHeight);
    treeList.setColour(juce::ListBox::backgroundColourId, skin.background);
    treeList.setColour(juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    treeList.addKeyListener(this);
    addAndMakeVisible(treeList);

    rebuildRows();
}

void PluginSearchDialog::rebuildRows()
{
    rows.clear();
    juce::String query = searchBox.getText().trim().toLowerCase();
    isFiltering = query.isNotEmpty();
    const bool needsVendor = isFiltering || browserMode == BrowserMode::Vendor;
    if (needsVendor && vendorCache == nullptr)
        vendorCache = getCachedVendorCache(allPlugins);

    struct VisiblePlugin
    {
        int pluginIndex = -1;
        juce::String manufacturer;
        juce::String role;
        juce::String category;
        juce::String format;
    };

    juce::Array<VisiblePlugin> visible;
    for (int i = 0; i < allPlugins.size(); ++i)
    {
        const auto& d = allPlugins[i];
        juce::String cleanMf;
        if (needsVendor)
        {
            cleanMf = vendorCache != nullptr ? vendorCache->resolveVendor(d) : d.manufacturerName;
            if (cleanMf.isEmpty()) cleanMf = "(Unknown)";
        }

        juce::String category = (isFiltering || browserMode == BrowserMode::Category)
                              ? normalisePluginCategory(d) : juce::String{};
        juce::String role     = browserMode == BrowserMode::Vendor ? pluginRoleLabel(d) : juce::String{};
        juce::String format   = d.pluginFormatName.isEmpty() ? "(Unknown Format)" : displayFormatLabel(d.pluginFormatName);

        if (!isFiltering && browserMode == BrowserMode::Category && category.isEmpty())
            continue;

        if (isFiltering)
        {
            juce::String searchableMf = cleanMf.toLowerCase();
            if (!d.name.toLowerCase().contains(query) &&
                !searchableMf.contains(query) &&
                !d.manufacturerName.toLowerCase().contains(query) &&
                !category.toLowerCase().contains(query) &&
                !format.toLowerCase().contains(query))
                continue;
        }

        visible.add({ i, cleanMf, role, category, format });
    }

    std::sort(visible.begin(), visible.end(),
              [this](const VisiblePlugin& a, const VisiblePlugin& b)
              {
                  const auto& da = allPlugins[a.pluginIndex];
                  const auto& db = allPlugins[b.pluginIndex];

                  const auto compareByPluginName = [&]() -> int
                  {
                      const int byName = da.name.compareIgnoreCase(db.name);
                      if (byName != 0) return byName;

                      const int byMaker = a.manufacturer.compareIgnoreCase(b.manufacturer);
                      if (byMaker != 0) return byMaker;

                      return da.pluginFormatName.compareIgnoreCase(db.pluginFormatName);
                  };

                  if (isFiltering)
                  {
                      return compareByPluginName() < 0;
                  }

                  if (browserMode == BrowserMode::Vendor)
                  {
                      const int byMaker = a.manufacturer.compareIgnoreCase(b.manufacturer);
                      if (byMaker != 0) return byMaker < 0;
                      const int byName = compareByPluginName();
                      if (byName != 0) return byName < 0;
                      return roleSortOrder(a.role) < roleSortOrder(b.role);
                  }
                  else if (browserMode == BrowserMode::Category)
                  {
                      const int byCategory = a.category.compareIgnoreCase(b.category);
                      if (byCategory != 0) return byCategory < 0;
                      return compareByPluginName() < 0;
                  }
                  else
                  {
                      const int byFormat = a.format.compareIgnoreCase(b.format);
                      if (byFormat != 0) return byFormat < 0;
                      return compareByPluginName() < 0;
                  }
              });

    if (isFiltering)
    {
        for (const auto& item : visible)
        {
            const auto& d = allPlugins[item.pluginIndex];
            Row pRow;
            pRow.kind = Row::Kind::Plugin;
            pRow.label = d.name;
            pRow.formatName = displayFormatLabel(d.pluginFormatName);
            pRow.pluginIndex = item.pluginIndex;
            pRow.depth = 0;
            rows.add(pRow);
        }

        treeList.updateContent();
        if (!rows.isEmpty())
            treeList.selectRow(0, juce::dontSendNotification);
        treeList.repaint();
        return;
    }

    juce::String curTopKey;
    juce::String curChildKey;

    for (int vi = 0; vi < visible.size(); ++vi)
    {
        const auto& d   = allPlugins[visible[vi].pluginIndex];
        juce::String fmt = displayFormatLabel(d.pluginFormatName);
        juce::String topLabel;
        juce::String topKey;
        juce::String childLabel;
        juce::String childKey;

        if (browserMode == BrowserMode::Vendor)
        {
            topLabel   = visible[vi].manufacturer;
            topKey     = groupKey("vendor", topLabel);
            childLabel = visible[vi].role;
            childKey   = groupKey(topKey, childLabel);
        }
        else if (browserMode == BrowserMode::Category)
        {
            topLabel = visible[vi].category;
            topKey   = groupKey("category", topLabel);
        }
        else
        {
            topLabel = visible[vi].format;
            topKey   = groupKey("format", topLabel);
        }

        if (topKey != curTopKey)
        {
            Row groupRow;
            groupRow.kind      = Row::Kind::Group;
            groupRow.label     = topLabel;
            groupRow.groupKey  = topKey;
            groupRow.depth     = 0;
            groupRow.collapsed = !isFiltering && collapsedGroups.contains(topKey);
            rows.add(groupRow);
            curTopKey = topKey;
            curChildKey.clear();
        }

        const bool topCollapsed = !isFiltering && collapsedGroups.contains(topKey);
        if (topCollapsed)
            continue;

        if (browserMode == BrowserMode::Vendor)
        {
            if (childKey != curChildKey)
            {
                Row childRow;
                childRow.kind      = Row::Kind::Group;
                childRow.label     = childLabel;
                childRow.groupKey  = childKey;
                childRow.depth     = 1;
                childRow.collapsed = !isFiltering && collapsedGroups.contains(childKey);
                rows.add(childRow);
                curChildKey = childKey;
            }

            if (!isFiltering && collapsedGroups.contains(childKey))
                continue;
        }

        Row pRow;
        pRow.kind       = Row::Kind::Plugin;
        pRow.label      = d.name;
        pRow.formatName = fmt;
        pRow.pluginIndex = visible[vi].pluginIndex;
        pRow.depth       = browserMode == BrowserMode::Vendor ? 2 : 1;
        rows.add(pRow);
    }

    treeList.updateContent();
    treeList.repaint();
}

void PluginSearchDialog::comboBoxChanged(juce::ComboBox* box)
{
    if (box == &modeBox)
    {
        browserMode = (BrowserMode) modeBox.getSelectedId();
        if (browserMode == BrowserMode::Vendor && vendorCache == nullptr)
            vendorCache = getCachedVendorCache(allPlugins);
        rebuildRows();
    }
}

void PluginSearchDialog::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    hostr::paintPanelFrame(g, getLocalBounds().toFloat(), skin, 6.0f, 1.0f);
    hostr::paintTextureOverlay(g, getLocalBounds().toFloat().reduced(1.0f), skin,
                               hostr::hasBitmapSkinSurface(skin) ? 0.10f : 0.18f);

    auto legendArea = getLocalBounds()
                        .reduced(kDialogPadding)
                        .removeFromBottom(kLegendHeight);

    g.setColour(skin.panelRaised.withAlpha(0.84f));
    g.fillRoundedRectangle(legendArea.toFloat(), 6.0f);
    g.setColour(skin.border.withAlpha(0.70f));
    g.drawRoundedRectangle(legendArea.toFloat().reduced(0.5f), 6.0f, 1.0f);

    const int dotY = legendArea.getCentreY() - kLegendDotSize / 2;
    int x = legendArea.getX() + 12;

    g.setColour(kAudioUnitColour);
    g.fillEllipse((float)x, (float)dotY, (float)kLegendDotSize, (float)kLegendDotSize);
    x += 14;

    g.setColour(skin.text.withAlpha(0.78f));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("AU", x, legendArea.getY(), 24, legendArea.getHeight(),
               juce::Justification::centredLeft);

    x += 42;
    g.setColour(kVST3Colour);
    g.fillEllipse((float)x, (float)dotY, (float)kLegendDotSize, (float)kLegendDotSize);
    x += 14;

    g.setColour(skin.text.withAlpha(0.78f));
    g.drawText("VST3", x, legendArea.getY(), 42, legendArea.getHeight(),
               juce::Justification::centredLeft);

    x += 52;
    g.setColour(kVST2Colour);
    g.fillEllipse((float)x, (float)dotY, (float)kLegendDotSize, (float)kLegendDotSize);
    x += 14;

    g.setColour(skin.text.withAlpha(0.78f));
    g.drawText("VST2", x, legendArea.getY(), 42, legendArea.getHeight(),
               juce::Justification::centredLeft);
}

void PluginSearchDialog::resized()
{
    auto b = getLocalBounds().reduced(kDialogPadding);
    searchBox.setBounds(b.removeFromTop(kSearchHeight));
    b.removeFromTop(6);
    modeBox.setBounds(b.removeFromTop(kModeHeight));
    b.removeFromTop(8);
    b.removeFromBottom(kLegendHeight);
    b.removeFromBottom(kLegendGap);
    treeList.setBounds(b);
}

void PluginSearchDialog::textEditorTextChanged(juce::TextEditor&)
{
    rebuildRows();
}

int PluginSearchDialog::getNumRows() { return rows.size(); }

void PluginSearchDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= rows.size()) return;
    const Row r = rows[row];
    const auto& skin = hostr::currentSkin();

    // Background
    if (selected && r.kind == Row::Kind::Plugin)
        g.fillAll(skin.accent.withAlpha(0.22f));
    else
        g.fillAll(skin.background.withAlpha(0.86f));

    int indent = r.depth * 18 + 6;

    if (r.kind == Row::Kind::Group)
    {
        juce::String arrow = r.collapsed ? "> " : "v ";

        g.setColour(skin.mutedText.withAlpha(0.60f));
        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        g.drawText(arrow, indent, 0, 16, h, juce::Justification::centredLeft);

        g.setColour(skin.text.withAlpha(0.90f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(r.label, indent + 18, 0, w - indent - 22, h, juce::Justification::centredLeft, true);
    }
    else
    {
        const bool isAU = r.formatName == "AudioUnit";
        const bool isVST2 = r.formatName == "VST2";
        auto fmtCol = isAU ? kAudioUnitColour : (isVST2 ? kVST2Colour : kVST3Colour);

        // Colored dot for format (AU = blue, VST3 = yellow, VST2 = purple)
        g.setColour(fmtCol.withAlpha(selected ? 0.7f : 0.35f));
        g.fillEllipse((float)(indent + 2), (float)(h / 2 - 3), 6.0f, 6.0f);

        g.setColour(selected ? skin.text : skin.text.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(r.label, indent + 14, 0, w - indent - 18, h, juce::Justification::centredLeft, true);
    }

    // Thin separator
    g.setColour(skin.border.withAlpha(0.20f));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void PluginSearchDialog::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= rows.size()) return;
    const Row r = rows[row];

    if (r.kind == Row::Kind::Group)
    {
        juce::String key = r.groupKey;
        if (collapsedGroups.contains(key))
            collapsedGroups.removeString(key);
        else
            collapsedGroups.add(key);
        rebuildRows();
    }
}

void PluginSearchDialog::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    selectAndLoad(row);
}

void PluginSearchDialog::selectAndLoad(int row)
{
    if (row < 0 || row >= rows.size()) return;
    const Row r = rows[row];
    if (r.kind != Row::Kind::Plugin) return;
    if (r.pluginIndex < 0 || r.pluginIndex >= allPlugins.size()) return;

    auto sel = allPlugins[r.pluginIndex];
    auto callback = onPluginSelected;
    if (auto* cw = findParentComponentOfClass<juce::CallOutBox>())
    {
        gActivePluginSearchCallout = nullptr;
        cw->dismiss();
    }
    juce::MessageManager::callAsync([callback = std::move(callback), sel]() mutable
    {
        if (callback) callback(sel);
    });
}

bool PluginSearchDialog::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::returnKey)
    {
        int sel = treeList.getSelectedRow();
        if (sel >= 0 && sel < rows.size() && rows[sel].kind == Row::Kind::Plugin)
            { selectAndLoad(sel); return true; }
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Row::Kind::Plugin)
                { selectAndLoad(i); return true; }
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (auto* cw = findParentComponentOfClass<juce::CallOutBox>())
        {
            gActivePluginSearchCallout = nullptr;
            cw->dismiss();
        }
        return true;
    }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        int cur = treeList.getSelectedRow();
        int next = cur;
        int delta = (key == juce::KeyPress::downKey) ? 1 : -1;
        int attempts = rows.size();
        do {
            next += delta;
            next = juce::jlimit(0, rows.size() - 1, next);
            if (--attempts <= 0) break;
        } while (rows[next].kind != Row::Kind::Plugin && next != cur);

        treeList.selectRow(next);
        treeList.scrollToEnsureRowIsOnscreen(next);
        return true;
    }
    return false;
}

void PluginSearchDialog::show(juce::Component* target,
                               const juce::Array<juce::PluginDescription>& plugins,
                               std::function<void(const juce::PluginDescription&)> callback)
{
    dismissActive();

    auto* dialog = new PluginSearchDialog(plugins);
    dialog->onPluginSelected = std::move(callback);
    dialog->setSize(kDialogWidth, kDialogHeight);
    auto& callout = juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(dialog),
        target->getScreenBounds(),
        nullptr);
    gActivePluginSearchCallout = &callout;
}

void PluginSearchDialog::dismissActive()
{
    if (gActivePluginSearchCallout != nullptr)
    {
        auto activeCallout = gActivePluginSearchCallout;
        gActivePluginSearchCallout = nullptr;
        activeCallout->dismiss();
    }
}

#endif // PLUGIN_SEARCH_DIALOG_CPP_INCLUDED
