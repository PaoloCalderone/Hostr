#include "PluginEditor.h"
#include "PluginSearchDialog.h"
#include "ParallelSplitComponent.h"
#include "PresetManager.h"

#include <algorithm>
#include <cmath>

// ==============================================================================
// IMPLEMENTS THE ENTIRE MAIN UI:
// - Master Chain Rendering
// - Preset Bar and Menu
// - Drag & Drop Between Slots
// - Parallel Split Panel
// - Scan Overlay
// ==============================================================================

// ==============================================================================
// CUSTOM LOOK AND FEEL
// ==============================================================================

void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    const auto& skin = hostr::currentSkin();
    const auto inset = juce::jmin(10.0f, (float)juce::jmin(width, height) * 0.18f);
    auto bounds  = juce::Rectangle<float>(x, y, width, height).reduced(inset);
    auto centre  = bounds.getCentre();
    auto radius  = bounds.getWidth() / 2.0f;
    auto visualPos = sliderPos;
    auto toAngle = rotaryStartAngle + visualPos * (rotaryEndAngle - rotaryStartAngle);

    if (skin.dimensionalControls)
    {
        const auto& knobImage = hostr::getSkinAssetImage(skin, "knob.png");
        if (knobImage.isValid())
        {
            auto imageBounds = bounds.expanded(bounds.getWidth() * 0.23f);
            const int knobW = juce::jmax(1, juce::roundToInt(imageBounds.getWidth()));
            const int knobH = juce::jmax(1, juce::roundToInt(imageBounds.getHeight()));
            const auto& scaledKnobImage = hostr::getScaledSkinAssetImage(skin, "knob.png", knobW, knobH);
            if (!scaledKnobImage.isValid())
                return;

            auto shadowBounds = imageBounds.reduced(imageBounds.getWidth() * 0.07f)
                                           .translated(imageBounds.getWidth() * 0.07f,
                                                       imageBounds.getHeight() * 0.09f);
            juce::ColourGradient castShadow(juce::Colours::black.withAlpha(0.48f),
                                            shadowBounds.getCentre(),
                                            juce::Colours::transparentBlack,
                                            shadowBounds.getBottomRight(), true);
            g.setGradientFill(castShadow);
            g.fillEllipse(shadowBounds);

            auto transform = juce::AffineTransform::translation((float)-scaledKnobImage.getWidth() * 0.5f,
                                                                (float)-scaledKnobImage.getHeight() * 0.5f)
                                                .rotated(toAngle)
                                                .translated(centre);

           #if JUCE_WINDOWS
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
           #else
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
           #endif
            g.drawImageTransformed(scaledKnobImage, transform, false);
            return;
        }

        auto outer = bounds.expanded(bounds.getWidth() * 0.055f);
        g.setColour(juce::Colours::black.withAlpha(0.42f));
        g.fillEllipse(outer.translated(outer.getWidth() * 0.05f, outer.getHeight() * 0.08f));
        juce::ColourGradient outerRing(skin.knobRing.brighter(0.22f), outer.getTopLeft(),
                                       skin.knobRing.darker(0.42f), outer.getBottomRight(), false);
        g.setGradientFill(outerRing);
        g.fillEllipse(outer);
        g.setColour(skin.panelInset.darker(0.25f));
        g.drawEllipse(outer.reduced(0.75f), 1.0f);

        juce::ColourGradient body(skin.knobFace.brighter(0.16f), bounds.getTopLeft(),
                                  skin.knobFace.darker(0.42f), bounds.getBottomRight(), false);
        g.setGradientFill(body);
        g.fillEllipse(bounds);
        g.setColour(skin.border.withAlpha(0.90f));
        g.drawEllipse(bounds.reduced(0.45f), 1.1f);

        auto inner = bounds.reduced(bounds.getWidth() * 0.10f);
        g.setColour(skin.text.withAlpha(0.08f));
        g.drawEllipse(inner, 0.9f);
        g.setColour(skin.accent.withAlpha(0.18f));
        g.drawEllipse(bounds.reduced(bounds.getWidth() * 0.04f), 1.0f);

        g.setColour(skin.text.withAlpha(0.05f * skin.wearAmount));
        for (int i = 0; i < 5; ++i)
        {
            const float y = bounds.getY() + bounds.getHeight() * (0.28f + 0.09f * (float)i);
            const float x = bounds.getX() + bounds.getWidth() * (0.23f + 0.07f * (float)(i % 2));
            g.drawLine(x, y, x + bounds.getWidth() * 0.18f, y - bounds.getHeight() * 0.025f, 0.6f);
        }
    }
    else
    {
        g.setColour(skin.panelInset);
        g.fillEllipse(bounds);
        g.setColour(skin.accent.withAlpha(0.34f));
        g.drawEllipse(bounds, 1.5f);
    }

    juce::Path p;
    p.addRectangle(-radius * 0.1f, -radius, radius * 0.2f, radius * 0.8f);
    p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(centre));
    g.setColour(skin.accent);
    g.fillPath(p);

    if (skin.dimensionalControls)
    {
        g.setColour(skin.accentSoft.withAlpha(0.42f));
        g.strokePath(p, juce::PathStrokeType(1.15f));
    }

}

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float,
                                         const juce::Slider::SliderStyle style, juce::Slider&)
{
    const auto& skin = hostr::currentSkin();
    g.setColour(skin.panelInset); g.fillRect(x, y, width, height);
    g.setColour(skin.accent.withAlpha(0.34f));
    g.drawRect(juce::Rectangle<int>(x, y, width, height), 1);
    if (style == juce::Slider::LinearVertical)
    { g.setColour(skin.accent); g.fillRect(x, (int)sliderPos, width, 8); }
}

void CustomLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto& skin = hostr::currentSkin();
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
    hostr::paintPanelFrame(g, bounds, skin, 4.0f, 1.0f);
    hostr::paintTextureOverlay(g, bounds.reduced(1.0f), skin,
                               hostr::hasBitmapSkinSurface(skin) ? 0.10f : 0.18f);
    g.setColour(skin.panelInset.withAlpha(hostr::hasBitmapSkinSurface(skin) ? 0.20f : 0.38f));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 3.5f);
    g.setColour(skin.border.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.5f, 1.0f);
}

void CustomLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu, const juce::String& text,
                                          const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon,
                                          const juce::Colour* textColourToUse)
{
    juce::ignoreUnused(shortcutKeyText, icon);
    const auto& skin = hostr::currentSkin();

    if (isSeparator)
    {
        g.setColour(skin.border.withAlpha(0.45f));
        g.drawHorizontalLine(area.getCentreY(), (float)area.getX() + 6.0f, (float)area.getRight() - 6.0f);
        return;
    }

    auto row = area.reduced(3, 1);
    if (isHighlighted && isActive)
    {
        g.setColour(skin.accent.withAlpha(hostr::hasBitmapSkinSurface(skin) ? 0.30f : 0.22f));
        g.fillRoundedRectangle(row.toFloat(), 3.0f);
        g.setColour(skin.accent.withAlpha(0.58f));
        g.drawRoundedRectangle(row.toFloat().reduced(0.5f), 3.0f, 1.0f);
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse : skin.text;
    if (!isActive)
        textColour = skin.mutedText.withAlpha(0.45f);

    const int markW = 18;
    if (isTicked)
    {
        g.setColour(skin.accent);
        auto tickArea = row.removeFromLeft(markW).toFloat().reduced(4.0f, 5.0f);
        juce::Path tick;
        tick.startNewSubPath(tickArea.getX(), tickArea.getCentreY());
        tick.lineTo(tickArea.getCentreX() - 1.0f, tickArea.getBottom());
        tick.lineTo(tickArea.getRight(), tickArea.getY());
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        row.removeFromLeft(markW);
    }

    if (hasSubMenu)
    {
        g.setColour(textColour.withAlpha(0.75f));
        g.drawText(">", row.removeFromRight(16), juce::Justification::centred);
    }

    g.setColour(textColour.withAlpha(isActive ? 0.92f : 0.45f));
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 13.0f, (float)area.getHeight() * 0.50f)).withStyle("Bold")));
    g.drawFittedText(text.toUpperCase(), row.reduced(2, 0), juce::Justification::centredLeft, 1, 0.82f);
}

void CustomLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
                                                   const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName)
{
    const auto& skin = hostr::currentSkin();
    g.setColour(skin.mutedText.withAlpha(0.62f));
    g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
    g.drawFittedText(sectionName.toUpperCase(), area.reduced(8, 0),
                     juce::Justification::centredLeft, 1, 0.82f);
}

// ==============================================================================
// METERS
// ==============================================================================

static constexpr float kGainMinDb = -100.0f;
static constexpr float kGainMaxDb =   24.0f;
static constexpr float kMinusInfThresholdDb = -99.9f;

static int commonGuiKnobSize(float zoom)
{
    return juce::jmax(20, juce::roundToInt(45.0f * zoom));
}

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


static juce::String cleanMacroParameterName(juce::String name)
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

static float masterMeterNormForDb(float db)
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

void MeterComponent::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto bounds = getLocalBounds().toFloat();
    g.setColour(skin.panelInset);
    g.fillRoundedRectangle(bounds, 2.0f);

    const float clampedLevel = juce::jlimit(-100.0f, 12.0f, level);
    const float norm = masterMeterNormForDb(clampedLevel);
    auto  bar  = bounds.withTop(bounds.getBottom() - bounds.getHeight() * norm);
    juce::ColourGradient grad(overZero ? skin.meterPeak : skin.accentAlt,
                              bounds.getBottomLeft(),
                              overZero ? skin.danger : skin.accentSoft,
                              bounds.getTopLeft(), false);
    g.setGradientFill(grad); g.fillRoundedRectangle(bar, 2.0f);
}

// ==============================================================================
// SCAN OVERLAY
// ==============================================================================

ScanOverlay::ScanOverlay(PluginProcessor& p) : processor(p)
{ setInterceptsMouseClicks(false, false); }

void ScanOverlay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xcc000000));
    g.fillAll();

    float progress = processor.getScanProgress();
    juce::String currentName = processor.getScanCurrentName();

    auto box = b.withSizeKeepingCentre(320.0f, 80.0f);
    g.setColour(juce::Colour(0xff1e1e1e));
    g.fillRoundedRectangle(box, 8.0f);
    g.setColour(juce::Colour(0xff444444));
    g.drawRoundedRectangle(box, 8.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    g.drawText("Scanning plugins...", box.withHeight(28.0f), juce::Justification::centred);

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    juce::String shortName = currentName.length() > 40
                             ? "..." + currentName.fromLastOccurrenceOf("/", false, false)
                                                  .fromLastOccurrenceOf("\\", false, false)
                             : currentName;
    g.drawText(shortName, box.withTrimmedTop(26.0f).withHeight(20.0f), juce::Justification::centred, true);

    auto barBg = box.withTrimmedTop(52.0f).withHeight(10.0f).reduced(12.0f, 0.0f);
    g.setColour(juce::Colour(0xff333333));
    g.fillRoundedRectangle(barBg, 4.0f);
    auto barFg = barBg.withWidth(barBg.getWidth() * progress);
    g.setColour(juce::Colour(0xff007aff));
    g.fillRoundedRectangle(barFg, 4.0f);

    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(juce::String((int)(progress * 100)) + "%",
               box.withTrimmedTop(64.0f).withHeight(14.0f), juce::Justification::centred);
}

void ScanOverlay::timerCallback()
{
    if (!processor.isScanning())
    {
        stopTimer();
        setAlwaysOnTop(false);
        setVisible(false);

        int newFound = processor.getNewPluginsFoundCount();
        int total    = processor.getKnownPluginCount();
        bool scanHadErrors = processor.didLastScanFinishWithError();

        juce::String msg;
        if (scanHadErrors)
        {
            msg = processor.getLastScanErrorMessage();

            if (total > 0)
                msg << "\n\nCurrently available in library: " << juce::String(total) << " plugin(s).";
        }
        else if (total == 0)
            msg = "No plugins found on this system.";
        else if (newFound == 0)
            msg = "Scan complete. No new plugins found.\n\n"
                  + juce::String(total) + " plugin(s) already in library.";
        else
            msg = "Scan complete!\n\n"
                  + juce::String(newFound) + " new plugin(s) added.\n"
                  + "Total library: " + juce::String(total) + " plugin(s).";

        juce::AlertWindow::showMessageBoxAsync(
            scanHadErrors ? juce::AlertWindow::WarningIcon
                          : (newFound > 0 ? juce::AlertWindow::InfoIcon
                                          : juce::AlertWindow::NoIcon),
            scanHadErrors ? "Plugin Scan Incomplete" : "Plugin Scan Complete",
            msg);
        return;
    }
    repaint();
}

// Bring the overlay to front and ensure it's always above other UI elements
void ScanOverlay::startMonitoring()
{
    setAlwaysOnTop(true);
    setVisible(true);
    toFront(true);
    startTimerHz(10);
}

MasterMeterOverlay::MasterMeterOverlay(PluginProcessor& p)
    : processor(p)
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
    startTimer(33);
}

MasterMeterOverlay::~MasterMeterOverlay()
{
    stopTimer();
    cancelPendingUpdate();
}

void MasterMeterOverlay::setGeometry(float zoom, int masterCentreX, int masterWidth, int outputKnobSize)
{
    zoomLevel = zoom;
    centreX = masterCentreX;
    masterW = masterWidth;
    knobSize = outputKnobSize;
    repaint();
}

void MasterMeterOverlay::updateMeters()
{
    const float newInputL  = processor.getInputLevel(0);
    const float newInputR  = processor.getInputLevel(1);
    const float newOutputL = processor.getOutputLevel(0);
    const float newOutputR = processor.getOutputLevel(1);

    const auto changedEnough = [](float previous, float current)
    {
        return std::abs(previous - current) >= 0.2f;
    };

    const bool changed = changedEnough(inputMeterL.load(std::memory_order_relaxed), newInputL)
                      || changedEnough(inputMeterR.load(std::memory_order_relaxed), newInputR)
                      || changedEnough(outputMeterL.load(std::memory_order_relaxed), newOutputL)
                      || changedEnough(outputMeterR.load(std::memory_order_relaxed), newOutputR);

    inputMeterL.store(newInputL, std::memory_order_relaxed);
    inputMeterR.store(newInputR, std::memory_order_relaxed);
    outputMeterL.store(newOutputL, std::memory_order_relaxed);
    outputMeterR.store(newOutputR, std::memory_order_relaxed);

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    auto updatePeak = [nowMs](float candidate, std::atomic<float>& peak, double& holdUntil)
    {
        const float currentPeak = peak.load(std::memory_order_relaxed);
        if (candidate >= currentPeak || nowMs >= holdUntil)
        {
            peak.store(candidate, std::memory_order_relaxed);
            holdUntil = nowMs + 300.0;
            return true;
        }

        return false;
    };

    auto updateClipHold = [nowMs](float candidate, std::atomic<bool>& held, double& holdUntil)
    {
        if (candidate > 0.3f)
            holdUntil = nowMs + 300.0;

        const bool isHeld = nowMs < holdUntil;
        const bool previous = held.exchange(isHeld, std::memory_order_relaxed);
        return previous != isHeld;
    };

    const auto inputPairPeak = juce::jmax(newInputL, newInputR);
    const auto outputPairPeak = juce::jmax(newOutputL, newOutputR);
    const bool peakChanged = updatePeak(inputPairPeak, inputPeakDb, inputPeakHoldUntilMs)
                          || updatePeak(outputPairPeak, outputPeakDb, outputPeakHoldUntilMs);
    const bool clipChanged = updateClipHold(inputPairPeak, inputClipHeld, inputClipHoldUntilMs)
                          || updateClipHold(outputPairPeak, outputClipHeld, outputClipHoldUntilMs);

    if (! changed && ! peakChanged && ! clipChanged)
        return;

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
    {
        if (mm->isThisTheMessageThread())
        {
            repaint();
            return;
        }
    }

    triggerAsyncUpdate();
}

void MasterMeterOverlay::hiResTimerCallback()
{
    updateMeters();
}

void MasterMeterOverlay::handleAsyncUpdate()
{
    repaint();
}

