#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"
#include <array>
#include <cmath>
#include <map>

namespace hostr
{
struct SkinPalette
{
    const char* menuName;
    juce::Colour background;
    juce::Colour panel;
    juce::Colour panelRaised;
    juce::Colour panelInset;
    juce::Colour separator;
    juce::Colour border;
    juce::Colour text;
    juce::Colour mutedText;
    juce::Colour accent;
    juce::Colour accentSoft;
    juce::Colour accentAlt;
    juce::Colour warning;
    juce::Colour danger;
    juce::Colour meter;
    juce::Colour meterPeak;
    juce::Colour knobFace;
    juce::Colour knobRing;
    const char* assetFolder = nullptr;
    bool textured = false;
    bool framedPanels = false;
    bool dimensionalControls = false;
    float wearAmount = 0.0f;
};

inline const std::array<SkinPalette, 7>& availableSkins()
{
    static const std::array<SkinPalette, 7> skins
    {{
        {
            "MODERN FLAT",
            juce::Colour(0xff17212a), juce::Colour(0xff17212a), juce::Colour(0xff26343f),
            juce::Colour(0xff11181f), juce::Colour(0xff344754), juce::Colour(0xff405764),
            juce::Colour(0xffeef6fb), juce::Colour(0xff9aaab4), juce::Colour(0xff28b7c9),
            juce::Colour(0xff6ed2df), juce::Colour(0xff2f8796), juce::Colour(0xffffc857),
            juce::Colour(0xffff6b6b), juce::Colour(0xff19e2c7), juce::Colour(0xffff5a52),
            juce::Colour(0xff1a5c69), juce::Colour(0xff405764), "modern-slate",
            false, false, false, 0.0f
        },
        {
            "BLACK FLAT",
            juce::Colour(0xff050607), juce::Colour(0xff050607), juce::Colour(0xff151719),
            juce::Colour(0xff020303), juce::Colour(0xff2c3033), juce::Colour(0xff565b60),
            juce::Colour(0xfff7f7f2), juce::Colour(0xffa8adb0), juce::Colour(0xfff2f2e8),
            juce::Colour(0xffffffff), juce::Colour(0xffc7c9c7), juce::Colour(0xffffc857),
            juce::Colour(0xffff6655), juce::Colour(0xff58df7f), juce::Colour(0xffff5148),
            juce::Colour(0xffd4d4d4), juce::Colour(0xff8c8c8c), "black-flat",
            false, false, false, 0.0f
        },
        {
            "WARM FLAT",
            juce::Colour(0xff12110f), juce::Colour(0xff12110f), juce::Colour(0xff262119),
            juce::Colour(0xff0d0c0a), juce::Colour(0xff4e4333), juce::Colour(0xff735c3b),
            juce::Colour(0xfff2ead8), juce::Colour(0xffb6a78c), juce::Colour(0xffffa928),
            juce::Colour(0xffffcf7a), juce::Colour(0xffd7892b), juce::Colour(0xffffc857),
            juce::Colour(0xffff6655), juce::Colour(0xff43dc83), juce::Colour(0xffff5c4d),
            juce::Colour(0xffc47f10), juce::Colour(0xff8b5508), "vintage-amber",
            false, false, false, 0.0f
        },
        {
            "DARK FLAT",
            juce::Colour(0xff111317), juce::Colour(0xff111317), juce::Colour(0xff232a31),
            juce::Colour(0xff0d1115), juce::Colour(0xff3c4650), juce::Colour(0xff6f7e8d),
            juce::Colour(0xffedf4fb), juce::Colour(0xffa2afbb), juce::Colour(0xff2ed8ff),
            juce::Colour(0xff7be9ff), juce::Colour(0xff2f73ff), juce::Colour(0xffffd166),
            juce::Colour(0xffff6b6b), juce::Colour(0xff50e38b), juce::Colour(0xffff5c5c),
            juce::Colour(0xff0f5fa8), juce::Colour(0xff3a7ec4), "rack-blue",
            false, false, false, 0.0f
        },
        {
            "METAL",
            juce::Colour(0xff101111), juce::Colour(0xff101111), juce::Colour(0xff1e1e1b),
            juce::Colour(0xff080909), juce::Colour(0xff252523), juce::Colour(0xff6f6a5e),
            juce::Colour(0xfff2f0ea), juce::Colour(0xffa9a59b), juce::Colour(0xff28e6f6),
            juce::Colour(0xff91f3ff), juce::Colour(0xff266bff), juce::Colour(0xffffa82a),
            juce::Colour(0xffff6655), juce::Colour(0xff56df75), juce::Colour(0xffff5148),
            juce::Colour(0xff191b1b), juce::Colour(0xff3a3935),
            "black-rack",
            true, true, true, 0.55f
        },
        {
            "500 BLUE",
            juce::Colour(0xff0d161b), juce::Colour(0xff102130), juce::Colour(0xff191815),
            juce::Colour(0xff090909), juce::Colour(0xff24323b), juce::Colour(0xff9a835f),
            juce::Colour(0xfff5f2df), juce::Colour(0xffc8c0a8), juce::Colour(0xff27dff4),
            juce::Colour(0xff90efff), juce::Colour(0xff1e84da), juce::Colour(0xffffad2f),
            juce::Colour(0xffff5a3e), juce::Colour(0xff61dd53), juce::Colour(0xffff4338),
            juce::Colour(0xff145c9c), juce::Colour(0xffc1b79b),
            "api-500",
            true, true, true, 0.82f
        },
        {
            "CONSOLE",
            juce::Colour(0xff2d3838), juce::Colour(0xff333f3f), juce::Colour(0xff1b1d1c),
            juce::Colour(0xff0d0f0f), juce::Colour(0xff4a5551), juce::Colour(0xff8b8a7e),
            juce::Colour(0xfff1eee2), juce::Colour(0xffc5c3b4), juce::Colour(0xff31dcea),
            juce::Colour(0xff9cf4f4), juce::Colour(0xff64208c), juce::Colour(0xffffa62b),
            juce::Colour(0xffe84f47), juce::Colour(0xff5ed36a), juce::Colour(0xffff4239),
            juce::Colour(0xff762522), juce::Colour(0xffb1ab9d),
            "neve-console",
            true, true, true, 0.68f
        }
    }};

    return skins;
}

inline int clampSkinIndex(int index)
{
    return juce::jlimit(0, (int)availableSkins().size() - 1, index);
}

inline int& activeSkinIndexStorage()
{
    static int current = 0;
    return current;
}

// =============================================================================
// Skin asset cache — decodes images lazily and serves them from cache on every
// paint. This avoids both per-paint PNG decode and loading every skin asset at
// startup when most of them may never be drawn.
// =============================================================================

namespace detail
{
    struct SkinAssetCache
    {
        int skinIndex = -1;
        juce::Image panelSurface;
        juce::Image knob;
        juce::Image knobSource;
        juce::Image masterOn;
        juce::Image masterOff;
        juce::Image masterStripOn;
        juce::Image masterStripOff;
        juce::Image masterButtonOn;
        juce::Image masterButtonOff;
        juce::Image chainPlus;
        juce::Image chainMinus;
        juce::Image chainSOn;
        juce::Image chainSOff;
        juce::Image stripOn;
        juce::Image stripOff;
        juce::Image macroPlus;
        juce::Image macroMinus;
        juce::Image macrosButtonOn;
        juce::Image macrosButtonOff;
        std::map<juce::String, bool> attemptedLoads;
        std::map<juce::String, juce::Image> scaledSurfaces;
        std::map<juce::String, juce::Image> scaledAssets;
    };

