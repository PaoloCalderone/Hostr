#ifndef PARALLEL_SPLIT_COMPONENT_CPP_INCLUDED
#define PARALLEL_SPLIT_COMPONENT_CPP_INCLUDED

#include "ParallelSplitComponent.h"
#include "ParallelSplitProcessor.h"
#include "PluginProcessor.h"
#include "PluginSearchDialog.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

// ==============================================================================
// This file contains the Parallel Split panel UI:
// - chain header/footer
// - chain and slots
// - input/output, solo, and gain knobs
// - context menus and drag & drop
// ==============================================================================

// ==============================================================================
// CHAIN VU METER
// ==============================================================================

static constexpr float kGainMinDb = -100.0f;
static constexpr float kGainMaxDb =   24.0f;
static constexpr float kMinusInfThresholdDb = -99.9f;

static float clampKnobDb(float value)
{
    return juce::jlimit(kGainMinDb, kGainMaxDb, value);
}

static juce::String formatKnobDb(float db)
{
    db = clampKnobDb(db);
    if (db <= kMinusInfThresholdDb) return "-inf";
    return (db >= 0.0f ? "+" : "") + juce::String(db, 1) + " dB";
}

static std::unique_ptr<juce::Drawable> createSearchMenuIcon(juce::Colour colour)
{
    auto icon = std::make_unique<juce::DrawablePath>();
    juce::Path lens;
    lens.addEllipse(2.0f, 2.0f, 8.0f, 8.0f);
    lens.startNewSubPath(8.7f, 8.7f);
    lens.lineTo(12.5f, 12.5f);
    icon->setPath(lens);
    icon->setStrokeFill(colour);
    icon->setFill(juce::Colours::transparentBlack);
    icon->setStrokeType(juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    return icon;
}

static bool parseKnobDbText(juce::String text, float& outDb)
{
    text = text.trim().toLowerCase()
               .replace(",", ".")
               .replace("db", "")
               .trim();
    if (text == "-inf" || text == "inf" || text == "off")
    {
        outDb = kGainMinDb;
        return true;
    }

    const double parsed = text.getDoubleValue();
    if (!std::isfinite(parsed))
        return false;

    outDb = clampKnobDb((float)parsed);
    return true;
}

static void beginInlineDbEdit(juce::Component& owner,
                              juce::Rectangle<int> bounds,
                              float currentDb,
                              std::function<void(float)> applyValue)
{
    auto* editor = new juce::TextEditor();
    editor->setText(formatKnobDb(currentDb).replace(" dB", ""), false);
    editor->setSelectAllWhenFocused(true);
    editor->setJustification(juce::Justification::centred);
    editor->setMultiLine(false);
    editor->setReturnKeyStartsNewLine(false);
    editor->setScrollbarsShown(false);
    editor->setPopupMenuEnabled(false);
    editor->setBorder({ 0, 0, 0, 0 });
    editor->setInputRestrictions(12, "-+0123456789.,infINF");

    const auto& skin = hostr::currentSkin();
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::textColourId, skin.text);
    editor->setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightedTextColourId, skin.text);
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::CaretComponent::caretColourId, skin.accent);

    bounds = bounds.withSizeKeepingCentre(juce::jmax(bounds.getWidth(), 54),
                                          juce::jmax(bounds.getHeight(), 18));
    owner.addAndMakeVisible(editor);
    editor->setBounds(bounds);
    editor->toFront(true);

    juce::Component::SafePointer<juce::TextEditor> safeEditor(editor);
    auto finish = [safeEditor, applyValue = std::move(applyValue)](bool commit) mutable
    {
        auto* ed = safeEditor.getComponent();
        if (ed == nullptr)
            return;

        if (commit)
        {
            float db = 0.0f;
            if (parseKnobDbText(ed->getText(), db) && applyValue)
                applyValue(db);
        }

        juce::MessageManager::callAsync([safeEditor]()
        {
            if (auto* e = safeEditor.getComponent())
                delete e;
        });
    };

    editor->onReturnKey = [finish]() mutable { finish(true); };
    editor->onEscapeKey = [finish]() mutable { finish(false); };
    editor->onFocusLost = [finish]() mutable { finish(false); };
    editor->grabKeyboardFocus();
}

static juce::NormalisableRange<double> knobDbRange()
{
    return { (double)kGainMinDb, (double)kGainMaxDb,
             [](double start, double end, double normalised)
             {
                 juce::ignoreUnused(start, end);
                 normalised = juce::jlimit(0.0, 1.0, normalised);
                if (normalised <= 0.03)
                    return juce::jmap(normalised, 0.0, 0.03, (double)kGainMinDb, -48.0);
                if (normalised <= 0.25)
                    return juce::jmap(normalised, 0.03, 0.25, -48.0, -24.0);
                if (normalised <= 0.50)
                    return juce::jmap(normalised, 0.25, 0.50, -24.0, 0.0);
                return juce::jmap(normalised, 0.50, 1.0, 0.0, (double)kGainMaxDb);
             },
             [](double start, double end, double value)
             {
                 juce::ignoreUnused(start, end);
                 value = juce::jlimit((double)kGainMinDb, (double)kGainMaxDb, value);
                if (value <= -48.0)
                    return juce::jmap(value, (double)kGainMinDb, -48.0, 0.0, 0.03);
                if (value <= -24.0)
                    return juce::jmap(value, -48.0, -24.0, 0.03, 0.25);
                if (value <= 0.0)
                    return juce::jmap(value, -24.0, 0.0, 0.25, 0.50);
                return juce::jmap(value, 0.0, (double)kGainMaxDb, 0.50, 1.0);
             },
             [](double start, double end, double value)
             {
                 return juce::jlimit(start, end, std::round(value * 10.0) / 10.0);
             } };
}

static int commonGuiKnobSize(float zoom)
{
    return juce::jmax(20, juce::roundToInt(45.0f * zoom));
}

static juce::NormalisableRange<double> outputKnobDbRange()
{
    return { (double)kGainMinDb, (double)kGainMaxDb,
             [](double start, double end, double normalised)
             {
                 juce::ignoreUnused(start, end);
                 normalised = juce::jlimit(0.0, 1.0, normalised);
                if (normalised <= 0.03)
                    return juce::jmap(normalised, 0.0, 0.03, (double)kGainMinDb, -48.0);
                if (normalised <= 0.25)
                    return juce::jmap(normalised, 0.03, 0.25, -48.0, -24.0);
                if (normalised <= 0.50)
                    return juce::jmap(normalised, 0.25, 0.50, -24.0, 0.0);
                return juce::jmap(normalised, 0.50, 1.0, 0.0, (double)kGainMaxDb);
             },
             [](double start, double end, double value)
             {
                 juce::ignoreUnused(start, end);
                 value = juce::jlimit((double)kGainMinDb, (double)kGainMaxDb, value);
                if (value <= -48.0)
                    return juce::jmap(value, (double)kGainMinDb, -48.0, 0.0, 0.03);
                if (value <= -24.0)
                    return juce::jmap(value, -48.0, -24.0, 0.03, 0.25);
                if (value <= 0.0)
                    return juce::jmap(value, -24.0, 0.0, 0.25, 0.50);
                return juce::jmap(value, 0.0, (double)kGainMaxDb, 0.50, 1.0);
             },
             [](double start, double end, double value)
             {
                 return juce::jlimit(start, end, std::round(value * 10.0) / 10.0);
             } };
}

juce::Colour ParallelSplitComponent::getSplitGroupColour(int splitGroupId)
{
    static constexpr juce::uint32 colours[] =
    {
        0xff28b7c9, 0xffb79cff, 0xff36ff98, 0xffffdf58,
        0xffff5ba0, 0xffc55cff, 0xff43e6ff, 0xffff8246
    };

    return juce::Colour(colours[(size_t)juce::jlimit(0, 7, splitGroupId % 8)]);
}

static std::uintptr_t nestedSplitGroupKey(ParallelSplitProcessor* owner, int chainIndex)
{
    auto raw = reinterpret_cast<std::uintptr_t>(owner);
    return (raw >> 4) ^ ((std::uintptr_t)chainIndex * 0x9e3779b97f4a7c15ULL);
}

static float parallelMeterNormForDb(float db)
{
    static constexpr float dbPoints[]   = { -100.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f };
    static constexpr float normPoints[] = {    0.0f,   1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f, 1.0f };

    db = juce::jlimit(dbPoints[0], dbPoints[6], db);
    for (int i = 0; i < 6; ++i)
    {
        if (db <= dbPoints[i + 1])
        {
            const float t = (db - dbPoints[i]) / (dbPoints[i + 1] - dbPoints[i]);
            return normPoints[i] + t * (normPoints[i + 1] - normPoints[i]);
        }
    }

    return 1.0f;
}

static juce::String formatMeterPeakDb(float db)
{
    db = juce::jlimit(-100.0f, 24.0f, db);
    if (db <= -99.0f)
        return "-inf";

    return (db >= 0.0f ? "+" : "") + juce::String(db, 1);
}

class ScopedPluginGraphMutation
{
public:
    explicit ScopedPluginGraphMutation(PluginProcessor* p) : processor(p)
    {
        if (processor != nullptr)
            processor->beginGraphMutation();
    }

    ~ScopedPluginGraphMutation()
    {
        if (processor != nullptr)
            processor->endGraphMutation();
    }

private:
    PluginProcessor* processor = nullptr;
};

