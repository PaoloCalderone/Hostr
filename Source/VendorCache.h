#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <set>

// ==============================================================================
// VendorCache — Dynamic vendor identification system for plugins
// Instead of using a static alias table, this system:
// 1. Analyzes loaded plugins to extract common patterns
// 2. Uses Levenshtein distance for fuzzy matching
// 3. Maintains an in-memory cache of already computed vendor mappings
// 4. Learns from recurring patterns in plugin metadata
// ==============================================================================
class VendorCache
{
public:
    explicit VendorCache();
    ~VendorCache() = default;

    // Parses a collection of plugins and builds the internal cache.
    // Must be called once with all available plugins.
    void buildCache(const juce::Array<juce::PluginDescription>& plugins);

    // Returns the canonical vendor for a PluginDescription.
    // Uses the internal cache, or calculates on the fly if not found.
    juce::String resolveVendor(const juce::PluginDescription& plugin);

    // Returns the number of unique vendors identified
    int getUniqueVendorCount() const { return static_cast<int>(vendorMap.size()); }

private:
    // Cache mapping: rawManufacturer (lower) -> canonical vendor name
    std::unordered_map<std::string, juce::String> vendorCache;
    
    // Database of frequent patterns: token -> list of (vendor, frequency)
    std::unordered_map<std::string, std::vector<std::pair<juce::String, int>>> patternFrequency;
    
    // Map of vendors and their observed variants
    std::unordered_map<std::string, std::set<std::string>> vendorMap;

    // Minimum similarity threshold (0.0 - 1.0)
    static constexpr float SIMILARITY_THRESHOLD = 0.75f;

    // Calculates the normalized Levenshtein distance between two strings.
    // Returns a value between 0.0 (identical) and 1.0 (completely different).
    static float levenshteinDistance(const juce::String& s1, const juce::String& s2);

    //Extracts significant tokens from a string (key words, brands).
    static std::vector<std::string> extractTokens(const juce::String& text);

    // Parse the PluginDescription and try to extract the candidate vendor.
    juce::String analyzePlugin(const juce::PluginDescription& d);

    // Finds the most similar vendor in the cache using Levenshtein distance.
    juce::String findMostSimilarVendor(const juce::String& candidate);

    // Cleans up the manufacturer label by removing copyright, version information, etc.
    static juce::String cleanupManufacturerLabel(juce::String text);

    // Extracts significant tokens from a string (key words, brands).
    static std::vector<std::string> extractSignificantTokens(const juce::String& text);

    // Calculates a "canonical-ness" score based on length and frequency.
    static int scoreCanonicalName(const juce::String& name, int frequency);

    // Removes numbers from a string, keeping only letters and spaces.
    static juce::String stripNumbers(juce::String text);

    // Removes everything except letters and returns a compact key in lower-case.
    static juce::String lettersOnlyKey(juce::String text);

    // Returns the grouping key for a vendor based on its alphabetical prefix.
    static juce::String vendorBucketKey(const juce::String& text);
};

// ==============================================================================
// VendorCache Implementation (Header-only)
// ==============================================================================

inline VendorCache::VendorCache()
{
}