void MasterMeterOverlay::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    const int W = getWidth();
    const int H = getHeight();
    if (W <= 0 || H <= 0 || masterW <= 0 || knobSize <= 0)
        return;

    const float knobSizeF = (float)knobSize;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * zoomLevel);
    const float knobY = juce::jmax(8.0f * zoomLevel, labelReferenceH * 0.16f + 1.0f * zoomLevel);
    const float knobMidY = knobY + knobSizeF * 0.5f;

    auto drawDotPair = [&](float leftDb, float rightDb, float pairCentreX, float peakDb, bool clipHeld)
    {
        const int dots = 11;
        static constexpr float thresholds[dots] = { -48.0f, -36.0f, -24.0f, -18.0f, -9.0f, -6.0f,
                                                     -3.0f, -1.0f, -0.3f, 0.0f, 0.3f };
        const float dot = juce::jlimit(2.8f, 5.4f, (float)H * 0.046f);
        const float gap = juce::jmax(1.0f, dot * 0.45f);
        const float colGap = dot * 0.90f;
        const float labelFont = juce::jmax(7.0f, labelReferenceH * 0.105f);
        const float peakMargin = juce::jmax(7.0f * zoomLevel, dot * 1.7f);
        const float totalH = dots * dot + (dots - 1) * gap;
        const float top = juce::jmax(labelFont + peakMargin, knobMidY - totalH * 0.5f);
        const float peakY = top - labelFont - peakMargin;
        const float pairW = dot * 2.0f + colGap;
        const auto peakBounds = juce::Rectangle<float>(pairCentreX - pairW * 1.1f, peakY,
                                                       pairW * 2.2f, labelFont + 3.0f).toNearestInt();

        g.setFont(juce::Font(juce::FontOptions(labelFont)));
        g.setColour(skin.text.withAlpha(0.76f));
        g.drawFittedText(formatMeterPeakDb(peakDb), peakBounds,
                         juce::Justification::centred, 1, 0.82f);

        const struct { int dotIndex; const char* label; } labels[] { { 9, "0" },
                                                                     { 6, "-3" },
                                                                     { 4, "-9" } };
        const bool labelsOnRight = pairCentreX < (float)centreX;
        const float dbLabelFont = juce::jmax(7.0f, labelReferenceH * 0.105f);
        const float dbLabelW = dot * 7.0f;
        const float dbLabelH = juce::jmax(dot * 1.75f, dbLabelFont + 2.0f);
        const float dbLabelX = labelsOnRight ? pairCentreX + pairW * 0.5f + dot * 0.75f
                                             : pairCentreX - pairW * 0.5f - dbLabelW - dot * 0.75f;
        g.setFont(juce::Font(juce::FontOptions(dbLabelFont)));
        g.setColour(skin.mutedText.withAlpha(0.58f));
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
                g.setColour(colour.withAlpha(lit ? 0.95f : 0.16f));
                g.fillEllipse(x, y, dot, dot);
            }
        };

        drawColumn(leftDb, pairCentreX - colGap * 0.5f - dot);
        drawColumn(rightDb, pairCentreX + colGap * 0.5f);
    };

    const float masterMeterOffset = knobSizeF * 0.5f + (float)masterW * 0.24f;
    drawDotPair(inputMeterL.load(std::memory_order_relaxed),
                inputMeterR.load(std::memory_order_relaxed),
                (float)centreX - masterMeterOffset,
                inputPeakDb.load(std::memory_order_relaxed),
                inputClipHeld.load(std::memory_order_relaxed));
    drawDotPair(outputMeterL.load(std::memory_order_relaxed),
                outputMeterR.load(std::memory_order_relaxed),
                (float)centreX + masterMeterOffset,
                outputPeakDb.load(std::memory_order_relaxed),
                outputClipHeld.load(std::memory_order_relaxed));
}

// ==============================================================================
// DRAG GHOST
// ==============================================================================

DragGhost::DragGhost()
{
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
    setVisible(false);
}

void DragGhost::start(const juce::String& name, bool isParallel, bool isBypassed,
                      int slotW, int slotH, juce::Point<int> screenPos)
{
    ghostName       = name;
    ghostIsParallel = isParallel;
    ghostIsBypassed = isBypassed;
    ghostW = slotW;
    ghostH = slotH;
    setVisible(true);
    toFront(false);
    moveTo(screenPos);
}

void DragGhost::moveTo(juce::Point<int> screenPos)
{
    if (auto* parent = getParentComponent())
    {
        auto local = parent->getLocalPoint(nullptr, screenPos);
        setBounds(local.x - ghostW/2, local.y - ghostH/2, ghostW, ghostH);
    }
}

void DragGhost::stop()
{
    setVisible(false);
}

void DragGhost::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(b.translated(2.0f, 3.0f), 20.0f);

    g.setColour(skin.panelInset);
    g.fillRoundedRectangle(b, 20.0f);

    g.setColour(skin.border.withAlpha(0.82f));
    g.drawRoundedRectangle(b, 20.0f, 2.0f);

    g.setColour(skin.panelRaised);
    g.fillRoundedRectangle(b.reduced(2), 18.0f);

    const float dotX = 15.0f, dotSz = 10.0f;
    float dotY = getHeight() / 2.0f - dotSz / 2.0f;
    g.setColour(ghostIsBypassed ? skin.separator : skin.accent);
    g.fillEllipse(dotX, dotY, dotSz, dotSz);

    g.setColour(ghostIsBypassed ? skin.text.withAlpha(0.4f) : skin.text);
    g.setFont(ghostIsParallel ? juce::Font(juce::FontOptions(14.0f).withStyle("Bold")) : juce::Font(juce::FontOptions(16.0f)));
    g.drawText(ghostName, (int)(dotX + dotSz + 8), 0,
               getWidth() - (int)(dotX + dotSz + 8) - 10, getHeight(),
               juce::Justification::centredLeft, true);

    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.fillRoundedRectangle(b, 20.0f);
}

// ==============================================================================
// RESIZE CORNER (zoom drag from bottom-right)
// ==============================================================================

ResizeCorner::ResizeCorner(PluginEditor& p, bool attachToLeft) : editor(p)
{
    juce::ignoreUnused(attachToLeft);
    setInterceptsMouseClicks(true, true);
    setAlwaysOnTop(true);
    setMouseCursor(juce::MouseCursor::BottomRightCornerResizeCursor);
}

void ResizeCorner::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto b = getLocalBounds().toFloat().reduced(2.0f);

    juce::Path triangle;
    triangle.startNewSubPath(b.getRight(), b.getY());
    triangle.lineTo(b.getRight(), b.getBottom());
    triangle.lineTo(b.getX(), b.getBottom());
    triangle.closeSubPath();

    g.setColour(skin.border.withAlpha(0.42f));
    g.fillPath(triangle);
    g.setColour(skin.mutedText.withAlpha(0.58f));
    g.strokePath(triangle, juce::PathStrokeType(1.0f));
}

void ResizeCorner::mouseDown(const juce::MouseEvent& e)
{
    isDragging = true;
    dragStartMouseOnScreen = e.getScreenPosition();
    dragStartWidth = juce::jmax(1, editor.getWidth());
    dragStartHeight = juce::jmax(1, editor.getHeight());
    const auto currentZoom = juce::jmax(0.001f, editor.getZoomLevel());
    dragBaseWidth = (float)dragStartWidth / currentZoom;
    dragBaseHeight = (float)dragStartHeight / currentZoom;
}

void ResizeCorner::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDragging) return;

    const auto delta = e.getScreenPosition() - dragStartMouseOnScreen;
    const auto desiredWidth = juce::jmax(1, dragStartWidth + delta.x);
    const auto desiredHeight = juce::jmax(1, dragStartHeight + delta.y);
    const float zoomFromWidth = (float)desiredWidth / juce::jmax(1.0f, dragBaseWidth);
    const float zoomFromHeight = (float)desiredHeight / juce::jmax(1.0f, dragBaseHeight);
    editor.setZoomLevelRaw(juce::jmin(zoomFromWidth, zoomFromHeight));
}

void ResizeCorner::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    isDragging = false;
}

// ==============================================================================
// PLUGIN SLOT COMPONENT
// ==============================================================================

static constexpr float DOT_X_RATIO  = 15.0f / 35.0f;   // dot x as fraction of slot height
static constexpr float DOT_SZ_RATIO = 10.0f / 35.0f;   // dot size as fraction of slot height
static constexpr float REMOVE_SZ_RATIO = 16.0f / 35.0f;

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

juce::Rectangle<float> PluginSlotComponent::getBypassDotBounds() const
{
    const float h   = (float)getHeight();
    const float baseSz = h * DOT_SZ_RATIO;
    const float sz  = baseSz * 0.875f;
    const float x   = h * DOT_X_RATIO + baseSz * 0.5f - sz * 0.5f;
    return { x, h / 2.0f - sz / 2.0f, sz, sz };
}

juce::Rectangle<int> PluginSlotComponent::getRemoveXBounds() const
{
    const float h  = (float)getHeight();
    const int maxSize = juce::jmax(1, juce::roundToInt(h - 4.0f));
    const int size = juce::jmin(maxSize,
                                juce::jmax(juce::roundToInt(h * REMOVE_SZ_RATIO),
                                           juce::roundToInt(h * DOT_SZ_RATIO)));
    const int x    = getWidth() - juce::roundToInt(h * DOT_X_RATIO) - size;
    const int y    = juce::roundToInt(h * 0.5f - (float)size * 0.5f);
    return { x, y, size, size };
}

PluginSlotComponent::PluginSlotComponent(PluginProcessor& p, int index)
    : processor(p), slotIndex(index)
{
}

void PluginSlotComponent::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    auto bounds     = getLocalBounds().toFloat();
    const bool bitmapSkin = hostr::hasBitmapSkinSurface(skin);
    bool isOccupied = processor.pluginSlots[slotIndex].isValid;
    bool isParallel = processor.pluginSlots[slotIndex].type == PluginProcessor::SlotType::ParallelSplit;
    bool isBypassed = processor.pluginSlots[slotIndex].bypassed;
    const float cornerSize = bounds.getHeight() * 0.57f;

    if (bitmapSkin)
        hostr::paintSkinSurface(g, bounds, skin, cornerSize);
    else
    {
        g.setColour(skin.panelInset);
        g.fillRoundedRectangle(bounds, cornerSize);
    }

    if (isDropTarget)
        g.setColour(skin.accent.withAlpha(0.9f));
    else if (isDragging)
        g.setColour(skin.text.withAlpha(0.08f));
    else
        g.setColour(skin.border.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds, cornerSize, 2.0f);

    if (!isOccupied)
    {
        g.setColour(skin.mutedText);
        if (!isDragging)
        {
            g.setFont(bounds.getHeight() * 0.69f);
            g.drawText("+", getLocalBounds(), juce::Justification::centred, false);
        }
        return;
    }

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

    bool hasSurface = false;
    if (bitmapSkin)
        hasSurface = hostr::paintSkinSurface(g, bounds.reduced(2.0f), skin, cornerSize - 1.0f);
    else
    {
        g.setColour(skin.panelRaised);
        g.fillRoundedRectangle(bounds.reduced(2), cornerSize - 1.0f);
    }
    hostr::paintTextureOverlay(g, bounds.reduced(3.0f), skin, hasSurface ? 0.16f : 0.45f);

    auto dot = getBypassDotBounds();
    {
        hostr::paintBypassLed(g, dot.expanded(dot.getWidth() * 0.08f),
                              skin,
                              !isBypassed,   // onState: true when NOT bypassed (active)
                              false);
    }

    const float h = (float)getHeight();
    g.setColour(isBypassed ? skin.text.withAlpha(0.4f) : skin.text);
    g.setFont(isParallel ? juce::Font(juce::FontOptions(h * 0.40f).withStyle("Bold")) : juce::Font(juce::FontOptions(h * 0.46f)));

    const int dotRight = (int)(dot.getRight() + h * 0.23f);
    const int xRight   = showRemove ? (int)(getRemoveXBounds().getX()) : (getWidth() - (int)(h * 0.11f));
    g.drawText(processor.pluginSlots[slotIndex].name,
               dotRight, 0, xRight - dotRight, getHeight(),
               juce::Justification::centredLeft, true);

    if (showRemove)
    {
        g.setColour(skin.text.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(h * 0.38f).withStyle("Bold")));
        g.drawText("x", getRemoveXBounds(), juce::Justification::centred, false);
    }
}

void PluginSlotComponent::resized() {}

void PluginSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    isDragging = false;
    dragStartSlotIndex = -1;

    bool isOccupied = processor.pluginSlots[slotIndex].isValid;
    bool isParallel = processor.pluginSlots[slotIndex].type == PluginProcessor::SlotType::ParallelSplit;

    if (isOccupied && showRemove &&
        getRemoveXBounds().contains(e.getPosition()) &&
        !e.mods.isRightButtonDown())
    {
        if (isParallel)
            if (auto* editor = findParentComponentOfClass<PluginEditor>())
                editor->prepareForParallelSplitRemoval();
        processor.removePlugin(slotIndex);
        updateState();
        if (auto* editor = findParentComponentOfClass<PluginEditor>())
            editor->refreshParallelLayoutNow();
        return;
    }

    if (!isOccupied)
    {
        if (!e.mods.isRightButtonDown())
            showPluginMenu();
        return;
    }

    auto dotHit = getBypassDotBounds().expanded(6.0f);
    if (dotHit.contains(e.position) && !e.mods.isRightButtonDown())
    {
        processor.setSlotBypassed(slotIndex, !processor.isSlotBypassed(slotIndex));
        if (isParallel)
            if (auto* editor = findParentComponentOfClass<PluginEditor>())
                editor->scheduleLayoutUpdate();
        repaint();
        return;
    }

    if (e.mods.isRightButtonDown())
    {
        showPluginMenu();
        return;
    }

    if (!isParallel && e.getNumberOfClicks() == 2)
    {
        processor.showPluginGUI(slotIndex);
        return;
    }

    dragStartSlotIndex = slotIndex;
}

void PluginSlotComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStartSlotIndex < 0) return;
    if (!processor.pluginSlots[dragStartSlotIndex].isValid) return;

    auto* editor = findParentComponentOfClass<PluginEditor>();
    if (!editor) return;

    if (!isDragging)
    {
        if (e.getDistanceFromDragStart() > 4)
        {
            isDragging = true;
            repaint();

            auto& slot = processor.pluginSlots[dragStartSlotIndex];
            PluginEditor::DragPayload payload;
            payload.source = PluginEditor::DragSource::MasterSlot;
            payload.masterSlot = dragStartSlotIndex;
            editor->beginParallelDrag(payload,
                slot.name,
                slot.bypassed,
                slot.type == PluginProcessor::SlotType::ParallelSplit,
                e.getScreenPosition());
        }
        return;
    }

    editor->updateParallelDrag(e.getScreenPosition());
}

void PluginSlotComponent::mouseUp(const juce::MouseEvent& e)
{
    auto* editor = findParentComponentOfClass<PluginEditor>();

    if (isDragging)
    {
        isDragging = false;
        repaint();

        PluginEditor::DragPayload payload;
        payload.source = PluginEditor::DragSource::MasterSlot;
        payload.masterSlot = dragStartSlotIndex;

        if (editor != nullptr)
            editor->completeCrossDrag(payload, e.getScreenPosition());

        dragStartSlotIndex = -1;
        return;
    }

    if (dragStartSlotIndex >= 0 &&
        processor.pluginSlots[dragStartSlotIndex].isValid &&
        !e.mods.isRightButtonDown())
    {
        if (processor.pluginSlots[dragStartSlotIndex].type == PluginProcessor::SlotType::Plugin)
            processor.showPluginGUI(dragStartSlotIndex);
        else if (processor.pluginSlots[dragStartSlotIndex].type == PluginProcessor::SlotType::ParallelSplit)
            if (editor != nullptr)
                editor->toggleParallelPanelVisibility(dragStartSlotIndex);
    }

    if (editor) { editor->getDragGhost()->stop(); editor->setDropTarget(-1); }
    dragStartSlotIndex = -1;
}