void ChainVuMeter::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto b = getLocalBounds().toFloat();
    if (b.isEmpty()) return;

    // Barre con gap centrale
    const float gap  = juce::jmax(1.0f, b.getWidth() / 14.4f);
    const float barW = (b.getWidth() - gap) * 0.5f;

    auto drawBar = [&](float x, float level)
    {
        juce::Rectangle<float> barBg(x, b.getY(), barW, b.getHeight());
        g.setColour(skin.panelInset);
        g.fillRoundedRectangle(barBg, 2.0f);

        float norm = parallelMeterNormForDb(level);
        if (norm > 0.0f)
        {
            auto fill = barBg.withTop(barBg.getBottom() - barBg.getHeight() * norm);
            juce::ColourGradient grad(overZero ? skin.meterPeak : skin.accentAlt,
                                      fill.getBottomLeft(),
                                      overZero ? skin.danger : skin.accentSoft,
                                      fill.getTopLeft(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fill, 2.0f);
        }
    };

    drawBar(b.getX(),              levelL);
    drawBar(b.getX() + barW + gap, levelR);
}

SplitArrowButton::SplitArrowButton()
    : juce::Button("Split Arrow")
{
    setWantsKeyboardFocus(false);
    setTriggeredOnMouseDown(true);
}

juce::Rectangle<float> SplitArrowButton::getHitBounds(juce::Point<float> start,
                                                      juce::Point<float> end,
                                                      float zoom,
                                                      float sourceHeight)
{
    const auto left   = juce::jmin(start.x, end.x);
    const auto right  = juce::jmax(start.x, end.x);
    const auto top    = juce::jmin(start.y, end.y);
    const auto bottom = juce::jmax(start.y, end.y);
    const float hitH = juce::jmax(18.0f * zoom, sourceHeight * 1.8f);
    const float hitW = juce::jmax(1.0f, right - left);

    return juce::Rectangle<float>(left, top, hitW, juce::jmax(hitH, bottom - top))
        .expanded(8.0f * zoom, 8.0f * zoom);
}

void SplitArrowButton::setArrow(juce::Point<float> startInParent,
                                juce::Point<float> endInParent,
                                float zoom,
                                juce::Colour colour,
                                bool active,
                                bool showEndSegment)
{
    const auto hit = getHitBounds(startInParent, endInParent, zoom, 20.0f * zoom);
    setBounds(hit.toNearestInt());

    localStart = startInParent - getPosition().toFloat();
    localEnd = endInParent - getPosition().toFloat();
    arrowZoom = zoom;
    arrowColour = colour;
    arrowActive = active;
    arrowShowEndSegment = showEndSegment;
    repaint();
}

