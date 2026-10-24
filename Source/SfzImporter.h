#pragma once
#include "Sample.h"
#include <map>

namespace bw
{
// A documented SFZ subset, imported into a SampleBank. Headers: <control> <global> <master> <group> <region>;
// later levels override earlier ones and each header resets the levels below it. Opcodes: default_path, sample,
// key, lokey, hikey, pitch_keycenter, lovel, hivel, loop_mode, loop_start, loop_end, offset, end, tune, pan, volume.
// Anything else (other headers, opcodes, #define/#include) is listed as unsupported, never silently converted.
// Text is limited to 1 MiB and 128 regions; sample paths must stay inside the .sfz file's folder.
constexpr size_t kMaxSfzBytes = (size_t) 1 << 20;
constexpr int kMaxSfzRegions = kMaxSampleZones;

inline const juce::StringArray& supportedSfzOpcodes ()
{
    static const juce::StringArray names { "default_path", "sample", "key", "lokey", "hikey", "pitch_keycenter", "lovel", "hivel",
                                           "loop_mode", "loop_start", "loop_end", "offset", "end", "tune", "pan", "volume" };
    return names;
}

struct SfzRegion
{
    juce::File sample;
    int lowKey = 0, highKey = 127, rootKey = 60, lowVel = 1, highVel = 127;
    juce::String loopMode = "no_loop";
    juce::int64 loopStart = -1, loopEnd = -1, offset = 0, end = -1; // frames; end and loop_end are inclusive
    float tune = 0, pan = 0, volume = 0;                            // cents, -100..100, dB
};

struct SfzImportResult
{
    std::vector<SfzRegion> regions;
    juce::StringArray unsupported, missingFiles, warnings;
    juce::String error;
    juce::String summary () const
    {
        juce::String text = juce::String ((int) regions.size ()) + " REGIONS";
        if (! unsupported.isEmpty ()) text << " / UNSUPPORTED: " << unsupported.joinIntoString (", ");
        if (! missingFiles.isEmpty ()) text << " / MISSING: " << missingFiles.joinIntoString (", ");
        if (! warnings.isEmpty ()) text << " / " << warnings.joinIntoString ("; ");
        return text;
    }
};

// MIDI note from a number (0..127) or a name such as c4 (= 60), f#3, eb-1; -1 when invalid.
inline int sfzNote (const juce::String& value)
{
    const auto v = value.trim ().toLowerCase ();
    if (v.isEmpty ()) return -1;
    if (v.containsOnly ("0123456789")) { const int n = v.getIntValue (); return v.length () <= 3 && n >= 0 && n <= 127 ? n : -1; }
    static const std::map<juce::juce_wchar, int> steps { { 'c', 0 }, { 'd', 2 }, { 'e', 4 }, { 'f', 5 }, { 'g', 7 }, { 'a', 9 }, { 'b', 11 } };
    const auto found = steps.find (v[0]);
    if (found == steps.end ()) return -1;
    int note = found->second, at = 1;
    if (v[at] == '#') { ++note; ++at; } else if (v[at] == 'b') { --note; ++at; }
    const auto octave = v.substring (at);
    if (octave.isEmpty () || ! octave.trimCharactersAtStart ("-").containsOnly ("0123456789") || octave.length () > 2) return -1;
    note += (octave.getIntValue () + 1) * 12;
    return note >= 0 && note <= 127 ? note : -1;
}

// Parses text (comments stripped) into regions; paths resolve against `folder` and must stay inside it.
inline SfzImportResult parseSfz (juce::String text, const juce::File& folder)
{
    SfzImportResult result;
    if ((size_t) text.getNumBytesAsUTF8 () > kMaxSfzBytes) { result.error = "SFZ file exceeds 1 MB"; return result; }
    // Strip /* */ and // comments.
    juce::String clean;
    for (int i = 0; i < text.length ();)
    {
        if (text[i] == '/' && text[i + 1] == '*') { const int close = text.indexOf (i + 2, "*/"); i = close < 0 ? text.length () : close + 2; clean << ' '; }
        else if (text[i] == '/' && text[i + 1] == '/') { const int line = text.indexOfChar (i, '\n'); i = line < 0 ? text.length () : line; }
        else clean << text[i++];
    }
    using Opcodes = std::map<juce::String, juce::String>;
    Opcodes control, global, master, group, region;
    Opcodes* level = nullptr;
    bool inRegion = false, ignoredHeader = false;
    auto noteUnsupported = [&] (const juce::String& name) { result.unsupported.addIfNotAlreadyThere (name); };
    auto finishRegion = [&]
    {
        if (! inRegion) return;
        inRegion = false;
        Opcodes merged;
        for (const auto* layer : { &global, &master, &group, &region }) for (const auto& [k, v] : *layer) merged[k] = v;
        auto get = [&] (const char* key) { const auto it = merged.find (key); return it == merged.end () ? juce::String () : it->second; };
        if ((int) result.regions.size () >= kMaxSfzRegions) { result.warnings.addIfNotAlreadyThere ("more than 128 regions: extra regions dropped"); return; }
        if (get ("sample").isEmpty ()) { result.warnings.add ("region without sample= skipped"); return; }
        SfzRegion r;
        auto path = (control.count ("default_path") ? control["default_path"] : juce::String ()) + get ("sample");
        path = path.replaceCharacter ('\\', '/').trim ();
        const auto file = folder.getChildFile (path);
        if (juce::File::isAbsolutePath (path) || path.startsWithChar ('/') || ! file.isAChildOf (folder))
        { result.warnings.add ("sample path outside the SFZ folder refused: " + path); return; }
        r.sample = file;
        auto note = [&] (const char* key, int& out)
        {
            if (const auto v = get (key); v.isNotEmpty ())
            { const int n = sfzNote (v); if (n < 0) result.warnings.add (juce::String (key) + "=" + v + " is not a MIDI note"); else out = n; }
        };
        auto integer = [&] (const char* key, auto& out, juce::int64 lo, juce::int64 hi)
        {
            if (const auto v = get (key).trim (); v.isNotEmpty ())
            {
                if (! v.containsOnly ("-0123456789") || v.length () > 12 || v.getLargeIntValue () < lo || v.getLargeIntValue () > hi)
                    result.warnings.add (juce::String (key) + "=" + v + " out of range");
                else out = (std::remove_reference_t<decltype (out)>) v.getLargeIntValue ();
            }
        };
        auto real = [&] (const char* key, float& out, float lo, float hi)
        {
            if (const auto v = get (key).trim (); v.isNotEmpty ())
            {
                const float f = v.getFloatValue ();
                if (! v.containsOnly ("-+.0123456789") || f < lo || f > hi) result.warnings.add (juce::String (key) + "=" + v + " out of range");
                else out = f;
            }
        };
        // key= sets the range and root; explicit lokey/hikey/pitch_keycenter then override it.
        if (const auto v = get ("key"); v.isNotEmpty ())
        { const int n = sfzNote (v); if (n < 0) result.warnings.add ("key=" + v + " is not a MIDI note"); else r.lowKey = r.highKey = r.rootKey = n; }
        note ("lokey", r.lowKey); note ("hikey", r.highKey); note ("pitch_keycenter", r.rootKey);
        integer ("lovel", r.lowVel, 1, 127); integer ("hivel", r.highVel, 1, 127);
        integer ("offset", r.offset, 0, kMaxSampleFrames); integer ("end", r.end, 0, kMaxSampleFrames);
        integer ("loop_start", r.loopStart, 0, kMaxSampleFrames); integer ("loop_end", r.loopEnd, 0, kMaxSampleFrames);
        real ("tune", r.tune, -100, 100); real ("pan", r.pan, -100, 100); real ("volume", r.volume, -144, 6);
        if (const auto v = get ("loop_mode").trim (); v.isNotEmpty ())
        {
            if (juce::StringArray { "no_loop", "one_shot", "loop_continuous", "loop_sustain" }.contains (v)) r.loopMode = v;
            else result.warnings.add ("loop_mode=" + v + " unknown, using no_loop");
        }
        if (r.loopMode == "one_shot") result.warnings.addIfNotAlreadyThere ("one_shot plays until the envelope release ends, not to the sample end");
        if (r.lowKey > r.highKey || r.lowVel > r.highVel) { result.warnings.add ("region with an empty key or velocity range skipped"); return; }
        result.regions.push_back (r);
    };

    for (int i = 0; i < clean.length ();)
    {
        if (juce::CharacterFunctions::isWhitespace (clean[i])) { ++i; continue; }
        if (clean[i] == '<')
        {
            const int close = clean.indexOfChar (i, '>');
            if (close < 0) { result.error = "Unterminated header"; return result; }
            const auto header = clean.substring (i + 1, close).trim ().toLowerCase ();
            i = close + 1;
            finishRegion ();
            ignoredHeader = false;
            if (header == "control") { level = &control; }
            else if (header == "global") { global.clear (); master.clear (); group.clear (); level = &global; }
            else if (header == "master") { master.clear (); group.clear (); level = &master; }
            else if (header == "group") { group.clear (); level = &group; }
            else if (header == "region") { region.clear (); level = &region; inRegion = true; }
            else { noteUnsupported ("<" + header + ">"); level = nullptr; ignoredHeader = true; }
            continue;
        }
        if (clean[i] == '#')
        {
            const int line = clean.indexOfChar (i, '\n');
            noteUnsupported (clean.substring (i, line < 0 ? clean.length () : line).upToFirstOccurrenceOf (" ", false, false));
            i = line < 0 ? clean.length () : line;
            continue;
        }
        // opcode=value; the value runs to the next opcode=, header or line end, so sample paths may contain spaces.
        const int equals = clean.indexOfChar (i, '=');
        if (equals < 0) { result.warnings.add ("text without opcode ignored: " + clean.substring (i).trim ().substring (0, 40)); break; }
        const auto name = clean.substring (i, equals).trim ().toLowerCase ();
        int valueEnd = equals + 1;
        for (int j = equals + 1; j <= clean.length (); ++j)
        {
            if (j == clean.length () || clean[j] == '<' || clean[j] == '\n' || clean[j] == '\r') { valueEnd = j; break; }
            if (juce::CharacterFunctions::isWhitespace (clean[j]))
            {
                int k = j; while (k < clean.length () && juce::CharacterFunctions::isWhitespace (clean[k])) ++k;
                int m = k; while (m < clean.length () && (juce::CharacterFunctions::isLetterOrDigit (clean[m]) || clean[m] == '_')) ++m;
                if (m > k && m < clean.length () && clean[m] == '=') { valueEnd = j; break; }
            }
        }
        const auto value = clean.substring (equals + 1, valueEnd).trim ();
        i = valueEnd;
        if (name.isEmpty () || name.containsAnyOf (" \t\r\n")) { result.warnings.add ("malformed opcode near " + name.substring (0, 40)); continue; }
        if (ignoredHeader) continue; // already reported with its header
        if (! supportedSfzOpcodes ().contains (name)) { noteUnsupported (name); continue; }
        if (level == nullptr) { result.warnings.addIfNotAlreadyThere ("opcodes before the first header ignored"); continue; }
        if (name == "default_path" && level != &control) { result.warnings.addIfNotAlreadyThere ("default_path outside <control> ignored"); continue; }
        (*level)[name] = value;
    }
    finishRegion ();
    if (result.regions.empty () && result.error.isEmpty ()) result.error = "No playable regions";
    return result;
}

// Parses `sfz`, decodes each referenced file once (missing ones become missing zones) within `maxBytes`,
// and builds the bank. Runs on a non-audio thread; returns null with result.error on failure.
inline std::shared_ptr<const SampleBank> importSfzBank (const juce::File& sfz, SfzImportResult& result,
                                                       const std::function<bool()>& shouldCancel = {},
                                                       const std::function<void (float)>& onProgress = {},
                                                       size_t maxBytes = kMaxAssetMemoryBytes)
{
    if (! sfz.existsAsFile () || sfz.getSize () > (juce::int64) kMaxSfzBytes)
    { result.error = sfz.existsAsFile () ? "SFZ file exceeds 1 MB" : "SFZ file not found"; return {}; }
    result = parseSfz (sfz.loadFileAsString (), sfz.getParentDirectory ());
    if (result.error.isNotEmpty ()) return {};
    std::map<juce::String, std::shared_ptr<const Sample>> decoded;
    size_t used = 0;
    auto bank = std::make_shared<SampleBank> ();
    for (size_t n = 0; n < result.regions.size (); ++n)
    {
        if (shouldCancel && shouldCancel ()) { result.error = "SFZ import cancelled"; return {}; }
        const auto& r = result.regions[n];
        const auto key = r.sample.getFullPathName ();
        if (! decoded.count (key))
        {
            std::shared_ptr<const Sample> asset;
            if (r.sample.existsAsFile ())
            {
                juce::String error;
                asset = loadSample (r.sample, error, shouldCancel, {}, maxBytes - used);
                if (! asset)
                {
                    if (error.contains ("budget")) { result.error = "SFZ samples exceed the 256 MB memory budget"; return {}; }
                    if (error.contains ("cancelled")) { result.error = "SFZ import cancelled"; return {}; }
                    result.warnings.add (r.sample.getFileName () + ": " + error);
                }
                else used += asset->bytes ();
            }
            if (! asset) result.missingFiles.addIfNotAlreadyThere (r.sample.getFileName ());
            decoded[key] = asset;
        }
        const auto& asset = decoded[key];
        SampleZone z;
        z.sample = asset; z.path = r.sample;
        z.lowKey = r.lowKey; z.highKey = r.highKey; z.rootKey = r.rootKey; z.lowVel = r.lowVel; z.highVel = r.highVel;
        z.tuneCents = r.tune; z.pan = r.pan / 100.0f; z.gainDb = r.volume;
        if (! asset) z.variants[0].missing = true;
        else
        {
            const double frames = asset->numFrames ();
            const double first = juce::jlimit (0.0, frames - kMinSampleRegion, (double) r.offset);
            const double last = r.end < 0 ? frames : juce::jlimit (first + kMinSampleRegion, frames, (double) r.end + 1);
            z.start = (float) (first / frames); z.end = (float) (last / frames);
            z.loop = r.loopMode == "loop_continuous" || r.loopMode == "loop_sustain";
            z.loopMode = r.loopMode == "loop_sustain" ? (int) LoopMode::tailed : (int) LoopMode::forward;
            if (z.loop && r.loopStart >= 0 && r.loopEnd > r.loopStart)
            {
                z.loopLinked = false;
                z.loopStart = (float) juce::jlimit (0.0, 1.0, (double) r.loopStart / frames);
                z.loopEnd = (float) juce::jlimit (0.0, 1.0, (double) (r.loopEnd + 1) / frames);
            }
            else if (z.loop) result.warnings.addIfNotAlreadyThere ("loop without loop_start/loop_end loops the whole region");
        }
        bank->zones.push_back (std::move (z));
        if (onProgress) onProgress ((float) (n + 1) / (float) result.regions.size ());
    }
    if (! bank->valid ())
    {
        result.error = "Overlapping regions are not supported (one zone per key, or two velocity layers sharing a key range)";
        return {};
    }
    return bank;
}
} // namespace bw
