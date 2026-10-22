#include "FactoryPresets.h"

namespace bw
{
namespace
{
// Matrix source and destination indices (see Modulation.h).
enum Source { lfo1 = 1, lfo2, m1, m2, m3, m4, env1, vel, env2 };
enum Destination { aPos, aLevel, aPan, aPitch, aDetune, aSpread, bPos, bLevel, bPan, bPitch, bDetune, bSpread,
                   cutoff, reso, subLevel, noiseLevel, cutoff2, reso2, fxDrive, fxDelay, fxReverb, aWarp, bWarp, fm };
// Table indices in WavetableBank.
enum Table { basic, sineTri, saw, odd, pulse, formant, bells, bend };
// Division indices in divisionNames(): straight, dotted, triplet.
enum Division { d2bars = 3, d1bar = 6, dHalf = 9, dQuarter = 12, dQuarterDot = 13, dEighth = 15, dEighthDot = 16, dEighthTri = 17, dSixteenth = 18, dSixteenthTri = 20, dThirtySecond = 21 };
enum Lfo { sine, triangle, rampUp, rampDown, square };

using Values = std::vector<PresetValue>;
using Routes = std::vector<PresetRoute>;

// Shared performance layer: Tone (bipolar cutoff, centred), Echo and Space macros start dry.
Values withMacros (Values own)
{
    Values v { { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", dEighthDot }, { "delayMix", 0 },
               { "reverbOn", 1 }, { "reverbMix", 0 }, { "macro1", .5f } };
    v.insert (v.end(), own.begin(), own.end());
    return v;
}
Routes withMacroRoutes (Routes own)
{
    Routes r { { m1, cutoff, .3f, true }, { m3, fxDelay, .45f }, { m4, fxReverb, .5f } };
    r.insert (r.end(), own.begin(), own.end());
    return r;
}
FactoryPreset make (const char* name, const char* category, const char* tags, const char* description,
                    Values values, Routes routes, const char* macro2, std::array<int, 2> shapes = { sine, sine })
{
    return { name, category, tags, description, withMacros (std::move (values)), withMacroRoutes (std::move (routes)),
             shapes, { "Tone", macro2, "Echo", "Space" } };
}

// Original harmonic recipes, prepared once on the control thread. Playback uses the
// existing band-limited user-table path, including embedding cycles in saved state.
std::shared_ptr<const Wavetable> atelierTable (int type)
{
    static const auto tables = []
    {
        std::array<std::shared_ptr<const Wavetable>, 4> result;
        for (int kind = 0; kind < 4; ++kind)
        {
            std::vector<std::vector<float>> cycles (16, std::vector<float> (Wavetable::kSize));
            for (int f = 0; f < 16; ++f)
            {
                const double t = f / 15.0;
                for (int h = 1; h <= 48; ++h)
                {
                    double amplitude = 0;
                    if (kind == 0) // Reed: odd core, increasingly bright even harmonics.
                        amplitude = std::exp (-h / (3.0 + 16*t)) * (h%2 ? 1.0 : .08 + .45*t) / h;
                    if (kind == 1) // Prism: moving harmonic ridge above a stable fundamental.
                        amplitude = (h == 1 ? .8 : .0) + .32 * std::exp (-std::pow ((h - (3 + 14*t)) / 2.2, 2)) / std::sqrt ((double) h);
                    if (kind == 2) // Tine: sparse harmonic clusters, not an inharmonic model.
                        amplitude = (h == 1 ? 1.0 : 0.0) + (h == 3 || h == 7 || h == 11 || h == 17 ? (.18 + .65*t) / std::sqrt ((double) h) : 0.0);
                    if (kind == 3) // Silk: soft fundamental opens into a smooth harmonic fan.
                        amplitude = std::exp (-h / (1.6 + 10*t)) / std::pow ((double) h, 1.35);
                    for (int i = 0; i < Wavetable::kSize; ++i)
                        cycles[(size_t) f][(size_t) i] += (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * h * i / Wavetable::kSize));
                }
            }
            auto table = std::make_shared<Wavetable> ();
            table->setFrames (cycles);
            result[(size_t) kind] = std::move (table);
        }
        return result;
    } ();
    return tables[(size_t) type];
}

LfoCurve contour (std::initializer_list<CurvePoint> points)
{
    LfoCurve result;
    result.count = (int) points.size ();
    std::copy (points.begin (), points.end (), result.points.begin ());
    jassert (result.valid ());
    return result;
}

FactoryPreset atelier (const char* name, const char* category, const char* tags, const char* description,
                       int table, Values values, Routes routes, std::array<const char*, 4> macros, LfoCurve curve)
{
    Values defaults { { "oscAUser", 1 }, { "level", .34f }, { "gain", -14 }, { "cutoff", 4500 },
                      { "delaySync", 1 }, { "delayDivision", dEighthDot }, { "delayFeedback", .24f },
                      { "delayMix", .12f }, { "reverbSize", .65f }, { "reverbMix", .16f } };
    defaults.insert (defaults.end (), values.begin (), values.end ());
    FactoryPreset p { name, category, tags, description, std::move (defaults), std::move (routes), { sine, sine }, macros };
    p.curves[0] = curve;
    p.tables[0] = atelierTable (table);
    static constexpr const char* names[] { "Atelier Reed", "Atelier Prism", "Atelier Tine", "Atelier Silk" };
    p.tableNames[0] = names[table];
    return p;
}
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        // ---- Revised first edition; names and factory indices remain stable.
        { "Init / Pure wave", "INIT", "Template Poly", "A dry sine template with neutral performance controls.",
          { { "attack", .005f }, { "release", .15f } }, {} },
        make ("Glass current", "PLUCK", "Poly Bright Bell Velocity", "Velocity-bright glass strike with a soft octave and restrained echoes.",
              { { "wtPos", .32f }, { "level", .5f }, { "cutoff", 5200 }, { "reso", 1.1f }, { "attack", .002f }, { "decay", .38f }, { "sustain", .08f },
                { "release", .38f }, { "gain", -12 }, { "wtTable", bells }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "levelB", .1f }, { "oscBPitch", 12 },
                { "env2Decay", .13f }, { "delayMix", .09f }, { "delayFeedback", .22f }, { "reverbMix", .12f } },
              { { env2, aPos, .19f }, { vel, aPos, .1f }, { m2, bLevel, .16f } }, "Shimmer"),
        make ("Slow coastline", "PAD", "Poly Wide Evolving", "A slow triangle and saw bloom with independent drift and a warm hall.",
              { { "wtPos", .23f }, { "level", .45f }, { "cutoff", 1900 }, { "reso", .8f }, { "attack", 1.4f }, { "decay", 1.8f }, { "sustain", .75f },
                { "release", 2.6f }, { "gain", -12 }, { "wtTable", sineTri }, { "oscAPan", -.2f }, { "oscBOn", 1 }, { "wtTableB", saw }, { "wtPosB", .25f },
                { "levelB", .21f }, { "oscBFine", 5 }, { "oscBPan", .2f }, { "oscAUnison", 4 }, { "oscBUnison", 4 }, { "oscADetune", 9 },
                { "oscBDetune", 11 }, { "oscASpread", .75f }, { "oscBSpread", .75f }, { "lfo1Rate", .13f }, { "lfo2Rate", .071f },
                { "reverbMix", .22f }, { "reverbDamping", .65f } },
              { { lfo1, bPos, .17f, true }, { lfo2, aPos, .08f, true }, { vel, cutoff, .05f }, { m2, bLevel, .15f } }, "Horizon"),
        make ("Undertow", "BASS", "Mono Sub Dark", "Focused pulse bass with a short filter attack and independent clean sub.",
              { { "wtPos", .58f }, { "level", .55f }, { "cutoff", 440 }, { "reso", 1.1f }, { "attack", .004f }, { "decay", .22f }, { "sustain", .65f },
                { "release", .1f }, { "gain", -12 }, { "wtTable", pulse }, { "subOn", 1 }, { "subLevel", .16f }, { "subRoute", 2 },
                { "voiceMode", 1 }, { "glide", .025f }, { "env2Decay", .14f } },
              { { env2, cutoff, .12f }, { vel, cutoff, .06f }, { m2, aPos, -.28f } }, "Hollow"),
        make ("Copper wire", "LEAD", "Mono Bright Velocity", "Articulate mono saw with a fading noise attack and warm lower octave.",
              { { "wtPos", .78f }, { "level", .4f }, { "cutoff", 3400 }, { "reso", .85f }, { "attack", .008f }, { "decay", .3f }, { "sustain", .76f },
                { "release", .18f }, { "gain", -12 }, { "wtTable", saw }, { "oscBOn", 1 }, { "wtTableB", odd }, { "wtPosB", .35f }, { "levelB", .15f },
                { "oscBPitch", -12 }, { "noiseOn", 1 }, { "noiseLevel", .006f }, { "env2Decay", .095f }, { "voiceMode", 1 }, { "glide", .035f },
                { "delayMix", .08f }, { "delayFeedback", .21f } },
              { { env2, noiseLevel, .018f }, { vel, cutoff, .08f }, { m2, bLevel, .17f } }, "Body"),

