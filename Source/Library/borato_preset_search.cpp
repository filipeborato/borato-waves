#include "borato_preset_search.h"

namespace borato
{
PresetInfo PresetIndex::readFromPath (const juce::File& f)
{
    PresetInfo p;
    p.file = f;
    p.name = f.getFileNameWithoutExtension();
    p.category = f.getParentDirectory().getFileName();
    return p;
}

void PresetIndex::rebuild (const juce::Array<juce::File>& roots, const juce::String& wildcard, MetadataReader reader)
{
    juce::Array<PresetInfo> fresh;
    for (auto& root : roots)
        for (auto& entry : juce::RangedDirectoryIterator (root, true, wildcard, juce::File::findFiles))
            fresh.add (reader (entry.getFile()));

    const juce::ScopedLock sl (lock);
    for (auto& p : fresh)
    {
        auto it = ratings.find (p.file.getFullPathName());
        if (it != ratings.end())
            p.rating = it->second;
    }
    presets.swapWith (fresh);
}

bool PresetIndex::matches (const PresetInfo& p, const PresetQuery& q)
{
    if (p.rating < q.minRating)
        return false;
    if (q.category.isNotEmpty() && ! p.category.equalsIgnoreCase (q.category))
        return false;

    if (! q.tags.isEmpty())
    {
        bool any = false;
        for (auto& t : q.tags)
            any = any || p.tags.contains (t, true);
        if (! any)
            return false;
    }

    const auto haystack = p.name + " " + p.author + " " + p.category + " " + p.description + " "
                        + p.comment + " " + p.tags.joinIntoString (" ");
    for (auto& token : juce::StringArray::fromTokens (q.text, true))
        if (! haystack.containsIgnoreCase (token))
            return false;
    return true;
}

juce::Array<PresetInfo> PresetIndex::search (const PresetQuery& q) const
{
    juce::Array<PresetInfo> out;
    const juce::ScopedLock sl (lock);
    for (auto& p : presets)
        if (matches (p, q))
            out.add (p);
    return out;
}

juce::StringArray PresetIndex::categories() const
{
    juce::StringArray out;
    const juce::ScopedLock sl (lock);
    for (auto& p : presets)
        if (p.category.isNotEmpty())
            out.addIfNotAlreadyThere (p.category, true);
    out.sortNatural();
    return out;
}

juce::StringArray PresetIndex::tags() const
{
    juce::StringArray out;
    const juce::ScopedLock sl (lock);
    for (auto& p : presets)
        for (auto& t : p.tags)
            out.addIfNotAlreadyThere (t, true);
    out.sortNatural();
    return out;
}

void PresetIndex::setRating (const juce::File& f, int rating)
{
    const juce::ScopedLock sl (lock);
    ratings[f.getFullPathName()] = rating;
    for (auto& p : presets)
        if (p.file == f)
            p.rating = rating;
}

int PresetIndex::size() const
{
    const juce::ScopedLock sl (lock);
    return presets.size();
}
} // namespace borato