void SplitArrowButton::paintArrow(juce::Graphics& g,
                                  juce::Point<float> start,
                                  juce::Point<float> end,
                                  float zoom,
                                  juce::Colour colour,
                                  bool active,
                                  bool showEndSegment)
{
    if (end.x <= start.x)
        return;

    const float arrowSize = juce::jmax(5.0f, 7.0f * zoom);
    const float availableW = end.x - start.x;
    const float trunkX = start.x + juce::jlimit(8.0f * zoom,
                                                juce::jmax(8.0f * zoom, availableW - arrowSize * 2.2f),
                                                availableW * 0.46f);

    juce::Path connector;
    connector.startNewSubPath(start);
    const float cornerRadius = juce::jmin(juce::jmax(4.0f, 7.0f * zoom),
                                          juce::jmax(1.0f, std::abs(end.y - start.y) * 0.42f));
    const float verticalSign = end.y >= start.y ? 1.0f : -1.0f;
    connector.lineTo(trunkX - cornerRadius, start.y);
    connector.quadraticTo(trunkX, start.y, trunkX, start.y + verticalSign * cornerRadius);
    connector.lineTo(trunkX, end.y - verticalSign * cornerRadius);
    if (showEndSegment)
    {
        connector.quadraticTo(trunkX, end.y, trunkX + cornerRadius, end.y);
        connector.lineTo(end.x - arrowSize, end.y);
    }

    g.setColour(active ? colour.withAlpha(0.94f) : colour.withAlpha(0.36f));
    g.strokePath(connector, juce::PathStrokeType(active ? juce::jmax(1.5f, 2.2f * zoom)
                                                       : juce::jmax(1.0f, 1.3f * zoom),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    if (showEndSegment)
    {
        juce::Path arrowHead;
        arrowHead.startNewSubPath(end);
        arrowHead.lineTo(end.x - arrowSize, end.y - arrowSize * 0.62f);
        arrowHead.lineTo(end.x - arrowSize, end.y + arrowSize * 0.62f);
        arrowHead.closeSubPath();
        g.fillPath(arrowHead);
    }

    const float dotRadius = active ? juce::jmax(2.4f, 4.1f * zoom)
                                   : juce::jmax(1.8f, 3.0f * zoom);
    g.fillEllipse(start.x - dotRadius, start.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
}

bool SplitArrowButton::hitTest(int x, int y)
{
    if (localEnd.x <= localStart.x)
        return false;

    const juce::Point<float> p((float)x, (float)y);
    const float arrowSize = juce::jmax(5.0f, 7.0f * arrowZoom);
    const float availableW = localEnd.x - localStart.x;
    const float trunkX = localStart.x + juce::jlimit(8.0f * arrowZoom,
                                                     juce::jmax(8.0f * arrowZoom, availableW - arrowSize * 2.2f),
                                                     availableW * 0.46f);

    const auto distanceToSegment = [](juce::Point<float> point,
                                      juce::Point<float> a,
                                      juce::Point<float> b)
    {
        const auto ab = b - a;
        const auto ap = point - a;
        const float lenSq = ab.x * ab.x + ab.y * ab.y;
        if (lenSq <= 0.0001f)
            return point.getDistanceFrom(a);

        const float t = juce::jlimit(0.0f, 1.0f, (ap.x * ab.x + ap.y * ab.y) / lenSq);
        return point.getDistanceFrom({ a.x + ab.x * t, a.y + ab.y * t });
    };

    const juce::Point<float> trunkStart(trunkX, localStart.y);
    const juce::Point<float> trunkEnd(trunkX, localEnd.y);
    const juce::Point<float> arrowBase(localEnd.x - arrowSize, localEnd.y);
    const float tolerance = juce::jmax(7.0f, 8.0f * arrowZoom);

    return distanceToSegment(p, localStart, trunkStart) <= tolerance
        || distanceToSegment(p, trunkStart, trunkEnd) <= tolerance
        || distanceToSegment(p, trunkEnd, arrowBase) <= tolerance
        || p.getDistanceFrom(localStart) <= tolerance
        || p.getDistanceFrom(localEnd) <= tolerance + arrowSize;
}

void SplitArrowButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    auto colour = arrowColour;
    if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter(0.20f);
    if (shouldDrawButtonAsDown)
        colour = colour.brighter(0.35f);

    paintArrow(g, localStart, localEnd, arrowZoom, colour, arrowActive, arrowShowEndSegment);
}

// ==============================================================================
// PARALLEL CHAIN SLOT
// ==============================================================================

class NestedParallelSplitWindow : public juce::DocumentWindow
{
public:
    NestedParallelSplitWindow(ParallelSplitProcessor* split, PluginProcessor* pluginProc)
        : DocumentWindow("Parallel Split",
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(ResizableWindow::backgroundColourId),
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setAlwaysOnTop(true);
        setUsingNativeTitleBar(false);
        setTitleBarHeight(26);
        auto* component = new ParallelSplitComponent(split, pluginProc);
        component->setZoomLevel(1.0f);
        setContentOwned(component, true);
        setResizable(true, true);
        setResizeLimits(520, 360, 2400, 1200);
        centreWithSize(760, 560);
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};

static constexpr float PDOT_X_RATIO     = 15.0f / 35.0f;
static constexpr float PDOT_SIZE_RATIO  = 10.0f / 35.0f;
static constexpr float PREMOVE_SZ_RATIO = 16.0f / 35.0f;

juce::Rectangle<float> ParallelChainSlotComponent::getBypassDotBounds() const
{
    const float h  = (float)getHeight();
    const float baseSz = h * PDOT_SIZE_RATIO;
    const float sz = baseSz * 0.875f;
    const float x  = h * PDOT_X_RATIO + baseSz * 0.5f - sz * 0.5f;
    return { x, h / 2.0f - sz / 2.0f, sz, sz };
}

juce::Rectangle<int> ParallelChainSlotComponent::getRemoveBtnBounds() const
{
    const float h  = (float)getHeight();
    const int maxSize = juce::jmax(1, juce::roundToInt(h - 4.0f));
    const int size = juce::jmin(maxSize,
                                juce::jmax(juce::roundToInt(h * PREMOVE_SZ_RATIO),
                                           juce::roundToInt(h * PDOT_SIZE_RATIO)));
    const int x    = getWidth() - juce::roundToInt(h * PDOT_X_RATIO) - size;
    const int y    = getHeight() / 2 - size / 2;
    return { x, y, size, size };
}

ParallelChainSlotComponent::ParallelChainSlotComponent(
    ParallelSplitProcessor* proc,
    PluginProcessor*        pluginProc,
    int chainIdx, int slotIdx)
    : parallelProcessor(proc), pluginProcessor(pluginProc),
      chainIndex(chainIdx), slotIndex(slotIdx)
{}

void ParallelChainSlotComponent::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto bounds = getLocalBounds().toFloat();
    const bool bitmapSkin = hostr::hasBitmapSkinSurface(skin);
    if (bounds.isEmpty() || !parallelProcessor) return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    bool isOccupied = chain.slots[slotIndex].valid;
    bool isBypassed = chain.slots[slotIndex].bypassed;
    const bool effectivelyDisabled = disabledByAncestor || chain.muted;

    // Identico al master: cornerSize proporzionale all'altezza
    const float cornerSize = bounds.getHeight() * 0.57f;
    const float h = bounds.getHeight();

    // --- Sfondo nero + bordo (identico master) ---
    if (bitmapSkin)
        hostr::paintSkinSurface(g, bounds, skin, cornerSize);
    else
    {
        g.setColour(skin.panelInset);
        g.fillRoundedRectangle(bounds, cornerSize);
    }

    if (isDragging)
        g.setColour(skin.text.withAlpha(0.08f));
    else
        g.setColour(skin.border.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds, cornerSize, 2.0f);

    // --- EMPTY slot ---
    if (!isOccupied)
    {
        g.setColour(skin.mutedText.withAlpha(effectivelyDisabled ? 0.35f : 1.0f));
        g.setFont(h * 0.69f);
        g.drawText("+", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    // --- Slot in DRAG ---
    if (isDragging)
    {
        if (!bitmapSkin)
        {
            g.setColour(skin.text.withAlpha(0.05f));
            g.fillRoundedRectangle(bounds.reduced(2), cornerSize - 1.0f);
        }
        g.setColour(skin.border.withAlpha(0.55f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
        return;
    }

    // --- Internal fill same as master ---
    bool hasSurface = false;
    if (bitmapSkin)
        hasSurface = hostr::paintSkinSurface(g, bounds.reduced(2.0f), skin, cornerSize - 1.0f);
    else
    {
        g.setColour(effectivelyDisabled ? skin.panelInset.darker(0.18f) : skin.panelRaised);
        g.fillRoundedRectangle(bounds.reduced(2), cornerSize - 1.0f);
    }
    hostr::paintTextureOverlay(g, bounds.reduced(3.0f), skin, hasSurface ? 0.16f : 0.45f);

    // --- Bypass dot (blue circular on/off) ---
    auto dot = getBypassDotBounds();

    hostr::paintBypassLed(g, dot, skin,
                          !isBypassed && !effectivelyDisabled,  // onState
                          effectivelyDisabled);

    // --- Plugin name same as master ---
    g.setColour((isBypassed || effectivelyDisabled) ? skin.text.withAlpha(effectivelyDisabled ? 0.24f : 0.4f)
                                                    : skin.text);
    g.setFont(juce::Font(juce::FontOptions(h * 0.46f)));

    const int dotRight = (int)(dot.getRight() + h * 0.23f);
    const int xRight   = showRemove ? getRemoveBtnBounds().getX() : getWidth() - 4;
    g.drawText(chain.slots[slotIndex].name,
               dotRight, 0, xRight - dotRight, getHeight(),
               juce::Justification::centredLeft, true);

    // --- X button (same as master) ---
    if (showRemove)
    {
        g.setColour(skin.text.withAlpha(effectivelyDisabled ? 0.24f : 0.5f));
        g.setFont(juce::Font(juce::FontOptions(h * 0.38f).withStyle("Bold")));
        g.drawText("x", getRemoveBtnBounds(), juce::Justification::centred, false);
    }
}

void ParallelChainSlotComponent::resized() {}

void ParallelChainSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!parallelProcessor || !pluginProcessor) return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    bool isOccupied = chain.slots[slotIndex].valid;

    if (isOccupied &&
        getRemoveBtnBounds().contains(e.getPosition()) &&
        !e.mods.isRightButtonDown())
    {
        auto* splitComponent = findParentComponentOfClass<ParallelSplitComponent>();
        auto* editor = findParentComponentOfClass<PluginEditor>();
        juce::Component::SafePointer<ParallelChainSlotComponent> safeThis(this);
        juce::Component::SafePointer<ParallelSplitComponent> safeSplit(splitComponent);
        juce::Component::SafePointer<PluginEditor> safeEditor(editor);

        if (auto basePath = pluginProcessor->getMacroTargetPathForSplit(parallelProcessor); basePath.isNotEmpty())
            pluginProcessor->removeMacroMappingsForTargetPath(
                basePath + "/C" + juce::String(chainIndex) + "/S" + juce::String(slotIndex),
                chain.slots[(size_t)slotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit);

        {
            ScopedPluginGraphMutation mutation(pluginProcessor);
            parallelProcessor->removePluginFromChain(chain, slotIndex);
        }

        if (safeSplit != nullptr)
            safeSplit->rebuildChainComponents();
        if (safeEditor != nullptr)
            safeEditor->scheduleLayoutUpdate();
        if (safeThis != nullptr)
            safeThis->updateState();
        return;
    }

    if (!isOccupied) { if (!e.mods.isRightButtonDown()) showPluginMenu(); return; }
    if (e.mods.isRightButtonDown()) { showPluginMenu(); return; }

    // Click on bypass toggle area (dot)
    auto dotHit = getBypassDotBounds().expanded(6.0f);
    if (dotHit.contains(e.position))
    {
        parallelProcessor->setSlotBypassed(chain, slotIndex, !chain.slots[slotIndex].bypassed);
        repaint();
        return;
    }


    if (e.getNumberOfClicks() == 2)
    {
        if (chain.slots[slotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit
            && chain.slots[slotIndex].parallelProcessor != nullptr)
        {
            if (auto* splitComponent = findParentComponentOfClass<ParallelSplitComponent>())
                splitComponent->toggleNestedSplitVisibility(parallelProcessor, chainIndex, slotIndex);
        }
        else
        {
            parallelProcessor->showPluginGUI(chain, slotIndex);
        }
        dragStartSlotIndex = -1;
        return;
    }

    dragStartSlotIndex = slotIndex;
}

void ParallelChainSlotComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStartSlotIndex < 0) return;
    if (!parallelProcessor || !pluginProcessor) return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    if (!chain.slots[dragStartSlotIndex].valid) return;

    auto* editor = findParentComponentOfClass<PluginEditor>();
    if (!editor) return;

    if (!isDragging)
    {
        if (e.getDistanceFromDragStart() > 4)
        {
            isDragging = true;
            repaint();

            // Activate ghost drag (same as master chain)
            PluginEditor::DragPayload payload;
            payload.source       = PluginEditor::DragSource::ParallelSlot;
            payload.chainIndex   = chainIndex;
            payload.slotIndex    = dragStartSlotIndex;
            payload.parallelProc = parallelProcessor;

            editor->beginParallelDrag(payload,
                chain.slots[dragStartSlotIndex].name,
                chain.slots[dragStartSlotIndex].bypassed,
                chain.slots[dragStartSlotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit,
                e.getScreenPosition());
        }
        return;
    }

    editor->updateParallelDrag(e.getScreenPosition());
}

void ParallelChainSlotComponent::mouseUp(const juce::MouseEvent& e)
{
    if (!isDragging)
    {
        if (dragStartSlotIndex >= 0 && parallelProcessor != nullptr)
        {
            auto& chain = parallelProcessor->getChain(chainIndex);
            if (chain.slots[dragStartSlotIndex].valid
                && chain.slots[dragStartSlotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit
                && chain.slots[dragStartSlotIndex].parallelProcessor != nullptr)
            {
                if (auto* splitComponent = findParentComponentOfClass<ParallelSplitComponent>())
                    splitComponent->toggleNestedSplitVisibility(parallelProcessor, chainIndex, dragStartSlotIndex);
            }
            else if (chain.slots[dragStartSlotIndex].valid)
            {
                parallelProcessor->showPluginGUI(chain, dragStartSlotIndex);
            }
        }

        dragStartSlotIndex = -1;
        return;
    }

    isDragging = false;
    repaint();

    auto* editor = findParentComponentOfClass<PluginEditor>();

    if (!parallelProcessor || !pluginProcessor)
    {
        if (editor) editor->clearParallelDrag();
        dragStartSlotIndex = -1;
        return;
    }

    if (editor && editor->isParallelDragActive())
    {
        // Delegate all drop logic to the editor — handles both parallel and master slot (cross-chain) drops
        PluginEditor::DragPayload payload;
        payload.source       = PluginEditor::DragSource::ParallelSlot;
        payload.chainIndex   = chainIndex;
        payload.slotIndex    = dragStartSlotIndex;
        payload.parallelProc = parallelProcessor;

        editor->completeCrossDrag(payload, e.getScreenPosition());
    }

    dragStartSlotIndex = -1;
}

void ParallelChainSlotComponent::showPluginMenu()
{
    juce::PopupMenu menu;
    if (!parallelProcessor || !pluginProcessor) return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    bool isOccupied = chain.slots[slotIndex].valid;

    const bool isParallelSplit = isOccupied
        && chain.slots[slotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit;

    if (isOccupied)
    {
        if (!isParallelSplit)
            menu.addItem(998, "Open Plugin GUI");
        else
            menu.addItem(996, "Open Parallel Split");
        menu.addItem(997, "Remove Plugin");
        menu.addSeparator();
    }
    const auto& skin = hostr::currentSkin();
    menu.addItem(juce::PopupMenu::Item("Load").setEnabled(false));
    int chainSplitCount = 0;
    for (const auto& slotInfo : chain.slots)
        if (slotInfo.valid
            && slotInfo.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit)
            ++chainSplitCount;
    const bool canAddParallelSplit = isParallelSplit
        || chainSplitCount < ParallelSplitProcessor::maxChains;
    menu.addItem(999, "Parallel Split", canAddParallelSplit);

    menu.addItem(juce::PopupMenu::Item("Search Plugin").setID(1).setImage(createSearchMenuIcon(skin.text.withAlpha(0.85f))));

    juce::Component::SafePointer<ParallelChainSlotComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int r)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr || !self->parallelProcessor || !self->pluginProcessor) return;

        auto& ch = self->parallelProcessor->getChain(self->chainIndex);

        if (r == 997)
        {
            auto* splitComponent = self->findParentComponentOfClass<ParallelSplitComponent>();
            auto* editor = self->findParentComponentOfClass<PluginEditor>();
            juce::Component::SafePointer<ParallelChainSlotComponent> safeSlot(self);
            juce::Component::SafePointer<ParallelSplitComponent> safeSplit(splitComponent);
            juce::Component::SafePointer<PluginEditor> safeEditor(editor);

            if (auto basePath = self->pluginProcessor->getMacroTargetPathForSplit(self->parallelProcessor); basePath.isNotEmpty())
                self->pluginProcessor->removeMacroMappingsForTargetPath(
                    basePath + "/C" + juce::String(self->chainIndex) + "/S" + juce::String(self->slotIndex),
                    ch.slots[(size_t)self->slotIndex].type == ParallelSplitProcessor::ChainSlotType::ParallelSplit);

            {
                ScopedPluginGraphMutation mutation(self->pluginProcessor);
                self->parallelProcessor->removePluginFromChain(ch, self->slotIndex);
            }

            if (safeSplit != nullptr)
                safeSplit->rebuildChainComponents();
            if (safeEditor != nullptr)
                safeEditor->scheduleLayoutUpdate();
            if (safeSlot != nullptr)
                safeSlot->updateState();
            return;
        }
        if (r == 998) { self->parallelProcessor->showPluginGUI(ch, self->slotIndex); return; }
        if (r == 996)
        {
            auto& sl = ch.slots[self->slotIndex];
            if (sl.parallelProcessor)
            {
                if (sl.editorWindow)
                {
                    sl.editorWindow->setAlwaysOnTop(true);
                    sl.editorWindow->setVisible(true);
                    sl.editorWindow->toFront(true);
                }
                else
                {
                    sl.editorWindow = std::make_unique<NestedParallelSplitWindow>(
                        sl.parallelProcessor, self->pluginProcessor);
                }
            }
            return;
        }

        if (r == 999)
        {
            {
                ScopedPluginGraphMutation mutation(self->pluginProcessor);
                self->parallelProcessor->createParallelSplitInChain(ch, self->slotIndex);
            }
            if (auto* splitComponent = self->findParentComponentOfClass<ParallelSplitComponent>())
            {
                splitComponent->toggleNestedSplitVisibility(self->parallelProcessor, self->chainIndex, self->slotIndex);
            }
            if (auto* editor = self->findParentComponentOfClass<PluginEditor>())
                editor->refreshParallelLayoutNow();
            return;
        }

        if (r == 1)
        {
            auto allPlugins = self->pluginProcessor->getLoadablePluginDescriptions();

            PluginSearchDialog::show(self, allPlugins,
                [safeThis](const juce::PluginDescription& desc)
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr || !self->parallelProcessor || !self->pluginProcessor) return;
                    auto& ch2 = self->parallelProcessor->getChain(self->chainIndex);
                    juce::String err;
                    std::unique_ptr<juce::AudioProcessor> inst;
                    const double sr = self->pluginProcessor->getSampleRate() > 0.0
                                        ? self->pluginProcessor->getSampleRate() : 44100.0;
                    const int bs = self->pluginProcessor->getBlockSize() > 0
                                        ? self->pluginProcessor->getBlockSize() : 512;
                    const auto loadDesc = self->pluginProcessor->canonicalizePluginDescriptionForLoad(desc);
                    try { inst = self->pluginProcessor->formatManager.createPluginInstance(
                              loadDesc, sr, bs, err); }
                    catch (...) { err = "Exception"; }

                    if (inst)
                    {
                        {
                            ScopedPluginGraphMutation mutation(self->pluginProcessor);
                            self->parallelProcessor->loadPluginInChain(ch2, self->slotIndex, std::move(inst), loadDesc.name,
                                                                       loadDesc.pluginFormatName, loadDesc.fileOrIdentifier,
                                                                       loadDesc.uniqueId, true);
                        }
                        juce::MessageManager::callAsync([safeThis]()
                        {
                            if (safeThis != nullptr)
                                safeThis->updateState();
                        });
                    }
                    else
                    {
                        self->pluginProcessor->removeKnownPlugin(desc);
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            "Plugin Load Failed", "Could not load: " + desc.name + "\n" + err);
                    }
                });
        }
    });
}
void ParallelChainSlotComponent::updateState()
{
    if (!parallelProcessor) return;
    auto& chain = parallelProcessor->getChain(chainIndex);
    showRemove = chain.slots[slotIndex].valid;
    repaint();
}

