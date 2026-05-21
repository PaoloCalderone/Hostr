#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <map>
#include <vector>

class ParallelSplitProcessor;
class PluginProcessor;

// ==============================================================================
// ParallelSplitComponent
// UI of a single Parallel Split block.
// Contains headers, slots, and footers of parallel chains.
// ==============================================================================

// ==============================================================================
// VU Meter stereo with peak hold and clipping indication, used in chain footers.
// ==============================================================================
class ChainVuMeter : public juce::Component
{
public:
    void setLevels(float l, float r, bool forceOverZero = false)
    {
        const bool newOverZero = forceOverZero || l > 0.0f || r > 0.0f;

        if (std::abs(levelL - l) < 0.2f
            && std::abs(levelR - r) < 0.2f
            && overZero == newOverZero)
            return;

        levelL = l;
        levelR = r;
        overZero = newOverZero;
        repaint();
    }
    void paint(juce::Graphics& g) override;
private:
    float levelL = -100.0f, levelR = -100.0f;
    bool overZero = false;
};

// Clickable text button that leaves all drawing to the parent component.
class PaintlessTextButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void paintButton(juce::Graphics&, bool, bool) override {}
};

class SplitArrowButton : public juce::Button
{
public:
    SplitArrowButton();

    void setArrow(juce::Point<float> startInParent,
                  juce::Point<float> endInParent,
                  float zoom,
                  juce::Colour colour,
                  bool active,
                  bool showEndSegment = true);
    bool hitTest(int x, int y) override;
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    static juce::Rectangle<float> getHitBounds(juce::Point<float> start,
                                               juce::Point<float> end,
                                               float zoom,
                                               float sourceHeight);
    static void paintArrow(juce::Graphics& g,
                           juce::Point<float> start,
                           juce::Point<float> end,
                           float zoom,
                           juce::Colour colour,
                           bool active,
                           bool showEndSegment = true);

private:
    juce::Point<float> localStart;
    juce::Point<float> localEnd;
    float arrowZoom = 1.0f;
    juce::Colour arrowColour { juce::Colours::cyan };
    bool arrowActive = false;
    bool arrowShowEndSegment = true;
};

class ParallelChainSlotComponent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ParallelChainSlotComponent(ParallelSplitProcessor* proc,
                               PluginProcessor*        pluginProc,
                               int chainIndex,
                               int slotIndex);
    ~ParallelChainSlotComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;

    void updateState();
    int  getSlotIndex()  const { return slotIndex;  }
    int  getChainIndex() const { return chainIndex; }

    void setRemoveBtnVisible(bool v) { showRemove = v; repaint(); }
    void setDisabledByAncestor(bool shouldDisable) { disabledByAncestor = shouldDisable; repaint(); }

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray&, int, int) override {}

    ParallelSplitProcessor* parallelProcessor;
    PluginProcessor*        pluginProcessor;

private:
    int chainIndex;
    int slotIndex;

    bool showRemove      = false;
    bool isDragging      = false;
    bool disabledByAncestor = false;
    int  dragStartSlotIndex = -1;

    juce::Rectangle<float> getBypassDotBounds() const;
    juce::Rectangle<int>   getRemoveBtnBounds() const;
    void showPluginMenu();
};

// ==============================================================================
// Chain header: active state and input gain.
// ==============================================================================
class ChainHeaderComponent : public juce::Component
{
public:
    ChainHeaderComponent(ParallelSplitProcessor* proc, int chainIdx);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Callback chiamato quando bypass o solo cambiano
    std::function<void()> onStateChanged;
    std::function<void()> onAddChain;
    std::function<void()> onRemoveChain;

    // Called before the processor is destroyed to prevent dangling pointer access
    void nullifyProcessor() { parallelProcessor = nullptr; }

    // Sync slider/button UI from current processor data (called after preset load)
    void syncFromProcessor();
    void configureChainButtons(bool canAdd, bool canRemove);
    void setAccentColour(juce::Colour colour) { accentColour = colour; repaint(); }
    void setDisabledByAncestor(bool shouldDisable);
    void setCommonKnobSize(int size) { commonKnobSize = juce::jmax(14, size); resized(); repaint(); }

private:
    ParallelSplitProcessor* parallelProcessor;
    int chainIndex;
    juce::Colour accentColour { juce::Colour(0xff007aff) };

    juce::Slider          gainKnob;   // -24 .. +24 dB
    PaintlessTextButton   soloBtn;    // "S" — top-right, aligned with chain number
    PaintlessTextButton   addBtn;     // "+"
    PaintlessTextButton   removeBtn;  // "-"
    bool showAddButton = false;
    bool showRemoveButton = false;
    bool disabledByAncestor = false;
    int commonKnobSize = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainHeaderComponent)
};

// ==============================================================================
// Footer of a chain: output gain. VU meter e clipping indication.
// ==============================================================================
class ChainFooterComponent : public juce::Component
{
public:
    ChainFooterComponent(ParallelSplitProcessor* proc, int chainIdx);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    void updateMeters();