        // ---- Bass
        make ("Rubber Floor", "BASS", "Mono Pluck Env2", "Snappy saw bass; Env 2 opens a 24 dB filter on every note.",
              { { "subRoute", 2 }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", saw }, { "wtPos", .85f }, { "filterType", 1 }, { "cutoff", 180 }, { "reso", 1.6f }, { "attack", .002f }, { "decay", .32f },
                { "sustain", .85f }, { "release", .085f }, { "env2Decay", .16f }, { "env2Release", .15f }, { "subOn", 1 }, { "subLevel", .3f },
                { "distOn", 1 }, { "distDrive", 3 }, { "distMix", .6f }, { "gain", -12.2f } },
              { { vel, cutoff, .07f }, { env2, cutoff, .42f }, { m2, fxDrive, .5f } }, "Drive"),
        make ("Reese Current", "BASS", "Mono Wide Detuned", "Two detuned saw stacks drift against each other under a slow filter.",
              { { "subRoute", 2 }, { "oscBSpread", .3f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", saw }, { "wtPos", 1 }, { "oscAUnison", 4 }, { "oscADetune", 18 }, { "oscASpread", .3f }, { "oscBOn", 1 },
                { "wtTableB", saw }, { "wtPosB", 1 }, { "levelB", .5f }, { "oscBFine", -9 }, { "oscBUnison", 4 }, { "oscBDetune", 19 },
                { "filterType", 1 }, { "cutoff", 900 }, { "reso", .9f }, { "lfo1Rate", .25f }, { "subOn", 1 }, { "subLevel", .35f },
                { "attack", .012f }, { "sustain", 1 }, { "release", .15f }, { "gain", -10.2f } },
              { { vel, aPos, -.08f }, { lfo1, cutoff, .08f, true }, { m2, aSpread, .6f } }, "Width", { triangle, sine }),
        make ("Acid Wire", "BASS", "Mono Acid Resonant", "Squelchy resonant line; velocity accents the filter sweep.",
              { { "decay", .24f }, { "attack", .002f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", basic }, { "wtPos", .5f }, { "filterType", 1 }, { "cutoff", 260 }, { "reso", 5.6f }, { "env2Decay", .14f },
                { "sustain", .8f }, { "release", .08f }, { "distOn", 1 }, { "distMode", 1 }, { "distDrive", 9 }, { "distMix", .6f }, { "gain", -15.4f } },
              { { vel, aPos, .035f }, { env2, cutoff, .45f }, { vel, cutoff, .15f }, { m2, reso, .25f } }, "Accent"),
        make ("Wobble Engine", "BASS", "Mono Wobble Sync", "Tempo-synced filter wobble with a growl you can push.",
              { { "subRoute", 2 }, { "release", .11f }, { "attack", .006f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", basic }, { "wtPos", .38f }, { "oscBOn", 1 }, { "wtTableB", odd }, { "wtPosB", .72f }, { "levelB", .4f },
                { "lfo1Sync", 1 }, { "lfo1Division", dEighth }, { "filterType", 1 }, { "cutoff", 150 }, { "reso", 2.5f },
                { "subOn", 1 }, { "subLevel", .3f }, { "distOn", 1 }, { "distDrive", 10 }, { "distMix", .5f }, { "sustain", 1 }, { "gain", -18.3f } },
              { { vel, cutoff, .045f }, { lfo1, cutoff, .45f }, { m2, aPos, .5f } }, "Growl"),
        make ("Sub Pressure", "BASS", "Mono 808 Sub", "Sine body with a pitch drop and soft saturation, for long low notes.",
              { { "subRoute", 2 }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", basic }, { "wtPos", 0 }, { "env2Decay", .065f }, { "subOn", 1 }, { "subLevel", .28f }, { "distOn", 1 },
                { "distDrive", 8 }, { "distMix", .4f }, { "attack", .001f }, { "decay", 1.25f }, { "sustain", .6f }, { "release", .22f },
                { "cutoff", 2000 }, { "gain", -15.2f } },
              { { vel, aPos, .025f }, { env2, aPitch, .12f }, { m2, fxDrive, .12f } }, "Heat"),
        make ("Fold Bass", "BASS", "Mono Fold Gritty", "Wavefolded triangle; a slow LFO reshapes the folds.",
              { { "release", .14f }, { "sustain", .7f }, { "decay", .38f }, { "attack", .004f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", sineTri }, { "wtPos", .45f }, { "distOn", 1 }, { "distMode", 2 }, { "distDrive", 11 }, { "distMix", .8f },
                { "cutoff", 3000 }, { "env2Decay", .3f }, { "lfo2Rate", .3f }, { "gain", -14.5f } },
              { { vel, aPos, .1f }, { env2, cutoff, .3f }, { lfo2, aPos, .2f, true }, { m2, fxDrive, .4f } }, "Fold", { sine, triangle }),
        make ("Pulse Pump", "BASS", "Mono Sidechain Sync", "A pulse bass that ducks on every beat, like a sidechained mix.",
              { { "release", .09f }, { "attack", .004f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", pulse }, { "wtPos", .2f }, { "level", .18f }, { "lfo1Sync", 1 }, { "lfo1Division", dQuarter }, { "lfo1Mode", 2 },
                { "cutoff", 1200 }, { "subOn", 1 }, { "subLevel", .1f }, { "sustain", 1 }, { "gain", -15.3f } },
              { { vel, cutoff, .06f }, { lfo1, aLevel, .6f }, { lfo1, subLevel, .4f }, { m2, aPos, .5f } }, "Width", { rampUp, sine }),
        make ("Low Glass", "BASS", "Mono Bell Sub", "Bell harmonics on the attack melt into a solid sub.",
              { { "subRoute", 2 }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", bells }, { "wtPos", .12f }, { "subOn", 1 }, { "subLevel", .35f }, { "env2Decay", .16f }, { "cutoff", 2500 },
                { "attack", .002f }, { "decay", .48f }, { "sustain", .5f }, { "release", .2f }, { "gain", -9.1f } },
              { { vel, aPos, .13f }, { env2, aPos, .4f }, { m2, subLevel, .4f } }, "Sub"),

        // ---- Lead
        make ("Supernova Saw", "LEAD", "Poly Supersaw Wide", "Eight-voice saw stacks with ping-pong echoes.",
              { { "wtTable", saw }, { "wtPos", 1 }, { "oscAUnison", 8 }, { "oscADetune", 18 }, { "oscASpread", .9f }, { "oscBOn", 1 },
                { "wtTableB", saw }, { "wtPosB", .9f }, { "oscBPitch", 12 }, { "levelB", .25f }, { "oscBUnison", 4 }, { "oscBDetune", 11 },
                { "oscBSpread", .7f }, { "cutoff", 9000 }, { "reso", .8f }, { "attack", .018f }, { "decay", .4f }, { "sustain", .9f },
                { "release", .28f }, { "delayPingPong", 1 }, { "delayFeedback", .24f }, { "delayMix", .18f }, { "reverbMix", .15f }, { "gain", 0.2f } },
              { { vel, cutoff, .045f }, { m2, aDetune, .4f } }, "Detune"),
        make ("Pulse Runner", "LEAD", "Mono PWM", "Pulse width sweeps under a lively filter contour.",
              { { "release", .18f }, { "sustain", .72f }, { "decay", .35f }, { "attack", .008f }, { "wtPos", .22f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", pulse }, { "lfo1Rate", .4f }, { "oscAUnison", 2 }, { "oscADetune", 6 }, { "cutoff", 5000 }, { "reso", 1.2f },
                { "env2Decay", .22f }, { "delayDivision", dQuarter }, { "delayMix", .15f }, { "gain", -8.1f } },
              { { vel, cutoff, .07f }, { lfo1, aPos, .35f }, { env2, cutoff, .2f }, { m2, aPos, .3f } }, "Width", { triangle, sine }),
        make ("Formant Talker", "LEAD", "Mono Vocal Formant", "Vowel-like formants move in time with the beat.",
              { { "release", .16f }, { "sustain", .78f }, { "decay", .28f }, { "attack", .014f }, { "wtPos", .12f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", formant }, { "lfo1Sync", 1 }, { "lfo1Division", dQuarterDot }, { "cutoff", 6000 }, { "distOn", 1 },
                { "distDrive", 6 }, { "distMix", .3f }, { "gain", -9.5f } },
              { { vel, cutoff, .07f }, { lfo1, aPos, .48f }, { m2, aPos, .28f } }, "Vowel", { triangle, sine }),
        make ("Sync Bite", "LEAD", "Mono Aggressive", "Phase-bent attack sweep over a sub-octave saw.",
              { { "release", .14f }, { "sustain", .75f }, { "decay", .3f }, { "attack", .004f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", bend }, { "wtPos", .18f }, { "env2Decay", .22f }, { "env2Sustain", .2f }, { "oscBOn", 1 }, { "wtTableB", saw },
                { "wtPosB", 1 }, { "oscBPitch", -12 }, { "levelB", .3f }, { "cutoff", 7000 }, { "reso", 1 }, { "distOn", 1 }, { "distMode", 1 },
                { "distDrive", 4 }, { "distMix", .35f }, { "gain", -12.7f } },
              { { vel, aPos, .08f }, { env2, aPos, .45f }, { m2, fxDrive, .5f } }, "Drive"),
        make ("Square Hero", "LEAD", "Mono Retro Vibrato", "Hollow square lead with gentle vibrato and an octave shimmer.",
              { { "delayFeedback", .24f }, { "release", .18f }, { "sustain", .85f }, { "decay", .4f }, { "attack", .008f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", odd }, { "wtPos", 1 }, { "oscBOn", 1 }, { "wtTableB", odd }, { "wtPosB", 1 }, { "oscBPitch", 12 }, { "oscBFine", 6 },
                { "levelB", .16f }, { "lfo1Rate", 5.5f }, { "cutoff", 5000 }, { "delayDivision", dEighth }, { "delayMix", .2f }, { "reverbMix", .12f },
                { "gain", -12 } },
              { { vel, cutoff, .065f }, { lfo1, aPitch, .008f, true }, { lfo1, bPitch, .008f, true }, { m2, bLevel, .4f } }, "Octave"),
        make ("Bitter Lead", "LEAD", "Mono Fold Bright", "Folded saw, velocity-bright, cleaned by a serial high-pass.",
              { { "release", .14f }, { "sustain", .7f }, { "decay", .23f }, { "attack", .005f }, { "glide", .025f }, { "voiceMode", 1 }, { "wtTable", basic }, { "wtPos", .75f }, { "distOn", 1 }, { "distMode", 2 }, { "distDrive", 10 }, { "distMix", .5f },
                { "filter2On", 1 }, { "filter2Type", 2 }, { "cutoff2", 180 }, { "cutoff", 6000 }, { "gain", -10.3f } },
              { { env2, aPos, -.12f }, { vel, cutoff, .2f }, { m2, fxDrive, .5f } }, "Drive"),

        // ---- Pad
        make ("Glacier Hall", "PAD", "Poly Wide Shimmer", "Slow triangle choir with a bell octave in a large hall.",
              { { "lfo2Rate", .043f }, { "wtTable", sineTri }, { "wtPos", .4f }, { "oscAUnison", 6 }, { "oscADetune", 14 }, { "oscASpread", 1 }, { "oscBOn", 1 },
                { "wtTableB", bells }, { "wtPosB", .3f }, { "levelB", .14f }, { "oscBPitch", 12 }, { "attack", 1.7f }, { "decay", 2 },
                { "sustain", .8f }, { "release", 3.3f }, { "cutoff", 3200 }, { "reso", .7f }, { "lfo1Rate", .07f }, { "reverbSize", .9f },
                { "reverbDamping", .5f }, { "reverbMix", .38f }, { "gain", -13.4f } },
              { { vel, cutoff, .04f }, { lfo2, bPos, .08f, true }, { lfo1, aPos, .3f, true }, { m2, bLevel, .4f } }, "Shimmer"),
        make ("Dust Choir", "PAD", "Poly Vocal Air", "Breathy formant voices; noise runs through its own high-pass in parallel.",
              { { "reverbDamping", .65f }, { "wtTable", formant }, { "wtPos", .45f }, { "oscAUnison", 5 }, { "oscADetune", 18 }, { "oscASpread", .9f }, { "lfo1Rate", .11f },
                { "lfo2Rate", .23f }, { "noiseOn", 1 }, { "noiseLevel", .018f }, { "noiseRoute", 1 }, { "filter2On", 1 }, { "filter2Type", 2 },
                { "cutoff2", 4000 }, { "filterRouting", 1 }, { "cutoff", 2600 }, { "attack", 1.2f }, { "sustain", .85f }, { "release", 2.6f },
                { "reverbSize", .8f }, { "reverbMix", .4f }, { "gain", -7.3f } },
              { { vel, cutoff, .045f }, { lfo1, aPos, .4f, true }, { lfo2, cutoff, .1f, true }, { m2, noiseLevel, .3f } }, "Breath", { triangle, sine }),
        make ("Warm Blanket", "PAD", "Poly Warm Analog", "Soft saw ensemble with a slow filter bloom.",
              { { "reverbDamping", .72f }, { "wtTable", saw }, { "wtPos", .55f }, { "oscAUnison", 7 }, { "oscADetune", 8 }, { "oscASpread", .8f }, { "oscBOn", 1 },
                { "wtTableB", sineTri }, { "oscBPitch", -12 }, { "levelB", .24f }, { "filterType", 1 }, { "cutoff", 1400 }, { "reso", .8f },
                { "env2Attack", 1.4f }, { "env2Decay", 3 }, { "env2Sustain", .5f }, { "env2Release", 2 }, { "attack", .8f }, { "sustain", .9f },
                { "release", 2.5f }, { "reverbMix", .3f }, { "gain", -8.8f } },
              { { vel, cutoff, .05f }, { env2, cutoff, .15f }, { m2, bLevel, .4f } }, "Body"),
        make ("Night Drift", "PAD", "Poly Evolving Echo", "Phase-bent table drifts over two bars; echoes bounce across.",
              { { "wtTable", bend }, { "wtPos", .2f }, { "oscBOn", 1 }, { "wtTableB", pulse }, { "wtPosB", .42f }, { "oscBFine", 9 }, { "levelB", .3f },
                { "lfo1Sync", 1 }, { "lfo1Division", d2bars }, { "lfo1Mode", 2 }, { "lfo2Rate", .073f }, { "delayDivision", dQuarterDot },
                { "delayFeedback", .34f }, { "delayMix", .25f }, { "delayPingPong", 1 }, { "reverbMix", .35f }, { "cutoff", 2400 },
                { "attack", 1 }, { "sustain", .85f }, { "release", 2.5f }, { "gain", -11.6f } },
              { { vel, aPos, .07f }, { lfo1, aPos, .5f }, { lfo2, bPos, .4f, true }, { m2, aPos, .4f } }, "Drift", { triangle, sine }),
        make ("Aurora Strings", "PAD", "Poly Strings Vibrato", "Bright string ensemble with a light vibrato.",
              { { "lfo2Rate", .13f }, { "wtTable", saw }, { "wtPos", .9f }, { "oscAUnison", 8 }, { "oscADetune", 13 }, { "oscASpread", 1 }, { "filter2On", 1 },
                { "filter2Type", 2 }, { "cutoff2", 170 }, { "cutoff", 4500 }, { "lfo1Rate", 4.8f }, { "attack", .42f }, { "decay", 1 },
                { "sustain", .85f }, { "release", 1.55f }, { "reverbSize", .7f }, { "reverbMix", .3f }, { "gain", -2.2f } },
              { { vel, cutoff, .045f }, { lfo2, aPos, -.06f }, { lfo1, aPitch, .003f, true }, { m2, aDetune, .4f } }, "Ensemble"),
        make ("Hollow Cathedral", "PAD", "Poly Band Huge", "Band-passed organ tones in an enormous space.",
              { { "wtTable", odd }, { "wtPos", .4f }, { "oscAUnison", 4 }, { "oscADetune", 9 }, { "oscBOn", 1 }, { "wtTableB", basic },
                { "oscBPitch", 12 }, { "levelB", .24f }, { "filterType", 4 }, { "cutoff", 1100 }, { "reso", 1.4f }, { "lfo1Rate", .09f },
                { "reverbSize", 1 }, { "reverbDamping", .52f }, { "reverbMix", .43f }, { "attack", 1.8f }, { "sustain", .9f }, { "release", 4 },
                { "gain", -2 } },
              { { vel, cutoff, .045f }, { lfo1, cutoff, .12f, true }, { m2, reso, .3f } }, "Focus"),

        // ---- Pluck
        make ("Kalimba Drop", "PLUCK", "Poly Mallet Echo", "Woody tine with a bell overtone and dotted echoes.",
              { { "wtTable", sineTri }, { "wtPos", .3f }, { "oscBOn", 1 }, { "wtTableB", bells }, { "wtPosB", .5f }, { "oscBPitch", 12 },
                { "levelB", .18f }, { "attack", .001f }, { "decay", .5f }, { "sustain", 0 }, { "release", .32f }, { "env2Decay", .085f },
                { "cutoff", 1500 }, { "reverbMix", .25f }, { "delayMix", .2f }, { "delayFeedback", .24f }, { "gain", -8.6f } },
              { { vel, bLevel, .09f }, { env2, cutoff, .35f }, { m2, bLevel, .4f } }, "Tine"),
        make ("Paper Pluck", "PLUCK", "Poly Saw Echo", "Tight 24 dB saw pluck with a ping-pong tail.",
              { { "attack", .002f }, { "wtTable", saw }, { "wtPos", 1 }, { "filterType", 1 }, { "cutoff", 250 }, { "reso", 1.5f }, { "env2Decay", .145f },
                { "decay", .35f }, { "sustain", 0 }, { "release", .23f }, { "oscAUnison", 3 }, { "oscADetune", 6 }, { "oscASpread", .6f },
                { "delayPingPong", 1 }, { "delayMix", .22f }, { "delayFeedback", .28f }, { "gain", 1.9f } },
              { { vel, cutoff, .06f }, { env2, cutoff, .5f }, { m2, reso, .3f } }, "Snap"),
        make ("Marimba Grain", "PLUCK", "Poly Mallet", "Sine body with a two-octave transient from oscillator B.",
              { { "attack", .001f }, { "wtTable", basic }, { "wtPos", 0 }, { "oscBOn", 1 }, { "wtTableB", odd }, { "wtPosB", .6f }, { "oscBPitch", 24 }, { "levelB", .02f },
                { "decay", .38f }, { "sustain", 0 }, { "release", .19f }, { "env2Decay", .035f }, { "cutoff", 5000 }, { "reverbMix", .2f }, { "gain", -8.1f } },
              { { vel, bLevel, .09f }, { env2, bLevel, .4f }, { m2, bLevel, .3f } }, "Strike"),
        make ("Digital Rain", "PLUCK", "Poly Digital Echo", "Phase-bent blips scatter into sixteenth-note echoes.",
              { { "attack", .002f }, { "wtTable", bend }, { "wtPos", .1f }, { "env2Decay", .105f }, { "decay", .25f }, { "sustain", 0 }, { "release", .42f },
                { "delayDivision", dSixteenth }, { "delayFeedback", .4f }, { "delayMix", .3f }, { "delayPingPong", 1 }, { "reverbMix", .3f },
                { "gain", -4.5f } },
              { { vel, aPos, .1f }, { env2, aPos, .4f }, { m2, aPos, .5f } }, "Bend"),
        make ("Harp Mist", "PLUCK", "Poly Soft Hall", "Soft triangle harp ringing out in a misty hall.",
              { { "wtTable", sineTri }, { "wtPos", .55f }, { "oscAUnison", 3 }, { "oscADetune", 5 }, { "oscASpread", .7f }, { "attack", .001f },
                { "decay", 1.4f }, { "sustain", 0 }, { "release", 1.05f }, { "env2Decay", .28f }, { "cutoff", 1800 }, { "reverbSize", .75f },
                { "reverbMix", .35f }, { "gain", -3 } },
              { { vel, cutoff, .08f }, { env2, cutoff, .3f }, { m2, aPos, .3f } }, "Colour"),

        // ---- Keys
        make ("Velvet EP", "KEYS", "Poly EPiano Tremolo", "Electric-piano body with a velocity tine and auto-pan.",
              { { "wtTable", sineTri }, { "wtPos", .15f }, { "oscBOn", 1 }, { "wtTableB", bells }, { "oscBPitch", 12 }, { "levelB", .08f },
                { "env2Decay", .65f }, { "attack", .002f }, { "decay", 2.5f }, { "sustain", .22f }, { "release", .4f }, { "cutoff", 4000 },
                { "lfo1Rate", 3.8f }, { "lfo1Mode", 1 }, { "reverbMix", .18f }, { "gain", -13.8f } },
              { { vel, cutoff, .04f }, { vel, bLevel, .3f }, { env2, bLevel, .25f }, { lfo1, aPan, .25f, true }, { m2, bLevel, .3f } }, "Tine"),
        make ("Crystal Bells", "KEYS", "Poly Bell Long", "Two detuned bell tables with long ringing decay.",
              { { "attack", .002f }, { "wtTable", bells }, { "wtPos", .42f }, { "oscBOn", 1 }, { "wtTableB", bells }, { "oscBPitch", 12 }, { "oscBFine", 4 }, { "levelB", .24f },
                { "decay", 3 }, { "sustain", 0 }, { "release", 2.4f }, { "env2Decay", .7f }, { "reverbSize", .8f }, { "reverbMix", .35f }, { "gain", -14.9f } },
              { { vel, aPos, .08f }, { env2, aPos, .3f }, { m2, aPos, .4f } }, "Metal"),
        make ("Toy Organ", "KEYS", "Poly Organ Tremolo", "Square drawbars with a sub and a wobbly tremolo.",
              { { "wtTable", odd }, { "wtPos", .5f }, { "oscBOn", 1 }, { "wtTableB", odd }, { "wtPosB", .2f }, { "oscBPitch", 12 }, { "levelB", .32f },
                { "subOn", 1 }, { "subLevel", .2f }, { "attack", .003f }, { "sustain", 1 }, { "release", .065f }, { "lfo1Rate", 5.4f }, { "lfo1Mode", 1 },
                { "cutoff", 6000 }, { "reverbMix", .15f }, { "gain", -17.7f } },
              { { vel, bLevel, .04f }, { lfo1, bLevel, .04f, true }, { lfo1, aLevel, .08f, true }, { m2, bLevel, .4f } }, "Drawbar"),
        make ("Mallet Hall", "KEYS", "Poly Mallet Hall", "Sine mallet with a bright strike in a medium hall.",
              { { "attack", .001f }, { "wtTable", basic }, { "wtPos", 0 }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "wtPosB", 1 }, { "oscBPitch", 24 }, { "levelB", .025f },
                { "env2Decay", .045f }, { "decay", .8f }, { "sustain", 0 }, { "release", .55f }, { "cutoff", 6000 }, { "reverbSize", .58f },
                { "reverbMix", .3f }, { "gain", -8.9f } },
              { { vel, bLevel, .1f }, { env2, bLevel, .5f }, { m2, bLevel, .3f } }, "Strike"),
        make ("Soft Clav", "KEYS", "Poly Funky Pluck", "Narrow pulse with a short filter blip.",
              { { "attack", .002f }, { "wtTable", pulse }, { "wtPos", .6f }, { "cutoff", 2200 }, { "reso", 1.2f }, { "env2Decay", .09f }, { "decay", .28f },
                { "sustain", .12f }, { "release", .07f }, { "gain", -8.7f } },
              { { vel, cutoff, .09f }, { env2, cutoff, .3f }, { m2, aPos, .4f } }, "Width"),

        // ---- Synth
        make ("Brass Section", "SYNTH", "Poly Brass Swell", "Slow filter swell and a hint of vibrato, like synth brass.",
              { { "wtTable", saw }, { "wtPos", 1 }, { "oscAUnison", 3 }, { "oscADetune", 7 }, { "oscASpread", .5f }, { "env2Attack", .055f },
                { "env2Decay", .4f }, { "env2Sustain", .4f }, { "cutoff", 500 }, { "reso", 1 }, { "attack", .022f }, { "decay", .5f },
                { "sustain", .85f }, { "release", .2f }, { "lfo1Rate", 5 }, { "reverbMix", .15f }, { "gain", -5.5f } },
              { { vel, cutoff, .06f }, { env2, cutoff, .35f }, { lfo1, aPitch, .003f, true }, { m2, aDetune, .4f } }, "Section"),
        make ("Chord Stab", "SYNTH", "Poly Stab House", "Short detuned stab with saturation and dotted echoes.",
              { { "delayFeedback", .25f }, { "attack", .002f }, { "wtTable", basic }, { "wtPos", .6f }, { "oscAUnison", 4 }, { "oscADetune", 12 }, { "oscASpread", .8f }, { "oscBOn", 1 },
                { "wtTableB", saw }, { "wtPosB", 1 }, { "oscBPitch", 12 }, { "levelB", .25f }, { "decay", .21f }, { "sustain", 0 }, { "release", .16f },
                { "env2Decay", .11f }, { "cutoff", 800 }, { "distOn", 1 }, { "distDrive", 6 }, { "distMix", .4f }, { "delayDivision", dQuarterDot },
                { "delayPingPong", 1 }, { "delayMix", .2f }, { "reverbMix", .2f }, { "gain", 0.5f } },
              { { vel, cutoff, .06f }, { env2, cutoff, .35f }, { m2, fxDrive, .5f } }, "Drive"),
        make ("Retro Poly", "SYNTH", "Poly Analog PWM", "Two pulse oscillators with slow width motion.",
              { { "lfo2Rate", .37f }, { "sustain", .68f }, { "decay", .55f }, { "attack", .009f }, { "wtTable", pulse }, { "wtPos", .25f }, { "oscBOn", 1 }, { "wtTableB", pulse }, { "wtPosB", .42f }, { "oscBFine", 5 }, { "levelB", .4f },
                { "lfo1Rate", .6f }, { "cutoff", 3000 }, { "reso", 1.3f }, { "env2Decay", .5f }, { "env2Sustain", .3f }, { "release", .4f },
                { "gain", -15.6f } },
              { { vel, cutoff, .05f }, { lfo2, bPos, .12f, true }, { lfo1, aPos, .25f, true }, { env2, cutoff, .2f }, { m2, bPos, .4f } }, "Width", { triangle, sine }),
        make ("Neon Sweep", "SYNTH", "Poly Phaser Sweep", "Supersaw through a moving notch; Tone sweeps the main filter.",
              { { "release", .55f }, { "sustain", .72f }, { "decay", .8f }, { "attack", .075f }, { "wtTable", saw }, { "wtPos", 1 }, { "oscAUnison", 6 }, { "oscADetune", 16 }, { "oscASpread", .9f }, { "cutoff", 1200 },
                { "filter2On", 1 }, { "filter2Type", 5 }, { "cutoff2", 1500 }, { "reso2", 1.2f }, { "lfo2Rate", .11f }, { "reverbMix", .25f },
                { "gain", 0.9f } },
              { { vel, cutoff, .05f }, { lfo2, cutoff2, .3f, true }, { m2, reso2, .3f } }, "Notch", { sine, triangle }),
        make ("Metal Grid", "SYNTH", "Poly Metallic Gritty", "Clashing bent and bell tables with hard clipping.",
              { { "env2Decay", .18f }, { "release", .24f }, { "sustain", .6f }, { "decay", .48f }, { "attack", .003f }, { "wtTable", bend }, { "wtPos", .62f }, { "oscBOn", 1 }, { "wtTableB", bells }, { "wtPosB", .9f }, { "oscBPitch", 7 }, { "levelB", .24f },
                { "distOn", 1 }, { "distMode", 1 }, { "distDrive", 6 }, { "distMix", .3f }, { "cutoff", 5000 }, { "gain", -14.2f } },
              { { vel, aPos, .07f }, { env2, aPos, .16f }, { m2, fxDrive, .5f } }, "Drive"),

        // ---- Arp / Seq
        make ("Gate Arp", "ARP", "Poly Gate Sync", "Sixteenth-note gate on a bright saw. Hold a chord.",
              { { "delayFeedback", .26f }, { "release", .09f }, { "attack", .003f }, { "wtTable", saw }, { "wtPos", 1 }, { "level", .05f }, { "lfo1Sync", 1 }, { "lfo1Division", dSixteenth }, { "cutoff", 2500 },
                { "reso", 1.5f }, { "env2Decay", .22f }, { "sustain", 1 }, { "delayMix", .2f }, { "gain", -10 } },
              { { vel, cutoff, .07f }, { lfo1, aLevel, .9f }, { env2, cutoff, .25f }, { m2, reso, .3f } }, "Bite", { square, sine }),
        make ("Sequence Lab", "ARP", "Poly Seq Sync", "Saw-shaped filter steps with a one-bar timbre cycle.",
              { { "lfo2Mode", 2 }, { "delayFeedback", .28f }, { "release", .13f }, { "attack", .004f }, { "wtTable", pulse }, { "wtPos", .28f }, { "lfo1Sync", 1 }, { "lfo1Division", dEighth }, { "lfo2Sync", 1 }, { "lfo2Division", d1bar },
                { "filterType", 1 }, { "cutoff", 300 }, { "reso", 2 }, { "sustain", 1 }, { "delayDivision", dQuarterDot }, { "delayPingPong", 1 },
                { "delayMix", .25f }, { "gain", -10.9f } },
              { { vel, cutoff, .06f }, { lfo1, cutoff, .35f }, { lfo2, aPos, .4f }, { m2, reso, .3f } }, "Reso", { rampDown, triangle }),
        make ("Bounce Keys", "ARP", "Poly Bounce Echo", "Filter bounces on sixteenths into sixteenth echoes.",
              { { "release", .12f }, { "attack", .003f }, { "wtTable", sineTri }, { "wtPos", .5f }, { "lfo1Sync", 1 }, { "lfo1Division", dSixteenth }, { "cutoff", 750 }, { "sustain", 1 },
                { "delayDivision", dSixteenth }, { "delayFeedback", .28f }, { "delayMix", .3f }, { "gain", -9.6f } },
              { { vel, aPos, .1f }, { lfo1, cutoff, .3f }, { m2, aPos, .4f } }, "Colour", { square, sine }),
        make ("Ladder Steps", "ARP", "Poly Seq Octave", "Triplet table climb with an octave layer on the beat.",
              { { "delayFeedback", .27f }, { "release", .16f }, { "attack", .004f }, { "wtTable", basic }, { "wtPos", .32f }, { "oscBOn", 1 }, { "wtTableB", odd }, { "oscBPitch", 12 }, { "levelB", 0 }, { "lfo1Sync", 1 },
                { "lfo1Division", dEighthTri }, { "lfo2Sync", 1 }, { "lfo2Division", dQuarter }, { "cutoff", 4000 }, { "sustain", 1 },
                { "delayDivision", dEighth }, { "delayPingPong", 1 }, { "delayMix", .2f }, { "gain", -14.5f } },
              { { vel, cutoff, .07f }, { lfo1, aPos, .5f }, { lfo2, bLevel, .4f }, { m2, aPos, .3f } }, "Climb", { rampUp, square }),

        // ---- SFX / Soundscape
        make ("Rising Tide", "SFX", "Riser Noise", "Noise and a quiet wave rise over four bars. Hold a note.",
              { { "reverbDamping", .68f }, { "level", .1f }, { "noiseOn", 1 }, { "noiseLevel", .42f }, { "lfo1Sync", 1 }, { "lfo1Division", 0 }, { "filterType", 1 },
                { "cutoff", 200 }, { "reso", 3 }, { "attack", .8f }, { "sustain", 1 }, { "release", 2.2f }, { "reverbSize", .9f }, { "reverbMix", .5f },
                { "gain", -13.4f } },
              { { vel, cutoff, .04f }, { lfo1, cutoff, .6f }, { m2, cutoff, .4f } }, "Rise", { rampUp, sine }),
        make ("Laser Zap", "SFX", "Zap Pitch Echo", "A fast pitch dive bounced around by echoes.",
              { { "attack", .001f }, { "wtTable", basic }, { "wtPos", .5f }, { "env2Decay", .16f }, { "decay", .24f }, { "sustain", 0 }, { "release", .2f },
                { "delayDivision", dSixteenth }, { "delayFeedback", .36f }, { "delayMix", .3f }, { "delayPingPong", 1 }, { "gain", -5.8f } },
              { { vel, aPos, .1f }, { env2, aPitch, .45f }, { m2, aPos, .5f } }, "Shape"),
        make ("Wind Tunnel", "SFX", "Noise Atmosphere", "Band-passed noise sweeping like wind through a corridor.",
              { { "lfo2Rate", .071f }, { "reverbDamping", .7f }, { "oscAOn", 0 }, { "noiseOn", 1 }, { "noiseLevel", .6f }, { "filterType", 4 }, { "cutoff", 900 }, { "reso", 2.4f }, { "lfo1Rate", .13f },
                { "lfo1Mode", 1 }, { "reverbSize", .85f }, { "reverbMix", .4f }, { "attack", 1.1f }, { "sustain", 1 }, { "release", 2.4f }, { "gain", -5.2f } },
              { { vel, cutoff, .035f }, { lfo2, noiseLevel, .045f, true }, { lfo1, cutoff, .25f, true }, { m2, reso, .3f } }, "Whistle"),
        make ("Glitch Field", "SFX", "Glitch Rhythmic", "Thirty-second-note table jumps, folded and scattered.",
              { { "release", .13f }, { "attack", .003f }, { "wtTable", bend }, { "wtPos", .12f }, { "lfo1Sync", 1 }, { "lfo1Division", dThirtySecond }, { "lfo2Rate", 3.3f }, { "distOn", 1 },
                { "distMode", 2 }, { "distDrive", 8 }, { "distMix", .5f }, { "delayDivision", dSixteenthTri }, { "delayFeedback", .36f },
                { "delayMix", .3f }, { "sustain", 1 }, { "gain", -14.2f } },
              { { vel, cutoff, .06f }, { lfo1, aPos, .8f }, { lfo2, cutoff, .3f }, { m2, fxDrive, .5f } }, "Crush", { square, rampDown }),
        make ("Deep Space Drone", "SFX", "Drone Atmosphere Wide", "A vast detuned drone with slowly turning tables.",
              { { "wtTable", formant }, { "wtPos", .3f }, { "oscAPitch", -12 }, { "oscAUnison", 8 }, { "oscADetune", 24 }, { "oscASpread", 1 },
                { "oscBOn", 1 }, { "wtTableB", bells }, { "wtPosB", .48f }, { "oscBPitch", 7 }, { "levelB", .2f }, { "lfo1Rate", .03f },
                { "lfo1Mode", 1 }, { "lfo2Rate", .041f }, { "lfo2Mode", 1 }, { "reverbSize", 1 }, { "reverbMix", .46f }, { "delayDivision", dHalf },
                { "delayFeedback", .42f }, { "delayMix", .3f }, { "attack", 3 }, { "sustain", 1 }, { "release", 5 }, { "gain", -15.0f } },
              { { vel, cutoff, .035f }, { lfo1, aPos, .5f, true }, { lfo2, bPos, .35f, true }, { m2, bLevel, .4f } }, "Stars"),
        // FM: OSC B is a silent sine modulator; these spectra need the A/B interaction.
        make ("FM Glass Keys", "KEYS", "Poly FM Electric Velocity", "Sine carrier, octave modulator: a struck tine that mellows as Env 2 lowers the FM depth. Strike adds bite.",
              { { "oscBOn", 0 }, { "oscBPitch", 12 }, { "fmDepth", .08f }, { "env2Decay", .52f }, { "env2Release", .3f },
                { "attack", .002f }, { "decay", 1.6f }, { "sustain", .24f }, { "release", .38f }, { "cutoff", 9000 }, { "gain", -9 } },
              { { vel, cutoff, .04f }, { env2, fm, .3f }, { vel, fm, .15f }, { m2, fm, .3f } }, "Strike"),
        make ("FM Iron Bass", "BASS", "Mono FM Glide Punchy", "1:1 FM bass in mono with a short glide; Env 2 punches the depth, Grit adds more.",
              { { "subRoute", 2 }, { "voiceMode", 1 }, { "glide", .028f }, { "oscBOn", 0 }, { "fmDepth", .14f }, { "env2Decay", .13f }, { "env2Release", .1f },
                { "attack", .002f }, { "decay", .4f }, { "sustain", .8f }, { "release", .095f }, { "cutoff", 2400 }, { "subOn", 1 }, { "subLevel", .25f }, { "gain", -10 } },
              { { vel, fm, .09f }, { env2, fm, .35f }, { m2, fm, .35f } }, "Grit"),
        make ("FM Bell Tower", "PLUCK", "Poly FM Bell Inharmonic", "A 1:3.5 modulator rings inharmonic partials that fade with the note. Metal deepens the FM.",
              { { "cutoff", 9500 }, { "oscBOn", 0 }, { "oscBPitch", 22 }, { "oscBFine", -31 }, { "fmDepth", .065f }, { "env2Decay", 1.2f }, { "env2Release", 1.2f },
                { "attack", .001f }, { "decay", 2.8f }, { "sustain", 0 }, { "release", 1.4f }, { "reverbMix", .2f }, { "gain", -15.4f } },
              { { vel, fm, .075f }, { env2, fm, .25f }, { m2, fm, .3f } }, "Metal"),
        // Atelier: independent macro designs and original embedded harmonic tables.
        atelier ("Atelier - Velvet Sub", "BASS", "Atelier Poly Sub Warm Velocity", "Focused sub with a soft reed edge. Weight adds body; Edge opens harmonics. Play C1-C3.", 0,
            { { "lfo1Rate", .17f }, { "env2Release", .09f }, { "env2Decay", .16f }, { "wtPos", .08f }, { "cutoff", 700 }, { "subOn", 1 }, { "subLevel", .18f }, { "subRoute", 2 },
              { "attack", .006f }, { "decay", .35f }, { "sustain", .8f }, { "release", .1f }, { "distOn", 1 }, { "distDrive", 2 }, { "distMix", .25f } },
            { { env2, aPos, .075f }, { vel, cutoff, .08f }, { m1, subLevel, .16f }, { m2, aPos, .55f }, { m3, fxDrive, .15f }, { m4, cutoff, .12f }, { lfo1, aPos, .025f, true } },
            { "Weight", "Edge", "Heat", "Open" }, contour ({ { 0,.2f,.5f }, { .25f,.75f,-.5f }, { .8f,.35f,.4f }, { 1,.2f,0 } })),
        atelier ("Atelier - Knockwood", "BASS", "Atelier Poly Percussive Velocity", "Dry wooden knock with a pitch transient. Body adds a low fundamental; Bite opens the filter. Play C1-C3.", 2,
            { { "gain", -7 }, { "wtPos", .045f }, { "cutoff", 850 }, { "filterType", 1 }, { "env2Decay", .052f }, { "env2Release", .07f },
              { "attack", .002f }, { "decay", .29f }, { "sustain", .12f }, { "release", .075f }, { "macro1", .25f }, { "subOn", 1 }, { "subLevel", 0 }, { "subRoute", 2 } },
            { { vel, aPos, .07f }, { lfo1, aPos, .025f }, { env2, aPos, .4f }, { env2, aPitch, .025f }, { vel, cutoff, .09f }, { m1, aPos, .3f }, { m2, cutoff, .16f }, { m3, subLevel, .15f }, { m4, reso, .08f } },
            { "Wood", "Bite", "Body", "Hollow" }, contour ({ { 0,0,.7f }, { .12f,1,-.6f }, { .42f,.15f,.4f }, { 1,0,0 } })),
        atelier ("Atelier - Elastic Circuit", "BASS", "Atelier Poly Rhythmic Sync", "Asymmetric two-pulse spectral movement with a steady sub. Shape and Throat change the vowel-like motion. Play C1-C3.", 1,
            { { "lfo1Mode", 2 }, { "wtPos", .1f }, { "cutoff", 1300 }, { "lfo1Sync", 1 }, { "lfo1Division", dQuarter }, { "subOn", 1 }, { "subLevel", .16f }, { "subRoute", 2 },
              { "distOn", 1 }, { "distDrive", 4 }, { "distMix", .24f }, { "attack", .004f }, { "release", .12f }, { "sustain", .8f } },
            { { vel, cutoff, .05f }, { lfo1, aPos, .45f }, { lfo1, cutoff, .13f }, { vel, aLevel, .08f }, { m1, aPos, .2f }, { m2, reso, .12f }, { m3, fxDrive, .15f }, { m4, subLevel, .18f } },
            { "Shape", "Throat", "Heat", "Anchor" }, contour ({ { 0,0,.6f }, { .12f,1,-.8f }, { .4f,.12f,0 }, { .62f,.68f,-.5f }, { .9f,.05f,0 }, { 1,0,0 } })),
        atelier ("Atelier - Amber Voice", "LEAD", "Atelier Poly Soft Expressive", "Soft reed melody with restrained vibrato. Breath brightens the attack; Bloom adds an octave. Play C3-C6.", 0,
            { { "delayFeedback", .19f }, { "env2Release", .2f }, { "env2Decay", .24f }, { "env2Attack", .012f }, { "gain", -10 }, { "wtPos", .3f }, { "cutoff", 2800 }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "oscBPitch", 12 }, { "levelB", .04f },
              { "attack", .025f }, { "decay", .5f }, { "sustain", .75f }, { "release", .3f }, { "lfo1Rate", 5.1f }, { "delayOn", 1 }, { "reverbOn", 1 } },
            { { vel, aPos, .06f }, { lfo1, aPitch, .002f, true }, { vel, cutoff, .12f }, { env2, aPos, .12f }, { m1, cutoff, .12f }, { m2, bLevel, .14f }, { m3, fxDelay, .12f }, { m4, aPos, .35f } },
            { "Breath", "Bloom", "Trail", "Reed" }, contour ({ { 0,.5f,.2f }, { .2f,.95f,-.2f }, { .5f,.5f,.2f }, { .8f,.05f,-.2f }, { 1,.5f,0 } })),
        atelier ("Atelier - Neon Ribbon", "LEAD", "Atelier Poly Pulse Sync", "Hollow pulse layer against a moving spectral ridge. Ribbon shifts the ridge; Pulse changes the hollow layer. Play C3-C6.", 1,
            { { "delayFeedback", .2f }, { "env2Decay", .18f }, { "sustain", .76f }, { "decay", .38f }, { "gain", -10 }, { "wtPos", .25f }, { "oscBOn", 1 }, { "wtTableB", pulse }, { "wtPosB", .24f }, { "levelB", .12f }, { "cutoff", 5500 },
              { "lfo1Sync", 1 }, { "lfo1Division", dHalf }, { "attack", .012f }, { "release", .25f }, { "delayOn", 1 }, { "delayPingPong", 1 } },
            { { env2, aPos, .08f }, { lfo1, bPos, .18f, true }, { vel, cutoff, .08f }, { m1, aPos, .35f }, { m2, bPos, .35f }, { m3, bPan, .5f }, { m4, fxDelay, .14f } },
            { "Ribbon", "Pulse", "Offset", "Bounce" }, contour ({ { 0,.2f,.8f }, { .35f,.85f,-.5f }, { .7f,.4f,.5f }, { 1,.2f,0 } })),
        atelier ("Atelier - Horizon Brass", "LEAD", "Atelier Poly Wide Brass", "Broad silk stack with a filter bloom and bright octave edge. Fan widens the stack; Crest brings the upper voice. Play C3-C6.", 3,
            { { "gain", -3 }, { "wtPos", .7f }, { "oscAUnison", 5 }, { "oscADetune", 7 }, { "oscASpread", .55f }, { "oscBOn", 1 }, { "wtTableB", saw }, { "oscBPitch", 12 }, { "levelB", .06f },
              { "cutoff", 2600 }, { "env2Attack", .045f }, { "env2Decay", .36f }, { "env2Sustain", .25f }, { "attack", .018f }, { "release", .32f }, { "reverbOn", 1 }, { "lfo1Rate", .35f } },
            { { env2, bLevel, .035f }, { env2, cutoff, .12f }, { vel, cutoff, .09f }, { lfo1, aPos, .07f, true }, { m1, aSpread, .35f }, { m2, bLevel, .12f }, { m3, cutoff, .14f }, { m4, fxReverb, .16f } },
            { "Fan", "Crest", "Brass", "Stage" }, contour ({ { 0,.25f,.7f }, { .4f,.8f,-.4f }, { .75f,.45f,0 }, { 1,.25f,0 } })),
        atelier ("Atelier - Linen Sky", "PAD", "Atelier Poly Warm Wide", "Slow silk ensemble with independent upper shimmer. Weave opens harmonics; Halo adds the upper voice. Play C3-C5 chords.", 3,
            { { "reverbDamping", .67f }, { "lfo2Rate", .067f }, { "gain", -8 }, { "wtPos", .2f }, { "oscAUnison", 4 }, { "oscADetune", 8 }, { "oscASpread", .65f }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "oscBPitch", 12 }, { "levelB", .06f },
              { "attack", 1.05f }, { "decay", 1.3f }, { "sustain", .8f }, { "release", 2.2f }, { "cutoff", 2600 }, { "lfo1Rate", .11f }, { "reverbOn", 1 }, { "reverbMix", .25f } },
            { { lfo2, bPos, .08f, true }, { lfo1, aPos, .18f }, { vel, cutoff, .08f }, { m1, aPos, .3f }, { m2, bLevel, .1f }, { m3, aDetune, .12f }, { m4, fxReverb, .16f } },
            { "Weave", "Halo", "Drift", "Room" }, contour ({ { 0,.15f,.6f }, { .23f,.5f,-.3f }, { .65f,.9f,-.6f }, { 1,.15f,0 } })),
        atelier ("Atelier - Mineral Mist", "PAD", "Atelier Poly Texture Air", "Sparse harmonic texture with filtered air. Grain brings the upper partials; Air adds a separate high-pass noise layer. Play C3-C5 chords.", 2,
            { { "gain", -8 }, { "wtPos", .12f }, { "oscAUnison", 3 }, { "oscADetune", 11 }, { "oscASpread", .8f }, { "noiseOn", 1 }, { "noiseLevel", .006f },
              { "noiseRoute", 1 }, { "filter2On", 1 }, { "filter2Type", 2 }, { "filterRouting", 1 }, { "cutoff2", 4200 },
              { "attack", .85f }, { "sustain", .8f }, { "release", 2.5f }, { "lfo1Rate", .13f }, { "lfo2Rate", .19f }, { "reverbOn", 1 }, { "reverbMix", .28f } },
            { { lfo2, cutoff2, .035f, true }, { lfo1, aPos, .22f }, { lfo2, aPan, .12f, true }, { vel, aPos, .12f }, { m1, aPos, .35f }, { m2, noiseLevel, .025f }, { m3, cutoff2, -.08f }, { m4, fxReverb, .18f } },
            { "Grain", "Air", "Veil", "Distance" }, contour ({ { 0,.1f,.6f }, { .2f,.65f,-.5f }, { .45f,.3f,.5f }, { .8f,.9f,-.6f }, { 1,.1f,0 } })),
        atelier ("Atelier - Tidal Spectrum", "PAD", "Atelier Poly Evolving Sync", "A harmonic ridge rises twice over two bars while a low triangle anchors the chord. Tide shifts its colour; Current opens the filter. Play C3-C5.", 1,
            { { "lfo1Mode", 2 }, { "delayFeedback", .19f }, { "gain", -8 }, { "wtPos", .08f }, { "oscAUnison", 4 }, { "oscADetune", 7 }, { "oscASpread", .7f }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "levelB", .1f }, { "oscBPitch", -12 },
              { "lfo1Sync", 1 }, { "lfo1Division", d2bars }, { "lfo2Rate", .07f }, { "attack", .9f }, { "sustain", .8f }, { "release", 2 },
              { "delayOn", 1 }, { "delayDivision", dQuarterDot }, { "delayMix", .16f }, { "reverbOn", 1 }, { "reverbMix", .23f } },
            { { lfo2, bPos, .07f, true }, { lfo1, aPos, .45f }, { lfo2, cutoff, .06f, true }, { vel, bLevel, .05f }, { m1, aPos, .2f }, { m2, cutoff, .12f }, { m3, bLevel, .1f }, { m4, fxDelay, .13f } },
            { "Tide", "Current", "Anchor", "Wake" }, contour ({ { 0,0,.7f }, { .28f,.8f,-.3f }, { .45f,.25f,.4f }, { .72f,1,-.7f }, { 1,0,0 } })),
        atelier ("Atelier - Cedar Keys", "PLUCK", "Atelier Poly Wood Velocity", "Rounded wooden keys with a short harmonic knock. Mallet and Bark change the attack; Chamber adds a small room. Play C3-C6.", 0,
            { { "lfo1Rate", .37f }, { "gain", -8 }, { "wtPos", .05f }, { "cutoff", 2000 }, { "attack", .002f }, { "decay", .62f }, { "sustain", 0 }, { "release", .18f }, { "env2Decay", .09f },
              { "env2Release", .1f }, { "reverbOn", 1 }, { "reverbSize", .35f }, { "reverbMix", .1f } },
            { { vel, aPos, .065f }, { lfo1, aPan, .04f, true }, { env2, aPos, .5f }, { vel, cutoff, .12f }, { m1, aPos, .2f }, { m2, cutoff, .12f }, { m3, reso, .08f }, { m4, fxReverb, .18f } },
            { "Mallet", "Bark", "Cavity", "Chamber" }, contour ({ { 0,0,.5f }, { .08f,1,-.8f }, { .35f,.2f,-.2f }, { 1,0,0 } })),
        atelier ("Atelier - Silver Tines", "PLUCK", "Atelier Poly Metal Bell", "Bright harmonic tines with a soft octave body and restrained echoes. Alloy adds upper partials; Body fills the centre. Play C3-C6.", 2,
            { { "delayFeedback", .18f }, { "gain", -8 }, { "wtPos", .32f }, { "oscBOn", 1 }, { "wtTableB", sineTri }, { "oscBPitch", -12 }, { "levelB", .06f }, { "cutoff", 6500 },
              { "attack", .001f }, { "decay", 1.35f }, { "sustain", 0 }, { "release", .42f }, { "env2Decay", .2f }, { "delayOn", 1 }, { "reverbOn", 1 }, { "lfo1Rate", .7f } },
            { { env2, bLevel, .035f }, { env2, aPos, .25f }, { lfo1, aPan, .08f, true }, { vel, aPos, .15f }, { m1, aPos, .18f }, { m2, bLevel, .12f }, { m3, fxDelay, .15f }, { m4, fxReverb, .16f } },
            { "Alloy", "Body", "Echo", "Lustre" }, contour ({ { 0,.5f,.6f }, { .3f,.85f,-.6f }, { .75f,.15f,.4f }, { 1,.5f,0 } })),
        atelier ("Atelier - Pixel Dew", "PLUCK", "Atelier Poly Digital Sync", "Short digital droplets with a descending spectral attack. Pixel shifts the harmonic ridge; Scatter sends dotted echoes. Play C3-C6.", 1,
            { { "delayFeedback", .2f }, { "gain", -6 }, { "wtPos", .08f }, { "cutoff", 5500 }, { "attack", .002f }, { "decay", .31f }, { "sustain", 0 }, { "release", .13f },
              { "env2Decay", .12f }, { "lfo1Sync", 1 }, { "lfo1Division", dEighth }, { "delayOn", 1 }, { "delayPingPong", 1 }, { "delayMix", .18f }, { "reverbOn", 1 }, { "reverbMix", .08f } },
            { { vel, aPos, .07f }, { env2, aPos, .55f }, { lfo1, aPan, .12f, true }, { vel, cutoff, .1f }, { m1, aPos, .2f }, { m2, reso, .1f }, { m3, fxDelay, .15f }, { m4, fxReverb, .14f } },
            { "Pixel", "Focus", "Scatter", "Dew" }, contour ({ { 0,.5f,0 }, { .2f,.1f,.6f }, { .55f,.9f,-.6f }, { .85f,.3f,0 }, { 1,.5f,0 } })),
        // Appended so earlier factory indices stay stable. Chorus: two swept taps a side widen one detuned pair.
        make ("Chorus Silk Pad", "PAD", "Poly Chorus Wide Warm", "A soft saw pair widened by the stereo chorus. Shimmer brightens the table.",
              { { "lfo1Rate", .083f }, { "wtTable", saw }, { "wtPos", .3f }, { "oscAUnison", 2 }, { "oscADetune", 6 }, { "cutoff", 2800 }, { "attack", .65f }, { "decay", 1 },
                { "sustain", .8f }, { "release", 1.7f }, { "chorusOn", 1 }, { "chorusRate", .45f }, { "chorusDepth", .45f }, { "chorusMix", .38f },
                { "reverbMix", .15f }, { "gain", -9 } },
              { { vel, cutoff, .05f }, { lfo1, aPos, .08f, true }, { m2, aPos, .4f } }, "Shimmer"),
    };
    return presets;
}
} // namespace bw
