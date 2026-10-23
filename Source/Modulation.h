#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <mutex>

namespace bw
{
struct CurvePoint { float x = 0, y = 0.5f, curve = 0; };
struct LfoCurve
{
    int count = 2;
    std::array<CurvePoint, 32> points { CurvePoint { 0, 0.5f, 0 }, CurvePoint { 1, 0.5f, 0 } };
    static LfoCurve factory (int shape)
    {
        LfoCurve c;
        if (shape == 0) { c.count = 17; for (int i = 0; i < c.count; ++i) c.points[(size_t) i] = { i / 16.0f, 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * i / 16.0f), 0 }; }
        if (shape == 1) { c.count = 3; c.points[0] = { 0, 0, 0 }; c.points[1] = { .5f, 1, 0 }; c.points[2] = { 1, 0, 0 }; }
        if (shape == 2 || shape == 3) { c.count = 3; float a = shape == 2 ? 0.0f : 1.0f; c.points[0] = { 0, a, 0 }; c.points[1] = { .999f, 1-a, 0 }; c.points[2] = { 1, a, 0 }; }
        if (shape == 4) { c.count = 6; c.points[0] = { 0, 1, 0 }; c.points[1] = { .499f, 1, 0 }; c.points[2] = { .5f, 0, 0 }; c.points[3] = { .998f, 0, 0 }; c.points[4] = { .999f, 1, 0 }; c.points[5] = { 1, 1, 0 }; }
        c.points[(size_t) (c.count-1)].y = c.points[0].y;
        return c;
    }
    bool valid () const noexcept
    {
        if (count < 2 || count > 32 || points[0].x != 0 || points[(size_t) count-1].x != 1 || points[0].y != points[(size_t) count-1].y) return false;
        for (int i = 0; i < count; ++i) { const auto& p = points[(size_t) i]; if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.curve) || p.y < 0 || p.y > 1 || std::abs (p.curve)>1 || (i>0 && p.x <= points[(size_t)i-1].x)) return false; }
        return true;
    }
    float evaluate (double phase) const noexcept
    {
        phase -= std::floor (phase);
        for (int i = 1; i < count; ++i)
            if (phase <= points[(size_t) i].x)
            {
                const auto& a = points[(size_t)i-1]; const auto& b = points[(size_t)i];
                float t = (float) (phase-a.x) / (b.x-a.x);
                // Bounded polynomial bend preserves monotonicity and endpoints.
                t += a.curve * t * (1-t);
                return juce::jlimit (0.0f, 1.0f, a.y + (b.y-a.y)*t);
            }
        return points[0].y;
    }
};
inline juce::StringArray sourceNames () { return { "Off", "LFO 1", "LFO 2", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Env 1", "Velocity", "Env 2" }; }
inline juce::StringArray destinationNames () { return { "A Position", "A Level", "A Pan", "A Pitch", "A Detune", "A Spread", "B Position", "B Level", "B Pan", "B Pitch", "B Detune", "B Spread", "Cutoff", "Resonance", "Sub Level", "Noise Level",
                                                  "F2 Cutoff", "F2 Resonance", "FX Drive (macro)", "FX Delay Mix (macro)", "FX Reverb Mix (macro)", "A Warp", "B Warp", "FM Depth", "Sample Level", "Sample Pan", "Sample Pitch", "Grain Position", "Grain Size", "Grain Density", "Grain Pitch", "Spectral Pos", "Spectral Speed", "Spectral Pitch" }; }
// The kGlobalDestinations from kFirstGlobalDestination are global effects, driven by macro sources only.
// Later destinations are per voice again; they were appended to keep saved route indices stable.
constexpr int kDestinations = 34, kFirstGlobalDestination = 18, kGlobalDestinations = 3, kFirstWarpDestination = 21, kFmDestination = 23, kSources = 10, kSampleLevelDestination = 24, kSamplePanDestination = 25, kSamplePitchDestination = 26, kGrainPositionDestination = 27, kGrainSizeDestination = 28, kGrainDensityDestination = 29, kGrainPitchDestination = 30, kSpectralPositionDestination = 31, kSpectralSpeedDestination = 32, kSpectralPitchDestination = 33;
inline const std::array<const char*, kDestinations>& destinationIds () { static constexpr std::array<const char*, kDestinations> ids { "wtPos", "level", "oscAPan", "oscAPitch", "oscADetune", "oscASpread", "wtPosB", "levelB", "oscBPan", "oscBPitch", "oscBDetune", "oscBSpread", "cutoff", "reso", "subLevel", "noiseLevel",
    "cutoff2", "reso2", "distDrive", "delayMix", "reverbMix", "oscAWarp", "oscBWarp", "fmDepth", "sampleLevel", "samplePan", "samplePitch", "grainPosition", "grainSize", "grainDensity", "grainPitch", "spectralPos", "spectralSpeed", "spectralPitch" }; return ids; }
inline juce::StringArray divisionNames () { juce::StringArray a; for (auto base : { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }) { a.add (base); a.add (juce::String (base)+" D"); a.add (juce::String (base)+" T"); } return a; }
inline double divisionBeats (int index) noexcept { index = juce::jlimit (0, 26, index); return std::ldexp (16.0, -(index/3)) * (index%3 == 1 ? 1.5 : index%3 == 2 ? 2.0/3.0 : 1.0); }
struct LfoSettings { std::atomic<float> *rate, *sync, *division, *mode; };
struct RouteSettings { std::atomic<float> *enabled, *source, *destination, *amount, *bipolar; };
struct ModulationContext
{
    std::array<LfoSettings, 2> lfo {};
    std::array<RouteSettings, 16> routes {};
    std::array<std::atomic<float>*, 4> macros {};
    std::array<juce::NormalisableRange<float>, kDestinations> ranges;
    std::array<LfoCurve, 2> curves { LfoCurve::factory (0), LfoCurve::factory (0) };
    std::array<double, 2> phase {}, increment {};
    uint64_t curveGeneration = 0;
    std::array<std::atomic<float>, 2>* display = nullptr;
    std::atomic<int> latestVoice { -1 };
};
// One audio consumer. Serialized non-audio producers own the back buffer.
class CurveExchange
{
public:
    using Curves = std::array<LfoCurve, 2>;
    CurveExchange () { for (auto& b : buffers) b = { LfoCurve::factory (0), LfoCurve::factory (0) }; }
    void publish (const Curves& curves) noexcept { buffers[(size_t) back] = curves; back = middle.exchange (back | dirty, std::memory_order_acq_rel) & 3; }
    bool consume (Curves& curves) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & dirty) == 0)
            return false;
        front = middle.exchange (front, std::memory_order_acq_rel) & 3;
        curves = buffers[(size_t) front];
        return true;
    }
private:
    static constexpr int dirty = 4;
    std::array<Curves, 3> buffers;
    int front = 0, back = 2;
    std::atomic<int> middle { 1 };
};
}