// ==============================================================================
// CHAIN HEADER (bypass + input gain knob)
// ==============================================================================

ChainHeaderComponent::ChainHeaderComponent(ParallelSplitProcessor* proc, int chainIdx)
    : parallelProcessor(proc), chainIndex(chainIdx)
{
    // Input gain knob
    addAndMakeVisible(gainKnob);
    gainKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    gainKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainKnob.setNormalisableRange(knobDbRange());
    gainKnob.setMouseDragSensitivity(360);
    gainKnob.setValue(0.0);
    gainKnob.setDoubleClickReturnValue(true, 0.0);
    gainKnob.setColour(juce::Slider::thumbColourId, juce::Colours::cyan);
    gainKnob.setTooltip("Input Gain");
    gainKnob.onValueChange = [this]()
    {
        if (!parallelProcessor) return;
        auto& chain = parallelProcessor->getChain(chainIndex);
        chain.inputGainDb = clampKnobDb((float)gainKnob.getValue());
        repaint();
    };

    addAndMakeVisible(soloBtn);
    soloBtn.setButtonText("S");
    soloBtn.setClickingTogglesState(true);
    soloBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    soloBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    soloBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
    soloBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
    soloBtn.onClick = [this]()
    {
        if (!parallelProcessor) return;
        auto& chain = parallelProcessor->getChain(chainIndex);
        chain.solo = soloBtn.getToggleState();
        repaint();
        if (onStateChanged) onStateChanged();
    };

    auto setupRoundButton = [](PaintlessTextButton& b, const juce::String& text, const juce::String& tip)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        b.setTooltip(tip);
    };

    addAndMakeVisible(addBtn);
    setupRoundButton(addBtn, "+", "Add parallel chain");
    addBtn.onClick = [this]()
    {
        if (showAddButton && onAddChain)
        {
            auto callback = onAddChain;
            juce::MessageManager::callAsync([callback]()
            {
                if (callback) callback();
            });
        }
    };

    addAndMakeVisible(removeBtn);
    setupRoundButton(removeBtn, "-", "Remove parallel chain");
    removeBtn.onClick = [this]()
    {
        if (showRemoveButton && onRemoveChain)
        {
            auto callback = onRemoveChain;
            juce::MessageManager::callAsync([callback]()
            {
                if (callback) callback();
            });
        }
    };
}

void ChainHeaderComponent::configureChainButtons(bool canAdd, bool canRemove)
{
    showAddButton = canAdd;
    showRemoveButton = canRemove;
    addBtn.setVisible(canAdd);
    removeBtn.setVisible(canRemove);
    resized();
    repaint();
}

void ChainHeaderComponent::setDisabledByAncestor(bool shouldDisable)
{
    disabledByAncestor = shouldDisable;
    gainKnob.setAlpha(shouldDisable ? 0.42f : 1.0f);
    soloBtn.setAlpha(shouldDisable ? 0.42f : 1.0f);
    addBtn.setAlpha(shouldDisable ? 0.42f : 1.0f);
    removeBtn.setAlpha(shouldDisable ? 0.42f : 1.0f);
    repaint();
}

void ChainHeaderComponent::syncFromProcessor()
{
    if (!parallelProcessor) return;
    auto& chain = parallelProcessor->getChain(chainIndex);
    chain.inputGainDb = clampKnobDb(chain.inputGainDb);
    gainKnob.setValue(chain.inputGainDb, juce::dontSendNotification);
    soloBtn.setToggleState(chain.solo, juce::dontSendNotification);
    repaint();
}

void ChainHeaderComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!parallelProcessor) return;

    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float buttonSize = juce::jmin(W * 0.30f, H * 0.275f);
    const float circleSize = buttonSize * 0.78f;
    const float buttonX    = W * 0.06f;
    const float buttonY    = H * 0.18f;
    const float circleX    = buttonX + (buttonSize - circleSize) * 0.5f;
    const float circleY    = buttonY + (buttonSize - circleSize) * 0.5f;
    juce::Rectangle<float> circleRect(circleX, circleY, circleSize, circleSize);

    if (circleRect.contains(e.position))
    {
        auto& chain = parallelProcessor->getChain(chainIndex);
        chain.muted = !chain.muted;
        repaint();
        if (onStateChanged) onStateChanged();
    }
}