void PluginSlotComponent::showPluginMenu()
{
    juce::PopupMenu menu;
    const auto& skin = hostr::currentSkin();
    menu.addItem(juce::PopupMenu::Item("Load").setEnabled(false));
    int existingSplits = 0;
    for (const auto& slot : processor.pluginSlots)
        if (slot.isValid
            && slot.type == PluginProcessor::SlotType::ParallelSplit)
            ++existingSplits;
    menu.addItem(999, "Parallel Split", existingSplits < ParallelSplitProcessor::maxChains);
    menu.addItem(juce::PopupMenu::Item("Search Plugin").setID(1).setImage(createSearchMenuIcon(skin.text.withAlpha(0.85f))));

    if (processor.pluginSlots[slotIndex].isValid
        && processor.pluginSlots[slotIndex].type == PluginProcessor::SlotType::Plugin)
    {
        auto parameterNames = processor.getMappableParametersForMasterSlot(slotIndex);
        if (!parameterNames.isEmpty())
        {
            menu.addSeparator();
            juce::PopupMenu macroMenu;
            for (int macroIndex = 0; macroIndex < PluginProcessor::macroControlCount; ++macroIndex)
            {
                juce::PopupMenu paramMenu;
                const int count = juce::jmin(parameterNames.size(), 128);
                for (int paramIndex = 0; paramIndex < count; ++paramIndex)
                    paramMenu.addItem(20000 + macroIndex * 1000 + paramIndex,
                                      parameterNames[paramIndex]);

                macroMenu.addSubMenu(processor.getMacroName(macroIndex), paramMenu);
            }
            menu.addSubMenu("Map Parameter to Macro", macroMenu);
        }
    }

    juce::Component::SafePointer<PluginSlotComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int r)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        if (r == 999)
        {
            self->processor.createParallelSplit(self->slotIndex);
            self->updateState();
            if (auto* editor = self->findParentComponentOfClass<PluginEditor>())
                editor->refreshParallelLayoutNow();
        }
        else if (r == 1)
        {
            auto allPlugins = self->processor.getLoadablePluginDescriptions();

            PluginSearchDialog::show(self, allPlugins,
                [safeThis](const juce::PluginDescription& desc)
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr)
                        return;

                    self->processor.loadPlugin(desc, self->slotIndex);
                    juce::MessageManager::callAsync([safeThis]()
                    {
                        if (safeThis != nullptr)
                            safeThis->updateState();
                    });
                });
        }
        else if (r >= 20000)
        {
            const int encoded = r - 20000;
            const int macroIndex = encoded / 1000;
            const int parameterIndex = encoded % 1000;
            if (self->processor.mapMacroToMasterSlotParameter(macroIndex, self->slotIndex, parameterIndex))
            {
                const auto& slot = self->processor.pluginSlots[(size_t)self->slotIndex];
                if (slot.node != nullptr)
                    if (auto* plugin = slot.node->getProcessor())
                    {
                        const auto& params = plugin->getParameters();
                        if (parameterIndex >= 0 && parameterIndex < params.size() && params[parameterIndex] != nullptr)
                        {
                            const float value = params[parameterIndex]->getValue();
                            self->processor.setMacroValue(macroIndex, value);
                            if (auto* editor = self->findParentComponentOfClass<PluginEditor>())
                            {
                                editor->refreshAllSlots();
                                editor->repaint();
                            }
                        }
                    }
            }
        }
    });
}

bool PluginSlotComponent::isInterestedInFileDrag(const juce::StringArray&) { return true; }
void PluginSlotComponent::filesDropped(const juce::StringArray&, int, int) {}

void PluginSlotComponent::updateState()
{
    showRemove = processor.pluginSlots[slotIndex].isValid;
    repaint();
    if (auto* editor = findParentComponentOfClass<PluginEditor>())
        editor->scheduleLayoutUpdate();
}

// ==============================================================================
// MULTI PARALLEL PANEL
// ==============================================================================

MultiParallelPanel::MultiParallelPanel()
{
    setInterceptsMouseClicks(true, true);
    addAndMakeVisible(horizontalScrollBar);
    horizontalScrollBar.addListener(this);
    horizontalScrollBar.setVisible(false);
    startTimerHz(20);
}

MultiParallelPanel::~MultiParallelPanel()
{
    stopTimer();
    horizontalScrollBar.removeListener(this);
}

void MultiParallelPanel::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved == &horizontalScrollBar)
        setHorizontalScroll(juce::roundToInt(newRangeStart));
}

ParallelSplitComponent* MultiParallelPanel::getActivePanel() const
{
    for (auto* entry : panels)
        if (entry != nullptr
            && entry->masterSlotIndex == activeSlotIndex
            && entry->panel != nullptr)
            return entry->panel.get();

    return nullptr;
}

int MultiParallelPanel::getVisibleChainColumns() const
{
    if (auto* panel = getActivePanel())
        return juce::jlimit(1, 4, panel->getNumDisplayedChains());

    return 1;
}

int MultiParallelPanel::getActiveContentWidth() const
{
    if (auto* panel = getActivePanel())
    {
        const int totalColumns = juce::jmax(1, panel->getNumDisplayedChains());
        return panel->getContentWidthForColumns(totalColumns);
    }

    return getWidth();
}

void MultiParallelPanel::setHorizontalScroll(int newOffset)
{
    const int maxScroll = getMaxHorizontalScroll();
    const int clamped = juce::jlimit(0, maxScroll, newOffset);
    if (horizontalScroll == clamped)
        return;

    horizontalScroll = clamped;
    resized();
    if (auto* editor = findParentComponentOfClass<PluginEditor>())
        editor->scheduleLayoutUpdate();
}

int MultiParallelPanel::getMaxHorizontalScroll() const
{
    if (auto* panel = getActivePanel())
    {
        if (panel->getNumDisplayedChains() <= getVisibleChainColumns())
            return 0;
    }

    return juce::jmax(0, getActiveContentWidth() - getWidth());
}

int MultiParallelPanel::getScrollBarThickness() const
{
    return juce::jmax(3, juce::roundToInt(6.0f * scrollBarZoom));
}

int MultiParallelPanel::getScrollBarHotZoneHeight() const
{
    return juce::jmax(getScrollBarThickness() * 3, juce::roundToInt(18.0f * scrollBarZoom));
}

bool MultiParallelPanel::shouldShowHorizontalScrollBar() const
{
    return getMaxHorizontalScroll() > 0 && (scrollBarHot || horizontalScrollBar.isMouseButtonDown());
}

void MultiParallelPanel::updateScrollBarHotState(juce::Point<int> mousePos)
{
    const bool hot = getMaxHorizontalScroll() > 0
                  && getLocalBounds().contains(mousePos)
                  && mousePos.y >= getHeight() - getScrollBarHotZoneHeight();

    if (scrollBarHot != hot)
    {
        scrollBarHot = hot;
        updateHorizontalScrollBar();
    }
}

void MultiParallelPanel::timerCallback()
{
    if (!isMouseOver(true) && !horizontalScrollBar.isMouseButtonDown())
    {
        if (scrollBarHot)
        {
            scrollBarHot = false;
            updateHorizontalScrollBar();
        }
        return;
    }

    updateScrollBarHotState(getMouseXYRelative());
}

void MultiParallelPanel::updateHorizontalScrollBar()
{
    const int contentW = getActiveContentWidth();
    const int maxScroll = getMaxHorizontalScroll();
    horizontalScroll = juce::jlimit(0, maxScroll, horizontalScroll);

    horizontalScrollBar.setVisible(shouldShowHorizontalScrollBar());
    horizontalScrollBar.setRangeLimits(0.0, (double)contentW);
    horizontalScrollBar.setCurrentRange((double)horizontalScroll, (double)getWidth(), juce::dontSendNotification);
    horizontalScrollBar.setSingleStepSize(24.0);
}

void MultiParallelPanel::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    hostr::paintPanelFrame(g, b, hostr::currentSkin(), 8.0f, 1.0f);
}

void MultiParallelPanel::paintOverChildren(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void MultiParallelPanel::resized()
{
    int cY = 0;
    // The scrollbar is an overlay: it doesn't subtract height from the content,
    // so the gain knob, output knob, and label don't move when it appears.
    updateHorizontalScrollBar();
    const int scrollBarH = getScrollBarThickness();
    int cH = juce::jmax(10, getHeight() - cY);
    const int contentW = getActiveContentWidth();
    for (auto* e : panels)
    {
        bool vis = (e->masterSlotIndex == activeSlotIndex);
        e->panel->setTopInset(contentTopOffset);
        e->panel->setVisible(vis);
        if (vis) e->panel->setBounds(-horizontalScroll, cY,
                                     juce::jmax(getWidth(), contentW), cH);
    }

    horizontalScrollBar.setBounds(0, getHeight() - scrollBarH, getWidth(), scrollBarH);
    horizontalScrollBar.toFront(false);
}

void MultiParallelPanel::mouseDown(const juce::MouseEvent& e)
{
    updateScrollBarHotState(e.getPosition());
}

void MultiParallelPanel::mouseMove(const juce::MouseEvent& e)
{
    updateScrollBarHotState(e.getPosition());
}

void MultiParallelPanel::mouseExit(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    if (!horizontalScrollBar.isMouseOverOrDragging())
    {
        scrollBarHot = false;
        updateHorizontalScrollBar();
    }
}

int MultiParallelPanel::rebuild(PluginProcessor& proc)
{
    juce::Array<int> active;
    for (int i = 0; i < 8; ++i)
        if (proc.pluginSlots[i].isValid &&
            proc.pluginSlots[i].type == PluginProcessor::SlotType::ParallelSplit &&
            proc.pluginSlots[i].parallelProcessor != nullptr)
            active.add(i);

    if (active.isEmpty()) { panels.clear(); activeSlotIndex = -1; return -1; }

    for (int i = panels.size()-1; i >= 0; --i)
        if (!active.contains(panels[i]->masterSlotIndex)) panels.remove(i);

    for (int si : active)
    {
        bool found = false;
        for (auto* en : panels) if (en->masterSlotIndex == si) { found = true; break; }
        if (!found)
        {
            auto* en = new Entry();
            en->masterSlotIndex = si;
            en->panel = std::make_unique<ParallelSplitComponent>(
                proc.pluginSlots[si].parallelProcessor, &proc);
            addAndMakeVisible(*en->panel);
            panels.add(en);
        }
    }

    for (auto* en : panels)
        if (en != nullptr && en->panel != nullptr)
            en->panel->setRootDisabledByAncestor(proc.pluginSlots[en->masterSlotIndex].bypassed);

    struct Cmp { static int compareElements(const Entry* a, const Entry* b)
                 { return a->masterSlotIndex - b->masterSlotIndex; } } cmp;
    panels.sort(cmp, false);

    bool ok = false;
    for (auto* en : panels) if (en->masterSlotIndex == activeSlotIndex) { ok = true; break; }
    if (!ok) activeSlotIndex = panels[0]->masterSlotIndex;

    resized();
    return activeSlotIndex;
}

void MultiParallelPanel::setActiveSlotIndex(int idx)
{ activeSlotIndex = idx; horizontalScroll = 0; resized(); repaint(); }

int MultiParallelPanel::getActiveDisplayedChainCount() const
{
    for (auto* entry : panels)
        if (entry != nullptr
            && entry->masterSlotIndex == activeSlotIndex
            && entry->panel != nullptr)
            return entry->panel->getNumDisplayedChains();

    return 2;
}

// ==============================================================================
// PRESET BAR COMPONENT
// ==============================================================================

PresetBarComponent::PresetBarComponent(PluginProcessor& p) : processor(p)
{
    auto setupButton = [this](PaintlessTextButton& button, const juce::String& text, const juce::String& tooltip)
    {
        button.setButtonText(text);
        button.setTooltip(tooltip);
        button.setWantsKeyboardFocus(false);
        addAndMakeVisible(button);
    };

    setupButton(optionsBtn, {}, "Options");
    optionsBtn.onClick = [this] { if (onOptionsMenu) onOptionsMenu(); };

    setupButton(abToggleButton, "A", "Toggle A/B");
    abToggleButton.onClick = [this] { toggleABSlot(); };

    setupButton(undoBtn, "UNDO", "Undo");
    undoBtn.onClick = [this] { doUndo(); };

    setupButton(redoBtn, "REDO", "Redo");
    redoBtn.onClick = [this] { doRedo(); };

    setupButton(presetMenuBtn, "PRESETS", "Preset menu");
    presetMenuBtn.onClick = [this] { showPresetMenu(); };

    setupButton(prevPresetBtn, "<", "Previous preset");
    prevPresetBtn.onClick = [this] { loadPreviousPreset(); };

    setupButton(nextPresetBtn, ">", "Next preset");
    nextPresetBtn.onClick = [this] { loadNextPreset(); };

    setupButton(loadBtn, "LOAD", "Load preset");
    loadBtn.onClick = [this] { doLoad(); };

    setupButton(saveBtn, "SAVE", "Save preset");
    saveBtn.onClick = [this] { doSave(); };

    setupButton(saveAsBtn, "SAVE AS", "Save preset as");
    saveAsBtn.onClick = [this] { doSaveAs(); };

    refreshSkinColours();
    currentSnapshot = captureSnapshot();
    abSnapshotA = processor.abSnapshotA.isNotEmpty() ? processor.abSnapshotA : currentSnapshot;
    abSnapshotB = processor.abSnapshotB.isNotEmpty() ? processor.abSnapshotB : currentSnapshot;
    activeABSlotIsA = processor.activeABSlotIsA;
    abToggleButton.setButtonText(activeABSlotIsA ? "A" : "B");
    syncABCacheToProcessor();
    startTimerHz(1);
}

void PresetBarComponent::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    const auto bounds = getLocalBounds().toFloat();
    hostr::paintPanelFrame(g, bounds, skin, 2.0f, 1.0f);
    hostr::paintTextureOverlay(g, bounds.reduced(1.0f), skin, hostr::hasBitmapSkinSurface(skin) ? 0.08f : 0.18f);

    if (optionsBtn.isVisible())
        drawSegmentButton(g, optionsBtn, {}, true, true);
    drawSegmentButton(g, abToggleButton, activeABSlotIsA ? "A" : "B", true);
    if (undoBtn.isVisible())
        drawSegmentButton(g, undoBtn, "UNDO", !undoStack.empty());
    if (redoBtn.isVisible())
        drawSegmentButton(g, redoBtn, "REDO", !redoStack.empty());
    drawSegmentButton(g, presetMenuBtn, compactPresetActions ? juce::String{} : "PRESETS", true, compactPresetActions);
    if (prevPresetBtn.isVisible())
        drawSegmentButton(g, prevPresetBtn, "<");
    if (nextPresetBtn.isVisible())
        drawSegmentButton(g, nextPresetBtn, ">");
    if (loadBtn.isVisible())
        drawSegmentButton(g, loadBtn, "LOAD");
    if (saveBtn.isVisible())
        drawSegmentButton(g, saveBtn, "SAVE");
    if (saveAsBtn.isVisible())
        drawSegmentButton(g, saveAsBtn, "SAVE AS");

    auto nameBounds = getLocalBounds().reduced(6, 2);
    nameBounds.setLeft((undoBtn.isVisible() ? redoBtn.getRight() : abToggleButton.getRight()) + 6);
    nameBounds.setRight((prevPresetBtn.isVisible() ? prevPresetBtn : presetMenuBtn).getX() - 6);

    if (nameBounds.getWidth() > 6 && processor.presetManager)
    {
        auto presetName = processor.presetManager->getCurrentPresetName();
        if (presetName.isEmpty())
            presetName = "Default";

        g.setColour(skin.text.withAlpha(0.90f));
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(7.5f, 12.5f, getHeight() * 0.50f))));
        g.drawFittedText(presetName.toUpperCase(), nameBounds, juce::Justification::centred, 1, 0.35f);
    }

    const auto activeBounds = abToggleButton.getBounds().reduced(2).toFloat();
    g.setColour(skin.accent.withAlpha(0.72f));
    g.drawRect(activeBounds.toNearestInt(), juce::jmax(1, juce::roundToInt((float)getHeight() * 0.05f)));
}

void PresetBarComponent::refreshSkinColours()
{
    repaint();
}

void MultiParallelPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e);
    if (getMaxHorizontalScroll() <= 0)
        return;

    const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    const int step = juce::jmax(16, getWidth() / 8);
    setHorizontalScroll(horizontalScroll - juce::roundToInt(delta * (float)step * 8.0f));
    updateHorizontalScrollBar();
}

