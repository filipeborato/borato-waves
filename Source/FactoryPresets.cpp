#include "FactoryPresets.h"

namespace bw
{
namespace
{
// Matrix source and destination indices (see Modulation.h).
enum Source { lfo1 = 1, lfo2, m1, m2, m3, m4, env1, vel, env2 };
enum Destination { aPos, aLevel, aPan, aPitch, aDetune, aSpread, bPos, bLevel, bPan, bPitch, bDetune, bSpread,
                   cutoff, reso, subLevel, noiseLevel, cutoff2, reso2, fxDrive, fxDelay, fxReverb, aWarp, bWarp, fm,
                   sampleLevel, samplePan, samplePitch, grainPos, grainSz, grainDens, grainTune,
                   specPos, specSpeed, specTune, specStage1, specStage2, specLow, specHigh, specShift, specMix };
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

std::shared_ptr<const SampleBank> atelierSampleBank (int kind)
{
    static const auto banks = []
    {
        std::array<std::shared_ptr<const SampleBank>, 3> result {};
        constexpr double sampleRate = 24000.0;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        auto makeWave = [&] (int frames, const std::function<float (int, double)>& gen)
        {
            auto s = std::make_shared<Sample> ();
            s->rate = sampleRate;
            s->data.resize ((size_t) frames);
            for (int n = 0; n < frames; ++n)
                s->data[(size_t) n] = juce::jlimit (-0.95f, 0.95f, gen (n, (double) n / sampleRate));
            s->updateHash ();
            return s;
        };

        // Bank 0: Velvet Keys — 3 key zones x 2 velocity layers with equal-power crossfade.
        {
            auto bank = std::make_shared<SampleBank> ();
            struct KeyRange { int low, high, root; };
            static constexpr std::array<KeyRange, 3> ranges { { { 0, 47, 36 }, { 48, 63, 55 }, { 64, 127, 72 } } };
            for (const auto& kr : ranges)
            {
                const double f0 = juce::MidiMessage::getMidiNoteInHertz (kr.root);
                const int frames = 9600;
                SampleZone soft;
                soft.lowKey = kr.low; soft.highKey = kr.high; soft.rootKey = kr.root;
                soft.lowVel = 1; soft.highVel = 68; soft.crossfadeVel = 16;
                soft.crossfadeLaw = VelocityCrossfadeLaw::equalPower;
                soft.loop = true; soft.loopStart = 0.25f; soft.loopEnd = 0.85f;
                soft.sample = makeWave (frames, [f0] (int, double t)
                {
                    const double env = 0.55 + 0.35 * std::exp (-t * 7.0);
                    const double sig = 0.72 * std::sin (twoPi * f0 * t)
                                     + 0.20 * std::sin (twoPi * 2.0 * f0 * t)
                                     + 0.08 * std::sin (twoPi * 3.0 * f0 * t);
                    return (float) (0.72 * env * sig);
                });
                soft.variants[0].sample = soft.sample;
                bank->zones.push_back (std::move (soft));

                SampleZone bright;
                bright.lowKey = kr.low; bright.highKey = kr.high; bright.rootKey = kr.root;
                bright.lowVel = 69; bright.highVel = 127; bright.crossfadeVel = 16;
                bright.crossfadeLaw = VelocityCrossfadeLaw::equalPower;
                bright.loop = true; bright.loopStart = 0.25f; bright.loopEnd = 0.85f;
                bright.sample = makeWave (frames, [f0] (int, double t)
                {
                    const double body = 0.50 + 0.40 * std::exp (-t * 6.0);
                    const double bell = std::exp (-t * 18.0);
                    const double sig = 0.42 * std::sin (twoPi * f0 * t)
                                     + 0.28 * std::sin (twoPi * 2.0 * f0 * t + 0.3)
                                     + 0.22 * std::sin (twoPi * 3.0 * f0 * t)
                                     + 0.18 * std::sin (twoPi * 4.0 * f0 * t + 0.7)
                                     + (0.24 * bell + 0.10) * std::sin (twoPi * 6.0 * f0 * t);
                    return (float) (0.78 * body * sig);
                });
                bright.variants[0].sample = bright.sample;
                bank->zones.push_back (std::move (bright));
            }
            jassert (bank->valid ());
            result[0] = std::move (bank);
        }

        // Bank 1: Round Robin Pluck — 3 key zones, each with 4 distinct round-robin variants.
        {
            auto bank = std::make_shared<SampleBank> ();
            struct KeyRange { int low, high, root; };
            static constexpr std::array<KeyRange, 3> ranges { { { 0, 47, 36 }, { 48, 63, 55 }, { 64, 127, 72 } } };
            for (const auto& kr : ranges)
            {
                const double f0 = juce::MidiMessage::getMidiNoteInHertz (kr.root);
                const int frames = 9600;
                SampleZone zone;
                zone.lowKey = kr.low; zone.highKey = kr.high; zone.rootKey = kr.root;
                zone.lowVel = 1; zone.highVel = 127; zone.numVariants = 4;
                zone.loop = true; zone.loopStart = 0.30f; zone.loopEnd = 0.90f;
                for (int v = 0; v < 4; ++v)
                {
                    auto varSample = makeWave (frames, [f0, v] (int, double t)
                    {
                        const double attack = 1.0 - std::exp (-t * (900.0 + 250.0 * v));
                        const double sustain = 0.42 + 0.48 * std::exp (-t * (6.5 + 1.2 * v));
                        double sig = 0.0;
                        if (v == 0)
                            sig = 0.56 * std::sin (twoPi * f0 * t) + 0.24 * std::sin (twoPi * 2.0 * f0 * t) + 0.18 * std::sin (twoPi * 4.0 * f0 * t) + 0.14 * std::sin (twoPi * 6.0 * f0 * t);
                        else if (v == 1)
                            sig = 0.58 * std::sin (twoPi * f0 * t + 0.4) + 0.34 * std::sin (twoPi * 3.0 * f0 * t) + 0.18 * std::sin (twoPi * 5.0 * f0 * t);
                        else if (v == 2)
                            sig = 0.50 * std::sin (twoPi * f0 * t + 0.9) + 0.32 * std::sin (twoPi * 2.0 * f0 * t) + 0.26 * std::sin (twoPi * 4.0 * f0 * t);
                        else
                            sig = 0.54 * std::sin (twoPi * f0 * t + 1.4) + 0.28 * std::sin (twoPi * 2.0 * f0 * t) + 0.22 * std::sin (twoPi * 3.0 * f0 * t) + 0.16 * std::sin (twoPi * 6.0 * f0 * t);
                        return (float) (0.78 * attack * sustain * sig);
                    });
                    zone.variants[(size_t) v].sample = varSample;
                    if (v == 0) zone.sample = varSample;
                }
                bank->zones.push_back (std::move (zone));
            }
            jassert (bank->valid ());
            result[1] = std::move (bank);
        }

        // Bank 2: Sliced Groove — 32 slices starting at root 36 (C2..G4), built via buildSliceBank.
        {
            constexpr int numSlices = 32;
            constexpr int sliceFrames = 1200;
            constexpr int totalFrames = numSlices * sliceFrames;
            std::vector<int> markers;
            markers.reserve (numSlices);
            for (int s = 0; s < numSlices; ++s) markers.push_back (s * sliceFrames);

            auto groove = makeWave (totalFrames, [] (int n, double)
            {
                const int sliceIdx = juce::jlimit (0, numSlices - 1, n / sliceFrames);
                const int local = n % sliceFrames;
                const double lt = (double) local / sampleRate;
                static constexpr std::array<double, 8> ratios { 1.0, 1.25, 1.3333333, 1.5, 1.6666667, 2.0, 0.75, 1.125 };
                const double baseHz = 130.81278265 * ratios[(size_t) (sliceIdx % 8)] * (1.0 + 0.03 * (sliceIdx / 8));
                const double env = (1.0 - std::exp (-lt * 1200.0)) * (0.48 + 0.45 * std::exp (-lt * 18.0));
                const double transient = std::exp (-lt * 95.0) * std::sin (twoPi * (baseHz * (3.0 + (sliceIdx % 4))) * lt);
                const double tone = 0.62 * std::sin (twoPi * baseHz * lt + 0.25 * sliceIdx)
                                  + 0.28 * std::sin (twoPi * 2.0 * baseHz * lt)
                                  + 0.16 * std::sin (twoPi * 3.0 * baseHz * lt);
                return (float) (0.78 * (env * tone + 0.25 * transient));
            });

            juce::String err;
            auto slicedBank = buildSliceBank (groove, {}, { markers, 36, true }, err);
            jassert (slicedBank != nullptr && slicedBank->valid () && slicedBank->sliced && err.isEmpty ());
            result[2] = std::move (slicedBank);
        }

        return result;
    } ();
    return banks[(size_t) juce::jlimit (0, 2, kind)];
}

FactoryPreset atelierSample (const char* name, const char* category, const char* tags, const char* description,
                             int bankKind, Values values, Routes routes, std::array<const char*, 4> macros, LfoCurve curve)
{
    Values defaults { { "oscAOn", 0 }, { "oscBOn", 0 }, { "sampleOn", 1 }, { "sampleMode", 0 },
                      { "sampleLevel", .85f }, { "sampleRoute", 0 }, { "gain", -4 }, { "cutoff", 3200 }, { "reso", 1.1f },
                      { "distOn", 1 }, { "distDrive", 2.5f }, { "distMix", .18f },
                      { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", dEighthDot }, { "delayFeedback", .24f }, { "delayMix", .08f },
                      { "reverbOn", 1 }, { "reverbSize", .62f }, { "reverbMix", .12f } };
    defaults.insert (defaults.end (), values.begin (), values.end ());
    FactoryPreset p { name, category, tags, description, std::move (defaults), std::move (routes), { sine, sine }, macros };
    p.curves[0] = curve;
    p.sampleBank = atelierSampleBank (bankKind);
    return p;
}

// Original stereo source material for the granular and spectral presets, rendered once from the Atelier
// tables (no external files). Everything is pitched to C4, the default sample root, so keys play in tune.
enum EngineSource { glassChoir, tempoShards, tapeKeys, haloPad, stretchChoir, metalBloom };

std::shared_ptr<const Sample> atelierSource (int kind)
{
    static const auto sources = []
    {
        constexpr double rate = 48000.0, root = 261.6255653, twoPi = juce::MathConstants<double>::twoPi;
        auto semis = [] (double s) { return std::exp2 (s / 12.0); };
        enum { reed, prism, tine, silk };
        // One band-limited oscillator voice reading an Atelier table.
        struct Osc
        {
            double phase = 0;
            float next (const Wavetable& table, double hz, float pos) noexcept
            {
                const float y = table.read (phase, juce::jlimit (0.0f, 1.0f, pos), Wavetable::levelFor (hz, rate));
                phase += hz / rate;
                phase -= std::floor (phase);
                return y;
            }
        };
        // Two vocal-tract resonances; vowel formants in Hz.
        struct Formant
        {
            juce::IIRFilter a, b;
            void set (double f1, double f2)
            {
                a.setCoefficients (juce::IIRCoefficients::makeBandPass (rate, f1, 4.0));
                b.setCoefficients (juce::IIRCoefficients::makeBandPass (rate, f2, 6.0));
            }
            float process (float x) noexcept { return a.processSingleSampleRaw (x) + .7f * b.processSingleSampleRaw (x); }
        };
        auto render = [&] (double seconds, const std::function<void (int, double, float&, float&)>& gen)
        {
            auto s = std::make_shared<Sample> ();
            s->rate = rate;
            const auto frames = (size_t) (seconds * rate);
            s->data.resize (frames);
            s->right.resize (frames);
            for (size_t n = 0; n < frames; ++n)
                gen ((int) n, (double) n / rate, s->data[n], s->right[n]);
            // Short edge fades, then a -3 dBFS peak; the presets set the final level.
            float peak = 1.0e-9f;
            for (size_t n = 0; n < frames; ++n)
            {
                const float edge = (float) juce::jmin (1.0, (double) n / 96.0, (double) (frames - 1 - n) / 960.0);
                s->data[n] *= edge; s->right[n] *= edge;
                peak = juce::jmax (peak, std::abs (s->data[n]), std::abs (s->right[n]));
            }
            juce::FloatVectorOperations::multiply (s->data.data (), .708f / peak, (int) frames);
            juce::FloatVectorOperations::multiply (s->right.data (), .708f / peak, (int) frames);
            s->updateHash ();
            return std::shared_ptr<const Sample> (std::move (s));
        };
        juce::Random noise (0x5eed);
        std::array<std::shared_ptr<const Sample>, 6> result {};

        // Glass Choir: five detuned Silk voices plus an octave, sung through an "ah" -> "oh" formant pair.
        {
            std::array<Osc, 12> osc {};
            Formant fl, fr;
            constexpr std::array<double, 5> cents { -14, -6, 0, 7, 13 };
            result[glassChoir] = render (3.5, [&] (int n, double t, float& l, float& r)
            {
                const float pos = (float) (.45 + .35 * std::sin (twoPi * .21 * t));
                if (n % 64 == 0)
                {
                    const double v = .5 - .5 * std::cos (twoPi * t / 3.5);
                    fl.set (750 - 300 * v, 1150 - 330 * v); fr.set (770 - 310 * v, 1210 - 360 * v);
                }
                double sl = 0, sr = 0;
                for (size_t i = 0; i < cents.size (); ++i)
                {
                    const float y = osc[i].next (*atelierTable (silk), root * std::exp2 (cents[i] / 1200), pos);
                    const float yo = osc[i + 5].next (*atelierTable (silk), 2 * root * std::exp2 (-cents[i] / 1200), pos);
                    const double pan = (double) i / 4.0;
                    sl += (y + .35 * yo) * std::sqrt (1 - pan); sr += (y + .35 * yo) * std::sqrt (pan);
                }
                const float air = .015f * (noise.nextFloat () * 2 - 1);
                l = (float) (.55 * sl) + 1.6f * fl.process ((float) sl + air);
                r = (float) (.55 * sr) + 1.6f * fr.process ((float) sr + air);
            });
        }

        // Tempo Shards: a minor-ninth Tine figure, one bright hit per eighth note at 120 BPM.
        {
            Osc a, b;
            constexpr std::array<double, 8> figure { 0, 7, 14, 10, 15, 7, 3, 12 };
            result[tempoShards] = render (4.0, [&] (int, double t, float& l, float& r)
            {
                const int step = juce::jmin (7, (int) (t / .25));
                const double local = t - step * .25;
                const double hz = root * semis (figure[(size_t) step]);
                const double env = (1 - std::exp (-local * 900)) * std::exp (-local * 13);
                const float pos = (float) (.25 + .6 * step / 7.0);
                const double click = std::exp (-local * 600) * (noise.nextFloat () * 2 - 1) * .25;
                const double y = env * (a.next (*atelierTable (tine), hz, pos) + .3 * b.next (*atelierTable (prism), hz * 2, pos)) + click;
                const double pan = step % 2 ? .35 : .65;
                l = (float) (y * std::sqrt (1 - pan) * 1.4); r = (float) (y * std::sqrt (pan) * 1.4);
            });
        }

        // Tape Keys: a Reed key that darkens as it sustains, with tape wow/flutter and soft saturation.
        {
            Osc a, b, c;
            result[tapeKeys] = render (3.0, [&] (int, double t, float& l, float& r)
            {
                const double wowL = 1 + .0035 * std::sin (twoPi * .7 * t) + .0012 * std::sin (twoPi * 6.1 * t);
                const double wowR = 1 + .0035 * std::sin (twoPi * .7 * t + 1.1) + .0012 * std::sin (twoPi * 5.7 * t + .4);
                const float pos = (float) (.25 + .6 * std::exp (-t * 2.2));
                const double env = (1 - std::exp (-t * 400)) * (.55 + .45 * std::exp (-t * 3));
                const double octave = .16 * c.next (*atelierTable (prism), root * 2, pos);
                const double yl = env * (a.next (*atelierTable (reed), root * wowL, pos) + octave);
                const double yr = env * (b.next (*atelierTable (reed), root * wowR, pos) + octave);
                l = (float) (std::tanh (1.7 * yl) / std::tanh (1.7)); r = (float) (std::tanh (1.7 * yr) / std::tanh (1.7));
            });
        }

        // Halo Pad: an open root-fifth-ninth Prism chord whose harmonic ridge sweeps, with an octave shimmer.
        {
            std::array<Osc, 8> osc {};
            constexpr std::array<double, 4> chord { 0, 7, 14, 24 };
            result[haloPad] = render (4.0, [&] (int, double t, float& l, float& r)
            {
                const float pos = (float) (t / 4.0);
                const double swell = (1 - std::exp (-t * 2.5)) * (.8 + .2 * std::sin (twoPi * .37 * t));
                double sl = 0, sr = 0;
                for (size_t i = 0; i < chord.size (); ++i)
                {
                    const double gain = i == 3 ? .22 : 1.0;
                    const double hz = root * semis (chord[i]);
                    sl += gain * osc[i].next (*atelierTable (prism), hz * std::exp2 (-6 / 1200.0), pos);
                    sr += gain * osc[i + 4].next (*atelierTable (prism), hz * std::exp2 (6 / 1200.0), 1 - pos * .6f);
                }
                l = (float) (swell * sl); r = (float) (swell * sr);
            });
        }

        // Stretch Choir: a sung phrase, "ah-eh-ee-oh" with gliding pitch, for independent time and pitch.
        {
            std::array<Osc, 6> osc {};
            Formant fl, fr;
            constexpr std::array<std::array<double, 2>, 4> vowels { { { 750, 1150 }, { 530, 1850 }, { 300, 2250 }, { 450, 800 } } };
            constexpr std::array<double, 4> notes { 0, 2, 3, 0 };
            result[stretchChoir] = render (4.0, [&] (int n, double t, float& l, float& r)
            {
                const double seg = juce::jmin (2.999, t);
                const int i = (int) seg;
                const double u = seg - i, glide = juce::jmin (1.0, u * 6);
                const double note = notes[(size_t) i] + (notes[(size_t) i + 1] - notes[(size_t) i]) * juce::jmax (0.0, (u - .8) * 5);
                if (n % 64 == 0)
                {
                    const auto& from = vowels[(size_t) juce::jmax (0, i - 1)];
                    const auto& to = vowels[(size_t) i];
                    const double f1 = from[0] + (to[0] - from[0]) * glide, f2 = from[1] + (to[1] - from[1]) * glide;
                    fl.set (f1, f2); fr.set (f1 * 1.03, f2 * 1.02);
                }
                const double vibrato = 1 + .004 * std::sin (twoPi * 5.2 * t) * juce::jmin (1.0, t);
                const double hz = root * semis (note) * vibrato;
                double sl = 0, sr = 0;
                for (size_t k = 0; k < 3; ++k)
                {
                    sl += osc[k].next (*atelierTable (silk), hz * std::exp2 ((k * 8.0 - 8) / 1200), .7f);
                    sr += osc[k + 3].next (*atelierTable (silk), hz * std::exp2 ((8 - k * 8.0) / 1200), .7f);
                }
                const double env = 1 - std::exp (-t * 6);
                l = (float) (env * (.35 * sl + 1.8 * fl.process ((float) sl)));
                r = (float) (env * (.35 * sr + 1.8 * fr.process ((float) sr)));
            });
        }

        // Metal Bloom: a struck Tine with sparse bell partials and a clear transient over a sustained body.
        {
            Osc a, b;
            constexpr std::array<double, 4> ratios { 2.76, 5.40, 8.93, 13.34 };
            result[metalBloom] = render (3.0, [&] (int, double t, float& l, float& r)
            {
                const double body = (1 - std::exp (-t * 1500)) * (.55 + .45 * std::exp (-t * 1.6));
                double bell = 0;
                for (size_t i = 0; i < ratios.size (); ++i)
                    bell += std::sin (twoPi * root * ratios[i] * t + i) * std::exp (-t * (2.0 + 1.5 * i)) / (1.0 + i);
                const double strike = std::exp (-t * 250) * (noise.nextFloat () * 2 - 1) * .12;
                const double yl = body * a.next (*atelierTable (tine), root, 1.0f) + .45 * bell + strike;
                const double yr = body * b.next (*atelierTable (tine), root * std::exp2 (5 / 1200.0), .85f) + .45 * bell * .9 + strike;
                l = (float) yl; r = (float) yr;
            });
        }
        return result;
    } ();
    return sources[(size_t) juce::jlimit (0, 5, kind)];
}

// A granular (mode 1) or spectral (mode 2) instrument over one Atelier source. The source is embedded in
// presets and saved state; the spectral analysis is rebuilt on load.
FactoryPreset atelierEngine (const char* name, const char* category, const char* tags, const char* description,
                             int source, int mode, Values values, Routes routes, std::array<const char*, 4> macros, LfoCurve curve)
{
    Values defaults { { "oscAOn", 0 }, { "oscBOn", 0 }, { "sampleOn", 1 }, { "sampleMode", (float) mode },
                      { "sampleLevel", .8f }, { "sampleRoute", 0 }, { "grainSeed", 4242 }, { "gain", -6 },
                      { "cutoff", 12000 }, { "reso", .9f },
                      { "delayOn", 1 }, { "delaySync", 1 }, { "delayDivision", dEighthDot }, { "delayFeedback", .3f }, { "delayMix", .08f },
                      { "reverbOn", 1 }, { "reverbSize", .72f }, { "reverbMix", .18f } };
    // Macros with a bipolar route start centred, so the patch rests at its designed sound.
    for (const auto& route : routes)
        if (route.bipolar && route.source >= m1 && route.source <= m4)
            defaults.push_back ({ std::array { "macro1", "macro2", "macro3", "macro4" }[(size_t) (route.source - m1)], .5f });
    defaults.insert (defaults.end (), values.begin (), values.end ());
    FactoryPreset p { name, category, tags, description, std::move (defaults), std::move (routes), { sine, sine }, macros };
    p.curves[0] = curve;
    p.sample = atelierSource (source);
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
        atelierSample ("Atelier Sample - Velvet Keys", "KEYS", "Sample Multi Velocity Layers Poly",
            "Dual-layer electric mallet keys with equal-power velocity crossfade. Tone opens the filter; Bark drives the harmonic bite. Play C2-C6.", 0,
            { { "cutoff", 850 }, { "reso", 1.35f }, { "distMix", .35f }, { "attack", .004f }, { "decay", 1.1f }, { "sustain", .68f }, { "release", .32f },
              { "env2Attack", .003f }, { "env2Decay", .28f }, { "env2Sustain", .15f }, { "env2Release", .22f }, { "lfo1Rate", 3.4f }, { "gain", -9.5f } },
            { { vel, cutoff, .14f }, { env2, cutoff, .16f }, { lfo1, samplePan, .12f, true },
              { m1, cutoff, .48f }, { m2, fxDrive, .65f }, { m2, cutoff, .22f }, { m3, fxDelay, .55f }, { m4, fxReverb, .58f } },
            { "Tone", "Bark", "Echo", "Space" }, contour ({ { 0,.5f,.3f }, { .25f,.85f,-.3f }, { .75f,.15f,.3f }, { 1,.5f,0 } })),
        atelierSample ("Atelier Sample - Round Robin Pluck", "PLUCK", "Sample Multi RoundRobin Pluck Poly",
            "Four-variant round-robin acoustic pluck across three key zones. Tone opens the filter; Body shapes resonance and stereo motion. Play C2-C6.", 1,
            { { "cutoff", 460 }, { "reso", 1.35f }, { "distMix", .35f }, { "attack", .002f }, { "decay", .85f }, { "sustain", .55f }, { "release", .28f },
              { "env2Attack", .002f }, { "env2Decay", .22f }, { "env2Sustain", .1f }, { "env2Release", .18f }, { "lfo1Rate", 2.2f }, { "gain", -8.5f } },
            { { vel, cutoff, .12f }, { env2, cutoff, .18f }, { lfo1, samplePan, .14f, true },
              { m1, cutoff, .62f }, { m2, reso, .45f }, { m2, fxDrive, .55f }, { m3, fxDelay, .55f }, { m4, fxReverb, .58f } },
            { "Tone", "Body", "Echo", "Space" }, contour ({ { 0,.2f,.6f }, { .35f,.9f,-.5f }, { .75f,.3f,.4f }, { 1,.2f,0 } })),
        atelierSample ("Atelier Sample - Sliced Groove", "ARP", "Sample Sliced Loop Groove Sync",
            "Chromatic 32-slice harmonic groove mapped from C2 to G4. Tone sweeps the low-pass filter; Punch adds resonant drive. Play C2-G4.", 2,
            { { "cutoff", 480 }, { "reso", 1.35f }, { "distMix", .35f }, { "attack", .002f }, { "decay", .95f }, { "sustain", .65f }, { "release", .24f },
              { "lfo1Sync", 1 }, { "lfo1Division", dEighth }, { "delayDivision", dSixteenth }, { "gain", -10.5f } },
            { { vel, cutoff, .12f }, { lfo1, cutoff, .14f, true },
              { m1, cutoff, .62f }, { m2, fxDrive, .65f }, { m2, reso, .35f }, { m3, fxDelay, .55f }, { m4, fxReverb, .58f } },
            { "Tone", "Punch", "Echo", "Space" }, contour ({ { 0,0,.7f }, { .2f,1,-.6f }, { .55f,.35f,.5f }, { .85f,.75f,-.5f }, { 1,0,0 } })),
        atelierEngine ("Grain Cloud - Glass Choir", "PAD", "Granular Cloud Choir Wide Static",
            "A frozen cloud of Gaussian grains over a sung glass choir. Position scans the vowel, Density thickens the cloud, Window sharpens it. Granular; up to 32 grains per voice.",
            glassChoir, 1,
            { { "grainWindow", 2 }, { "grainWindowAmount", .35f }, { "grainSize", 190 }, { "grainDensity", 34 }, { "grainPosition", .4f },
              { "grainSpreadPos", .22f }, { "grainJitter", 45 }, { "grainPan", 75 }, { "grainPitchSpread", .12f }, { "grainLoop", 1 },
              { "attack", .9f }, { "decay", 1.2f }, { "sustain", .9f }, { "release", 2.4f }, { "cutoff", 7800 },
              { "chorusOn", 1 }, { "chorusRate", .3f }, { "chorusDepth", .4f }, { "chorusMix", .25f },
              { "reverbSize", .88f }, { "reverbMix", .3f }, { "lfo1Rate", .11f }, { "gain", -8 } },
            { { lfo1, grainPos, .08f, true }, { vel, sampleLevel, .12f },
              { m1, grainPos, .5f, true }, { m2, grainDens, .55f }, { m3, grainSz, -.5f }, { m3, cutoff, .45f, true }, { m4, fxReverb, .55f }, { m4, fxDelay, .3f } },
            { "Position", "Density", "Window", "Space" }, contour ({ { 0,.5f,.4f }, { .5f,.8f,-.4f }, { 1,.5f,0 } })),
        atelierEngine ("Grain Pulse - Tempo Shards", "ARP", "Granular BPM Sync Pulse Rhythmic",
            "Tempo-locked grain bursts cut from a minor-ninth figure. Grains restart on every note (Jump-Start). Density shortens the grains into a stutter (BPM-locked rate, overlap varies); Pitch adds a fifth above. Granular; up to 32 grains per voice.",
            tempoShards, 1,
            { { "grainDensityMode", 1 }, { "grainDensityDivision", dSixteenth }, { "grainJumpStart", 1 }, { "grainWindow", 5 }, { "grainWindowAmount", .55f },
              { "grainSize", 95 }, { "grainPosition", .2f }, { "grainScan", .6f }, { "grainSpreadPos", .05f }, { "grainJitter", 5 }, { "grainPan", 45 },
              { "attack", .002f }, { "decay", .5f }, { "sustain", .7f }, { "release", .35f }, { "cutoff", 5200 }, { "reso", 1.6f },
              { "env2Attack", .002f }, { "env2Decay", .18f }, { "env2Sustain", .2f }, { "env2Release", .2f },
              { "delayDivision", dEighth }, { "delayFeedback", .38f }, { "delayMix", .14f }, { "lfo1Sync", 1 }, { "lfo1Division", dQuarter } },
            { { env2, cutoff, .2f }, { vel, cutoff, .15f }, { lfo1, grainPos, .1f, true },
              { m1, grainPos, .6f }, { m2, grainSz, -.7f }, { m3, grainTune, .29f }, { m3, cutoff, .25f }, { m4, fxDelay, .5f }, { m4, fxReverb, .45f } },
            { "Position", "Density", "Pitch", "Space" }, contour ({ { 0,0,.6f }, { .25f,1,-.6f }, { .5f,.3f,.3f }, { .75f,.9f,-.5f }, { 1,0,0 } })),
        atelierEngine ("Grain Drift - Tape Keys", "KEYS", "Granular Keys Tape Wobble Lofi",
            "Worn tape keys: ExpDec grains make each note bloom, longer grains on low keys keep the body. Position scans the decay, Density adds grit, Window shortens the grains. Granular; up to 32 grains per voice.",
            tapeKeys, 1,
            { { "grainWindow", 8 }, { "grainWindowAmount", .4f }, { "grainSize", 140 }, { "grainDensity", 28 }, { "grainPosition", .08f },
              { "grainScan", .35f }, { "grainSpreadPos", .06f }, { "grainKeyTrack", 60 }, { "grainJitter", 30 }, { "grainPan", 35 },
              { "attack", .004f }, { "decay", 1.4f }, { "sustain", .55f }, { "release", .6f }, { "cutoff", 6400 },
              { "distOn", 1 }, { "distDrive", 3.0f }, { "distMix", .15f }, { "lfo1Rate", .6f }, { "gain", -18 } },
            { { vel, sampleLevel, .2f }, { vel, cutoff, .15f }, { lfo1, grainTune, .01f, true },
              { m1, grainPos, .55f }, { m2, grainDens, .6f }, { m2, fxDrive, .45f }, { m3, grainSz, -.5f }, { m4, fxReverb, .55f }, { m4, fxDelay, .35f } },
            { "Position", "Density", "Window", "Space" }, contour ({ { 0,.5f,0 }, { .5f,.6f,0 }, { 1,.5f,0 } })),
        atelierEngine ("Spectral Halo - Freeze Pad", "PAD", "Spectral Freeze Pad Shimmer Wide",
            "A frozen spectral snapshot of a sweeping Prism chord. Scrub picks the moment, Motion drifts it, Color tilts the spectrum. Spectral resynthesis; fast transients smear when frozen.",
            haloPad, 2,
            { { "spectralFreeze", 1 }, { "spectralPos", .45f }, { "spectralStage1Type", 2 }, { "spectralStage1Amount", 0 },
              { "attack", 1.1f }, { "decay", 1.5f }, { "sustain", .9f }, { "release", 3.0f }, { "cutoff", 9000 },
              { "reverbSize", .9f }, { "reverbMix", .32f }, { "lfo1Rate", .07f }, { "gain", -8 } },
            { { lfo1, specPos, .03f, true }, { vel, sampleLevel, .1f },
              { m1, specPos, .5f, true }, { m2, specPos, .12f }, { m2, specTune, .08f }, { m3, specStage1, .6f, true }, { m3, cutoff, .2f, true }, { m4, fxReverb, .5f }, { m4, fxDelay, .3f } },
            { "Scrub", "Motion", "Color", "Space" }, contour ({ { 0,.5f,.3f }, { .5f,.9f,-.3f }, { 1,.5f,0 } })),
        atelierEngine ("Spectral Stretch - Slow Choir", "LEAD", "Spectral Stretch Choir Vocal Pitch",
            "A sung phrase played at half speed with its pitch held, so each vowel lingers. Speed changes time only, Pitch changes pitch only, Air opens the top. Spectral resynthesis; consonant-like attacks soften.",
            stretchChoir, 2,
            { { "spectralSpeed", .5f }, { "spectralPhaseLock", 1 }, { "spectralStage1Type", 2 }, { "spectralFilterLow", 120 }, { "spectralFilterHigh", 3500 },
              { "attack", .25f }, { "decay", 1.0f }, { "sustain", .85f }, { "release", 1.2f }, { "cutoff", 3800 },
              { "chorusOn", 1 }, { "chorusRate", .5f }, { "chorusDepth", .35f }, { "chorusMix", .3f }, { "reverbMix", .24f }, { "gain", -5 } },
            { { vel, sampleLevel, .15f }, { vel, cutoff, .12f },
              { m1, specSpeed, .55f }, { m2, specTune, .5f, true }, { m3, specStage1, .55f }, { m3, specHigh, .5f }, { m4, fxReverb, .5f }, { m4, fxDelay, .35f } },
            { "Speed", "Pitch", "Air", "Space" }, contour ({ { 0,.5f,0 }, { 1,.5f,0 } })),
        atelierEngine ("Spectral Bloom - Metal Shift", "KEYS", "Spectral BinShift Tilt Metallic Bell",
            "A struck metal tine pushed through Bin Shift and Spectral Tilt, then a Post band filter. Shift detunes the partials inharmonically, Tilt brightens, Band narrows. Spectral resynthesis; dense transients smear.",
            metalBloom, 2,
            { { "spectralStage1Type", 1 }, { "spectralStage1Amount", .12f }, { "spectralStage2Type", 2 }, { "spectralStage2Amount", .1f },
              { "spectralFilterOrder", 1 }, { "spectralFilterLow", 180 }, { "spectralFilterHigh", 7000 }, { "spectralMix", .85f },
              { "attack", .002f }, { "decay", 1.6f }, { "sustain", .65f }, { "release", 1.4f }, { "cutoff", 10000 },
              { "delayDivision", dQuarterDot }, { "delayFeedback", .35f }, { "delayMix", .12f }, { "reverbMix", .22f }, { "gain", -8 } },
            { { vel, specStage2, .1f }, { vel, sampleLevel, .15f }, { lfo1, specShift, .05f, true },
              { m1, specStage1, .3f, true }, { m2, specStage2, .2f, true }, { m3, specHigh, -.2f }, { m3, specLow, .15f }, { m4, fxReverb, .35f }, { m4, fxDelay, .2f } },
            { "Shift", "Tilt", "Band", "Space" }, contour ({ { 0,.5f,.5f }, { .3f,.9f,-.5f }, { .7f,.2f,.5f }, { 1,.5f,0 } })),
    };
    return presets;
}
} // namespace bw
