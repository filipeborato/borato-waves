/*
 BEGIN_JUCE_MODULE_DECLARATION
  ID:            borato_preset_search
  vendor:        borato
  version:       0.1.0
  name:          Preset index + search (Serum 2 style)
  description:   Scans preset folders into an in-memory index; filters by text, tags (any-of), category and rating.
  dependencies:  juce_core
  license:       Proprietary
 END_JUCE_MODULE_DECLARATION
*/
#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <map>

namespace borato
{
// Mirrors Serum 2's Presets row + Presets_Tags + UserData.rating.
struct PresetInfo
{
    juce::File file;
    juce::String name, author, category, description, comment;
    juce::StringArray tags;
    int rating = 0; // 0 = unrated, 1..5
};

struct PresetQuery
{
    juce::String text;       // whitespace tokens, all must match (case-insensitive)
    juce::StringArray tags;  // any-of, like Serum's "WHERE Tags.name IN (...)"
    juce::String category;   // empty = all
    int minRating = 0;
};

class PresetIndex
{
public:
    using MetadataReader = std::function<PresetInfo (const juce::File&)>;

    // Default reader: name = file name, category = parent folder. Pass your own to parse preset headers.
    static PresetInfo readFromPath (const juce::File&);

    // Blocking scan (Serum runs this on a background thread: PresetManager::startRebuildThread).
    // Safe to call from any thread; search() keeps serving the old index until the swap.
    void rebuild (const juce::Array<juce::File>& roots, const juce::String& wildcard,
                  MetadataReader reader = readFromPath);

    juce::Array<PresetInfo> search (const PresetQuery&) const;
    juce::StringArray categories() const; // SELECT DISTINCT category FROM Presets
    juce::StringArray tags() const;       // SELECT name FROM Tags
    void setRating (const juce::File&, int rating);
    int size() const;

    static bool matches (const PresetInfo&, const PresetQuery&);

private:
    juce::CriticalSection lock;
    juce::Array<PresetInfo> presets;
    // ponytail: ratings keyed by path and kept in memory only; Serum keys by content hash
    // (UserData.hash) so ratings survive renames. Persist + hash if users ask for it.
    std::map<juce::String, int> ratings;
};
} // namespace borato