void PresetBarComponent::resized()
{
    auto b = getLocalBounds().reduced(1, 1);
    const int h = b.getHeight();
    const int iconW = juce::jmax(24, juce::roundToInt((float)h * 1.28f));
    const int abW = juce::jmax(20, juce::roundToInt((float)h * 1.05f));
    const int textW = juce::jmax(34, juce::roundToInt((float)h * 2.00f));
    const int presetW = juce::jmax(48, juce::roundToInt((float)h * 2.48f));
    const int arrowW = juce::jmax(18, juce::roundToInt((float)h * 1.02f));
    const int minPresetNameW = juce::jmax(82, juce::roundToInt((float)h * 4.8f));
    const int saveAsW = juce::jmax(50, juce::roundToInt((float)h * 2.55f));
    const int fullControlsW = iconW + abW + textW * 4 + saveAsW + presetW + arrowW * 2;
    compactPresetActions = b.getWidth() < fullControlsW + minPresetNameW;

    optionsBtn.setBounds(b.removeFromLeft(iconW));
    abToggleButton.setBounds(b.removeFromLeft(abW));
    undoBtn.setVisible(!compactPresetActions);
    redoBtn.setVisible(!compactPresetActions);
    prevPresetBtn.setVisible(!compactPresetActions);
    nextPresetBtn.setVisible(!compactPresetActions);
    if (compactPresetActions)
    {
        undoBtn.setBounds(0, 0, 0, 0);
        redoBtn.setBounds(0, 0, 0, 0);
        prevPresetBtn.setBounds(0, 0, 0, 0);
        nextPresetBtn.setBounds(0, 0, 0, 0);
    }
    else
    {
        undoBtn.setBounds(b.removeFromLeft(textW));
        redoBtn.setBounds(b.removeFromLeft(textW));
    }

    if (!compactPresetActions)
    {
        presetMenuBtn.setBounds(b.removeFromRight(presetW));
        saveAsBtn.setVisible(true);
        saveBtn.setVisible(true);
        loadBtn.setVisible(true);
        saveAsBtn.setBounds(b.removeFromRight(saveAsW));
        saveBtn.setBounds(b.removeFromRight(textW));
        loadBtn.setBounds(b.removeFromRight(textW));
        nextPresetBtn.setBounds(b.removeFromRight(arrowW));
        prevPresetBtn.setBounds(b.removeFromRight(arrowW));
    }
    else
    {
        saveBtn.setVisible(false);
        saveAsBtn.setVisible(false);
        loadBtn.setVisible(false);
        presetMenuBtn.setBounds(b.removeFromRight(iconW));
    }

}

void PresetBarComponent::drawSegmentButton(juce::Graphics& g, const juce::Button& button,
                                           const juce::String& text, bool enabled,
                                           bool drawHamburger)
{
    const auto& skin = hostr::currentSkin();
    auto bounds = button.getBounds().toFloat();
    if (bounds.isEmpty())
        return;

    const bool over = button.isMouseOver(true);
    const bool down = button.isDown();
    auto fill = down ? skin.panelInset : skin.panelRaised;
    const float fillAlpha = hostr::hasBitmapSkinSurface(skin) ? 0.18f : 0.34f;

    g.setColour(fill.withAlpha((enabled ? fillAlpha : fillAlpha * 0.45f) + (over && enabled ? 0.10f : 0.0f)));
    g.fillRect(bounds);
    g.setColour(skin.border.withAlpha(0.62f));
    g.drawRect(bounds.toNearestInt(), 1);

    const auto textColour = enabled ? skin.text.withAlpha(0.88f)
                                    : skin.mutedText.withAlpha(0.42f);
    if (drawHamburger)
    {
        const float lineW = juce::jmax(10.0f, bounds.getWidth() * 0.34f);
        const float lineX = bounds.getCentreX() - lineW * 0.5f;
        const float step = bounds.getHeight() * 0.18f;
        g.setColour(textColour);
        for (int i = -1; i <= 1; ++i)
        {
            const float y = bounds.getCentreY() + (float)i * step;
            g.drawLine(lineX, y, lineX + lineW, y, juce::jmax(1.0f, bounds.getHeight() * 0.055f));
        }
        return;
    }

    if (text.isNotEmpty())
    {
        g.setColour(textColour);
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(7.0f, 11.0f, bounds.getHeight() * 0.46f)).withStyle("Bold")));
        g.drawFittedText(text.toUpperCase(), bounds.toNearestInt().reduced(3, 1),
                         juce::Justification::centred, 1, 0.72f);
    }
}

std::vector<juce::File> PresetBarComponent::getPresetFiles() const
{
    std::vector<juce::File> files;
    juce::Array<juce::File> found;
    PresetManager::getPresetsFolder()
        .findChildFiles(found, juce::File::findFiles, false, "*.hostrpreset");

    files.reserve((size_t)found.size());
    for (auto& file : found)
        files.push_back(file);

    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

juce::String PresetBarComponent::captureSnapshot() const
{
    if (!processor.presetManager)
        return {};

    auto name = processor.presetManager->getCurrentPresetName();
    if (name.isEmpty())
        name = "Default";

    std::unique_ptr<juce::XmlElement> xml(processor.presetManager->createPresetXml(name));
    return xml ? xml->toString() : juce::String {};
}

void PresetBarComponent::recordStateIfChanged()
{
    if (!processor.presetManager || processor.presetManager->isApplyingPreset)
        return;

    auto snapshot = captureSnapshot();
    if (snapshot.isEmpty())
        return;

    if (currentSnapshot.isEmpty())
    {
        currentSnapshot = snapshot;
        if (activeABSlotIsA)
            abSnapshotA = snapshot;
        else
            abSnapshotB = snapshot;
        syncABCacheToProcessor();
        return;
    }

    if (snapshot != currentSnapshot)
    {
        undoStack.push_back(currentSnapshot);
        if (undoStack.size() > 32)
            undoStack.erase(undoStack.begin());

        currentSnapshot = snapshot;
        if (activeABSlotIsA)
            abSnapshotA = snapshot;
        else
            abSnapshotB = snapshot;
        redoStack.clear();
        syncABCacheToProcessor();
        repaint();
    }
}

void PresetBarComponent::timerCallback()
{
    if (suppressNextSnapshot)
    {
        suppressNextSnapshot = false;
        return;
    }

    recordStateIfChanged();
}

void PresetBarComponent::applySnapshot(const juce::String& snapshot)
{
    if (!processor.presetManager || snapshot.isEmpty())
        return;

    auto xml = juce::XmlDocument::parse(snapshot);
    if (!xml)
        return;

    suppressNextSnapshot = true;
    if (processor.presetManager->applyPresetXml(*xml))
    {
        currentSnapshot = snapshot;
        if (activeABSlotIsA)
            abSnapshotA = snapshot;
        else
            abSnapshotB = snapshot;
        syncABCacheToProcessor();
        refreshLabel();
        if (auto* pe = findParentComponentOfClass<PluginEditor>())
        {
            pe->refreshAllSlots();
            pe->scheduleLayoutUpdate();
            juce::Component::SafePointer<PluginEditor> safeEd(pe);
            juce::MessageManager::callAsync([safeEd]()
            {
                if (safeEd) safeEd->syncAllParallelPanels();
            });
        }
    }
    repaint();
}

void PresetBarComponent::selectABSlot(bool useA)
{
    if (activeABSlotIsA == useA)
    {
        repaint();
        return;
    }

    recordStateIfChanged();
    if (currentSnapshot.isNotEmpty())
    {
        if (activeABSlotIsA)
            abSnapshotA = currentSnapshot;
        else
            abSnapshotB = currentSnapshot;
    }

    activeABSlotIsA = useA;
    abToggleButton.setButtonText(activeABSlotIsA ? "A" : "B");
    const auto& target = useA ? abSnapshotA : abSnapshotB;
    if (target.isNotEmpty())
        applySnapshot(target);
    else
    {
        currentSnapshot = captureSnapshot();
        if (useA)
            abSnapshotA = currentSnapshot;
        else
            abSnapshotB = currentSnapshot;
    }

    syncABCacheToProcessor();
    repaint();
}

void PresetBarComponent::toggleABSlot()
{
    selectABSlot(!activeABSlotIsA);
}

void PresetBarComponent::copyABSlot(bool copyAToB)
{
    recordStateIfChanged();
    if (currentSnapshot.isNotEmpty())
    {
        if (activeABSlotIsA)
            abSnapshotA = currentSnapshot;
        else
            abSnapshotB = currentSnapshot;
    }

    if (copyAToB)
        abSnapshotB = abSnapshotA;
    else
        abSnapshotA = abSnapshotB;

    syncABCacheToProcessor();
    repaint();
}

void PresetBarComponent::swapABSlots()
{
    recordStateIfChanged();
    std::swap(abSnapshotA, abSnapshotB);
    activeABSlotIsA = !activeABSlotIsA;
    abToggleButton.setButtonText(activeABSlotIsA ? "A" : "B");
    syncABCacheToProcessor();
    repaint();
}

void PresetBarComponent::syncABCacheToProcessor()
{
    processor.abSnapshotA = abSnapshotA;
    processor.abSnapshotB = abSnapshotB;
    processor.activeABSlotIsA = activeABSlotIsA;
}

void PresetBarComponent::doSave()
{
    if (!processor.presetManager) return;
    const juce::String cur = processor.presetManager->getCurrentPresetName();

    if (cur.isEmpty() || cur == "(unsaved)")
    { doSaveAs(); return; }

    if (processor.presetManager->savePresetAs(cur))
    {
        refreshLabel();
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Preset Saved",
            "Preset '" + cur + "' saved.", "OK");
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Error",
            "Unable to save the preset.", "OK");
    }
}

void PresetBarComponent::doSaveAs()
{
    if (!processor.presetManager) return;

    auto* dialog = new juce::AlertWindow("Save Preset As",
                                          "Enter a name for the preset:",
                                          juce::AlertWindow::NoIcon);
    dialog->addTextEditor("name",
                          processor.presetManager->getCurrentPresetName(),
                          "Preset name:");
    dialog->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([this, dialog](int result)
        {
            if (result == 1)
            {
                juce::String presetName = dialog->getTextEditorContents("name").trim();
                if (presetName.isNotEmpty() && processor.presetManager)
                {
                    if (processor.presetManager->savePresetAs(presetName))
                    {
                        refreshLabel();
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::InfoIcon, "Preset Saved",
                            "Preset '" + presetName + "' saved.", "OK");
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::AlertWindow::WarningIcon, "Error",
                            "Unable to save the preset.", "OK");
                    }
                }
            }
            delete dialog;
        }), true);
}

void PresetBarComponent::doLoad()
{
    if (!processor.presetManager) return;

    processor.presetManager->loadPresetFromFile(
        [this](bool ok, const juce::String& /*name*/, bool cancelled)
        {
            if (ok)
            {
                if (currentSnapshot.isNotEmpty())
                {
                    undoStack.push_back(currentSnapshot);
                    if (undoStack.size() > 32)
                        undoStack.erase(undoStack.begin());
                }
                currentSnapshot = captureSnapshot();
                if (activeABSlotIsA)
                    abSnapshotA = currentSnapshot;
                else
                    abSnapshotB = currentSnapshot;
                syncABCacheToProcessor();
                redoStack.clear();
                refreshLabel();
                if (auto* pe = findParentComponentOfClass<PluginEditor>())
                {
                    pe->refreshAllSlots();
                    pe->scheduleLayoutUpdate();
                    juce::Component::SafePointer<PluginEditor> safeEd(pe);
                    juce::MessageManager::callAsync([safeEd]()
                    {
                        if (safeEd) safeEd->syncAllParallelPanels();
                    });
                }
            }
            else if (!cancelled)
            {
                // Real error (not parsable or apply failed) — mostra messaggio
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Error",
                    "Unable to load the preset.", "OK");
            }
            // If cancelled == true the user pressed Cancel: no message
        });
}

void PresetBarComponent::doUndo()
{
    recordStateIfChanged();
    if (undoStack.empty())
        return;

    if (currentSnapshot.isNotEmpty())
        redoStack.push_back(currentSnapshot);

    auto snapshot = undoStack.back();
    undoStack.pop_back();
    applySnapshot(snapshot);
}

void PresetBarComponent::doRedo()
{
    if (redoStack.empty())
        return;

    if (currentSnapshot.isNotEmpty())
        undoStack.push_back(currentSnapshot);

    auto snapshot = redoStack.back();
    redoStack.pop_back();
    applySnapshot(snapshot);
}

void PresetBarComponent::loadPresetFileAsync(const juce::File& fileToLoad)
{
    juce::Component::SafePointer<PresetBarComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, fileToLoad]()
    {
        if (!safeThis) return;
        auto& self = *safeThis;
        if (!self.processor.presetManager) return;

        auto xml = juce::XmlDocument::parse(fileToLoad);
        if (!xml)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Error",
                "Unable to read the preset file.", "OK");
            return;
        }

        self.recordStateIfChanged();
        if (self.currentSnapshot.isNotEmpty())
        {
            self.undoStack.push_back(self.currentSnapshot);
            if (self.undoStack.size() > 32)
                self.undoStack.erase(self.undoStack.begin());
        }

        if (self.processor.presetManager->applyPresetXml(*xml))
        {
            self.currentSnapshot = self.captureSnapshot();
            if (self.activeABSlotIsA)
                self.abSnapshotA = self.currentSnapshot;
            else
                self.abSnapshotB = self.currentSnapshot;
            self.syncABCacheToProcessor();
            self.redoStack.clear();
            self.refreshLabel();
            if (auto* pe = self.findParentComponentOfClass<PluginEditor>())
            {
                pe->refreshAllSlots();
                pe->scheduleLayoutUpdate();
                juce::Component::SafePointer<PluginEditor> safeEd(pe);
                juce::MessageManager::callAsync([safeEd]()
                {
                    if (safeEd) safeEd->syncAllParallelPanels();
                });
            }
        }
        else
        {
            if (!self.undoStack.empty())
                self.undoStack.pop_back();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Error",
                "Unable to apply the preset.", "OK");
        }
        self.repaint();
    });
}

void PresetBarComponent::loadPreviousPreset()
{
    const auto files = getPresetFiles();
    if (files.empty() || !processor.presetManager)
        return;

    const auto current = processor.presetManager->getCurrentPresetName();
    int idx = 0;
    for (int i = 0; i < (int)files.size(); ++i)
        if (files[(size_t)i].getFileNameWithoutExtension().equalsIgnoreCase(current))
            idx = i;

    idx = (idx + (int)files.size() - 1) % (int)files.size();
    loadPresetFileAsync(files[(size_t)idx]);
}

void PresetBarComponent::loadNextPreset()
{
    const auto files = getPresetFiles();
    if (files.empty() || !processor.presetManager)
        return;

    const auto current = processor.presetManager->getCurrentPresetName();
    int idx = -1;
    for (int i = 0; i < (int)files.size(); ++i)
        if (files[(size_t)i].getFileNameWithoutExtension().equalsIgnoreCase(current))
            idx = i;

    idx = (idx + 1) % (int)files.size();
    loadPresetFileAsync(files[(size_t)idx]);
}