    inline SkinAssetCache& getSkinAssetCache()
    {
        static SkinAssetCache cache;
        return cache;
    }

    inline juce::Image& getFallbackSkinImage()
    {
        static juce::Image image;
        return image;
    }

    inline const juce::Image& getEmptySkinImage()
    {
        static juce::Image image;
        return image;
    }

    inline bool preferFastBitmapSkinPainting()
    {
       #if JUCE_WINDOWS
        return true;
       #else
        return false;
       #endif
    }

    inline juce::Graphics::ResamplingQuality bitmapSkinResamplingQuality()
    {
        return preferFastBitmapSkinPainting() ? juce::Graphics::mediumResamplingQuality
                                              : juce::Graphics::highResamplingQuality;
    }

    inline const juce::Image& getScaledImage(const juce::Image& source,
                                             const juce::String& keyPrefix,
                                             int width,
                                             int height)
    {
        if (!source.isValid() || width <= 0 || height <= 0)
            return getEmptySkinImage();

        auto& cache = getSkinAssetCache();
        const auto key = keyPrefix + ":" + juce::String(width) + "x" + juce::String(height);
        auto& scaledImages = keyPrefix == "panel" ? cache.scaledSurfaces : cache.scaledAssets;
        if (auto found = scaledImages.find(key); found != scaledImages.end())
            return found->second;

        if (scaledImages.size() > (keyPrefix == "panel" ? 24u : 96u))
            scaledImages.clear();

        juce::Image scaled(juce::Image::ARGB, width, height, true);
        juce::Graphics sg(scaled);
        sg.setImageResamplingQuality(bitmapSkinResamplingQuality());
        sg.drawImageWithin(source, 0, 0, width, height, juce::RectanglePlacement::stretchToFit);

        return scaledImages.emplace(key, std::move(scaled)).first->second;
    }
}

// =============================================================================
// Embedded resource lookup — maps a relative skin asset path to its index in
// BinaryData::namedResourceList.  This is used by loadEmbeddedSkinAssetImage.
// =============================================================================

inline int embeddedSkinResourceIndexForPath(const juce::String& relativePath)
{
    const auto normalised = relativePath.replaceCharacter('\\', '/');
    const auto folder = normalised.upToLastOccurrenceOf("/", false, false)
        .fromLastOccurrenceOf("/", false, false);
    const auto filename = normalised.fromLastOccurrenceOf("/", false, false);

    if (folder.isEmpty() || filename.isEmpty())
        return -1;

    static constexpr std::array<const char*, 16> commonSkinFiles
    {
        "master-button-off.png",
        "chain-minus.png",
        "master-strip-off.png",
        "macro-minus.png",
        "macro-plus.png",
        "master-button-on.png",
        "chain-plus.png",
        "chain-s-off.png",
        "strip-on.png",
        "master-on.png",
        "chain-s-on.png",
        "master-off.png",
        "macros-button-on.png",
        "macros-button-off.png",
        "strip-off.png",
        "master-strip-on.png"
    };

    auto indexInList = [&filename](const auto& files) -> int
    {
        for (size_t i = 0; i < files.size(); ++i)
            if (filename == files[i])
                return (int) i;
        return -1;
    };

    auto commonIndex = [&]() -> int
    {
        return indexInList(commonSkinFiles);
    };

    auto apiOrConsoleIndex = [&]() -> int
    {
        static constexpr std::array<const char*, 19> orderedFiles
        {
            "master-button-off.png",
            "chain-minus.png",
            "master-strip-off.png",
            "macro-minus.png",
            "macro-plus.png",
            "master-button-on.png",
            "chain-plus.png",
            "knob-source.png",
            "chain-s-off.png",
            "strip-on.png",
            "knob.png",
            "master-on.png",
            "chain-s-on.png",
            "master-off.png",
            "macros-button-on.png",
            "panel-surface.png",
            "macros-button-off.png",
            "strip-off.png",
            "master-strip-on.png"
        };

        return indexInList(orderedFiles);
    };

    auto blackRackIndex = [&]() -> int
    {
        static constexpr std::array<const char*, 19> orderedFiles
        {
            "panel-surface.png",
            "macros-button-off.png",
            "strip-off.png",
            "master-strip-on.png",
            "master-off.png",
            "macros-button-on.png",
            "chain-s-on.png",
            "master-on.png",
            "master-button-off.png",
            "chain-minus.png",
            "master-strip-off.png",
            "macro-minus.png",
            "macro-plus.png",
            "master-button-on.png",
            "chain-plus.png",
            "knob-source.png",
            "chain-s-off.png",
            "strip-on.png",
            "knob.png"
        };

        return indexInList(orderedFiles);
    };

    // All non-textured / flat skins share the same common embedded resources (first 16 entries).
    // This ensures they work on any PC, even when asset files aren't available on disk.
    if (folder == "modern-slate"
     || folder == "black-flat"
     || folder == "vintage-amber"
     || folder == "rack-blue")
        return commonIndex();
    if (folder == "studio-3d")
    {
        if (filename == "knob-source.png") return 48;
        if (filename == "knob.png") return 49;
        if (filename == "panel-surface.png") return 50;
        return -1;
    }
    if (folder == "api-500")
        return 67 + apiOrConsoleIndex();
    if (folder == "neve-console")
        return 86 + apiOrConsoleIndex();
    if (folder == "black-rack")
        return 105 + blackRackIndex();

    return -1;
}

inline juce::Image loadEmbeddedSkinAssetImage(const juce::String& relativePath)
{
    const auto resourceIndex = embeddedSkinResourceIndexForPath(relativePath);
    if (resourceIndex < 0 || resourceIndex >= BinaryData::namedResourceListSize)
        return {};

    const auto* resourceName = BinaryData::namedResourceList[(size_t) resourceIndex];
    int dataSize = 0;
    const auto* resourceData = BinaryData::getNamedResource(resourceName, dataSize);
    if (resourceData == nullptr || dataSize <= 0)
        return {};

    juce::MemoryInputStream stream(resourceData, static_cast<size_t> (dataSize), false);
    return juce::ImageFileFormat::loadFrom(stream);
}

inline juce::File findSkinAssetFile(const juce::String& relativePath)
{
    if (relativePath.isEmpty())
        return {};

    auto checkRoot = [&relativePath](juce::File root) -> juce::File
    {
        for (int i = 0; i < 8 && root.exists(); ++i)
        {
            auto candidate = root.getChildFile(relativePath);
            if (candidate.existsAsFile())
                return candidate;

            auto resourcesCandidate = root.getChildFile("Resources").getChildFile(relativePath);
            if (resourcesCandidate.existsAsFile())
                return resourcesCandidate;

            auto parent = root.getParentDirectory();
            if (parent == root)
                break;
            root = parent;
        }

        return {};
    };

    if (auto file = checkRoot(juce::File::getCurrentWorkingDirectory()); file.existsAsFile())
        return file;

    auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (auto file = checkRoot(executable.getParentDirectory()); file.existsAsFile())
        return file;

    auto developmentProject = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("TESI")
        .getChildFile("Hostr");
    if (auto file = checkRoot(developmentProject); file.existsAsFile())
        return file;

    return {};
}

inline void buildSkinAssetCache(const SkinPalette& skin)
{
    auto& cache = detail::getSkinAssetCache();
    cache = {};
    cache.skinIndex = skin.assetFolder == nullptr ? -1 : activeSkinIndexStorage();
}

// Map asset filename to the corresponding cached image member.
static inline juce::Image* lookupCachedAsset(const char* filename)
{
    if (filename == nullptr)
        return nullptr;

    auto& cache = detail::getSkinAssetCache();

    if (juce::String(filename) == "panel-surface.png")       return &cache.panelSurface;
    if (juce::String(filename) == "knob.png")                return &cache.knob;
    if (juce::String(filename) == "knob-source.png")         return &cache.knobSource;
    if (juce::String(filename) == "master-on.png")           return &cache.masterOn;
    if (juce::String(filename) == "master-off.png")          return &cache.masterOff;
    if (juce::String(filename) == "master-strip-on.png")     return &cache.masterStripOn;
    if (juce::String(filename) == "master-strip-off.png")    return &cache.masterStripOff;
    if (juce::String(filename) == "master-button-on.png")    return &cache.masterButtonOn;
    if (juce::String(filename) == "master-button-off.png")   return &cache.masterButtonOff;
    if (juce::String(filename) == "chain-plus.png")          return &cache.chainPlus;
    if (juce::String(filename) == "chain-minus.png")         return &cache.chainMinus;
    if (juce::String(filename) == "chain-s-on.png")          return &cache.chainSOn;
    if (juce::String(filename) == "chain-s-off.png")         return &cache.chainSOff;
    if (juce::String(filename) == "strip-on.png")            return &cache.stripOn;
    if (juce::String(filename) == "strip-off.png")           return &cache.stripOff;
    if (juce::String(filename) == "macro-plus.png")          return &cache.macroPlus;
    if (juce::String(filename) == "macro-minus.png")         return &cache.macroMinus;
    if (juce::String(filename) == "macros-button-on.png")    return &cache.macrosButtonOn;
    if (juce::String(filename) == "macros-button-off.png")   return &cache.macrosButtonOff;

    return nullptr;
}

inline juce::Image loadSkinAssetImage(const SkinPalette& skin, const char* filename)
{
    if (skin.assetFolder == nullptr || filename == nullptr)
        return {};

    const juce::String relative = "assets/skins/" + juce::String(skin.assetFolder) + "/" + filename;
    juce::Image image = loadEmbeddedSkinAssetImage(relative);

    if (!image.isValid())
    {
        auto file = findSkinAssetFile(relative);
        if (file.existsAsFile())
            image = juce::ImageFileFormat::loadFrom(file);
    }

    return image;
}

inline const juce::Image& getSkinAssetImage(const SkinPalette& skin, const char* filename)
{
    if (skin.assetFolder == nullptr || filename == nullptr)
        return detail::getEmptySkinImage();

    auto& cache = detail::getSkinAssetCache();
    if (cache.skinIndex != activeSkinIndexStorage())
        buildSkinAssetCache(skin);

    if (auto* cached = lookupCachedAsset(filename))
    {
        const juce::String key(filename);
        if (!cached->isValid() && !cache.attemptedLoads[key])
        {
            *cached = loadSkinAssetImage(skin, filename);
            cache.attemptedLoads[key] = true;
        }

        return *cached;
    }

    auto& fallback = detail::getFallbackSkinImage();
    fallback = loadSkinAssetImage(skin, filename);
    return fallback;
}

inline const juce::Image& getScaledSkinAssetImage(const SkinPalette& skin,
                                                  const char* filename,
                                                  int width,
                                                  int height)
{
    const auto& source = getSkinAssetImage(skin, filename);
    if (!source.isValid())
        return detail::getEmptySkinImage();

    return detail::getScaledImage(source, juce::String(filename), width, height);
}

// --- Must appear before hasBitmapSkinSurface which calls them ---
inline void setActiveSkinIndex(int index)
{
    activeSkinIndexStorage() = clampSkinIndex(index);
    buildSkinAssetCache(availableSkins()[(size_t)activeSkinIndexStorage()]);
}

inline int getActiveSkinIndex()
{
    return activeSkinIndexStorage();
}

inline const SkinPalette& currentSkin()
{
    return availableSkins()[(size_t)getActiveSkinIndex()];
}

inline bool hasBitmapSkinSurface(const SkinPalette& skin)
{
    // Check cache first (fast), fall back to loading if needed.
    auto& cache = detail::getSkinAssetCache();
    if (cache.skinIndex == getActiveSkinIndex() && cache.panelSurface.isValid())
        return true;
    return getSkinAssetImage(skin, "panel-surface.png").isValid();
}

inline bool paintSkinSurface(juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             const SkinPalette& skin,
                             float clipRadius = 0.0f)
{
    const auto& panelImage = getSkinAssetImage(skin, "panel-surface.png");
    if (!panelImage.isValid() || bounds.isEmpty())
        return false;

    g.saveState();
    if (clipRadius > 0.0f)
    {
        juce::Path clip;
        clip.addRoundedRectangle(bounds, clipRadius);
        g.reduceClipRegion(clip, juce::AffineTransform());
    }
    else
    {
        g.reduceClipRegion(bounds.toNearestInt());
    }
    const int x = juce::roundToInt(bounds.getX());
    const int y = juce::roundToInt(bounds.getY());
    const int width = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
    const int height = juce::jmax(1, juce::roundToInt(bounds.getHeight()));
    const auto& scaledImage = detail::getScaledImage(panelImage, "panel", width, height);
    if (scaledImage.isValid())
        g.drawImageAt(scaledImage, x, y);
    g.restoreState();
    return true;
}

inline void paintTextureOverlay(juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                const SkinPalette& skin,
                                float strength = 1.0f)
{
    if (bounds.isEmpty() || skin.wearAmount <= 0.0f)
        return;

    const float wear = juce::jlimit(0.0f, 1.0f, skin.wearAmount * strength);
    const int left = juce::roundToInt(bounds.getX());
    const int top = juce::roundToInt(bounds.getY());
    const int width = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
    const int height = juce::jmax(1, juce::roundToInt(bounds.getHeight()));

    const int scratchCount = detail::preferFastBitmapSkinPainting() ? 6 : 18;
    g.setColour(skin.text.withAlpha((detail::preferFastBitmapSkinPainting() ? 0.020f : 0.030f) * wear));
    for (int i = 0; i < scratchCount; ++i)
    {
        const int x = left + (i * 37 + width / 9) % width;
        const int y = top + (i * 53 + height / 7) % height;
        const float len = juce::jlimit(5.0f, 22.0f, (float)width * (0.015f + 0.003f * (float)(i % 5)));
        g.drawLine((float)x, (float)y,
                   (float)x + len,
                   (float)y + (float)((i % 3) - 1) * 2.0f,
                   0.7f);
    }

    g.setColour(skin.border.withAlpha(0.050f * wear));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 5.0f, 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.10f * wear));
    g.fillRect(bounds.removeFromBottom(juce::jmax(1.0f, bounds.getHeight() * 0.018f)));
}