void ChainHeaderComponent::paint(juce::Graphics& g)
{
    if (!parallelProcessor) return;
    const auto& skin = hostr::currentSkin();
    auto& chain = parallelProcessor->getChain(chainIndex);
    const bool effectivelyDisabled = disabledByAncestor || chain.muted;

    const float W = (float)getWidth();
    const float H = (float)getHeight();

    // Chain enable dot — top-left, coloured when active.
    juce::Rectangle<float> circleRect;
    {
        const float buttonSize = juce::jmin(W * 0.30f, H * 0.275f);
        const float circleSize = buttonSize * 0.78f;
        const float buttonX    = W * 0.06f;
        const float buttonY    = H * 0.18f;
        const float circleX    = buttonX + (buttonSize - circleSize) * 0.5f;
        const float circleY    = buttonY + (buttonSize - circleSize) * 0.5f;
        circleRect = { circleX, circleY, circleSize, circleSize };

        const auto colour = effectivelyDisabled ? skin.panelRaised : accentColour;
        g.setColour(juce::Colours::black.withAlpha(0.22f));
        g.fillEllipse(circleRect.translated(circleRect.getWidth() * 0.08f, circleRect.getHeight() * 0.10f));
        g.setColour(colour.darker(0.42f).withAlpha(effectivelyDisabled ? 0.34f : 0.78f));
        g.fillEllipse(circleRect);
        g.setColour(colour.brighter(0.12f).withAlpha(effectivelyDisabled ? 0.24f : 0.72f));
        g.fillEllipse(circleRect.reduced(circleRect.getWidth() * 0.16f));
        g.setColour(effectivelyDisabled ? skin.border.withAlpha(0.34f) : accentColour.brighter(0.22f).withAlpha(0.62f));
        g.drawEllipse(circleRect.reduced(0.5f), 1.0f);
    }

    auto soloBounds = soloBtn.getBounds().toFloat();
    const bool soloOn = soloBtn.getToggleState();
    hostr::paintLedButton(g, soloBounds, skin,
                          soloOn ? "chain-s-on.png" : "chain-s-off.png",
                          "S",
                          soloOn ? skin.warning : skin.text.withAlpha(0.68f),
                          effectivelyDisabled);

    auto drawRoundHeaderButton = [&](const juce::TextButton& button,
                                     const juce::String& text,
                                     juce::Colour accent)
    {
        if (!button.isVisible()) return;
        auto rb = button.getBounds().toFloat();
        juce::ignoreUnused(accent);
        const bool isAdd = text == "+";
        hostr::paintLedButton(g, rb, skin,
                              isAdd ? "chain-plus.png" : "chain-minus.png",
                              text,
                              skin.text.withAlpha(isAdd ? 0.92f : 0.68f),
                              effectivelyDisabled);
    };

    drawRoundHeaderButton(addBtn, "+", accentColour.brighter(0.20f));
    drawRoundHeaderButton(removeBtn, "-", skin.danger);

    const float knobSize = (float)commonKnobSize;
    const float knobX    = (W - knobSize) / 2.0f;
    const float knobY    = H * 0.36f;
    const float knobMidY = knobY + knobSize * 0.5f;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * ((float)commonKnobSize / 45.0f));
    const int rangeLabelH = juce::jmax(12, juce::roundToInt(labelReferenceH * 0.15f));
    const int rangeLabelY = juce::roundToInt(knobMidY - (float)rangeLabelH * 0.5f);

    // Range labels "-24" / "24" on knob sides, aligned with knob center 
    g.setFont(juce::jmax(8.0f, labelReferenceH * 0.15f));
    g.setColour(skin.mutedText.withAlpha(disabledByAncestor ? 0.45f : 1.0f));
    // Knob LEFT side
    const float rangeGap = W * 0.008f;
    const float rangeW = W * 0.16f;
    g.drawText("-inf", juce::roundToInt(knobX - rangeW - rangeGap), rangeLabelY,
               (int)(W * 0.16f), rangeLabelH, juce::Justification::right);
    g.drawText("0",   (int)(knobX + knobSize * 0.35f), juce::jmax(0, juce::roundToInt(knobY - labelReferenceH * 0.12f)),
               (int)(knobSize * 0.30f), juce::roundToInt(labelReferenceH * 0.13f), juce::Justification::centred);
    // Knob RIGHT side
    g.drawText("24",  juce::roundToInt(knobX + knobSize + rangeGap), rangeLabelY,
               (int)(W * 0.16f), rangeLabelH, juce::Justification::left);

    // INPUT GAIN label
    const float controlLabelFontSize = juce::jmax(8.0f, 10.2f * ((float)commonKnobSize / 45.0f));
    const int inputNameY = juce::roundToInt(knobY + knobSize + 3.0f * ((float)commonKnobSize / 45.0f));
    const int inputDbY = inputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    const int inputDbH = juce::jmax(14, juce::roundToInt(labelReferenceH * 0.13f));
    g.setFont(juce::Font(juce::FontOptions(controlLabelFontSize)));
    g.drawText("INPUT GAIN",
               (int)(knobX - W * 0.12f), inputNameY,
               (int)(knobSize + W * 0.24f), juce::roundToInt(labelReferenceH * 0.13f),
               juce::Justification::centred);

    // Gain value dB
    const auto inputDb = clampKnobDb(chain.inputGainDb);
    g.setFont(juce::jmax(7.0f, labelReferenceH * 0.105f));
    g.setColour(skin.mutedText.withAlpha(disabledByAncestor ? 0.35f : 0.7f));
    g.drawText(formatKnobDb(inputDb), 0, inputDbY,
               (int)W, inputDbH,
               juce::Justification::centred);
}

void ChainHeaderComponent::resized()
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();

    const int knobSize = commonKnobSize;
    const int knobX    = ((int)W - knobSize) / 2;
    const int knobY    = (int)(H * 0.36f);
    gainKnob.setBounds(knobX, knobY, knobSize, knobSize);

    const int buttonSize = (int)juce::jmin(W * 0.30f, H * 0.275f);
    const int circleSize = buttonSize;
    const int circleY    = (int)(H * 0.18f);
    const int circleX    = (int)(W * 0.06f);
    const int controlsX = (int)W - circleX - circleSize;
    const int addY = circleY;

    const int buttonGap = juce::jmax(2, (int)(H * 0.04f));
    const int removeY = addY + circleSize + buttonGap;
    soloBtn.setBounds(circleX, removeY, circleSize, circleSize);
    removeBtn.setBounds(controlsX, removeY, circleSize, circleSize);
    if (showAddButton)
    {
        addBtn.setBounds(controlsX, addY, circleSize, circleSize);
    }
    else
    {
        addBtn.setBounds(controlsX, addY, 0, 0);
    }
}

void ChainHeaderComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!parallelProcessor) return;

    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float knobSize = (float)commonKnobSize;
    const float knobX = (W - knobSize) / 2.0f;
    const float knobY = H * 0.36f;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * ((float)commonKnobSize / 45.0f));
    const int inputNameY = juce::roundToInt(knobY + knobSize + 3.0f * ((float)commonKnobSize / 45.0f));
    const int inputDbY = inputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    const int inputDbH = juce::jmax(14, juce::roundToInt(labelReferenceH * 0.13f));
    const auto valueBounds = juce::Rectangle<int>(0, inputDbY, (int)W, inputDbH).expanded(4);

    if (!valueBounds.contains(e.getPosition()))
        return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    beginInlineDbEdit(*this, valueBounds, clampKnobDb(chain.inputGainDb),
                      [this](float db)
                      {
                          if (!parallelProcessor) return;
                          auto& targetChain = parallelProcessor->getChain(chainIndex);
                          targetChain.inputGainDb = db;
                          gainKnob.setValue(db, juce::dontSendNotification);
                          repaint();
                      });
    juce::ignoreUnused(knobX);
}

// ==============================================================================
// CHAIN FOOTER (output gain knob)
// ==============================================================================

ChainFooterComponent::ChainFooterComponent(ParallelSplitProcessor* proc, int chainIdx)
    : parallelProcessor(proc), chainIndex(chainIdx)
{
    addAndMakeVisible(volumeKnob);
    volumeKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volumeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeKnob.setNormalisableRange(outputKnobDbRange());
    volumeKnob.setMouseDragSensitivity(360);
    volumeKnob.setValue(0.0);
    volumeKnob.setDoubleClickReturnValue(true, 0.0);
    volumeKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff4a9eff));
    volumeKnob.setTooltip("Output Volume");
    volumeKnob.onValueChange = [this]()
    {
        if (!parallelProcessor) return;
        auto& chain = parallelProcessor->getChain(chainIndex);
        chain.outputVolDb = clampKnobDb((float)volumeKnob.getValue());
        repaint();
    };
}