    std::function<void()> onStateChanged;

    // Called before the processor is destroyed to prevent dangling pointer access
    void nullifyProcessor() { parallelProcessor = nullptr; }

    void syncFromProcessor();
    void setDisabledByAncestor(bool shouldDisable);
    void setCommonKnobSize(int size) { commonKnobSize = juce::jmax(14, size); resized(); repaint(); }

private:
    ParallelSplitProcessor* parallelProcessor;
    int chainIndex;

    juce::Slider     volumeKnob;  // -24 .. +24 dB
    bool             disabledByAncestor = false;
    int              commonKnobSize = 32;
    float            inputPeakDb = -100.0f;
    float            outputPeakDb = -100.0f;
    double           inputPeakHoldUntilMs = 0.0;
    double           outputPeakHoldUntilMs = 0.0;
    bool             inputClipHeld = false;
    bool             outputClipHeld = false;
    double           inputClipHoldUntilMs = 0.0;
    double           outputClipHoldUntilMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainFooterComponent)
};

// ==============================================================================
// Parallel Split Main Panel
// ==============================================================================
class ParallelSplitComponent : public juce::Component, public juce::Timer
{
public:
    ParallelSplitComponent(ParallelSplitProcessor* proc, PluginProcessor* pluginProc);
    ~ParallelSplitComponent() override;

    // Draws the container, backgrounds and graphic references of the two chains.
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    // Align header, slot and footer while maintaining the common grid with the master.
    void resized() override;
    // Update meters and small UI states without touching audio processing.
    void timerCallback() override;

    // Update a single slot after load, remove, move, or bypass.
    void updateSlotState(int chainIndex, int slotIndex);
    // Force all slots in a chain to refresh.
    void refreshChainSlots(int chainIndex);
    void rebuildChainComponents();

    // Sets the zoom level — must be called by the editor every time the zoom changes, BEFORE the component is resized.
    // Automatically propagates to all subcomponents and calls resized().
    void setZoomLevel(float z);
    float getZoomLevel() const { return zoomLevel; }
    void setTopInset(int insetPixels) { topInset = juce::jmax(0, insetPixels); resized(); repaint(); }
    int getTopInset() const { return topInset; }
    void setOutputFooterTop(int footerTopPixels);
    void setRootDisabledByAncestor(bool shouldDisable);

    // Sync all UI controls from processor data after a preset load
    void syncFromProcessor();

    ParallelSplitProcessor* getProcessor()       { return parallelProcessor; }
    PluginProcessor*        getPluginProcessor()  { return pluginProcessor;  }

    juce::OwnedArray<ParallelChainSlotComponent>& getSlotsForChain(int chainIndex);
    const juce::OwnedArray<ParallelChainSlotComponent>& getSlotsForChain(int chainIndex) const;
    int getNumDisplayedChains() const { return (int)chainSlots.size(); }
    int getColumnWidth() const;
    int getContentWidthForColumns(int columnCount) const;
    ParallelSplitProcessor* getProcessorForDisplayedChain(int chainIndex) const;
    int getProcessorChainIndexForDisplayedChain(int chainIndex) const;
    void toggleNestedSplitVisibility(ParallelSplitProcessor* owner, int chainIndex, int slotIndex);
    static juce::Colour getSplitGroupColour(int splitGroupId);

private:
    struct DisplayChain
    {
        ParallelSplitProcessor* processor = nullptr;
        int chainIndex = 0;
        int depth = 0;
        int splitGroupId = 0;
        bool disabledByAncestor = false;
    };

    struct NestedSplitTab
    {
        ParallelSplitProcessor* processor = nullptr;
        int slotIndex = 0;
    };

    struct NestedSplitGroup
    {
        std::uintptr_t key = 0;
        int sourceVisualIndex = -1;
        int sourceSlotIndex = -1;
        int startVisualIndex = -1;
        int chainCount = 0;
        int activeSlotIndex = -1;
        int splitGroupId = 0;
        std::vector<NestedSplitTab> tabs;
    };

    ParallelSplitProcessor* parallelProcessor;
    PluginProcessor*        pluginProcessor;

    float zoomLevel = 1.0f;
    int topInset = 0;
    int outputFooterTop = -1;

    std::vector<std::unique_ptr<ChainHeaderComponent>> headers;
    std::vector<std::unique_ptr<ChainFooterComponent>> footers;
    std::vector<std::unique_ptr<juce::OwnedArray<ParallelChainSlotComponent>>> chainSlots;
    std::vector<DisplayChain> displayChains;
    std::vector<NestedSplitGroup> nestedSplitGroups;
    std::vector<std::unique_ptr<SplitArrowButton>> nestedArrowButtons;
    std::map<std::uintptr_t, int> activeNestedSplitByGroup;
    std::map<std::uintptr_t, int> collapsedNestedSplitByGroup;
    bool rootDisabledByAncestor = false;

    void drawNestedSplitOverlays(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParallelSplitComponent)
};