inline void paintPanelFrame(juce::Graphics& g,
                            juce::Rectangle<float> bounds,
                            const SkinPalette& skin,
                            float radius,
                            float outlineWidth = 1.0f)
{
    g.setColour(skin.panel);
    g.fillRoundedRectangle(bounds, radius);

    const bool hasSurface = paintSkinSurface(g, bounds, skin, radius);

    paintTextureOverlay(g, bounds.reduced(1.5f), skin, hasSurface ? 0.18f : (skin.framedPanels ? 0.85f : 0.45f));
    g.setColour(skin.border.withAlpha(skin.framedPanels ? 0.88f : 0.52f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, outlineWidth);

    if (skin.dimensionalControls)
    {
        g.setColour(skin.text.withAlpha(0.035f));
        g.drawRoundedRectangle(bounds.reduced(2.0f), juce::jmax(1.0f, radius - 1.5f), 0.8f);
        g.setColour(skin.panelInset.withAlpha(0.36f));
        g.drawRoundedRectangle(bounds.reduced(3.5f), juce::jmax(1.0f, radius - 2.4f), 0.8f);
    }
}

inline void paintBypassLed(juce::Graphics& g,
                           juce::Rectangle<float> bounds,
                           const SkinPalette& skin,
                           bool onState,
                           bool disabled = false)
{
    if (bounds.isEmpty())
        return;

    const float alpha = disabled ? 0.42f : 1.0f;
    const juce::Colour blueOn  = juce::Colour(0xff28b7c9);
    const juce::Colour blueOff = juce::Colour(0xff1a5c69);
    const auto colour = onState
        ? blueOn.withAlpha(alpha)
        : blueOff.withAlpha(alpha);

    const bool round = std::abs(bounds.getWidth() - bounds.getHeight()) < 2.0f;
    if (round)
    {
        g.setColour(colour.darker(0.45f));
        g.fillEllipse(bounds);
        g.setColour(colour.brighter(0.18f));
        g.fillEllipse(bounds.reduced(bounds.getWidth() * 0.14f));
        g.setColour(skin.border.withAlpha(0.72f * alpha));
        g.drawEllipse(bounds.reduced(0.5f), 1.0f);
    }
    else
    {
        const auto radius = bounds.getHeight() * 0.42f;
        g.setColour(colour.darker(0.50f));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(colour.withAlpha(0.84f * alpha));
        g.fillRoundedRectangle(bounds.reduced(bounds.getHeight() * 0.10f), radius * 0.80f);
        g.setColour(skin.border.withAlpha(0.72f * alpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }
}

inline bool paintLedButton(juce::Graphics& g,
                           juce::Rectangle<float> bounds,
                           const SkinPalette& skin,
                           const char* assetName,
                           const juce::String& label = {},
                           juce::Colour textColour = juce::Colours::white,
                           bool disabled = false)
{
    if (bounds.isEmpty())
        return false;

    const float alpha = disabled ? 0.42f : 1.0f;
    bool drewImage = false;

    if (assetName != nullptr)
    {
        const auto& image = getSkinAssetImage(skin, assetName);
        if (image.isValid())
        {
            g.saveState();
            g.setOpacity(alpha);
            const int x = juce::roundToInt(bounds.getX());
            const int y = juce::roundToInt(bounds.getY());
            const int width = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
            const int height = juce::jmax(1, juce::roundToInt(bounds.getHeight()));
            const auto& scaledImage = detail::getScaledImage(image, juce::String(assetName), width, height);
            if (scaledImage.isValid())
                g.drawImageAt(scaledImage, x, y);
            g.restoreState();
            drewImage = true;
        }
    }

    if (!drewImage)
    {
        const bool round = std::abs(bounds.getWidth() - bounds.getHeight()) < 2.0f;
        auto colour = skin.accent.withAlpha(alpha);
        if (assetName != nullptr)
        {
            const juce::String name(assetName);
            if (name.containsIgnoreCase("danger") || name.containsIgnoreCase("minus"))
                colour = skin.danger.withAlpha(alpha);
            else if (name.containsIgnoreCase("warning") || name.containsIgnoreCase("plus"))
                colour = skin.warning.withAlpha(alpha);
            else if (name.containsIgnoreCase("off") || name.containsIgnoreCase("muted"))
                colour = skin.panelRaised.withAlpha(alpha);
        }

        if (round)
        {
            g.setColour(colour.darker(0.45f));
            g.fillEllipse(bounds);
            g.setColour(colour.brighter(0.18f));
            g.fillEllipse(bounds.reduced(bounds.getWidth() * 0.14f));
            g.setColour(skin.border.withAlpha(0.72f * alpha));
            g.drawEllipse(bounds.reduced(0.5f), 1.0f);
        }
        else
        {
            const auto radius = bounds.getHeight() * 0.42f;
            g.setColour(colour.darker(0.50f));
            g.fillRoundedRectangle(bounds, radius);
            g.setColour(colour.withAlpha(0.84f * alpha));
            g.fillRoundedRectangle(bounds.reduced(bounds.getHeight() * 0.10f), radius * 0.80f);
            g.setColour(skin.border.withAlpha(0.72f * alpha));
            g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
        }
    }

    if (label.isNotEmpty())
    {
        const bool roundLabel = std::abs(bounds.getWidth() - bounds.getHeight()) < 2.0f;
        auto textBounds = bounds.toNearestInt();
        if (roundLabel)
            textBounds = textBounds.withTrimmedBottom(juce::jmax(1, juce::roundToInt(bounds.getHeight() * 0.07f)));

        g.setColour(disabled ? textColour.withMultipliedAlpha(0.55f) : textColour);
        g.setFont(juce::Font(juce::FontOptions(juce::jmax(7.0f, bounds.getHeight() * (roundLabel ? 0.43f : 0.48f))).withStyle("Bold")));
        g.drawFittedText(label, textBounds, juce::Justification::centred, 1, 0.82f);
    }

    return drewImage;
}
}