void PresetBarComponent::showPresetMenu()
{
    if (!processor.presetManager) return;

    juce::PopupMenu menu;
    menu.addItem(5, "Load...");
    menu.addItem(1, "Save");
    menu.addItem(2, "Save As...");
    menu.addItem(3, "Set As Default Preset");
    menu.addItem(4, "Reset Default Preset");
    menu.addSeparator();
    menu.addSectionHeader("A/B TESTING");
    menu.addItem(6, "Recall A", true, activeABSlotIsA);
    menu.addItem(7, "Recall B", true, !activeABSlotIsA);
    menu.addItem(8, "Copy A to B");
    menu.addItem(9, "Copy B to A");
    menu.addItem(10, "Swap A/B");
    menu.addSeparator();

    // Enumerate the presets saved in the folder
    auto presetFiles = getPresetFiles();

    if (!presetFiles.empty())
    {
        juce::PopupMenu recentMenu;
        for (int i = 0; i < (int)presetFiles.size(); ++i)
            recentMenu.addItem(100 + i, presetFiles[(size_t)i].getFileNameWithoutExtension());
        menu.addSubMenu("Presets", recentMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetMenuBtn),
        [this, presetFiles](int result)
        {
            if (!processor.presetManager) return;

            if (result == 1)
            {
                doSave();
            }
            else if (result == 5)
            {
                doLoad();
            }
            else if (result == 2)
            {
                doSaveAs();
            }
            else if (result == 3)
            {
                bool ok = processor.presetManager->saveAsDefault();
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    ok ? "Default Saved" : "Error",
                    ok ? "This preset will load automatically at startup."
                       : "Unable to save the default preset.",
                    "OK");
            }
            else if (result == 4)
            {
                juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::QuestionIcon,
                    "Reset Default Preset",
                    "This will remove the user default preset.\n"
                    "The plugin will start with all slots empty.",
                    "Reset", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create([this](int r)
                    {
                        if (r != 1 || !processor.presetManager) return;

                        juce::Component::SafePointer<PresetBarComponent> safeThis(this);
                        juce::MessageManager::callAsync([safeThis]()
                        {
                            if (!safeThis) return;
                            auto& self = *safeThis;
                            if (!self.processor.presetManager) return;

                            bool ok = self.processor.presetManager->resetDefaultPreset();
                            if (ok)
                            {
                                self.refreshLabel();
                                if (auto* pe = self.findParentComponentOfClass<PluginEditor>())
                                {
                                    pe->refreshAllSlots();
                                    pe->scheduleLayoutUpdate();
                                }
                            }
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::AlertWindow::InfoIcon,
                                ok ? "Default Removed" : "Error",
                                ok ? "Default preset removed. The slots have been cleared."
                                   : "Unable to remove the default preset.",
                                "OK");
                        });
                    }));
            }
            else if (result == 6)
            {
                selectABSlot(true);
            }
            else if (result == 7)
            {
                selectABSlot(false);
            }
            else if (result == 8)
            {
                copyABSlot(true);
            }
            else if (result == 9)
            {
                copyABSlot(false);
            }
            else if (result == 10)
            {
                swapABSlots();
            }
            else if (result >= 100)
            {
                int idx = result - 100;
                if (idx >= 0 && idx < (int)presetFiles.size())
                {
                    loadPresetFileAsync(presetFiles[(size_t)idx]);
                }
            }
        });
}

// ==============================================================================
// PLUGIN EDITOR
// ==============================================================================

static float snapToMenuZoom(float zoom)
{
    constexpr float levels[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    float best = levels[0];
    float bestDistance = std::abs(zoom - best);

    for (auto level : levels)
    {
        const float distance = std::abs(zoom - level);
        if (distance < bestDistance)
        {
            best = level;
            bestDistance = distance;
        }
    }

    return best;
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      masterMeterOverlay(p),
      scanOverlay(p)
{
    hostr::setActiveSkinIndex(processor.getEditorSkinIndex());
    setLookAndFeel(&customLook);
    setResizable(true, true);
    zoomLevel = snapToMenuZoom(processor.getEditorZoomScale());
    processor.setEditorZoomScale(zoomLevel);

    // Preset bar — contains MENU + name + Save + Load + ▼
    presetBar = std::make_unique<PresetBarComponent>(p);
    presetBar->onOptionsMenu = [this]() { showOptionsMenu(); };
    addAndMakeVisible(*presetBar);
    refreshSkinColours();

    addAndMakeVisible(macroKnob);
    macroKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    macroKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    macroKnob.setNormalisableRange(knobDbRange());
    macroKnob.setMouseDragSensitivity(360);
    macroKnob.setDoubleClickReturnValue(true, 0.0);
    macroKnob.setValue(clampKnobDb(processor.inputGain.load()), juce::dontSendNotification);
    macroKnob.onValueChange = [this]
    {
        processor.setInputGainDb(clampKnobDb((float)macroKnob.getValue()));
        repaint();
    };

    for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
    {
        auto& slider = macroControlKnobs[(size_t)i];
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0.0, 1.0, 0.001);
        slider.setMouseDragSensitivity(240);
        slider.setDoubleClickReturnValue(true, 0.0);
        slider.setTooltip(processor.getMacroName(i));
        slider.setValue(processor.getMacroValue(i), juce::dontSendNotification);
        slider.onValueChange = [this, i]()
        {
            processor.setMacroValue(i, (float)macroControlKnobs[(size_t)i].getValue());
            repaint();
        };

        auto& hotspot = macroAssignHotspots[(size_t)i];
        addAndMakeVisible(hotspot);
        hotspot.setButtonText("");
        hotspot.setWantsKeyboardFocus(false);
        hotspot.setAlpha(0.01f);
        hotspot.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        hotspot.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        hotspot.setTooltip("Assign this macro");
        hotspot.onClick = [this, i]()
        {
            if (macroClearMode)
                clearMacroMappingsForIndex(i);
            else if (macroAssignmentMode)
                beginMacroLearn(i);
        };
    }

    addAndMakeVisible(macroAssignBtn);
    macroAssignBtn.setButtonText("+");
    macroAssignBtn.setClickingTogglesState(true);
    macroAssignBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff8a1f));
    macroAssignBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffa33a));
    macroAssignBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::black.withAlpha(0.85f));
    macroAssignBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    macroAssignBtn.setTooltip("Assign macros to plugin parameters");
    macroAssignBtn.onClick = [this]()
    {
        macroAssignmentMode = macroAssignBtn.getToggleState();
        if (macroAssignmentMode)
        {
            macroClearMode = false;
            macroClearBtn.setToggleState(false, juce::dontSendNotification);
        }
        else
        {
            macroLearnCandidates.clear();
            macroLearnIndex = -1;
        }
        updateMacroAssignmentMode();
        repaint();
    };

    addAndMakeVisible(macroClearBtn);
    macroClearBtn.setButtonText("-");
    macroClearBtn.setClickingTogglesState(true);
    macroClearBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd83a2e));
    macroClearBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff4a3d));
    macroClearBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
    macroClearBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    macroClearBtn.setTooltip("Clear mappings from the next macro you click");
    macroClearBtn.onClick = [this]()
    {
        macroClearMode = macroClearBtn.getToggleState();
        if (macroClearMode)
        {
            macroAssignmentMode = false;
            macroAssignBtn.setToggleState(false, juce::dontSendNotification);
            macroLearnCandidates.clear();
            macroLearnIndex = -1;
        }
        updateMacroAssignmentMode();
        repaint();
    };
    updateMacroAssignmentMode();

    // Preset name label centered above "MASTER" in the header
    presetNameLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.65f));
    presetNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(presetNameLabel);

    // The text is updated by refreshAllSlots() → syncPresetName()
    // Bypasses the entire master chain (all plugins in series and split chains).
    addAndMakeVisible(masterBypassBtn);
    masterBypassBtn.setClickingTogglesState(true);
    masterBypassBtn.setColour(juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    masterBypassBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    masterBypassBtn.setColour(juce::TextButton::textColourOffId,  juce::Colours::transparentBlack);
    masterBypassBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::transparentBlack);
    masterBypassBtn.onClick = [this]()
    {
        bool bypassed = masterBypassBtn.getToggleState();
        for (int i = 0; i < 8; ++i)
            if (processor.pluginSlots[i].isValid)
                processor.setSlotBypassed(i, bypassed);
        scheduleLayoutUpdate();
        repaint();
    };

    addAndMakeVisible(parallelCollapseBtn);
    parallelCollapseBtn.setButtonText("MACROS");
    parallelCollapseBtn.setColour(juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    parallelCollapseBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    parallelCollapseBtn.setColour(juce::TextButton::textColourOffId,  juce::Colours::transparentBlack);
    parallelCollapseBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::transparentBlack);
    parallelCollapseBtn.setTooltip("Show or hide macros");
    parallelCollapseBtn.setVisible(true);
    parallelCollapseBtn.onClick = [this]()
    {
        macroLaneVisible = !macroLaneVisible;
        updateMacroAssignmentMode();
        scheduleLayoutUpdate();
    };

    for (int i = 0; i < 8; ++i)
    {
        auto* slot = new PluginSlotComponent(p, i);
        addAndMakeVisible(slot);
        slots.add(slot);
    }

    addAndMakeVisible(masterOutputKnob);
    masterOutputKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterOutputKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterOutputKnob.setNormalisableRange(outputKnobDbRange());
    masterOutputKnob.setMouseDragSensitivity(360);
    masterOutputKnob.setValue(processor.getMasterVolumeDb(), juce::dontSendNotification);
    masterOutputKnob.setDoubleClickReturnValue(true, 0.0);
    masterOutputKnob.setTooltip("Master Output");
    masterOutputKnob.onValueChange = [this]
    {
        const auto db = clampKnobDb((float)masterOutputKnob.getValue());
        processor.setMasterVolumeDb(db);
        repaint();
    };

    addAndMakeVisible(masterMeterOverlay);

    dragGhost = std::make_unique<DragGhost>();
    addAndMakeVisible(*dragGhost);
    dragGhost->setVisible(false);

    resizeCorner = std::make_unique<ResizeCorner>(*this, false);
    addAndMakeVisible(*resizeCorner);

    addAndMakeVisible(scanOverlay);
    scanOverlay.setAlwaysOnTop(true);
    scanOverlay.setVisible(false);

    // Register the callback for safe destruction of the MultiParallelPanel
    // before applyPresetXml removes the ParallelSplitProcessors.
    // Without this, the ParallelSplitComponent (raw pointers to the processors) would receive
    // timer/paint callbacks on dangling pointers → EXC_BAD_ACCESS.
    if (processor.presetManager)
    {
        juce::Component::SafePointer<PluginEditor> safeThis(this);
        processor.presetManager->onPrepareForPresetLoad = [safeThis]()
        {
            if (auto* ed = safeThis.getComponent())
            {
                ed->multiPanel.reset();
                ed->syncSlotHighlights();
            }
        };
        processor.presetManager->onPresetApplied = [safeThis]()
        {
            juce::MessageManager::callAsync([safeThis]()
            {
                if (auto* ed = safeThis.getComponent())
                {
                    ed->refreshAllSlots();
                    ed->syncAllParallelPanels();
                    ed->repaint();
                }
            });
        };
    }

    refreshAllSlots();
    refreshParallelLayoutNow();

    if (const auto recoveryMessage = processor.getPendingScanRecoveryMessage(); recoveryMessage.isNotEmpty())
    {
        juce::Component::SafePointer<PluginEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis, recoveryMessage]()
        {
            if (auto* ed = safeThis.getComponent())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Previous Scan Recovered",
                                                       recoveryMessage);
                ed->processor.clearPendingScanRecoveryMessage();
            }
        });
    }

    startTimerHz(20);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    PluginSearchDialog::dismissActive();
    setLookAndFeel(nullptr);

    // Cancel the callback before destroying the components
    // prevent applyPresetXml (called from another context) 
    // from attempting to access this editor after it has already been destroyed.
    if (processor.presetManager)
    {
        processor.presetManager->onPrepareForPresetLoad = nullptr;
        processor.presetManager->onPresetApplied = nullptr;
    }

    multiPanel.reset();
    dragGhost.reset();
    presetBar.reset();
}

void PluginEditor::updateMacroAssignmentMode()
{
    macroAssignBtn.setToggleState(macroAssignmentMode, juce::dontSendNotification);
    macroClearBtn.setToggleState(macroClearMode, juce::dontSendNotification);
    macroAssignBtn.setVisible(macroLaneVisible);
    macroClearBtn.setVisible(macroLaneVisible);
    for (auto& slider : macroControlKnobs)
        slider.setVisible(macroLaneVisible);
    const bool hotspotsActive = macroLaneVisible && (macroAssignmentMode || macroClearMode);
    for (auto& hotspot : macroAssignHotspots)
    {
        hotspot.setVisible(hotspotsActive);
        hotspot.setEnabled(hotspotsActive);
    }
}

void PluginEditor::clearMacroMappingsForIndex(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= PluginProcessor::macroControlCount)
        return;

    auto mappings = processor.getMacroMappings();
    for (int i = (int)mappings.size(); --i >= 0;)
        if (mappings[(size_t)i].macroIndex == macroIndex)
            processor.removeMacroMapping((size_t)i);

    macroClearMode = false;
    updateMacroAssignmentMode();
    repaint();
}

void PluginEditor::cancelMacroLearn()
{
    macroLearnCandidates.clear();
    macroLearnIndex = -1;
    macroAssignmentMode = false;
    macroClearMode = false;
    updateMacroAssignmentMode();
    repaint();
}

bool PluginEditor::cancelMacroModesFromExternalClick()
{
    if (!macroAssignmentMode && !macroClearMode && macroLearnIndex < 0)
        return false;

    cancelMacroLearn();
    return true;
}

void PluginEditor::prepareForParallelSplitRemoval()
{
    multiPanel.reset();
    masterSplitArrowButtons.clear();
    parallelPanelsCollapsed = false;
    syncSlotHighlights();
    scheduleLayoutUpdate();
}

void PluginEditor::mouseDown(const juce::MouseEvent& e)
{
    if (macroLaneVisible)
    {
        if (macroAssignBtn.getBounds().contains(e.getPosition())
            || macroClearBtn.getBounds().contains(e.getPosition()))
            return;

        for (const auto& hotspot : macroAssignHotspots)
            if (hotspot.isVisible() && hotspot.getBounds().contains(e.getPosition()))
                return;
    }

    cancelMacroModesFromExternalClick();
}

void PluginEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const bool zoomGesture = e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown();
    if (!zoomGesture)
    {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) < 0.0001f)
        return;

    const float step = delta > 0.0f ? 0.05f : -0.05f;
    setZoomLevelRaw(zoomLevel + step);
}

void PluginEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int macroLaneW = juce::roundToInt((float)(macroLaneVisible ? MACRO_LANE_W : MACRO_COLLAPSED_LANE_W) * zoomLevel);
    const int masterX = macroLaneW;
    const int masterW = (int)(MASTER_CHAIN_W * zoomLevel);

    auto inputDbBounds = juce::Rectangle<int>(masterX, macroKnob.getBottom() + juce::roundToInt(14.0f * zoomLevel),
                                              masterW, juce::jmax(16, juce::roundToInt(14.0f * zoomLevel)));
    auto outputDbBounds = juce::Rectangle<int>(masterX, masterOutputKnob.getBottom() + juce::roundToInt(14.0f * zoomLevel),
                                               masterW, juce::jmax(16, juce::roundToInt(14.0f * zoomLevel)));

    if (inputDbBounds.expanded(4).contains(e.getPosition()))
    {
        beginInlineDbEdit(*this, inputDbBounds, clampKnobDb(processor.inputGain.load()),
                          [this](float db)
                          {
                              processor.setInputGainDb(db);
                              macroKnob.setValue(db, juce::dontSendNotification);
                              repaint();
                          });
    }
    else if (outputDbBounds.expanded(4).contains(e.getPosition()))
    {
        beginInlineDbEdit(*this, outputDbBounds, processor.getMasterVolumeDb(),
                          [this](float db)
                          {
                              processor.setMasterVolumeDb(db);
                              masterOutputKnob.setValue(db, juce::dontSendNotification);
                              repaint();
                          });
    }
}