void ChainFooterComponent::paint(juce::Graphics& g)
{
    if (!parallelProcessor) return;
    const auto& skin = hostr::currentSkin();
    auto& chain = parallelProcessor->getChain(chainIndex);
    const bool effectivelyDisabled = disabledByAncestor || chain.muted;

    const int W = getWidth(), H = getHeight();

    const float knobSize = (float)commonKnobSize;
    const float knobX    = ((float)W - knobSize) / 2.0f;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * ((float)commonKnobSize / 45.0f));
    const float controlLabelFontSize = juce::jmax(8.0f, 10.2f * ((float)commonKnobSize / 45.0f));
    const float outputDbH = juce::jmax(15.0f, labelReferenceH * 0.16f);
    const float knobY    = juce::jmax(8.0f * ((float)commonKnobSize / 45.0f),
                                      labelReferenceH * 0.16f + 1.0f * ((float)commonKnobSize / 45.0f));
    const float knobMidY = knobY + knobSize * 0.5f;
    const int rangeLabelH = juce::jmax(12, juce::roundToInt(labelReferenceH * 0.15f));
    const int rangeLabelY = juce::roundToInt(knobMidY - (float)rangeLabelH * 0.5f);

    g.setFont(juce::jmax(8.0f, labelReferenceH * 0.15f));
    g.setColour(skin.mutedText.withAlpha(effectivelyDisabled ? 0.42f : 1.0f));
    const float rangeGap = W * 0.008f;
    const float rangeW = W * 0.16f;
    g.drawText("-inf", juce::roundToInt(knobX - rangeW - rangeGap), rangeLabelY,
               juce::roundToInt(rangeW), rangeLabelH, juce::Justification::right);
    g.drawText("0", (int)(knobX + knobSize * 0.35f), juce::jmax(0, juce::roundToInt(knobY - labelReferenceH * 0.12f)),
               (int)(knobSize * 0.30f), juce::roundToInt(labelReferenceH * 0.13f), juce::Justification::centred);
    g.drawText("24", juce::roundToInt(knobX + knobSize + rangeGap), rangeLabelY,
               juce::roundToInt(rangeW), rangeLabelH, juce::Justification::left);

    g.setFont(juce::Font(juce::FontOptions(controlLabelFontSize)));
    const int outputNameY = juce::roundToInt(knobY + knobSize + 3.0f * ((float)commonKnobSize / 45.0f));
    const int outputDbY = outputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    g.drawText("OUTPUT", 0, outputNameY,
               W, juce::roundToInt(labelReferenceH * 0.13f),
               juce::Justification::centred);

    const auto outputDb = clampKnobDb(chain.outputVolDb);
    g.setFont(juce::jmax(7.0f, labelReferenceH * 0.105f));
    g.setColour(skin.mutedText.withAlpha(effectivelyDisabled ? 0.35f : 0.7f));
    g.drawText(formatKnobDb(outputDb),
               0, outputDbY,
               W, juce::roundToInt(outputDbH),
               juce::Justification::centred);

    auto drawDotPair = [&](float leftDb, float rightDb, float centreX, float peakDb, bool clipHeld)
    {
        const int dots = 11;
        static constexpr float thresholds[dots] = { -48.0f, -36.0f, -24.0f, -18.0f, -9.0f, -6.0f,
                                                     -3.0f, -1.0f, -0.3f, 0.0f, 0.3f };
        const float dot = juce::jlimit(2.8f, 5.4f, H * 0.046f);
        const float gap = juce::jmax(1.0f, dot * 0.45f);
        const float colGap = dot * 0.90f;
        const float zoom = (float)commonKnobSize / 45.0f;
        const float pairW = dot * 2.0f + colGap;
        const float peakFont = juce::jmax(7.0f, labelReferenceH * 0.105f);
        const float peakMargin = juce::jmax(7.0f * zoom, dot * 1.7f);
        const float totalH = dots * dot + (dots - 1) * gap;
        const float top = juce::jmax(peakFont + peakMargin, knobMidY - totalH * 0.5f);
        const float peakY = top - peakFont - peakMargin;

        g.setFont(juce::Font(juce::FontOptions(peakFont)));
        g.setColour(skin.text.withAlpha(effectivelyDisabled ? 0.35f : 0.76f));
        g.drawFittedText(formatMeterPeakDb(peakDb),
                         juce::Rectangle<float>(centreX - pairW * 1.1f, peakY,
                                                pairW * 2.2f, peakFont + 3.0f).toNearestInt(),
                         juce::Justification::centred, 1, 0.82f);

        const struct { int dotIndex; const char* label; } labels[] { { 9, "0" },
                                                                     { 6, "-3" },
                                                                     { 4, "-9" } };
        const bool labelsOnRight = centreX < (float)W * 0.5f;
        const float dbLabelFont = juce::jmax(7.0f, labelReferenceH * 0.105f);
        const float dbLabelW = dot * 7.0f;
        const float dbLabelH = juce::jmax(dot * 1.75f, dbLabelFont + 2.0f);
        const float dbLabelX = labelsOnRight ? centreX + pairW * 0.5f + dot * 0.75f
                                             : centreX - pairW * 0.5f - dbLabelW - dot * 0.75f;
        g.setFont(juce::Font(juce::FontOptions(dbLabelFont)));
        g.setColour(skin.mutedText.withAlpha(effectivelyDisabled ? 0.25f : 0.58f));
        for (const auto& label : labels)
        {
            const float dotY = top + (float)(dots - 1 - label.dotIndex) * (dot + gap);
            const float y = dotY + dot * 0.5f - dbLabelH * 0.5f;
            g.drawText(label.label,
                       juce::roundToInt(dbLabelX),
                       juce::roundToInt(y),
                       juce::roundToInt(dbLabelW),
                       juce::roundToInt(dbLabelH),
                       labelsOnRight ? juce::Justification::left : juce::Justification::right);
        }

        auto drawColumn = [&](float db, float x)
        {
            for (int i = 0; i < dots; ++i)
            {
                const bool clipDot = i == dots - 1;
                const bool redDot = i >= dots - 2;
                const bool yellowDot = i >= 6 && i <= 8;
                const bool lit = clipDot ? (db > 0.3f || clipHeld)
                                         : db >= thresholds[(size_t)i];
                const float y = top + (float)(dots - 1 - i) * (dot + gap);
                juce::Colour colour = redDot ? skin.meterPeak
                                   : yellowDot ? skin.warning
                                               : skin.meter;
                g.setColour(colour.withAlpha(lit && !effectivelyDisabled ? 0.95f : 0.16f));
                g.fillEllipse(x, y, dot, dot);
            }
        };

        drawColumn(leftDb, centreX - colGap * 0.5f - dot);
        drawColumn(rightDb, centreX + colGap * 0.5f);
    };

    const float meterOffset = knobSize * 0.5f + W * 0.24f;
    drawDotPair(chain.inLevelL, chain.inLevelR, W * 0.5f - meterOffset, inputPeakDb, inputClipHeld);
    drawDotPair(chain.outLevelL, chain.outLevelR, W * 0.5f + meterOffset, outputPeakDb, outputClipHeld);

}

void ChainFooterComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!parallelProcessor) return;

    const int W = getWidth();
    const float knobSize = (float)commonKnobSize;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * ((float)commonKnobSize / 45.0f));
    const float knobY = juce::jmax(8.0f * ((float)commonKnobSize / 45.0f),
                                   labelReferenceH * 0.16f + 1.0f * ((float)commonKnobSize / 45.0f));
    const int outputNameY = juce::roundToInt(knobY + knobSize + 3.0f * ((float)commonKnobSize / 45.0f));
    const int outputDbY = outputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    const int outputDbH = juce::jmax(15, juce::roundToInt(labelReferenceH * 0.16f));
    const auto valueBounds = juce::Rectangle<int>(0, outputDbY, W, outputDbH).expanded(4);

    if (!valueBounds.contains(e.getPosition()))
        return;

    auto& chain = parallelProcessor->getChain(chainIndex);
    beginInlineDbEdit(*this, valueBounds, clampKnobDb(chain.outputVolDb),
                      [this](float db)
                      {
                          if (!parallelProcessor) return;
                          auto& targetChain = parallelProcessor->getChain(chainIndex);
                          targetChain.outputVolDb = db;
                          volumeKnob.setValue(db, juce::dontSendNotification);
                          repaint();
                      });
}

void ChainFooterComponent::syncFromProcessor()
{
    if (!parallelProcessor) return;
    auto& chain = parallelProcessor->getChain(chainIndex);
    chain.outputVolDb = clampKnobDb(chain.outputVolDb);
    volumeKnob.setValue(chain.outputVolDb, juce::dontSendNotification);
    repaint();
}

void ChainFooterComponent::setDisabledByAncestor(bool shouldDisable)
{
    disabledByAncestor = shouldDisable;
    volumeKnob.setAlpha(shouldDisable ? 0.42f : 1.0f);
    repaint();
}

void ChainFooterComponent::resized()
{
    int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;

    const int knobSize = commonKnobSize;
    const int knobX    = (W - knobSize) / 2;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * ((float)commonKnobSize / 45.0f));
    const int knobY    = juce::jmax(juce::roundToInt(8.0f * ((float)commonKnobSize / 45.0f)),
                                    juce::roundToInt(labelReferenceH * 0.16f
                                                     + 1.0f * ((float)commonKnobSize / 45.0f)));
    volumeKnob.setBounds(knobX, knobY, knobSize, knobSize);
}

void ChainFooterComponent::updateMeters()
{
    if (!parallelProcessor) return;
    auto& chain = parallelProcessor->getChain(chainIndex);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    auto updatePeak = [nowMs](float candidate, float& peak, double& holdUntil)
    {
        if (candidate >= peak || nowMs >= holdUntil)
        {
            peak = candidate;
            holdUntil = nowMs + 300.0;
        }
    };
    auto updateClipHold = [nowMs](float candidate, bool& held, double& holdUntil)
    {
        if (candidate > 0.3f)
            holdUntil = nowMs + 300.0;

        held = nowMs < holdUntil;
    };

    const auto inputPairPeak = juce::jmax(chain.inLevelL, chain.inLevelR);
    const auto outputPairPeak = juce::jmax(chain.outLevelL, chain.outLevelR);
    updatePeak(inputPairPeak, inputPeakDb, inputPeakHoldUntilMs);
    updateClipHold(inputPairPeak, inputClipHeld, inputClipHoldUntilMs);
    updatePeak(outputPairPeak, outputPeakDb, outputPeakHoldUntilMs);
    updateClipHold(outputPairPeak, outputClipHeld, outputClipHoldUntilMs);
    repaint();
}

// ==============================================================================
// PARALLEL SPLIT COMPONENT
// ==============================================================================

ParallelSplitComponent::ParallelSplitComponent(ParallelSplitProcessor* proc,
                                               PluginProcessor*        pluginProc)
    : parallelProcessor(proc), pluginProcessor(pluginProc)
{
    rebuildChainComponents();

    startTimerHz(15);
}

ParallelSplitComponent::~ParallelSplitComponent()
{
    stopTimer();
    
    parallelProcessor = nullptr;
    pluginProcessor   = nullptr;
    for (auto& arr : chainSlots)
        for (auto* s : *arr)
        {
            s->parallelProcessor = nullptr;
            s->pluginProcessor = nullptr;
        }
    for (auto& h : headers) h->nullifyProcessor();
    for (auto& f : footers) f->nullifyProcessor();
}