inline float VendorCache::levenshteinDistance(const juce::String& s1, const juce::String& s2)
{
    auto str1 = s1.toLowerCase().toStdString();
    auto str2 = s2.toLowerCase().toStdString();
    
    const size_t len1 = str1.length();
    const size_t len2 = str2.length();
    
    if (len1 == 0) return len2 > 0 ? 1.0f : 0.0f;
    if (len2 == 0) return 1.0f;
    
    // DP matrix for Levenshtein distance
    std::vector<std::vector<int>> dp(len1 + 1, std::vector<int>(len2 + 1));
    
    for (size_t i = 0; i <= len1; ++i) dp[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= len2; ++j) dp[0][j] = static_cast<int>(j);
    
    for (size_t i = 1; i <= len1; ++i)
    {
        for (size_t j = 1; j <= len2; ++j)
        {
            const int cost = (str1[i - 1] == str2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,      // deletion
                dp[i][j - 1] + 1,      // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }
    
    const int distance = dp[len1][len2];
    const int maxLen = static_cast<int>(std::max(len1, len2));
    const float normalized = maxLen > 0 ? static_cast<float>(distance) / static_cast<float>(maxLen) : 0.0f;
    
    return normalized;
}

inline std::vector<std::string> VendorCache::extractTokens(const juce::String& text)
{
    std::vector<std::string> tokens;
    auto lower = text.toLowerCase().toStdString();
    
    std::istringstream stream(lower);
    std::string token;
    while (stream >> token)
    {
        // Remove non-alphanumeric characters from borders
        while (!token.empty() && !std::isalnum(token.front()))
            token.erase(token.begin());
        while (!token.empty() && !std::isalnum(token.back()))
            token.pop_back();
        
        if (!token.empty() && token.length() > 2)
            tokens.push_back(token);
    }
    
    return tokens;
}

inline std::vector<std::string> VendorCache::extractSignificantTokens(const juce::String& text)
{
    auto tokens = extractTokens(text);
    
    // Filter out useless common words
    const std::vector<std::string> stopwords = {
        "inc", "inc.", "llc", "ltd", "ltd.", "gmbh", "srl", "spa",
        "b.v.", "bv", "kg", "co", "corp", "corp.", "company",
        "the", "and", "or", "copyright", "all", "rights", "reserved",
        "plugin", "vst", "au", "aax", "version", "v", "audio", "digital",
        "music", "production", "software", "product", "2024", "2025", "2026"
    };
    
    std::vector<std::string> result;
    for (const auto& token : tokens)
    {
        if (std::find(stopwords.begin(), stopwords.end(), token) == stopwords.end())
            result.push_back(token);
    }
    
    return result;
}

inline juce::String VendorCache::cleanupManufacturerLabel(juce::String text)
{
    text = text.trim();
    text = text.replace("©", " ");
    text = text.replace("(c)", " ", true);
    text = text.replace("Copyright", " ", true);
    text = text.replace("All Rights Reserved", " ", true);
    text = text.replace("_", " ").replace("-", " ");
    text = text.replaceCharacter(',', ' ');
    text = text.replaceCharacter('/', ' ');
    text = text.replaceCharacter('\\', ' ');
    text = text.replaceCharacter('(', ' ');
    text = text.replaceCharacter(')', ' ');
    text = text.replaceCharacter('[', ' ');
    text = text.replaceCharacter(']', ' ');
    
    while (text.contains("  "))
        text = text.replace("  ", " ");
    
    text = text.trim();
    
    // Remove common legal suffixes
    auto tokens = juce::StringArray::fromTokens(text, " ", {});
    const juce::StringArray suffixes { "inc", "inc.", "llc", "ltd", "ltd.", "gmbh", "srl", "spa",
                                      "b.v.", "bv", "kg", "co", "corp", "corp.", "company" };
    
    while (tokens.size() > 1 && suffixes.contains(tokens[tokens.size() - 1].toLowerCase()))
        tokens.remove(tokens.size() - 1);
    
    text = tokens.joinIntoString(" ").trim();
    
    return text;
}

inline int VendorCache::scoreCanonicalName(const juce::String& name, int frequency)
{
    int score = frequency * 100;
    
// Penalty for names that are too long or look like versions/copyrights
    if (name.length() > 25)
        score -= 50;
    
    if (name.toLowerCase().contains("copyright") || 
        name.toLowerCase().contains("version") ||
        name.toLowerCase().contains("all rights"))
        score = -1000;
    
    return score;
}

inline juce::String VendorCache::stripNumbers(juce::String text)
{
    juce::String result;
    for (auto c : text)
    {
        if (std::isalpha(c) || std::isspace(c))
            result += c;
    }
    
// Normalize multiple spaces
    while (result.contains("  "))
        result = result.replace("  ", " ");
    
    return result.trim();
}

    inline juce::String VendorCache::lettersOnlyKey(juce::String text)
    {
        juce::String result;

        for (auto c : text)
        {
            if (std::isalpha((unsigned char) c))
                result += juce::CharacterFunctions::toLowerCase(c);
        }

        return result;
    }

    inline juce::String VendorCache::vendorBucketKey(const juce::String& text)
    {
        auto key = lettersOnlyKey(stripNumbers(cleanupManufacturerLabel(text)));

        if (key.isEmpty())
            return {};

        if (key.length() <= 2)
            return key;

        return key.substring(0, 2);
    }

inline void VendorCache::buildCache(const juce::Array<juce::PluginDescription>& plugins)
{
    vendorCache.clear();
    vendorMap.clear();
    patternFrequency.clear();
    
    // First phase: we collect all candidate vendor names
    std::unordered_map<std::string, std::vector<juce::String>> vendorVariants;
    
    for (const auto& plugin : plugins)
    {
        auto cleaned = stripNumbers(cleanupManufacturerLabel(plugin.manufacturerName));

        if (cleaned.isEmpty())
            cleaned = stripNumbers(cleanupManufacturerLabel(plugin.name));
        
        if (cleaned.isEmpty() || cleaned.equalsIgnoreCase("unknown"))
            continue;
        
        auto bucketKey = vendorBucketKey(cleaned);
        if (bucketKey.isEmpty())
            continue;

        vendorVariants[bucketKey.toStdString()].push_back(cleaned);
        
        // Collect pattern tokens
        auto tokens = extractSignificantTokens(cleaned + " " + plugin.name);
        for (const auto& token : tokens)
        {
            patternFrequency[token].push_back({ cleaned, 1 });
        }
    }
    
    // Second phase: consolidate vendor variants into canonical names
    for (const auto& [lowerName, variants] : vendorVariants)
    {
        // Find the best variant: the shortest and cleanest, so that identical prefixes merge.
        juce::String bestCanonical;
        int bestScore = -1;
        
        std::unordered_map<std::string, int> freqMap;
        for (const auto& v : variants)
            freqMap[v.toStdString()]++;
        
        for (const auto& v : variants)
        {
            const int score = scoreCanonicalName(v, freqMap[v.toStdString()]) - v.length();
            if (score > bestScore)
            {
                bestScore = score;
                bestCanonical = v;
            }
        }
        
        if (bestCanonical.isNotEmpty())
        {
            bestCanonical = stripNumbers(bestCanonical);
            if (bestCanonical.isEmpty())
                continue;
                
            vendorMap[lowerName] = {};
            for (const auto& v : variants)
                vendorMap[lowerName].insert(v.toStdString());
            
            // Map all variants to the canonical one
            for (const auto& v : variants)
            {
                vendorCache[v.toLowerCase().toStdString()] = bestCanonical;
            }

            vendorCache[lowerName] = bestCanonical;
        }
    }
}

inline juce::String VendorCache::findMostSimilarVendor(const juce::String& candidate)
{
    auto candidateKey = vendorBucketKey(candidate);
    if (candidateKey.isEmpty())
        candidateKey = lettersOnlyKey(stripNumbers(candidate));

    float bestSimilarity = SIMILARITY_THRESHOLD;
    juce::String bestMatch;
    
    for (const auto& [vendorLower, _] : vendorMap)
    {
        float dist = levenshteinDistance(candidateKey, juce::String(vendorLower));
        float similarity = 1.0f - dist;
        
        if (similarity > bestSimilarity)
        {
            bestSimilarity = similarity;
            bestMatch = juce::String(vendorLower);
        }
    }
    
    if (bestMatch.isNotEmpty())
    {
        auto it = vendorCache.find(bestMatch.toLowerCase().toStdString());
        if (it != vendorCache.end())
            return it->second;
    }
    
    return {};
}

inline juce::String VendorCache::analyzePlugin(const juce::PluginDescription& d)
{
    juce::String raw = d.manufacturerName.trim();
    
    if (raw.isEmpty())
    {
        // Fallback: extract the first token from the plugin name
        auto tokens = juce::StringArray::fromTokens(d.name, " ", {});
        if (tokens.size() > 0)
            return tokens[0];
        return "(Unknown)";
    }
    
    // Extract after the last comma (often the true brand for VST3)
    int lastComma = raw.lastIndexOfChar(',');
    if (lastComma >= 0)
    {
        auto afterComma = cleanupManufacturerLabel(raw.substring(lastComma + 1));
        if (afterComma.isNotEmpty() && !afterComma.toLowerCase().startsWith("copyright"))
            raw = afterComma;
    }
    
    auto cleaned = cleanupManufacturerLabel(raw);
    if (cleaned.isEmpty() || cleaned.equalsIgnoreCase("unknown"))
    {
        auto tokens = juce::StringArray::fromTokens(d.name, " ", {});
        return tokens.size() > 0 ? tokens[0] : "(Unknown)";
    }
    
    return cleaned;
}

inline juce::String VendorCache::resolveVendor(const juce::PluginDescription& plugin)
{
    auto candidate = analyzePlugin(plugin);
    
    if (candidate.isEmpty() || candidate.equalsIgnoreCase("unknown"))
        return "(Unknown)";
    
    auto cleanedCandidate = stripNumbers(candidate);
    auto bucketKey = vendorBucketKey(cleanedCandidate);
    auto lowerCandidate = cleanedCandidate.toLowerCase().toStdString();
    auto lowerBucketKey = bucketKey.toLowerCase().toStdString();
    
    // Cache check
    auto it = vendorCache.find(lowerCandidate);
    if (it != vendorCache.end())
        return stripNumbers(it->second);

    if (!lowerBucketKey.empty())
    {
        auto bucketIt = vendorCache.find(lowerBucketKey);
        if (bucketIt != vendorCache.end())
            return stripNumbers(bucketIt->second);
    }
    
    // Try fuzzy matching
    auto similar = findMostSimilarVendor(candidate);
    if (similar.isNotEmpty())
    {
        auto cleaned = stripNumbers(similar);
        vendorCache[lowerCandidate] = cleaned;
        return cleaned;
    }
    
    // Fallback: return the cleaned candidate
    auto cleaned = stripNumbers(candidate);
    if (cleaned.isEmpty())
        cleaned = "(Unknown)";
    if (!lowerBucketKey.empty())
        vendorCache[lowerBucketKey] = cleaned;
    vendorCache[lowerCandidate] = cleaned;
    return cleaned;
}