void PluginEditor::beginMacroLearn(int macroIndex)
{
    if (macroIndex < 0 || macroIndex >= PluginProcessor::macroControlCount)
        return;

    macroLearnCandidates.clear();
    macroLearnIndex = macroIndex;

    auto addPluginCandidates = [this, macroIndex](juce::AudioProcessor* plugin,
                                                   PluginProcessor::MacroMapping mapping,
                                                   juce::String targetPath,
                                                   auto& addPluginCandidatesRef) -> void
    {
        if (plugin == nullptr)
            return;

        mapping.macroIndex = macroIndex;
        mapping.targetPath = targetPath;
        mapping.pluginName = plugin->getName();

        const auto& params = plugin->getParameters();
        const int count = juce::jmin(params.size(), 1024);
        for (int parameterIndex = 0; parameterIndex < count; ++parameterIndex)
        {
            auto* parameter = params[parameterIndex];
            if (parameter == nullptr)
                continue;

            auto item = mapping;
            item.parameterIndex = parameterIndex;
            item.parameterName = cleanMacroParameterName(parameter->getName(96));
            if (item.parameterName.isEmpty())
                item.parameterName = "Parameter " + juce::String(parameterIndex + 1);

            MacroLearnCandidate candidate;
            candidate.mapping = item;
            candidate.processor = plugin;
            candidate.parameterIndex = parameterIndex;
            candidate.initialValue = parameter->getValue();
            macroLearnCandidates.push_back(candidate);
        }
    };

    auto collectSplit = [&](ParallelSplitProcessor* split,
                            int masterSlot,
                            const juce::String& basePath,
                            auto& collectSplitRef) -> void
    {
        if (split == nullptr)
            return;

        for (int chainIndex = 0; chainIndex < split->getNumChains(); ++chainIndex)
        {
            const auto& chain = split->getChain(chainIndex);
            for (int slotIndex = 0; slotIndex < (int)chain.slots.size(); ++slotIndex)
            {
                const auto& slot = chain.slots[(size_t)slotIndex];
                if (!slot.valid)
                    continue;

                const auto path = basePath + "/C" + juce::String(chainIndex) + "/S" + juce::String(slotIndex);
                if (slot.type == ParallelSplitProcessor::ChainSlotType::Plugin && slot.node != nullptr)
                {
                    PluginProcessor::MacroMapping mapping;
                    mapping.scope = PluginProcessor::MacroTargetScope::ParallelSlot;
                    mapping.masterSlot = masterSlot;
                    mapping.chainIndex = chainIndex;
                    mapping.parallelSlot = slotIndex;
                    mapping.pluginName = slot.name;
                    addPluginCandidates(slot.node->getProcessor(), mapping, path, addPluginCandidates);
                }
                else if (slot.type == ParallelSplitProcessor::ChainSlotType::ParallelSplit
                         && slot.parallelProcessor != nullptr)
                {
                    collectSplitRef(slot.parallelProcessor, masterSlot, path, collectSplitRef);
                }
            }
        }
    };

    for (int masterSlot = 0; masterSlot < 8; ++masterSlot)
    {
        const auto& slot = processor.pluginSlots[(size_t)masterSlot];
        if (!slot.isValid)
            continue;

        const auto path = "M" + juce::String(masterSlot);
        if (slot.type == PluginProcessor::SlotType::Plugin && slot.node != nullptr)
        {
            PluginProcessor::MacroMapping mapping;
            mapping.scope = PluginProcessor::MacroTargetScope::MasterSlot;
            mapping.masterSlot = masterSlot;
            mapping.pluginName = slot.name;
            addPluginCandidates(slot.node->getProcessor(), mapping, path, addPluginCandidates);
        }
        else if (slot.type == PluginProcessor::SlotType::ParallelSplit && slot.parallelProcessor != nullptr)
        {
            collectSplit(slot.parallelProcessor, masterSlot, path, collectSplit);
        }
    }

    if (macroLearnCandidates.empty())
    {
        cancelMacroLearn();
        return;
    }

    macroAssignmentMode = true;
    updateMacroAssignmentMode();
    repaint();
}

void PluginEditor::pollMacroLearn()
{
    if (macroLearnIndex < 0 || macroLearnCandidates.empty())
        return;

    for (const auto& candidate : macroLearnCandidates)
    {
        if (candidate.processor == nullptr)
            continue;

        const auto& params = candidate.processor->getParameters();
        if (candidate.parameterIndex < 0 || candidate.parameterIndex >= params.size())
            continue;

        auto* parameter = params[candidate.parameterIndex];
        if (parameter == nullptr)
            continue;

        const float currentValue = parameter->getValue();
        if (std::abs(currentValue - candidate.initialValue) <= 0.0025f)
            continue;

        auto mapping = candidate.mapping;
        processor.addMacroMapping(mapping);
        processor.setMacroValue(macroLearnIndex, currentValue);
        macroControlKnobs[(size_t)macroLearnIndex].setValue(currentValue, juce::dontSendNotification);
        macroControlKnobs[(size_t)macroLearnIndex].setTooltip(
            processor.getMacroName(macroLearnIndex) + " -> " + mapping.pluginName + " / " + mapping.parameterName);
        cancelMacroLearn();
        repaint();
        return;
    }
}

void PluginEditor::setDropTarget(int slotIndex)
{
    if (currentDropTarget == slotIndex) return;
    int old = currentDropTarget;
    currentDropTarget = slotIndex;
    if (old >= 0 && old < 8)               { slots[old]->isDropTarget = false; slots[old]->repaint(); }
    if (slotIndex >= 0 && slotIndex < 8)   { slots[slotIndex]->isDropTarget = true; slots[slotIndex]->repaint(); }
}

void PluginEditor::beginParallelDrag(const DragPayload& payload,
                                      const juce::String& pluginName,
                                      bool bypassed,
                                      bool isParallel,
                                      juce::Point<int> screenPos)
{
    activeDragPayload = payload;
    if (dragGhost)
        dragGhost->start(pluginName, isParallel, bypassed,
                         getSlotWidth(), getSlotHeight(), screenPos);
}

void PluginEditor::updateParallelDrag(juce::Point<int> screenPos)
{
    if (dragGhost) dragGhost->moveTo(screenPos);

    // Highlight master drop targets
    int dropIdx = -1;
    for (auto* s : slots)
    {
        if (s->localAreaToGlobal(s->getLocalBounds()).contains(screenPos))
        { dropIdx = s->getSlotIndex(); break; }
    }
    if (activeDragPayload.source == DragSource::MasterSlot && dropIdx == activeDragPayload.masterSlot)
        dropIdx = -1;
    setDropTarget(dropIdx);
}

void PluginEditor::completeCrossDrag(const DragPayload& payload, juce::Point<int> screenDropPos)
{
    if (dragGhost) dragGhost->stop();
    setDropTarget(-1);
    clearParallelDrag();

    juce::Component::SafePointer<PluginEditor> safeThis(this);

    // Determine the destination
    // 1. Check master slot
    for (auto* s : slots)
    {
        if (s->localAreaToGlobal(s->getLocalBounds()).contains(screenDropPos))
        {
            int dstMaster = s->getSlotIndex();
            juce::MessageManager::callAsync([safeThis, payload, dstMaster]()
            {
                if (!safeThis) return;
                if (payload.source == DragSource::ParallelSlot)
                {
                    safeThis->processor.moveFromParallelToMaster(payload.parallelProc,
                        payload.chainIndex, payload.slotIndex, dstMaster);
                    safeThis->refreshAllSlots();
                    safeThis->scheduleLayoutUpdate();
                }
                else if (payload.source == DragSource::MasterSlot && payload.masterSlot != dstMaster)
                {
                    safeThis->processor.moveOrSwapPlugin(payload.masterSlot, dstMaster);
                    safeThis->refreshAllSlots();
                    safeThis->scheduleLayoutUpdate();
                }
            });
            return;
        }
    }

    // 2. Check parallel chain slots
    if (!multiPanel) return;
    for (int ci = 0; ci < multiPanel->getNumChildComponents(); ++ci)
    {
        auto* psc = dynamic_cast<ParallelSplitComponent*>(multiPanel->getChildComponent(ci));
        if (!psc || !psc->isVisible()) continue;

        auto checkSlots = [&](juce::OwnedArray<ParallelChainSlotComponent>& arr, int chainIdx) -> bool
        {
            for (auto* pslot : arr)
            {
                if (pslot->localAreaToGlobal(pslot->getLocalBounds()).contains(screenDropPos))
                {
                    int dstSlot = pslot->getSlotIndex();
                    auto* destProc = psc->getProcessorForDisplayedChain(chainIdx);
                    const int destChainIdx = psc->getProcessorChainIndexForDisplayedChain(chainIdx);
                    juce::MessageManager::callAsync([safeThis, payload, destProc, destChainIdx, dstSlot]()
                    {
                        if (!safeThis || destProc == nullptr) return;
                        if (payload.source == DragSource::ParallelSlot)
                        {
                            safeThis->processor.moveParallelToParallel(
                                payload.parallelProc, payload.chainIndex, payload.slotIndex,
                                destProc, destChainIdx, dstSlot);
                        }
                        else if (payload.source == DragSource::MasterSlot)
                        {
                            safeThis->processor.moveFromMasterToParallel(
                                payload.masterSlot, destProc, destChainIdx, dstSlot);
                        }
                        safeThis->refreshAllSlots();
                        safeThis->scheduleLayoutUpdate();
                    });
                    return true;
                }
            }
            return false;
        };
        for (int chainIdx = 0; chainIdx < psc->getNumDisplayedChains(); ++chainIdx)
            if (checkSlots(psc->getSlotsForChain(chainIdx), chainIdx))
                return;
    }
}

void PluginEditor::startScanOverlay()
{
    scanOverlay.startMonitoring();
}

void PluginEditor::scheduleLayoutUpdate()
{
    if (layoutUpdatePending) return;
    layoutUpdatePending = true;
    juce::Component::SafePointer<PluginEditor> safe(this);
    juce::MessageManager::callAsync([safe]()
    {
        if (safe) { safe->layoutUpdatePending = false; safe->performLayoutUpdate(); }
    });
}

void PluginEditor::refreshParallelLayoutNow()
{
    layoutUpdatePending = false;

    for (auto* slot : slots)
    {
        if (slot == nullptr)
            continue;

        slot->setRemoveBtnVisible(processor.pluginSlots[slot->getSlotIndex()].isValid);
        slot->repaint();
    }

    performLayoutUpdate();

    if (multiPanel != nullptr)
    {
        multiPanel->resized();
        multiPanel->repaint();
    }

    repaint();
}

void PluginEditor::refreshAllSlots()
{
    for (auto* slot : slots)
    {
        slot->setRemoveBtnVisible(processor.pluginSlots[slot->getSlotIndex()].isValid);
        slot->repaint();
    }
    // Update the preset name in the header
    if (processor.presetManager)
    {
        const auto& n = processor.presetManager->getCurrentPresetName();
        presetNameLabel.setText(n.isEmpty() ? "(unsaved)" : n, juce::dontSendNotification);
    }
    macroKnob.setValue(clampKnobDb(processor.inputGain.load()), juce::dontSendNotification);
    for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
    {
        macroControlKnobs[(size_t)i].setTooltip(processor.getMacroName(i));
        macroControlKnobs[(size_t)i].setValue(processor.getMacroValue(i), juce::dontSendNotification);
    }
    masterOutputKnob.setValue(processor.getMasterVolumeDb(), juce::dontSendNotification);
    scheduleLayoutUpdate();
}

void PluginEditor::syncSlotHighlights()
{
    int active = multiPanel ? multiPanel->getActiveSlotIndex() : -1;
    for (auto* slot : slots)
    {
        bool ap = (slot->getSlotIndex() == active);
        if (slot->isActiveParallel != ap) { slot->isActiveParallel = ap; slot->repaint(); }
    }
}

void PluginEditor::performLayoutUpdate()
{
    bool hasAny = false;
    for (int i = 0; i < 8; ++i)
        if (processor.pluginSlots[i].isValid &&
            processor.pluginSlots[i].type == PluginProcessor::SlotType::ParallelSplit &&
            processor.pluginSlots[i].parallelProcessor)
        { hasAny = true; break; }

    if (!hasAny)
    {
        multiPanel.reset();
        parallelPanelsCollapsed = false;
        parallelCollapseBtn.setVisible(true);
        syncSlotHighlights();
        updateResizeLimits(false);
        auto targetSize = getSizeForScale(false, zoomLevel);
        if (getWidth() != targetSize.getWidth() || getHeight() != targetSize.getHeight())
            setSize(targetSize.getWidth(), targetSize.getHeight());
        else
            resized();
    }
    else
    {
        if (!multiPanel) { multiPanel = std::make_unique<MultiParallelPanel>(); addAndMakeVisible(*multiPanel); }
        multiPanel->rebuild(processor);
        multiPanel->setVisible(!parallelPanelsCollapsed);
        parallelCollapseBtn.setVisible(true);
        parallelCollapseBtn.setButtonText("MACROS");
        // Current zoomLevel to all ParallelSplitComponents
        // BEFORE resized() calculates the layout — this way all values
        // in resized/paint of the panel use the correct zoom.
        for (int i = 0; i < 8; ++i)
        {
            auto& slot = processor.pluginSlots[i];
            if (!slot.isValid || slot.type != PluginProcessor::SlotType::ParallelSplit
                || !slot.parallelProcessor) continue;
            for (int c = 0; c < multiPanel->getNumChildComponents(); ++c)
            {
                auto* psc = dynamic_cast<ParallelSplitComponent*>(multiPanel->getChildComponent(c));
                if (psc && psc->getProcessor() == slot.parallelProcessor)
                    psc->setZoomLevel(zoomLevel);
            }
        }
        syncSlotHighlights();
        if (!parallelPanelsCollapsed)
            syncAllParallelPanels();
        const bool showParallelPanels = !parallelPanelsCollapsed;
        updateResizeLimits(showParallelPanels);
        auto targetSize = getSizeForScale(showParallelPanels, zoomLevel);
        if (getWidth() != targetSize.getWidth() || getHeight() != targetSize.getHeight())
            setSize(targetSize.getWidth(), targetSize.getHeight());
        else
            resized();
    }
    repaint();
}

void PluginEditor::syncAllParallelPanels()
{
    if (!multiPanel) return;
    // Walk all ParallelSplitComponent panels and sync their UI from the processor data.
    // This restores fader positions, knob values and bypass states after a preset load.
    for (int i = 0; i < 8; ++i)
    {
        auto& slot = processor.pluginSlots[i];
        if (!slot.isValid || slot.type != PluginProcessor::SlotType::ParallelSplit
            || !slot.parallelProcessor) continue;

        // Find the corresponding ParallelSplitComponent inside the multiPanel
        for (int c = 0; c < multiPanel->getNumChildComponents(); ++c)
        {
            auto* psc = dynamic_cast<ParallelSplitComponent*>(multiPanel->getChildComponent(c));
            if (psc && psc->getProcessor() == slot.parallelProcessor)
            {
                psc->syncFromProcessor();
                break;
            }
        }
    }
}

bool PluginEditor::hasVisibleParallelPanels() const
{
    return multiPanel != nullptr && multiPanel->isVisible();
}

void PluginEditor::toggleParallelPanelVisibility(int masterSlotIndex)
{
    if (!multiPanel)
        return;

    const bool sameSlot = multiPanel->getActiveSlotIndex() == masterSlotIndex;
    if (!sameSlot)
    {
        multiPanel->setActiveSlotIndex(masterSlotIndex);
        parallelPanelsCollapsed = false;
    }
    else
    {
        parallelPanelsCollapsed = !parallelPanelsCollapsed;
    }

    syncSlotHighlights();
    scheduleLayoutUpdate();
    repaint();
}

int PluginEditor::getDisplayedParallelChainCount() const
{
    if (multiPanel)
        return juce::jlimit(1, 4, multiPanel->getActiveDisplayedChainCount());

    return 1;
}

int PluginEditor::getLeftBaseWidth() const
{
    return (macroLaneVisible ? MACRO_LANE_W : MACRO_COLLAPSED_LANE_W) + MASTER_CHAIN_W;
}

int PluginEditor::getBaseWidth(bool includeParallelPanels) const
{
    if (!includeParallelPanels)
        return getLeftBaseWidth();

    const int slotPad = 35;
    const int slotW   = MASTER_CHAIN_W - 2 * slotPad;
    const int displayedChains = getDisplayedParallelChainCount();
    const int colW = slotPad * 2 + slotW;
    const int panelW  = displayedChains * colW + (displayedChains - 1) * 2;
    return getLeftBaseWidth() + 10 + panelW;
}

int PluginEditor::getBaseHeight() const
{
    const int presetBarH   = 30;
    const int presetBarMrg = 4;
    const int topOffset    = presetBarH + presetBarMrg * 2;
    const int headerH      = 116;
    const int slotH        = 20;
    const int mrg          = 6;
    const int slotsBottom  = topOffset + headerH + 10 + 8 * slotH + 7 * mrg;
    const int masterKnobSize = commonGuiKnobSize(1.0f);
    const int macroPadX = 5;
    const int macroColGap = 5;
    const int macroCellW = juce::jmax(1, (MACRO_LANE_W - macroPadX * 2 - macroColGap) / 2);
    const int macroTopLabelH = 11;
    const int macroBottomLabelH = 10;
    const int macroTextGap = 2;
    const int macroRowGap = 4;
    const int macroGridTop = topOffset + headerH + 10;
    const int macroSize = juce::jmax(12, juce::jmin(masterKnobSize, macroCellW));
    const int macroRows = (PluginProcessor::macroControlCount + 1) / 2;
    const int macrosBtnH = slotH;
    const int compactGap = 3;
    const int footerTop = slotsBottom + mrg + macrosBtnH + compactGap;
    const int outputKnobTop = juce::jmax(8, juce::roundToInt(78.0f * 0.16f + 1.0f));
    const int masterOutputLower = 4;
    const int outputKnobY = footerTop + outputKnobTop + masterOutputLower;
    const int minMacroRowH = macroTopLabelH + macroTextGap + macroSize
                           + macroBottomLabelH + macroTextGap + macroRowGap;
    const int macroRowH = macroRows > 1
                        ? juce::jmax(macroSize + macroBottomLabelH + macroTextGap,
                                     (outputKnobY - macroGridTop) / (macroRows - 1))
                        : minMacroRowH;
    const int macroRowsBottom = macroGridTop + (macroRows - 1) * macroRowH
                              + macroSize + macroBottomLabelH + macroTextGap;
    const int outputNameH = juce::roundToInt(78.0f * 0.13f);
    const int outputDbH = juce::jmax(15, juce::roundToInt(78.0f * 0.16f));
    const int footerH = outputKnobTop + masterOutputLower + masterKnobSize + outputNameH + outputDbH + 12;
    const int bottomMrg = 12;
    return juce::jmax(footerTop + footerH, macroRowsBottom) + bottomMrg;
}