void ParallelSplitComponent::rebuildChainComponents()
{
    if (!parallelProcessor) return;

    std::vector<DisplayChain> flattened;
    std::vector<NestedSplitGroup> groups;
    int nextSplitGroupId = 1;

    std::function<void(ParallelSplitProcessor*, int, int, bool)> appendSplit =
        [&](ParallelSplitProcessor* split, int depth, int splitGroupId, bool disabledByAncestor)
    {
        if (!split) return;
        for (int c = 0; c < split->getNumChains(); ++c)
        {
            const int sourceVisualIndex = (int)flattened.size();
            flattened.push_back({ split, c, depth, splitGroupId, disabledByAncestor });

            auto& chain = split->getChain(c);
            const bool childDisabledByAncestor = disabledByAncestor || chain.muted;
            std::vector<NestedSplitTab> nestedTabs;
            for (const auto& slot : chain.slots)
                if (slot.valid
                    && slot.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit
                    && slot.parallelProcessor)
                {
                    const int slotIndex = (int)(&slot - chain.slots.data());
                    nestedTabs.push_back({ slot.parallelProcessor, slotIndex });
                }

            if (!nestedTabs.empty())
            {
                const auto key = nestedSplitGroupKey(split, c);
                if (collapsedNestedSplitByGroup.count(key) != 0)
                    continue;

                int activeSlotIndex = activeNestedSplitByGroup.count(key) != 0
                    ? activeNestedSplitByGroup[key]
                    : nestedTabs.front().slotIndex;

                auto activeIt = std::find_if(nestedTabs.begin(), nestedTabs.end(),
                                             [activeSlotIndex](const NestedSplitTab& tab)
                                             {
                                                 return tab.slotIndex == activeSlotIndex;
                                             });
                if (activeIt == nestedTabs.end())
                {
                    activeIt = nestedTabs.begin();
                    activeSlotIndex = activeIt->slotIndex;
                    activeNestedSplitByGroup[key] = activeSlotIndex;
                }

                NestedSplitGroup group;
                group.key = key;
                group.sourceVisualIndex = sourceVisualIndex;
                group.sourceSlotIndex = activeSlotIndex;
                group.startVisualIndex = (int)flattened.size();
                group.activeSlotIndex = activeSlotIndex;
                group.splitGroupId = nextSplitGroupId++;
                group.tabs = nestedTabs;

                const bool activeSplitSlotBypassed = chain.slots[(size_t)activeIt->slotIndex].bypassed;
                appendSplit(activeIt->processor, depth + 1, group.splitGroupId,
                            childDisabledByAncestor || activeSplitSlotBypassed);
                group.chainCount = (int)flattened.size() - group.startVisualIndex;
                groups.push_back(std::move(group));
            }
        }
    };
    appendSplit(parallelProcessor, 0, 0, rootDisabledByAncestor);

    headers.clear();
    footers.clear();
    chainSlots.clear();
    nestedArrowButtons.clear();
    displayChains = flattened;
    nestedSplitGroups = groups;

    for (int visualIdx = 0; visualIdx < (int)displayChains.size(); ++visualIdx)
    {
        auto* proc = displayChains[(size_t)visualIdx].processor;
        const int chainIdx = displayChains[(size_t)visualIdx].chainIndex;
        proc->getChain(chainIdx).name = juce::String(chainIdx + 1);

        auto header = std::make_unique<ChainHeaderComponent>(proc, chainIdx);
        auto footer = std::make_unique<ChainFooterComponent>(proc, chainIdx);
        const bool disabledByAncestor = displayChains[(size_t)visualIdx].disabledByAncestor;
        header->setAccentColour(getSplitGroupColour(displayChains[(size_t)visualIdx].splitGroupId));
        header->setDisabledByAncestor(disabledByAncestor);
        footer->setDisabledByAncestor(disabledByAncestor);
        header->onAddChain = [this, proc]()
        {
            if (!proc || !pluginProcessor || !proc->canAddChain())
                return;

            {
                ScopedPluginGraphMutation mutation(pluginProcessor);
                proc->addChain();
                if (pluginProcessor->getSampleRate() > 0.0 && pluginProcessor->getBlockSize() > 0)
                    proc->prepareToPlay(pluginProcessor->getSampleRate(), pluginProcessor->getBlockSize());
            }

            rebuildChainComponents();
            if (auto* editor = findParentComponentOfClass<PluginEditor>())
                editor->scheduleLayoutUpdate();
        };
        header->onRemoveChain = [this, proc, chainIdx]()
        {
            if (!proc || !pluginProcessor || !proc->canRemoveChain(chainIdx))
                return;

            {
                ScopedPluginGraphMutation mutation(pluginProcessor);
                proc->removeChain(chainIdx);
                if (pluginProcessor->getSampleRate() > 0.0 && pluginProcessor->getBlockSize() > 0)
                    proc->prepareToPlay(pluginProcessor->getSampleRate(), pluginProcessor->getBlockSize());
            }

            rebuildChainComponents();
            if (auto* editor = findParentComponentOfClass<PluginEditor>())
                editor->scheduleLayoutUpdate();
        };
        header->onStateChanged = [safeThis = juce::Component::SafePointer<ParallelSplitComponent>(this)]()
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (safeThis == nullptr)
                    return;

                safeThis->rebuildChainComponents();
                if (auto* editor = safeThis->findParentComponentOfClass<PluginEditor>())
                    editor->scheduleLayoutUpdate();
            });
        };
        const bool isRightmostChainForLevel = chainIdx == proc->getNumChains() - 1;
        header->configureChainButtons(isRightmostChainForLevel && proc->canAddChain(),
                                      proc->canRemoveChain(chainIdx));
        addAndMakeVisible(*header);
        addAndMakeVisible(*footer);
        headers.push_back(std::move(header));
        footers.push_back(std::move(footer));

        auto slotsForChain = std::make_unique<juce::OwnedArray<ParallelChainSlotComponent>>();
        for (int s = 0; s < 8; ++s)
        {
            auto* slot = new ParallelChainSlotComponent(proc, pluginProcessor, chainIdx, s);
            slot->setDisabledByAncestor(disabledByAncestor);
            addAndMakeVisible(slot);
            slotsForChain->add(slot);
        }
        chainSlots.push_back(std::move(slotsForChain));
    }

    for (const auto& group : nestedSplitGroups)
    {
        for (const auto& tabInfo : group.tabs)
        {
            auto button = std::make_unique<SplitArrowButton>();
            button->onClick = [this, key = group.key, slotIndex = tabInfo.slotIndex]()
            {
                activeNestedSplitByGroup[key] = slotIndex;
                collapsedNestedSplitByGroup.erase(key);
                rebuildChainComponents();
                if (auto* editor = findParentComponentOfClass<PluginEditor>())
                    editor->scheduleLayoutUpdate();
            };
            addAndMakeVisible(*button);
            nestedArrowButtons.push_back(std::move(button));
        }
    }

    for (auto& h : headers) h->syncFromProcessor();
    for (auto& f : footers) f->syncFromProcessor();
    for (int c = 0; c < (int)chainSlots.size(); ++c)
        refreshChainSlots(c);

    resized();
    repaint();
}

juce::OwnedArray<ParallelChainSlotComponent>& ParallelSplitComponent::getSlotsForChain(int chainIndex)
{
    return *chainSlots[(size_t)juce::jlimit(0, (int)chainSlots.size() - 1, chainIndex)];
}

const juce::OwnedArray<ParallelChainSlotComponent>& ParallelSplitComponent::getSlotsForChain(int chainIndex) const
{
    return *chainSlots[(size_t)juce::jlimit(0, (int)chainSlots.size() - 1, chainIndex)];
}

ParallelSplitProcessor* ParallelSplitComponent::getProcessorForDisplayedChain(int chainIndex) const
{
    if (displayChains.empty()) return parallelProcessor;
    return displayChains[(size_t)juce::jlimit(0, (int)displayChains.size() - 1, chainIndex)].processor;
}

int ParallelSplitComponent::getProcessorChainIndexForDisplayedChain(int chainIndex) const
{
    if (displayChains.empty()) return 0;
    return displayChains[(size_t)juce::jlimit(0, (int)displayChains.size() - 1, chainIndex)].chainIndex;
}

void ParallelSplitComponent::toggleNestedSplitVisibility(ParallelSplitProcessor* owner, int ownerChainIndex, int slotIndex)
{
    if (!owner)
        return;

    const auto key = nestedSplitGroupKey(owner, ownerChainIndex);
    const bool isExpanded = collapsedNestedSplitByGroup.count(key) == 0
                         && activeNestedSplitByGroup.count(key) != 0
                         && activeNestedSplitByGroup[key] == slotIndex;

    if (isExpanded)
    {
        collapsedNestedSplitByGroup[key] = slotIndex;
    }
    else
    {
        activeNestedSplitByGroup[key] = slotIndex;
        collapsedNestedSplitByGroup.erase(key);
    }

    rebuildChainComponents();
    if (auto* editor = findParentComponentOfClass<PluginEditor>())
        editor->scheduleLayoutUpdate();
}

void ParallelSplitComponent::syncFromProcessor()
{
    if (!parallelProcessor) return;
    rebuildChainComponents();
    for (auto& h : headers) h->syncFromProcessor();
    for (auto& f : footers) f->syncFromProcessor();
    for (auto& arr : chainSlots)
        for (auto* s : *arr)
            s->updateState();
    repaint();
}

