#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ParallelSplitComponent.h"
#include "HostrSkin.h"

// ==============================================================================
// Host plugin main UI:
// - preset bar
// - master chain
// - master meters and faders
// - open Parallel Split panel
// - drag & drop between slots/chains
// ==============================================================================

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColourToUse) override;
    void drawPopupMenuSectionHeader(juce::Graphics& g,
                                    const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*highlighted*/, bool /*down*/) override
    {
        const float h    = (float)button.getHeight();
        const float fs   = juce::jlimit(8.5f, 14.0f, h * 0.46f);
        g.setFont(juce::Font(juce::FontOptions(fs).withStyle("Bold")));
        g.setColour(button.findColour(button.getToggleState()
                    ? juce::TextButton::textColourOnId
                    : juce::TextButton::textColourOffId));
        g.drawFittedText(button.getButtonText().toUpperCase(),
                         button.getLocalBounds().reduced(2, 1),
                         juce::Justification::centred, 1,
                         1.0f);
    }
};

#include "PluginSearchDialog.h"

class MeterComponent : public juce::Component
{
public:
    void setLevel(float newLevel, bool newOverZero = false)
    {
        if (std::abs(level - newLevel) < 0.2f && overZero == newOverZero)
            return;

        level = newLevel;
        overZero = newOverZero;
        repaint();
    }
    void paint(juce::Graphics& g) override;
private:
    float level = 0.0f;
    bool overZero = false;
};

class ScanOverlay : public juce::Component, public juce::Timer
{
public:
    ScanOverlay(PluginProcessor& p);
    void paint(juce::Graphics& g) override;
    void startMonitoring();
    void timerCallback() override;
private:
    PluginProcessor& processor;
};

class MasterMeterOverlay : public juce::Component,
                           private juce::HighResolutionTimer,
                           private juce::AsyncUpdater
{
public:
    explicit MasterMeterOverlay(PluginProcessor& p);
    ~MasterMeterOverlay() override;

    void setGeometry(float zoom, int masterCentreX, int masterWidth, int outputKnobSize);
    void updateMeters();
    void paint(juce::Graphics& g) override;

private:
    void hiResTimerCallback() override;
    void handleAsyncUpdate() override;

    PluginProcessor& processor;
    float zoomLevel = 1.0f;
    int centreX = 0;
    int masterW = 0;
    int knobSize = 0;
    std::atomic<float> inputMeterL  { -100.0f };
    std::atomic<float> inputMeterR  { -100.0f };
    std::atomic<float> outputMeterL { -100.0f };
    std::atomic<float> outputMeterR { -100.0f };
    std::atomic<float> inputPeakDb  { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    double inputPeakHoldUntilMs = 0.0;
    double outputPeakHoldUntilMs = 0.0;
    std::atomic<bool> inputClipHeld  { false };
    std::atomic<bool> outputClipHeld { false };
    double inputClipHoldUntilMs = 0.0;
    double outputClipHoldUntilMs = 0.0;
};

// ==============================================================================
// Ghost graph shown while a plugin is dragged.
// ==============================================================================

class DragGhost : public juce::Component
{
public:
    DragGhost();
    void start(const juce::String& name, bool isParallel, bool isBypassed,
               int slotW, int slotH, juce::Point<int> screenPos);
    void moveTo(juce::Point<int> screenPos);
    void stop();
    void paint(juce::Graphics& g) override;
private:
    juce::String ghostName;
    bool         ghostIsParallel = false;
    bool         ghostIsBypassed = false;
    int          ghostW = 180;
    int          ghostH = 35;
};

class PluginEditor;

class ResizeCorner : public juce::Component
{
public:
    explicit ResizeCorner(PluginEditor& parent, bool attachToLeft);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    PluginEditor& editor;
    bool isDragging = false;
    juce::Point<int> dragStartMouseOnScreen;
    int dragStartWidth = 1;
    int dragStartHeight = 1;
    float dragBaseWidth = 1.0f;
    float dragBaseHeight = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResizeCorner)
};

// ==============================================================================
// Graphical representation of a single slot on the master chain.
// ==============================================================================

class PluginSlotComponent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    PluginSlotComponent(PluginProcessor& p, int index);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void showPluginMenu();
    void updateState();
    int  getSlotIndex() const { return slotIndex; }
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void setRemoveBtnVisible(bool v) { showRemove = v; repaint(); }
    juce::Rectangle<float> getBypassDotBounds() const;
    juce::Rectangle<int>   getRemoveXBounds() const;

    bool isActiveParallel = false;
    bool isDropTarget     = false;

private:
    PluginProcessor& processor;
    int  slotIndex;
    bool showRemove         = false;
    bool isDragging         = false;
    int  dragStartSlotIndex = -1;
};