juce::Rectangle<int> PluginEditor::getSizeForScale(bool includeParallelPanels, float scale) const
{
    const float clampedScale = juce::jlimit(MIN_ZOOM, MAX_ZOOM, scale);
    return { 0, 0,
             juce::roundToInt((float)getBaseWidth(includeParallelPanels) * clampedScale),
             juce::roundToInt((float)getBaseHeight() * clampedScale) };
}

void PluginEditor::updateResizeLimits(bool includeParallelPanels)
{
    auto fixedSize = getSizeForScale(includeParallelPanels, zoomLevel);
    setResizeLimits(fixedSize.getWidth(), fixedSize.getHeight(),
                    fixedSize.getWidth(), fixedSize.getHeight());

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio((double)getBaseWidth(includeParallelPanels)
                                         / (double)getBaseHeight());
}

void PluginEditor::setZoomLevel(float z)
{
    const float clamped = snapToMenuZoom(juce::jlimit(MIN_ZOOM, MAX_ZOOM, z));
    setZoomLevelRaw(clamped);
}

void PluginEditor::setZoomLevelRaw(float z)
{
    const float clamped = juce::jlimit(MIN_ZOOM, MAX_ZOOM, z);
    zoomLevel = clamped;
    processor.setEditorZoomScale(zoomLevel);

    if (multiPanel)
        for (int c = 0; c < multiPanel->getNumChildComponents(); ++c)
            if (auto* psc = dynamic_cast<ParallelSplitComponent*>(multiPanel->getChildComponent(c)))
            {
                psc->rebuildChainComponents();
                psc->setZoomLevel(zoomLevel);
            }

    updateResizeLimits(hasVisibleParallelPanels());
    auto targetSize = getSizeForScale(hasVisibleParallelPanels(), zoomLevel);
    setSize(targetSize.getWidth(), targetSize.getHeight());
    resized();
    repaint();
}

void PluginEditor::refreshSkinColours()
{
    const auto& skin = hostr::currentSkin();

    if (presetBar)
        presetBar->refreshSkinColours();

    presetNameLabel.setColour(juce::Label::textColourId, skin.text.withAlpha(0.65f));
    macroAssignBtn.setColour(juce::TextButton::buttonColourId, skin.warning);
    macroAssignBtn.setColour(juce::TextButton::buttonOnColourId, skin.warning.brighter(0.18f));
    macroClearBtn.setColour(juce::TextButton::buttonColourId, skin.danger.darker(0.10f));
    macroClearBtn.setColour(juce::TextButton::buttonOnColourId, skin.danger.brighter(0.10f));
    repaint();
    refreshAllSlots();
    syncAllParallelPanels();
}

void PluginEditor::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("PLUGINS");
    menu.addItem(1, "SCAN FOR NEW PLUGINS");
    menu.addItem(2, "FULL RESCAN (CLEAR CACHE)");
    menu.addItem(4, "SCAN SPECIFIC FOLDER...");
    menu.addSeparator();
    menu.addItem(3, "RESET ALL SLOTS");
    menu.addSeparator();
    menu.addSectionHeader("ZOOM");

    const float currentZoom = snapToMenuZoom(processor.getEditorZoomScale());
    menu.addItem(10, "75%",  true, std::abs(currentZoom - 0.75f) < 0.01f);
    menu.addItem(11, "100%", true, std::abs(currentZoom - 1.0f)  < 0.01f);
    menu.addItem(12, "125%", true, std::abs(currentZoom - 1.25f) < 0.01f);
    menu.addItem(13, "150%", true, std::abs(currentZoom - 1.5f)  < 0.01f);
    menu.addItem(14, "200%", true, std::abs(currentZoom - 2.0f)  < 0.01f);
    menu.addSeparator();
    menu.addSectionHeader("SKIN");

    const int currentSkin = processor.getEditorSkinIndex();
    const auto& skins = hostr::availableSkins();
    for (int i = 0; i < (int)skins.size(); ++i)
        menu.addItem(20 + i, juce::String(skins[(size_t)i].menuName).toUpperCase(), true, currentSkin == i);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetBar.get()), [this](int r)
    {
        auto triggerScan = [this](bool clearCache)
        {
            processor.startScanning(clearCache);
            scanOverlay.startMonitoring();
        };

        if      (r == 1)  triggerScan(false);
        else if (r == 2)  triggerScan(true);
        else if (r == 4)
        {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Choose a plugin folder to scan",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*");
            chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories
                                 | juce::FileBrowserComponent::canSelectFiles,
                                 [this, chooser](const juce::FileChooser& fc)
                                 {
                                     const auto folder = fc.getResult();
                                     if (folder.exists())
                                     {
                                         processor.startScanningFolder(folder);
                                         scanOverlay.startMonitoring();
                                     }
                                 });
        }
        else if (r == 3)
        {
            prepareForParallelSplitRemoval();
            for (int i = 0; i < 8; ++i) processor.removePlugin(i);
            for (auto* s : slots) s->updateState();
        }
        else if (r == 10) setZoomLevel(0.75f);
        else if (r == 11) setZoomLevel(1.0f);
        else if (r == 12) setZoomLevel(1.25f);
        else if (r == 13) setZoomLevel(1.5f);
        else if (r == 14) setZoomLevel(2.0f);
        else if (r >= 20 && r < 20 + (int)hostr::availableSkins().size())
        {
            processor.setEditorSkinIndex(r - 20);
            refreshSkinColours();
        }
    });
    if (processor.isScanning() && !scanOverlay.isVisible()) scanOverlay.startMonitoring();
}

void PluginEditor::paint(juce::Graphics& g)
{
    const auto& skin = hostr::currentSkin();
    const int macroLaneW = juce::roundToInt((float)(macroLaneVisible ? MACRO_LANE_W : MACRO_COLLAPSED_LANE_W) * zoomLevel);
    const int masterX = macroLaneW;
    const int masterW = (int)(MASTER_CHAIN_W * zoomLevel);
    const int leftCX = masterX + masterW / 2;
    const int presetBarH   = (int)(30 * zoomLevel);
    const int presetBarMrg = (int)(4  * zoomLevel);
    const int topOffset    = presetBarH + presetBarMrg * 2;
    const int headerH      = (int)(116 * zoomLevel);
    const int slotH        = (int)(20 * zoomLevel);
    const int slotGap      = (int)(6 * zoomLevel);
    const int parallelHeaderH = (int)(116 * zoomLevel);
    const int slotsTop     = topOffset + parallelHeaderH + (int)(10 * zoomLevel);
    const auto macroLabelColour = skin.text.withAlpha(0.62f);

    g.fillAll(skin.background);
    const bool hasSurface = hostr::paintSkinSurface(g, getLocalBounds().toFloat(), skin);
    hostr::paintTextureOverlay(g, getLocalBounds().toFloat(), skin, hasSurface ? 0.10f : 0.35f);

    // ========================================================================
    // MASTER BYPASS BUTTON & PARALLEL LANES COLLAPSE BUTTON
    // ========================================================================
    if (masterBypassBtn.isVisible())
    {
        auto buttonBounds = masterBypassBtn.getBounds().toFloat();
        const bool bypassed = masterBypassBtn.getToggleState();
        hostr::paintLedButton(g, buttonBounds, skin,
                              bypassed ? "master-button-off.png" : "master-button-on.png",
                              "BYPASS",
                              macroLabelColour,
                              false);
    }

    if (parallelCollapseBtn.isVisible())
    {
        const auto bounds = parallelCollapseBtn.getBounds().toFloat();
        const bool collapsed = !macroLaneVisible;
        hostr::paintLedButton(g, bounds, skin,
                              collapsed ? "macros-button-off.png" : "macros-button-on.png",
                              "MACROS",
                              macroLabelColour,
                              false);
    }

    const float headerWf = (float) masterW;
    const float headerHf = (float) headerH;
    const float knobSize = (float)commonGuiKnobSize(zoomLevel);
    const float knobX    = (float)masterX + (headerWf - knobSize) / 2.0f;
    const float labelReferenceH = juce::jmax(54.0f, 78.0f * zoomLevel);
    const float knobY    = headerHf * 0.36f;
    const float knobMidY = topOffset + knobY + knobSize * 0.5f;
    const float controlLabelFontSize = juce::jmax(8.0f, 10.2f * zoomLevel);
    const int rangeLabelW = juce::jmax(34, juce::roundToInt(34.0f * zoomLevel));
    const int inputRangeLabelH = juce::jmax(12, juce::roundToInt(labelReferenceH * 0.15f));
    const int inputRangeLabelY = juce::roundToInt(knobMidY - (float)inputRangeLabelH * 0.5f);

    g.setFont(juce::jmax(8.0f, labelReferenceH * 0.15f));
    g.setColour(skin.mutedText);
    const float rangeGap = 1.5f * zoomLevel;
    g.drawText("-inf",
               juce::roundToInt(knobX - (float)rangeLabelW - rangeGap), inputRangeLabelY,
               rangeLabelW, inputRangeLabelH,
               juce::Justification::right);
    g.drawText("0",
               leftCX - (int)(knobSize * 0.15f),
               topOffset + juce::roundToInt(knobY - labelReferenceH * 0.12f),
               (int)(knobSize * 0.30f), juce::roundToInt(labelReferenceH * 0.13f),
               juce::Justification::centred);
    g.drawText("24",
               juce::roundToInt(knobX + knobSize + rangeGap), inputRangeLabelY,
               rangeLabelW, inputRangeLabelH,
               juce::Justification::left);
    g.setFont(juce::Font(juce::FontOptions(controlLabelFontSize)));
    const int inputNameY = juce::roundToInt(topOffset + knobY + knobSize + 3.0f * zoomLevel);
    const int inputDbY = inputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    const int inputDbH = juce::jmax(14, juce::roundToInt(labelReferenceH * 0.13f));
    g.drawText("INPUT GAIN",
               (int)(knobX - headerWf * 0.12f), inputNameY,
               (int)(knobSize + headerWf * 0.24f), juce::roundToInt(labelReferenceH * 0.13f),
               juce::Justification::centred);
    const auto inputGainDb = clampKnobDb(processor.inputGain.load());
    g.setFont(juce::jmax(7.0f, labelReferenceH * 0.105f));
    g.setColour(skin.mutedText.withAlpha(0.78f));
    g.drawText(formatKnobDb(inputGainDb),
               masterX, inputDbY,
               masterW, inputDbH,
               juce::Justification::centred);

    const int slotsBottom_p = slotsTop + 8 * slotH + 7 * slotGap;
    auto mappedParameterName = [this](int macroIndex) -> juce::String
    {
        for (const auto& mapping : processor.getMacroMappings())
            if (mapping.enabled && mapping.macroIndex == macroIndex && mapping.parameterName.isNotEmpty())
                return cleanMacroParameterName(mapping.parameterName);

        return {};
    };

    if (macroLaneVisible)
    {
        const auto labelY = masterBypassBtn.getY();
        const auto labelH = masterBypassBtn.getHeight();
        g.setColour(macroLabelColour);
        g.setFont(juce::Font(juce::FontOptions(juce::jmax(8.0f, (float)labelH * 0.48f)).withStyle("Bold")));
        g.drawFittedText("EDIT MACROS", 0, labelY,
                         macroLaneW, labelH,
                         juce::Justification::centred, 1, 0.76f);

        auto drawMacroModeButton = [&](const PaintlessTextButton& button,
                                       juce::Colour colour,
                                       bool active,
                                       const juce::String& text,
                                       juce::Colour textColour)
        {
            if (!button.isVisible())
                return;

            const auto bounds = button.getBounds().toFloat();
            juce::ignoreUnused(colour);
            hostr::paintLedButton(g, bounds, skin,
                                  text == "+" ? "macro-plus.png" : "macro-minus.png",
                                  text,
                                  textColour,
                                  false);
        };

        drawMacroModeButton(macroAssignBtn,
                            macroAssignmentMode ? skin.warning.brighter(0.18f) : skin.warning,
                            macroAssignmentMode, "+", juce::Colours::black.withAlpha(0.72f));
        drawMacroModeButton(macroClearBtn,
                            macroClearMode ? skin.danger.brighter(0.10f) : skin.danger.darker(0.10f),
                            macroClearMode, "-", skin.text.withAlpha(0.92f));

        for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
        {
            const auto b = macroControlKnobs[(size_t)i].getBounds();
            const auto cell = macroAssignHotspots[(size_t)i].getBounds();
            const auto paramName = mappedParameterName(i);
            const auto label = paramName.isNotEmpty() ? paramName : juce::String("-");

            g.setFont(juce::Font(juce::FontOptions(juce::jmax(7.5f, controlLabelFontSize - 1.8f))));
            g.setColour(skin.text.withAlpha(paramName.isNotEmpty() ? 0.55f : 0.28f));
            g.drawFittedText(label, cell.getX(), cell.getY(),
                             cell.getWidth(), juce::jmax(1, b.getY() - cell.getY()),
                             juce::Justification::centred, 1, 0.72f);

            g.setFont(juce::Font(juce::FontOptions(juce::jmax(7.5f, controlLabelFontSize - 1.8f)).withStyle("Bold")));
            g.setColour(skin.text.withAlpha(0.66f));
            const int macroLabelW = juce::jmax(16, juce::roundToInt((float)b.getWidth() * 0.45f));
            const int macroLabelH = juce::jmax(8, juce::roundToInt(controlLabelFontSize + 1.0f));
            const int macroLabelX = juce::jmax(cell.getX(), b.getX() - juce::roundToInt(2.0f * zoomLevel));
            const int macroLabelY = juce::jmin(cell.getBottom() - macroLabelH,
                                               b.getBottom() + juce::roundToInt(1.0f * zoomLevel));
            g.drawText("M" + juce::String(i + 1),
                       macroLabelX, macroLabelY,
                       macroLabelW, macroLabelH,
                       juce::Justification::centredLeft, false);
        }
    }

    const int macrosBtnH_p  = masterBypassBtn.getHeight();
    const int meterGap      = juce::jmax(2, juce::roundToInt(3.0f * zoomLevel));
    const int footerTop     = parallelCollapseBtn.isVisible()
                            ? parallelCollapseBtn.getBottom() + meterGap
                            : slotsBottom_p + meterGap + macrosBtnH_p + meterGap;
    const float outputKnobSize = (float)commonGuiKnobSize(zoomLevel);
    const float outputKnobX = (float)leftCX - outputKnobSize * 0.5f;
    const int outputDbH = inputDbH;
    const float masterOutputLower = juce::roundToInt(4.0f * zoomLevel);
    const float outputKnobY = (float)footerTop
                            + juce::jmax(8.0f * zoomLevel, labelReferenceH * 0.16f + 1.0f * zoomLevel)
                            + masterOutputLower;
    const float outputMidY  = outputKnobY + outputKnobSize * 0.5f;
    const int outputRangeLabelH = inputRangeLabelH;
    const int outputRangeLabelY = juce::roundToInt(outputMidY - (float)outputRangeLabelH * 0.5f);

    g.setFont(juce::jmax(8.0f, labelReferenceH * 0.15f));
    g.setColour(skin.mutedText);
    g.drawText("-inf",
               juce::roundToInt(outputKnobX - (float)rangeLabelW - rangeGap), outputRangeLabelY,
               rangeLabelW, outputRangeLabelH, juce::Justification::right);
    g.drawText("0", leftCX - (int)(outputKnobSize * 0.15f),
               juce::jmax(footerTop, juce::roundToInt(outputKnobY - labelReferenceH * 0.12f)),
               (int)(outputKnobSize * 0.30f), juce::roundToInt(labelReferenceH * 0.13f), juce::Justification::centred);
    g.drawText("24",
               juce::roundToInt(outputKnobX + outputKnobSize + rangeGap), outputRangeLabelY,
               rangeLabelW, outputRangeLabelH, juce::Justification::left);
    g.setFont(juce::Font(juce::FontOptions(controlLabelFontSize)));
    const int outputNameY = juce::roundToInt(outputKnobY + outputKnobSize + 3.0f * zoomLevel);
    const int outputDbY = outputNameY + juce::roundToInt(labelReferenceH * 0.13f);
    g.drawText("OUTPUT",
               masterX, outputNameY,
               masterW, juce::roundToInt(labelReferenceH * 0.13f),
               juce::Justification::centred);
    const auto masterOutputDb = processor.getMasterVolumeDb();
    g.setFont(juce::jmax(7.0f, labelReferenceH * 0.105f));
    g.setColour(skin.mutedText.withAlpha(0.78f));
    g.drawText(formatKnobDb(masterOutputDb),
               masterX, outputDbY,
               masterW, outputDbH,
               juce::Justification::centred);

    juce::ignoreUnused(outputMidY);

}

void PluginEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;

    const int leftW  = juce::roundToInt((float)getLeftBaseWidth() * zoomLevel);
    const int macroLaneW = juce::roundToInt((float)(macroLaneVisible ? MACRO_LANE_W : MACRO_COLLAPSED_LANE_W) * zoomLevel);
    const int masterX = macroLaneW;
    const int masterW = (int)(MASTER_CHAIN_W * zoomLevel);
    const int leftCX = masterX + masterW / 2;

    // Preset bar takes up full width: [MENU][name][Save][Load][▼]
    const int presetBarH   = (int)(30 * zoomLevel);
    const int presetBarMrg = (int)(4  * zoomLevel);
    const int visualPresetBarH = juce::jmax(16, (int)(20 * zoomLevel));
    const int visualPresetBarY = juce::jmax(1,  (int)(2  * zoomLevel));
    if (presetBar)
    {
        presetBar->setBounds(presetBarMrg, visualPresetBarY,
                             juce::jmax(10, getWidth() - presetBarMrg * 2), visualPresetBarH);
        presetBar->toFront(false);
    }

    // Vertical offset for everything below the preset bar
    const int topOffset = presetBarH + presetBarMrg * 2;
    const int headerH   = (int)(116 * zoomLevel);

    const int headerControlSize = juce::jmax(16, juce::roundToInt(juce::jmin((float)masterW * 0.24f,
                                                                              (float)headerH * 0.22f)));
    const int headerControlY = topOffset + juce::jmax(2, juce::roundToInt(3.0f * zoomLevel));
    const int masterButtonW = juce::jmax(48, juce::roundToInt(56.7f * zoomLevel));
    const int masterButtonH = juce::jmax(15, juce::roundToInt(21.0f * zoomLevel));
    masterBypassBtn.setBounds(masterX + (masterW - masterButtonW) / 2,
                              headerControlY,
                              masterButtonW, masterButtonH);
    parallelCollapseBtn.setBounds(0, 0, 0, 0);

    presetNameLabel.setBounds(0, 0, 0, 0);

    if (processor.presetManager)
    {
        const auto& n = processor.presetManager->getCurrentPresetName();
        presetNameLabel.setText(n.isEmpty() ? "(unsaved)" : n, juce::dontSendNotification);
    }

    const int masterKnobSize = commonGuiKnobSize(zoomLevel);
    const int masterKnobX    = leftCX - masterKnobSize / 2;
    const int masterKnobY    = topOffset + (int)(headerH * 0.36f);
    macroKnob.setBounds(masterKnobX, masterKnobY, masterKnobSize, masterKnobSize);

    const int slotPad = (int)(35 * zoomLevel);
    const int slotW   = masterW - 2 * slotPad;
    const int slotH   = (int)(20  * zoomLevel);
    const int mrg     = (int)(6  * zoomLevel);
    int y = topOffset + headerH + (int)(10 * zoomLevel);

    for (int i = 0; i < 8; ++i)
    {
        slots[i]->setBounds(masterX + slotPad, y, slotW, slotH);
        y += slotH + mrg;
    }
    const int slotsBottom = y - mrg;

    const int macrosBtnH = masterButtonH;
    const int compactGap = juce::jmax(2, juce::roundToInt(3.0f * zoomLevel));

    const int assignBtnSize = juce::jmax(headerControlSize + 1,
                                         juce::roundToInt((float)headerControlSize * 1.25f));
    const int macroBtnGap = juce::jmax(2, juce::roundToInt(4.0f * zoomLevel));
    const int macroButtonsW = assignBtnSize * 2 + macroBtnGap;
    const int macroBtnX = juce::jmax(2, (macroLaneW - macroButtonsW) / 2);
    const int macroBtnY = topOffset + juce::roundToInt((float)headerH * 0.18f);
    macroAssignBtn.setBounds(macroBtnX, macroBtnY, assignBtnSize, assignBtnSize);
    macroClearBtn.setBounds(macroAssignBtn.getRight() + macroBtnGap,
                            macroBtnY,
                            assignBtnSize, assignBtnSize);

    const int macroPadX = juce::jmax(3, juce::roundToInt(5.0f * zoomLevel));
    const int macroColGap = juce::jmax(3, juce::roundToInt(5.0f * zoomLevel));
    const int macroSlotsTop = slots[0]->getY();
    const int macroCellW = juce::jmax(1, (macroLaneW - macroPadX * 2 - macroColGap) / 2);
    const int macroTopLabelH = juce::jmax(9, juce::roundToInt(11.0f * zoomLevel));
    const int macroBottomLabelH = juce::jmax(8, juce::roundToInt(10.0f * zoomLevel));
    const int macroTextGap = juce::jmax(1, juce::roundToInt(2.0f * zoomLevel));
    const int macroRowGap = juce::jmax(3, juce::roundToInt(4.0f * zoomLevel));
    const int macroSize = juce::jmax(12, juce::jmin(masterKnobSize, macroCellW));
    const int macroRows = (PluginProcessor::macroControlCount + 1) / 2;
    const int minMacroRowH = macroTopLabelH + macroTextGap + macroSize
                           + macroBottomLabelH + macroTextGap + macroRowGap;
    const int slotChainH = juce::jmax(1, slotsBottom - macroSlotsTop);
    const int centredMacroRowH = macroRows > 1
                               ? juce::jmax(minMacroRowH,
                                            (slotChainH - macroSize) / (macroRows - 1))
                               : minMacroRowH;
    const int macroGridH = macroRows > 1
                         ? (macroRows - 1) * centredMacroRowH + macroSize
                         : macroSize;
    const int macroGridTop = macroSlotsTop + slotChainH / 2 - macroGridH / 2;
    const int macroRowH = centredMacroRowH;
    int macroRowsBottom = macroGridTop;
    for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
    {
        const int row = i / 2;
        const int col = i % 2;
        const int cellX = macroPadX + col * (macroCellW + macroColGap);
        const int knobY = macroGridTop + row * macroRowH;
        const int cellY = knobY - macroTopLabelH - macroTextGap;
        const auto cell = juce::Rectangle<int>(cellX, cellY, macroCellW,
                                               macroRowH + macroTopLabelH + macroTextGap);
        const auto bounds = juce::Rectangle<int>(cell.getCentreX() - macroSize / 2,
                                                 knobY,
                                                 macroSize, macroSize);
        macroControlKnobs[(size_t)i].setBounds(bounds);
        macroAssignHotspots[(size_t)i].setBounds(cell);
        macroAssignHotspots[(size_t)i].toFront(false);
        macroRowsBottom = juce::jmax(macroRowsBottom, bounds.getBottom() + macroBottomLabelH + macroTextGap);
    }

    juce::ignoreUnused(macroRowsBottom);

    const int macrosBtnW = masterButtonW;
    const int macrosBtnY = slotsBottom + mrg;
    parallelCollapseBtn.setBounds(masterX + (masterW - macrosBtnW) / 2,
                                  macrosBtnY,
                                  macrosBtnW, macrosBtnH);
    parallelCollapseBtn.toFront(false);

    const int footerTop  = juce::jmax(slotsBottom + compactGap, macrosBtnY + macrosBtnH + compactGap);
    const int outputKnobSize = commonGuiKnobSize(zoomLevel);
    const int outputKnobX    = leftCX - outputKnobSize / 2;
    const int outputKnobTop  = juce::jmax(juce::roundToInt(8.0f * zoomLevel),
                                          juce::roundToInt(78.0f * zoomLevel * 0.16f + 1.0f * zoomLevel));
    const int masterOutputLower = juce::roundToInt(4.0f * zoomLevel);
    const float footerBottomPad = juce::jmax(8.0f * zoomLevel, 12.0f * zoomLevel);
    const float outputFooterContentH = (float)outputKnobSize
                                     + 78.0f * zoomLevel * 0.16f
                                     + (float)juce::jmax(15, juce::roundToInt(78.0f * zoomLevel * 0.16f));
    const int footerH    = outputKnobTop + masterOutputLower + juce::roundToInt(outputFooterContentH)
                         + juce::roundToInt(footerBottomPad);
    const int outputKnobY    = footerTop + outputKnobTop + masterOutputLower;
    masterOutputKnob.setBounds(outputKnobX, outputKnobY, outputKnobSize, outputKnobSize);
    masterMeterOverlay.setBounds(0, footerTop + masterOutputLower, leftW, juce::jmax(1, footerH - masterOutputLower));
    masterMeterOverlay.setGeometry(zoomLevel, leftCX, masterW, outputKnobSize);
    masterMeterOverlay.toBack();
    masterOutputKnob.toFront(false);

    masterSplitArrowButtons.clear();

    if (multiPanel && multiPanel->isVisible())
    {
        const int pad = mrg;
        const int panelTop = 0;
        const int sepW_mp  = 2;
        const int displayedChains = getDisplayedParallelChainCount();
        const int colW     = slotPad * 2 + slotW;
        const int panelRightMargin = juce::jmax(8, juce::roundToInt(10.0f * zoomLevel));
        const int panelW   = displayedChains * colW + (displayedChains - 1) * sepW_mp + panelRightMargin;

        for (int i = 0; i < 8; ++i)
        {
            auto& slot = processor.pluginSlots[i];
            if (!slot.isValid || slot.type != PluginProcessor::SlotType::ParallelSplit
                || !slot.parallelProcessor) continue;
            for (int c = 0; c < multiPanel->getNumChildComponents(); ++c)
            {
                auto* psc = dynamic_cast<ParallelSplitComponent*>(multiPanel->getChildComponent(c));
                if (psc && psc->getProcessor() == slot.parallelProcessor)
                {
                    psc->setZoomLevel(zoomLevel);
                    psc->setOutputFooterTop(footerTop + masterOutputLower);
                }
            }
        }

        multiPanel->setTabBarHeight(visualPresetBarH);
        multiPanel->setScrollBarZoom(zoomLevel);
        multiPanel->setContentTopOffset(topOffset);
        multiPanel->setBounds(leftW + pad, panelTop,
                              juce::jmax(10, panelW),
                              juce::jmax(10, getHeight() - panelTop - pad));

        const float childHeaderH = juce::jmax(78.0f, 116.0f * zoomLevel);
        const float childHeaderGap = 10.0f * zoomLevel;
        const float childSlotPad = 35.0f * zoomLevel;
        const float childSlotH = 20.0f * zoomLevel;
        const int splitScroll = multiPanel->getHorizontalScrollOffset();
        const float endX = (float)multiPanel->getX()
                         - (float)splitScroll
                         + childSlotPad;
        const float visibleEndX = splitScroll == 0 ? endX
                                                   : (float)multiPanel->getX();
        const float endY = (float)multiPanel->getY() + (float)topOffset
                         + childHeaderH + childHeaderGap + childSlotH * 0.5f;

        for (int i = 0; i < 8; ++i)
        {
            if (!processor.pluginSlots[i].isValid
                || processor.pluginSlots[i].type != PluginProcessor::SlotType::ParallelSplit)
                continue;

            auto arrow = std::make_unique<SplitArrowButton>();
            arrow->onClick = [this, i]()
            {
                if (multiPanel != nullptr)
                    multiPanel->setActiveSlotIndex(i);
                syncSlotHighlights();
                scheduleLayoutUpdate();
                repaint();
            };

            const auto source = slots[i]->getBounds().toFloat();
            arrow->setArrow({ source.getRight(), source.getCentreY() },
                            { visibleEndX, endY },
                            zoomLevel,
                            ParallelSplitComponent::getSplitGroupColour(0),
                            i == multiPanel->getActiveSlotIndex(),
                            splitScroll == 0);
            addAndMakeVisible(*arrow);
            arrow->toFront(false);
            masterSplitArrowButtons.push_back(std::move(arrow));
        }
    }

    scanOverlay.setBounds(getLocalBounds());
    scanOverlay.toFront(true);
    if (dragGhost) dragGhost->setBounds(getLocalBounds());

    const int cornerSize = 18;
    resizeCorner->setBounds(getWidth() - cornerSize, getHeight() - cornerSize, cornerSize, cornerSize);
    resizeCorner->toFront(false);
}

void PluginEditor::timerCallback()
{
    const bool macroLearnActive = macroLearnIndex >= 0 && !macroLearnCandidates.empty();

    if (++maintenanceTick >= 20)
    {
        maintenanceTick = 0;
        processor.removeInvalidMacroMappings();
    }

    if (macroLearnActive)
        pollMacroLearn();

    bool parallelOverload = false;
    for (int i = 0; i < 8; ++i)
    {
        const auto& slot = processor.pluginSlots[i];
        if (slot.isValid
            && slot.type == PluginProcessor::SlotType::ParallelSplit
            && slot.parallelProcessor != nullptr
            && slot.parallelProcessor->hasOutputOverload())
        {
            parallelOverload = true;
            break;
        }
    }

    juce::ignoreUnused(parallelOverload);
    if (processor.isScanning() && !scanOverlay.isVisible()) scanOverlay.startMonitoring();

    masterMeterOverlay.updateMeters();

    const auto setSliderIfChanged = [](juce::Slider& slider, double value)
    {
        if (!slider.isMouseButtonDown() && std::abs(slider.getValue() - value) > 0.0005)
            slider.setValue(value, juce::dontSendNotification);
    };

    setSliderIfChanged(macroKnob, clampKnobDb(processor.inputGain.load()));
    setSliderIfChanged(masterOutputKnob, processor.getMasterVolumeDb());

    for (int i = 0; i < PluginProcessor::macroControlCount; ++i)
        setSliderIfChanged(macroControlKnobs[(size_t)i], processor.getMacroValue(i));

    bool slotVisualStateChanged = false;
    for (int i = 0; i < 8; ++i)
    {
        const auto& slot = processor.pluginSlots[(size_t)i];
        const bool changed = lastSlotValidForUi[(size_t)i] != slot.isValid
                          || lastSlotBypassedForUi[(size_t)i] != slot.bypassed
                          || lastSlotTypeForUi[(size_t)i] != slot.type;

        if (!changed)
            continue;

        lastSlotValidForUi[(size_t)i] = slot.isValid;
        lastSlotBypassedForUi[(size_t)i] = slot.bypassed;
        lastSlotTypeForUi[(size_t)i] = slot.type;

        if (auto* slotComponent = slots[i])
            slotComponent->updateState();

        slotVisualStateChanged = true;
    }

    if (slotVisualStateChanged || macroLearnActive || macroAssignmentMode || macroClearMode)
        repaint();

    int pc = 0;
    for (int i = 0; i < 8; ++i)
        if (processor.pluginSlots[i].isValid &&
            processor.pluginSlots[i].type == PluginProcessor::SlotType::ParallelSplit &&
            processor.pluginSlots[i].parallelProcessor) ++pc;

    int mc = multiPanel ? multiPanel->getCount() : 0;
    if (pc != mc && !layoutUpdatePending) scheduleLayoutUpdate();
}