void ParallelSplitComponent::refreshChainSlots(int chainIdx)
{
    if (!parallelProcessor || chainIdx < 0 || chainIdx >= (int)chainSlots.size()) return;
    if (displayChains.empty()) return;
    auto* proc = getProcessorForDisplayedChain(chainIdx);
    const int procChainIdx = getProcessorChainIndexForDisplayedChain(chainIdx);
    if (!proc) return;
    auto& chain = proc->getChain(procChainIdx);
    auto& arr   = *chainSlots[(size_t)chainIdx];
    for (auto* s : arr)
    {
        s->setRemoveBtnVisible(chain.slots[s->getSlotIndex()].valid);
        s->repaint();
    }
}

void ParallelSplitComponent::updateSlotState(int chainIdx, int slotIdx)
{
    if (chainIdx >= 0 && chainIdx < (int)chainSlots.size() && slotIdx >= 0 && slotIdx < 8)
        (*chainSlots[(size_t)chainIdx])[slotIdx]->updateState();
}


void ParallelSplitComponent::setZoomLevel(float z)
{
    zoomLevel = z;
    resized();
    repaint();
}

void ParallelSplitComponent::setOutputFooterTop(int footerTopPixels)
{
    const int newFooterTop = footerTopPixels >= 0 ? footerTopPixels : -1;
    if (outputFooterTop == newFooterTop)
        return;

    outputFooterTop = newFooterTop;
    resized();
    repaint();
}

int ParallelSplitComponent::getColumnWidth() const
{
    return juce::jmax(1, juce::roundToInt(160.0f * zoomLevel));
}

int ParallelSplitComponent::getContentWidthForColumns(int columnCount) const
{
    const int columns = juce::jmax(1, columnCount);
    constexpr int sepW = 2;
    const int rightMargin = juce::jmax(8, juce::roundToInt(10.0f * zoomLevel));
    return columns * getColumnWidth() + (columns - 1) * sepW + rightMargin;
}

void ParallelSplitComponent::setRootDisabledByAncestor(bool shouldDisable)
{
    if (rootDisabledByAncestor == shouldDisable)
        return;

    rootDisabledByAncestor = shouldDisable;
    rebuildChainComponents();
}

void ParallelSplitComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto& skin = hostr::currentSkin();
    if (!hostr::paintSkinSurface(g, b, skin, 0.0f))
    {
        g.setColour(skin.background);
        g.fillRect(b);
        hostr::paintTextureOverlay(g, b, skin, skin.framedPanels ? 0.55f : 0.25f);
    }

    const int count = juce::jmax(1, (int)chainSlots.size());
    const int colW = getColumnWidth();
    const int sepW = 2;
    const float frameInsetY = juce::jmax(3.0f, 5.0f * zoomLevel);
    const float frameTop = (float)topInset + frameInsetY;
    const float frameH = juce::jmax(1.0f, (float)getHeight() - frameTop - frameInsetY);

    for (int c = 0; c < count; ++c)
    {
        const float x = (float)(c * (colW + sepW));
        hostr::paintPanelFrame(g,
                               juce::Rectangle<float>(x, frameTop, (float)colW, frameH),
                               skin, 8.0f, 1.0f);
    }
}

void ParallelSplitComponent::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
}

void ParallelSplitComponent::paintOverChildren(juce::Graphics& g)
{
    drawNestedSplitOverlays(g);
}

void ParallelSplitComponent::drawNestedSplitOverlays(juce::Graphics& g)
{
    for (const auto& group : nestedSplitGroups)
    {
        if (group.sourceVisualIndex < 0 || group.sourceVisualIndex >= (int)chainSlots.size()
            || group.startVisualIndex < 0 || group.startVisualIndex >= (int)headers.size()
            || group.sourceSlotIndex < 0 || group.sourceSlotIndex >= 8)
            continue;

        const auto colour = getSplitGroupColour(group.splitGroupId);
        if (group.startVisualIndex >= (int)chainSlots.size()
            || (*chainSlots[(size_t)group.startVisualIndex]).size() <= 0)
            continue;

        auto* targetSlot = (*chainSlots[(size_t)group.startVisualIndex])[0];
        if (!targetSlot) continue;

        const auto target = targetSlot->getBounds().toFloat();
        const float endX = target.getX();
        const float endY = target.getCentreY();

        for (const auto& tabInfo : group.tabs)
        {
            if (tabInfo.slotIndex < 0 || tabInfo.slotIndex >= 8)
                continue;

            auto* sourceSlot = (*chainSlots[(size_t)group.sourceVisualIndex])[tabInfo.slotIndex];
            if (!sourceSlot) continue;

            const auto source = sourceSlot->getBounds().toFloat();
            const float startX = source.getRight();
            const float startY = source.getCentreY();
            if (endX <= startX)
                continue;

            SplitArrowButton::paintArrow(g, { startX, startY }, { endX, endY },
                                          zoomLevel, colour,
                                          tabInfo.slotIndex == group.activeSlotIndex);
        }
    }
}

void ParallelSplitComponent::resized()
{
    int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;

    const float z = zoomLevel;

    const int headerH    = juce::jmax(78, (int)(116.0f * z));
    const int slotH      = (int)(20.0f * z);
    const int slotGap    = (int)(6.0f  * z);
    const int slotPad    = (int)(35.0f * z);
    const int headerGap  = (int)(10.0f * z);
    const int sepW       = 2;
    const int totalSlotH = 8 * slotH + 7 * slotGap;

    const int outerPad = 0;
    const int topPad   = topInset;
    const int bottomPad = juce::jmax(10, (int)(12.0f * z));
    const int count = juce::jmax(1, (int)chainSlots.size());
    const int colW  = getColumnWidth();

    int slotsTop = topPad + headerH + headerGap;
    const int compactGap = juce::jmax(2, (int)(3.0f * z));
    int footerTop = slotsTop + totalSlotH + compactGap;
    if (outputFooterTop >= 0)
        footerTop = juce::jmax(footerTop, outputFooterTop);
    const int commonFooterKnobSize = commonGuiKnobSize(z);
    const int footerKnobTop = juce::jmax(juce::roundToInt(8.0f * z),
                                         juce::roundToInt(juce::jmax(54.0f, 78.0f * z) * 0.16f + 1.0f * z));
    const int footerNameH = juce::roundToInt(juce::jmax(54.0f, 78.0f * z) * 0.13f);
    const int footerDbH = juce::jmax(15, juce::roundToInt(juce::jmax(54.0f, 78.0f * z) * 0.16f));
    const int footerMinH = footerKnobTop + commonFooterKnobSize + footerNameH + footerDbH + bottomPad;
    int footerH   = footerMinH;

    for (int c = 0; c < count; ++c)
    {
        const int x = outerPad + c * (colW + sepW);
        const int commonKnobSize = commonGuiKnobSize(z);
        headers[(size_t)c]->setCommonKnobSize(commonKnobSize);
        footers[(size_t)c]->setCommonKnobSize(commonKnobSize);

        headers[(size_t)c]->setBounds(x, topPad, colW, headerH);
        for (int i = 0; i < 8; ++i)
        {
            int y = slotsTop + i * (slotH + slotGap);
            (*chainSlots[(size_t)c])[i]->setBounds(x + slotPad, y,
                                                   juce::jmax(8, colW - slotPad * 2), slotH);
        }
        footers[(size_t)c]->setBounds(x, footerTop, colW, footerH);
    }

    int buttonIndex = 0;
    for (const auto& group : nestedSplitGroups)
    {
        if (group.sourceVisualIndex < 0
            || group.sourceVisualIndex >= (int)chainSlots.size()
            || group.startVisualIndex < 0
            || group.startVisualIndex >= (int)headers.size())
        {
            buttonIndex += (int)group.tabs.size();
            continue;
        }

        if (group.startVisualIndex >= (int)chainSlots.size()
            || (*chainSlots[(size_t)group.startVisualIndex]).size() <= 0)
        {
            buttonIndex += (int)group.tabs.size();
            continue;
        }

        auto* targetSlot = (*chainSlots[(size_t)group.startVisualIndex])[0];
        if (!targetSlot)
        {
            buttonIndex += (int)group.tabs.size();
            continue;
        }

        const auto target = targetSlot->getBounds().toFloat();
        const float endX = target.getX();
        const float endY = target.getCentreY();

        for (size_t i = 0; i < group.tabs.size() && buttonIndex < (int)nestedArrowButtons.size(); ++i)
        {
            const auto& tabInfo = group.tabs[i];
            auto* arrow = nestedArrowButtons[(size_t)buttonIndex].get();
            if (tabInfo.slotIndex >= 0 && tabInfo.slotIndex < 8)
            {
                auto* sourceSlot = (*chainSlots[(size_t)group.sourceVisualIndex])[tabInfo.slotIndex];
                if (sourceSlot != nullptr)
                {
                    const auto source = sourceSlot->getBounds().toFloat();
                    const float startX = source.getRight();
                    if (endX > startX)
                    {
                        arrow->setVisible(true);
                        arrow->setArrow({ startX, source.getCentreY() }, { endX, endY },
                                        zoomLevel, getSplitGroupColour(group.splitGroupId),
                                        tabInfo.slotIndex == group.activeSlotIndex);
                        ++buttonIndex;
                        continue;
                    }
                }
            }
            arrow->setBounds({});
            arrow->setVisible(false);
            ++buttonIndex;
        }
    }

}

void ParallelSplitComponent::timerCallback()
{
    for (auto& f : footers)
        f->updateMeters();
}

#endif // PARALLEL_SPLIT_COMPONENT_CPP_INCLUDED