// ==============================================================================
// Container that shows open Parallel Split panels as tabbed views..
// ==============================================================================
class MultiParallelPanel : public juce::Component,
                           private juce::ScrollBar::Listener,
                           private juce::Timer
{
public:
    MultiParallelPanel();
    ~MultiParallelPanel() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    int rebuild(PluginProcessor& proc);
    int getActiveSlotIndex() const { return activeSlotIndex; }
    int getActiveDisplayedChainCount() const;
    int getHorizontalScrollOffset() const { return horizontalScroll; }
    void setActiveSlotIndex(int masterSlotIdx);
    void setContentTopOffset(int y) { contentTopOffset = y; resized(); }
    void setTabBarHeight(int h) { tabBarHeight = juce::jmax(16, h); resized(); repaint(); }
    void setScrollBarZoom(float z) { scrollBarZoom = juce::jmax(0.25f, z); resized(); }
    int getCount() const { return (int)panels.size(); }

private:
    static constexpr int DEFAULT_TAB_H = 20;

    struct Entry
    {
        int masterSlotIndex = -1;
        std::unique_ptr<ParallelSplitComponent> panel;
    };

    juce::OwnedArray<Entry> panels;
    juce::ScrollBar horizontalScrollBar { false };
    int activeSlotIndex = -1;
    int contentTopOffset = DEFAULT_TAB_H + 4;
    int tabBarHeight = DEFAULT_TAB_H;
    int horizontalScroll = 0;
    float scrollBarZoom = 1.0f;
    bool scrollBarHot = false;

    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    void timerCallback() override;
    ParallelSplitComponent* getActivePanel() const;
    int getVisibleChainColumns() const;
    int getActiveContentWidth() const;
    int getMaxHorizontalScroll() const;
    int getScrollBarThickness() const;
    int getScrollBarHotZoneHeight() const;
    bool shouldShowHorizontalScrollBar() const;
    void updateScrollBarHotState(juce::Point<int> mousePos);
    void updateHorizontalScrollBar();
    void setHorizontalScroll(int newOffset);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiParallelPanel)
};

// ==============================================================================
// TOP BAR WITH MENU.
// ==============================================================================
class PresetBarComponent : public juce::Component,
                           private juce::Timer
{
public:
    explicit PresetBarComponent(PluginProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshSkinColours();

    void refreshLabel() {}

    std::function<void()> onOptionsMenu;

private:
    PluginProcessor& processor;

    PaintlessTextButton optionsBtn;
    PaintlessTextButton abToggleButton;
    PaintlessTextButton undoBtn, redoBtn;
    PaintlessTextButton presetMenuBtn, prevPresetBtn, nextPresetBtn;
    PaintlessTextButton loadBtn, saveBtn, saveAsBtn;

    void doSave();
    void doSaveAs();
    void doLoad();
    void doUndo();
    void doRedo();
    void loadPreviousPreset();
    void loadNextPreset();
    void showPresetMenu();
    void selectABSlot(bool useA);
    void toggleABSlot();
    void copyABSlot(bool copyAToB);
    void swapABSlots();
    void syncABCacheToProcessor();
    void timerCallback() override;
    void drawSegmentButton(juce::Graphics& g, const juce::Button& button,
                           const juce::String& text, bool enabled = true,
                           bool drawHamburger = false);
    std::vector<juce::File> getPresetFiles() const;
    void loadPresetFileAsync(const juce::File& fileToLoad);
    juce::String captureSnapshot() const;
    void recordStateIfChanged();
    void applySnapshot(const juce::String& snapshot);

    std::vector<juce::String> undoStack;
    std::vector<juce::String> redoStack;
    juce::String currentSnapshot;
    juce::String abSnapshotA;
    juce::String abSnapshotB;
    bool activeABSlotIsA = true;
    bool suppressNextSnapshot = false;
    bool compactPresetActions = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBarComponent)
};

class PluginEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    // Draw background, master chain, and parallel panels.
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;
    void showOptionsMenu();
    void setZoomLevel(float newZoom);
    void setZoomLevelRaw(float newZoom);
    float getZoomLevel() const { return zoomLevel; }

    // Schedule a new layout update to avoid redundant refreshes.
    void scheduleLayoutUpdate();
    // Force immediate layout and repaint when a structural change must appear instantly.
    void refreshParallelLayoutNow();
    // Show the plugin scan overlay while the worker is active.
    void startScanOverlay();
    // Rereads the state of all slots and updates their appearance.
    void refreshAllSlots();
    // Highlight the master slot that has the open parallel split.
    void syncSlotHighlights();
    // Propagate refresh and sync to open parallel panels.
    void syncAllParallelPanels();
    MultiParallelPanel* getMultiPanel() const { return multiPanel.get(); }
    void toggleParallelPanelVisibility(int masterSlotIndex);

    DragGhost* getDragGhost() const { return dragGhost.get(); }
    void setDropTarget(int slotIndex);
    int  getDropTarget() const { return currentDropTarget; }

    int getSlotWidth()  const { return (int)(MASTER_CHAIN_W * zoomLevel) - 2 * (int)(35 * zoomLevel); }
    int getSlotHeight() const { return (int)(20  * zoomLevel); }

    // Needed by PluginSlotComponent and ParallelChainSlotComponent for cross-chain drag
    const juce::OwnedArray<PluginSlotComponent>& getMasterSlots() const { return slots; }

    // Describe the source of a drag in progress.
    enum class DragSource { None, MasterSlot, ParallelSlot };
    struct DragPayload
    {
        DragSource source      = DragSource::None;
        int        masterSlot  = -1;
        int        chainIndex  = -1;
        int        slotIndex   = -1;
        // pointer to the originating parallel processor (used in completeCrossDrag)
        ParallelSplitProcessor* parallelProc = nullptr;
    };

    // Start the ghost drag for a plugin in the parallel chains.
    // Called by ParallelChainSlotComponent at the beginning of the drag.
    void beginParallelDrag(const DragPayload& payload,
                           const juce::String& pluginName,
                           bool bypassed,
                           bool isParallel,
                           juce::Point<int> screenPos);

    // Update the position of the ghost during the parallel drag.
    void updateParallelDrag(juce::Point<int> screenPos);

    // Complete the drag: transfers the plugin to the destination.
    // Called by ParallelChainSlotComponent on mouse up.
    void completeCrossDrag(const DragPayload& payload, juce::Point<int> screenDropPos);

    // Returns true if a drag originating from the parallel chains is in progress.
    bool isParallelDragActive() const { return activeDragPayload.source != DragSource::None; }
    void clearParallelDrag() { activeDragPayload = {}; }

    void refreshSkinColours();
    bool cancelMacroModesFromExternalClick();
    void prepareForParallelSplitRemoval();

private:
    PluginProcessor&  processor;
    CustomLookAndFeel customLook;

    juce::Slider          macroKnob;
    std::array<juce::Slider, PluginProcessor::macroControlCount> macroControlKnobs;
    std::array<juce::TextButton, PluginProcessor::macroControlCount> macroAssignHotspots;
    PaintlessTextButton   macroAssignBtn;
    PaintlessTextButton   macroClearBtn;
    PaintlessTextButton   masterBypassBtn;
    PaintlessTextButton   parallelCollapseBtn;
    juce::Label           presetNameLabel;   // preset name, centered above "MASTER"
    juce::Slider          masterOutputKnob;
    MasterMeterOverlay    masterMeterOverlay;
    ScanOverlay           scanOverlay;

    juce::OwnedArray<PluginSlotComponent> slots;
    std::vector<std::unique_ptr<SplitArrowButton>> masterSplitArrowButtons;

    std::unique_ptr<MultiParallelPanel>  multiPanel;
    std::unique_ptr<DragGhost>           dragGhost;
    std::unique_ptr<PresetBarComponent>  presetBar;
    std::unique_ptr<ResizeCorner>        resizeCorner;

    bool layoutUpdatePending = false;
    bool parallelPanelsCollapsed = false;
    bool macroLaneVisible = false;
    bool macroAssignmentMode = false;
    bool macroClearMode = false;
    int  currentDropTarget   = -1;
    float zoomLevel = 1.25f;
    DragPayload activeDragPayload;  // Cross-chain drag payload in progress
    std::array<bool, 8> lastSlotValidForUi {};
    std::array<bool, 8> lastSlotBypassedForUi {};
    std::array<PluginProcessor::SlotType, 8> lastSlotTypeForUi {};
    int maintenanceTick = 0;

    struct MacroLearnCandidate
    {
        PluginProcessor::MacroMapping mapping;
        juce::AudioProcessor* processor = nullptr;
        int parameterIndex = -1;
        float initialValue = 0.0f;
    };
    std::vector<MacroLearnCandidate> macroLearnCandidates;
    int macroLearnIndex = -1;

    static constexpr int MACRO_LANE_W  = 116;
    static constexpr int MACRO_COLLAPSED_LANE_W = 0;
    static constexpr int MASTER_CHAIN_W = 160;
    static constexpr int BASE_H        = 875;
    static constexpr int RIGHT_PANEL_W = 525;
    static constexpr float MIN_ZOOM    = 0.75f;
    static constexpr float MAX_ZOOM    = 2.00f;

    void beginMacroLearn(int macroIndex);
    void clearMacroMappingsForIndex(int macroIndex);
    void cancelMacroLearn();
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void updateMacroAssignmentMode();
    void pollMacroLearn();
    void performLayoutUpdate();
    bool hasVisibleParallelPanels() const;
    int  getDisplayedParallelChainCount() const;
    int  getLeftBaseWidth() const;
    int  getBaseWidth(bool includeParallelPanels) const;
    int  getBaseHeight() const;
    juce::Rectangle<int> getSizeForScale(bool includeParallelPanels, float scale) const;
    void updateResizeLimits(bool includeParallelPanels);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
