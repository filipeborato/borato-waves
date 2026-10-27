#include "Rig.h"
#include "Spectral.h"
#include <iostream>
#ifdef _WIN32
#include <stdlib.h>
#endif
#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool gCountingAudioHeap = false;
std::atomic<size_t> gAudioHeapOps { 0 };
struct ScopedAudioHeapGuard
{
    ScopedAudioHeapGuard () noexcept { gCountingAudioHeap = true; }
    ~ScopedAudioHeapGuard () noexcept { gCountingAudioHeap = false; }
};
} // namespace

void* operator new (std::size_t size)
{
    if (gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    if (void* ptr = std::malloc (size == 0 ? 1 : size)) return ptr;
    throw std::bad_alloc ();
}
void* operator new[] (std::size_t size)
{
    if (gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    if (void* ptr = std::malloc (size == 0 ? 1 : size)) return ptr;
    throw std::bad_alloc ();
}
void operator delete (void* ptr) noexcept
{
    if (ptr != nullptr && gCountingAudioHeap) gAudioHeapOps.fetch_add (1, std::memory_order_relaxed);
    std::free (ptr);
}
void operator delete (void* ptr, std::size_t) noexcept { operator delete (ptr); }
void operator delete[] (void* ptr) noexcept { operator delete (ptr); }
void operator delete[] (void* ptr, std::size_t) noexcept { operator delete (ptr); }

namespace
{
using namespace rig;
constexpr double kRate = 48000, kTwoPi = juce::MathConstants<double>::twoPi;

std::shared_ptr<bw::Sample> makeSample (int frames, const std::function<float (int)>& left, const std::function<float (int)>& right = {})
{
    auto sample = std::make_shared<bw::Sample> ();
    sample->rate = kRate;
    for (int i = 0; i < frames; ++i) sample->data.push_back (left (i));
    if (right) for (int i = 0; i < frames; ++i) sample->right.push_back (right (i));
    sample->updateHash ();
    return sample;
}
std::function<float (int)> sine (double hz, double amplitude = .5, double phase = 0)
{ return [=] (int i) { return (float) (amplitude * std::sin (kTwoPi * hz * i / kRate + phase)); }; }

std::shared_ptr<const bw::SpectralData> analyze (const bw::Sample& sample)
{
    juce::String error;
    auto data = bw::analyzeSpectral (sample, error);
    assert (data != nullptr && error.isEmpty ());
    return data;
}
bw::SpectralControls whole (const bw::SpectralData& data)
{
    bw::SpectralControls c;
    c.startFrame = bw::SpectralData::frameFor (0); c.endFrame = data.frames - 1;
    return c;
}
std::pair<std::vector<float>, std::vector<float>> render (bw::SpectralVoicePlayer& player, const bw::SpectralControls& c, int count)
{
    std::vector<float> l, r;
    for (int i = 0; i < count; ++i)
    {
        const auto [a, b] = player.process (c);
        assert (std::isfinite (a) && std::isfinite (b));
        l.push_back (a); r.push_back (b);
    }
    return { l, r };
}
double dbfs (double value) { return 20 * std::log10 (std::max (value, 1.0e-12)); }
double errorRms (const std::vector<float>& out, const std::vector<float>& reference)
{
    double sum = 0;
    for (size_t i = 0; i < reference.size (); ++i) { const double d = out[i] - reference[i]; sum += d * d; }
    return std::sqrt (sum / (double) reference.size ());
}
double cents (double measured, double expected) { return 1200 * std::log2 (measured / expected); }
// Phase (radians) of the hz component of x over [from, to), by projection.
double phaseAt (const std::vector<float>& x, size_t from, size_t to, double hz)
{
    double s = 0, c = 0;
    for (size_t i = from; i < to; ++i) { s += x[i] * std::sin (kTwoPi * hz * (double) i / kRate); c += x[i] * std::cos (kTwoPi * hz * (double) i / kRate); }
    return std::atan2 (c, s);
}

void testSleepingMixBranches ()
{
    const auto sample = makeSample (48000, sine (440), sine (660));
    const auto data = analyze (*sample);
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto controls = whole (*data); controls.loop = true;
        controls.stages[0] = { bw::SpectralStageType::binShift, .1f, false };
        auto low = controls, high = controls;
        low.wet = .25f; high.wet = .75f;
        bw::SpectralVoicePlayer quarter, threeQuarters, dry, wet;
        quarter.reset (data.get (), low, rate); threeQuarters.reset (data.get (), high, rate);
        auto d = controls, w = controls; d.wet = 0; w.wet = 1;
        dry.reset (data.get (), d, rate); wet.reset (data.get (), w, rate);
        double error = 0;
        const auto heapBefore = gAudioHeapOps.load ();
        for (int n = 0; n < 24000; ++n)
        {
            ScopedAudioHeapGuard guard;
            // Move the stage while the processed branch is asleep: its phase must keep advancing.
            if (n == 4000) low.stages[0].amount = high.stages[0].amount = d.stages[0].amount = w.stages[0].amount = -.15f;
            if (n == 8003) { d.wet = 1; w.wet = 0; }
            const auto a = quarter.process (low), b = threeQuarters.process (high);
            const auto x = dry.process (d), y = wet.process (w);
            assert (std::isfinite (x.first) && std::isfinite (y.second));
            // Four-hop pre-roll plus the existing 10 ms mix ramp must settle within this bound.
            if (n < 8003 || n > 8003 + 4 * bw::kSpectralHop + (int) (rate * .01) + 2)
            {
                const double expectedDry = 1.5 * a.first - .5 * b.first;
                const double expectedWet = 1.5 * b.first - .5 * a.first;
                error = std::max (error, std::abs (x.first - (n < 8003 ? expectedDry : expectedWet)));
                error = std::max (error, std::abs (y.first - (n < 8003 ? expectedWet : expectedDry)));
                const double rightDry = 1.5 * a.second - .5 * b.second;
                const double rightWet = 1.5 * b.second - .5 * a.second;
                error = std::max (error, std::abs (x.second - (n < 8003 ? rightDry : rightWet)));
                error = std::max (error, std::abs (y.second - (n < 8003 ? rightWet : rightDry)));
            }
        }
        assert (gAudioHeapOps.load () == heapBefore);
        std::cout << "sleeping spectral branches " << rate << " max error " << error << '\n';
        assert (error < 2e-6);
        // Rapid reversals during pre-roll must remain finite.
        for (int n = 0; n < 8000; ++n)
        {
            if (n % 173 == 0) d.wet = 1 - d.wet;
            const auto out = dry.process (d);
            assert (std::isfinite (out.first) && std::abs (out.first) < 2);
        }
        d.loop = false;
        for (int n = 0; n < 120000 && ! dry.idle (); ++n) dry.process (d);
        assert (dry.idle ());
    }
}

void testIdentityReconstruction ()
{
    const int frames = 48000;
    uint32_t state = 0x9e3779b9u;
    auto noise = [&state] (int) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return (float) ((double) state / 4294967296.0 - .5); };
    std::vector<float> noiseData; for (int i = 0; i < frames; ++i) noiseData.push_back (noise (i));
    const std::vector<std::shared_ptr<bw::Sample>> fixtures {
        makeSample (frames, [] (int i) { return i == 1000 ? 1.0f : 0.0f; }),
        makeSample (frames, sine (440)),
        makeSample (frames, [&] (int i) { return noiseData[(size_t) i]; }),
        makeSample (frames, sine (440), sine (660)) };
    for (const auto& sample : fixtures)
    {
        const auto data = analyze (*sample);
        assert (data->bytes () == bw::SpectralData::estimateBytes (frames, sample->numChannels ()));
        bw::SpectralVoicePlayer player;
        player.reset (data.get (), whole (*data), kRate);
        const auto [l, r] = render (player, whole (*data), frames + bw::kSpectralFftSize);
        // Zero latency: output sample n is source sample n, edges included; afterwards silence.
        const double errL = dbfs (errorRms (l, sample->data)), errR = dbfs (errorRms (r, sample->isStereo () ? sample->right : sample->data));
        std::cout << "identity " << sample->numChannels () << "ch error " << errL << " / " << errR << " dBFS\n";
        assert (errL < -90 && errR < -90);
        for (size_t i = (size_t) frames; i < l.size (); ++i) assert (std::abs (l[i]) < 1.0e-6f);
        assert (player.finished && player.idle ());
    }
    // The fixed latency matches an impulse's measured delay.
    const auto impulse = makeSample (frames, [] (int i) { return i == 2000 ? 1.0f : 0.0f; });
    const auto data = analyze (*impulse);
    bw::SpectralVoicePlayer player; player.reset (data.get (), whole (*data), kRate);
    const auto [l, r] = render (player, whole (*data), 4000);
    const auto peakAt = std::distance (l.begin (), std::max_element (l.begin (), l.end ()));
    assert (peakAt - 2000 == bw::kSpectralLatencySamples);
}

void testCancellation ()
{
    const auto sample = makeSample (480000, sine (440));
    juce::String error; int calls = 0;
    const auto data = bw::analyzeSpectral (*sample, error, [&calls] { return ++calls > 3; });
    assert (data == nullptr && error.contains ("cancelled"));
}

void testSpectralStages ()
{
    bw::SpectralBuffer bins {}, scratch {};
    bins[2*40] = 1; bins[2*100] = .5f; bins[0] = .1f; bins[2*(bw::kSpectralBins-1)] = .2f;
    const auto original = bins;
    bw::applySpectralStage (bins,scratch,{ bw::SpectralStageType::binShift,7.0f/64,false },48000.0/2048,0);
    assert (bins[2*47] == 1 && bins[2*107] == .5f && bins[2*40] == 0 && bins[0] == .1f && bins[2*(bw::kSpectralBins-1)] == .2f);
    bins = {}; bins[2*1020] = 1;
    bw::applySpectralStage (bins,scratch,{ bw::SpectralStageType::binShift,7.0f/64,false },48000.0/2048,0);
    for (float v : bins) assert (v == 0);
    bins = {}; bins[2*40] = 1;
    bw::applySpectralStage (bins,scratch,{ bw::SpectralStageType::binShift,7.5f/64,false },48000.0/2048,0);
    assert (bins[2*47] == .5f && bins[2*48] == .5f);
    bins = {}; for (int k : { 16,32,64,128,256,512 }) bins[2*(size_t) k] = 1;
    bw::applySpectralStage (bins,scratch,{ bw::SpectralStageType::tilt,.5f,false },48000.0/2048,0);
    for (int k : { 16,32,64,128,256,512 })
        assert (std::abs (20*std::log10 (bins[2*(size_t) k])-6*std::log2 (k*48000.0/2048/1000)) <= .5);
    auto first = original, second = original;
    bw::SpectralStage shift { bw::SpectralStageType::binShift,.5f,false }, tilt { bw::SpectralStageType::tilt,1,false };
    bw::applySpectralStage (first,scratch,shift,48000.0/2048,0); bw::applySpectralStage (first,scratch,tilt,48000.0/2048,0);
    bw::applySpectralStage (second,scratch,tilt,48000.0/2048,0); bw::applySpectralStage (second,scratch,shift,48000.0/2048,0);
    assert (std::abs (first[2*72]-second[2*72]) > .1);
    const auto sample = makeSample (24000,sine (1500,.2)); const auto data = analyze (*sample);
    auto c = whole (*data); c.stages[0] = { bw::SpectralStageType::binShift,7.0f/64,false };
    bw::SpectralVoicePlayer player; player.reset (data.get (),c,kRate);
    const auto rendered = render (player,c,24000).first;
    assert (std::abs (frequency (rendered,4000,20000,kRate)-(1500+7*kRate/2048)) < .1);
    c.stages[0].amount = 0; player.reset (data.get (),c,kRate);
    const auto neutral = render (player,c,24000).first; double error = 0;
    for (size_t i = 0; i < neutral.size (); ++i) error += std::pow (neutral[i]-sample->data[i],2);
    assert (error/neutral.size () < 1e-9);
    c.stages[0].amount = .13f; c.loop = true; player.reset (data.get (),c,kRate);
    float previous = 0; double maxJump = 0;
    for (int n = 0; n < 18000; ++n)
    {
        if (n == 4000) c.stages[0].amount = 0;
        if (n == 8000) { c.stages[0].amount = .13f; c.stages[1] = tilt; }
        if (n == 11000) c.reverseOrder = true;
        if (n == 14000) c.stages[0].bypass = true;
        const auto value = player.process (c).first;
        assert (std::isfinite (value));
        maxJump = std::max (maxJump,(double) std::abs (value-previous)); previous = value;
    }
    std::cout << "stage transitions max sample step " << maxJump << "\n";
    assert (maxJump < .3);
    std::cout << "ok: spectral stage precision, identity, order and transitions\n";
}

void testTransientIdentity ()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
    const auto sample = makeSample (12000, [] (int n) { return n == 3000 || n == 8000 ? .8f : 0.0f; });
    sample->rate = rate;
    const auto data = analyze (*sample);
    assert (! data->transientSamples.empty ());
    auto controls = whole (*data); controls.transientReset = true;
    bw::SpectralVoicePlayer player; player.reset (data.get (), controls, rate);
    const auto [left, right] = render (player, controls, 12000);
    double error = 0;
    for (size_t n = 0; n < left.size (); ++n) error += std::pow (left[n] - sample->data[n], 2);
    assert (error / left.size () < 1e-9);
    for (double speed : { .5, 2.0 })
    {
        controls.speed = speed; controls.transientReset = false;
        bw::SpectralVoicePlayer baseline; baseline.reset (data.get (), controls, rate);
        const auto plain = render (baseline, controls, (int) (12000/speed)).first;
        controls.transientReset = true;
        bw::SpectralVoicePlayer treated; treated.reset (data.get (), controls, rate);
        const auto improved = render (treated, controls, (int) (12000/speed)).first;
        double before = 0, after = 0;
        for (int attack : { 3000, 8000 })
            for (int n = std::max (0, (int) (attack/speed)-(int) (rate*.02)); n < (int) (attack/speed); ++n)
            {
                before += plain[(size_t) n]*plain[(size_t) n]; after += improved[(size_t) n]*improved[(size_t) n];
            }
        std::cout << "transient speed " << speed << " pre-echo " << before << " -> " << after << "\n";
        assert (after <= before*.75);
        for (int attack : { 3000, 8000 })
        {
            const int expected = (int) (attack/speed);
            double peak = 0; int peakAt = -1;
            for (int n = expected-16; n <= expected+16; ++n)
                if (std::abs (improved[(size_t) n]) > peak) { peak = std::abs (improved[(size_t) n]); peakAt = n; }
            std::cout << "attack " << expected << " peak " << peak << " at " << peakAt << "\n";
            assert (peak > .1 && std::abs (peakAt-expected) <= 1);
        }
    }
    }
}

void testTransientRatesAndCloseAttacks ()
{
    for (double sourceRate : { 48000.0, 96000.0 })
        for (double hostRate : { 48000.0, 96000.0 })
            for (int spacing : { 1200, 5000 })
            {
                const auto sample = makeSample (18000,[=] (int n) { return n >= 3000 && n <= 13000 && (n-3000)%spacing == 0 ? .8f : 0; });
                sample->rate = sourceRate; const auto data = analyze (*sample);
                for (double speed : { .5, 2.0 })
                {
                    auto c = whole (*data); c.speed = speed; c.transientReset = true;
                    bw::SpectralVoicePlayer player; player.reset (data.get (),c,hostRate);
                    const double step = speed*sourceRate/hostRate;
                    const auto rendered = render (player,c,(int) (18000/step)).first;
                    double onGrid = 0, offGrid = 0;
                    for (size_t n = 0; n < rendered.size (); ++n)
                    {
                        bool near = false;
                        for (int a = 3000; a <= 13000; a += spacing) near = near || std::abs ((double) n-a/step) <= 2;
                        (near ? onGrid : offGrid) += rendered[n]*rendered[n];
                    }
                    std::cout << "attacks source/host " << sourceRate << "/" << hostRate << " spacing " << spacing << " speed " << speed << " energy " << onGrid << " / " << offGrid << " detected " << data->transientSamples.size () << "\n";
                    assert (onGrid > .01 && offGrid < onGrid*.1);
                }
            }
}

void testPhaseLockFixtures ()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (bool percussion : { false, true })
        {
            auto signal = [=] (int n)
            {
                if (! percussion) return (float) (.15*std::sin (kTwoPi*440*n/rate)+.15*std::sin (kTwoPi*470*n/rate));
                const int age = n % (int) (rate*.08);
                return (float) (.6*std::exp (-age/(rate*.006))*std::cos (kTwoPi*1300*age/rate));
            };
            const auto sample = makeSample ((int) (rate*.25),signal,[&] (int n) { return -.5f*signal (n); }); sample->rate = rate;
            const auto data = analyze (*sample);
            for (double speed : { .5, 1.0, 2.0 })
                for (bool lock : { false, true })
                {
                    auto c = whole (*data); c.speed = speed; c.phaseLock = lock; c.transientReset = true;
                    bw::SpectralVoicePlayer player; player.reset (data.get (),c,rate);
                    const auto [l,r] = render (player,c,(int) (sample->numFrames ()/speed));
                    double energy = 0, identity = 0;
                    for (size_t n = 0; n < l.size (); ++n)
                    {
                        assert (std::isfinite (l[n]) && std::isfinite (r[n]) && std::abs (r[n]+.5*l[n]) < 1e-5);
                        energy += l[n]*l[n];
                        if (speed == 1) identity += std::pow (l[n]-sample->data[n],2);
                    }
                    assert (energy/l.size () > 1e-5);
                    if (speed == 1) assert (identity/l.size () < 1e-9);
                }
        }
    std::cout << "ok: phase-lock/percussion stereo and identity at 44.1/48/96 kHz\n";
}

void testFreezeAndScrub ()
{
    const auto sample = makeSample (48000, sine (440, .25));
    const auto data = analyze (*sample);
    auto c = whole (*data); c.freeze = true; c.position = .5;
    bw::SpectralVoicePlayer player; player.reset (data.get (), c, kRate);
    const auto [l, r] = render (player, c, (int) (10 * kRate));
    const double hz = frequency (l, 4800, l.size (), kRate);
    const double drift = dbfs (rms (l, l.size () - 48000, l.size ())) - dbfs (rms (l, 4800, 52800));
    std::cout << "freeze 10 s: " << cents (hz, 440) << " cents, drift " << drift << " dB\n";
    assert (std::abs (cents (hz, 440)) < 1 && std::abs (drift) < .2);
    assert (std::abs (dbfs (rms (l, 4800, 52800)) - dbfs (.25 / std::sqrt (2.0))) < .2);

    // Freeze on and off mid-note: the stationary sine continues without a transition residue.
    const auto longer = makeSample (96000, sine (440, .25));
    const auto longData = analyze (*longer);
    auto moving = whole (*longData);
    bw::SpectralVoicePlayer toggled; toggled.reset (longData.get (), moving, kRate);
    std::vector<float> out;
    for (int i = 0; i < 72000; ++i)
    {
        auto controls = moving;
        controls.freeze = i >= 24000 && i < 48000;
        controls.position = (bw::SpectralData::frameFor (24000) - controls.startFrame) / (controls.endFrame - controls.startFrame);
        out.push_back (toggled.process (controls).first);
    }
    // Reference: the best-fit stationary sine over the whole render.
    const double phi = phaseAt (out, 0, out.size (), 440);
    double residual = 0;
    for (size_t i = 2048; i < out.size (); ++i) { const double d = out[i] - .25 * std::sin (kTwoPi * 440 * (double) i / kRate + phi); residual = std::max (residual, std::abs (d)); }
    std::cout << "freeze toggle residue " << dbfs (residual) << " dBFS\n";
    assert (dbfs (residual) < -50);

    // Stereo: the L/R phase relation of the frozen frame is kept.
    const auto stereo = makeSample (48000, sine (440, .25), sine (440, .25, juce::MathConstants<double>::halfPi));
    const auto stereoData = analyze (*stereo);
    auto sc = whole (*stereoData); sc.freeze = true; sc.position = .3;
    bw::SpectralVoicePlayer frozen; frozen.reset (stereoData.get (), sc, kRate);
    const auto [fl, fr] = render (frozen, sc, (int) (5 * kRate));
    const double relation = bw::principalArgument (phaseAt (fr, 200000, 240000, 440) - phaseAt (fl, 200000, 240000, 440));
    std::cout << "frozen stereo relation " << relation << " rad\n";
    assert (std::abs (relation - juce::MathConstants<double>::halfPi) < .01);

    // Continuous scrub stays finite and bounded.
    auto scrub = whole (*data); scrub.freeze = true;
    bw::SpectralVoicePlayer scrubber; scrubber.reset (data.get (), scrub, kRate);
    for (int i = 0; i < 96000; ++i)
    {
        scrub.position = .5 + .5 * std::sin (kTwoPi * i / 48000.0);
        const auto v = scrubber.process (scrub).first;
        assert (std::isfinite (v) && std::abs (v) < 1);
    }
}

void testTimeAndPitchSeparation ()
{
    const int frames = 48000;
    const auto sample = makeSample (frames, sine (440, .25));
    const auto data = analyze (*sample);
    for (double speed : { .5, 1.0, 2.0 })
        for (int semitones : { -12, 0, 12 })
        {
            auto c = whole (*data); c.speed = speed; c.pitch = std::exp2 (semitones / 12.0);
            bw::SpectralVoicePlayer player; player.reset (data.get (), c, kRate);
            std::vector<float> out; int length = 0;
            while (! player.finished && length < 4 * frames) { out.push_back (player.process (c).first); ++length; }
            const double hz = frequency (out, (size_t) length / 4, (size_t) length * 3 / 4, kRate);
            const double expectedLength = frames / speed;
            std::cout << "speed " << speed << " pitch " << semitones << ": " << cents (hz, 440 * c.pitch) << " cents, length "
                      << length - expectedLength << " samples\n";
            assert (std::abs (cents (hz, 440 * c.pitch)) < 2);
            assert (std::abs (length - expectedLength) <= bw::kSpectralHop);
        }

    // Chirp 300 -> 600 Hz over 2 s, one octave up at unit speed.
    const int chirpFrames = 96000;
    const auto chirp = makeSample (chirpFrames, [] (int i) { const double t = i / kRate; return (float) (.25 * std::sin (kTwoPi * (300 * t + 75 * t * t))); });
    const auto chirpData = analyze (*chirp);
    auto cc = whole (*chirpData); cc.pitch = 2;
    bw::SpectralVoicePlayer chirper; chirper.reset (chirpData.get (), cc, kRate);
    const auto [chirped, unused] = render (chirper, cc, chirpFrames);
    for (int window = 1; window < 9; ++window)
    {
        const size_t from = (size_t) window * 9600, to = from + 9600;
        const double centre = (double) (from + to) / 2 / kRate;
        const double expected = 2 * (300 + 150 * centre);
        // The shifted frequency trajectory trails by half a hop (documented); pitch is measured against it.
        const double aligned = 2 * (300 + 150 * (centre - bw::kSpectralHop / 2 / kRate));
        const double chirpError = cents (frequency (chirped, from, to, kRate), aligned);
        std::cout << "chirp window " << window << ": " << chirpError << " cents after half-hop alignment, "
                  << cents (frequency (chirped, from, to, kRate), expected) << " unaligned\n";
        assert (std::abs (chirpError) < 2);
    }

    // Content shifted past Nyquist is dropped rather than folded back.
    const auto high = makeSample (frames, sine (15000, .25));
    const auto highData = analyze (*high);
    auto hc = whole (*highData); hc.pitch = 2;
    bw::SpectralVoicePlayer shifted; shifted.reset (highData.get (), hc, kRate);
    const auto [above, unusedHigh] = render (shifted, hc, frames);
    std::cout << "15 kHz +12 st residue " << dbfs (rms (above, 4800, 43200)) << " dBFS\n";
    assert (dbfs (rms (above, 4800, 43200)) < -60);

    // Impulse train (100 Hz) up a fifth: report energy and peak for listening notes; it must stay finite.
    const auto train = makeSample (frames, [] (int i) { return i % 480 == 0 ? .5f : 0.0f; });
    const auto trainData = analyze (*train);
    auto tc = whole (*trainData); tc.pitch = std::exp2 (7 / 12.0);
    bw::SpectralVoicePlayer trainer; trainer.reset (trainData.get (), tc, kRate);
    const auto [trained, unusedTrain] = render (trainer, tc, frames);
    std::cout << "impulse train +7 st: rms " << dbfs (rms (trained, 4800, 43200)) << " dBFS, input " << dbfs (rms (train->data, 4800, 43200)) << " dBFS\n";

    // Ramps of speed and pitch: no phase jumps on a sine (bounded step), no NaN.
    const auto longer = makeSample (4 * frames, sine (440, .25));
    const auto longData = analyze (*longer);
    auto rc = whole (*longData); rc.loop = true;
    bw::SpectralVoicePlayer ramped; ramped.reset (longData.get (), rc, kRate);
    float previous = 0, largest = 0;
    for (int i = 0; i < 4 * frames; ++i)
    {
        const double t = (double) i / (4 * frames);
        rc.speed = .5 + 1.5 * t; rc.pitch = std::exp2 ((-12 + 24 * t) / 12.0);
        const float v = ramped.process (rc).first;
        assert (std::isfinite (v));
        if (i > 4096) largest = std::max (largest, std::abs (v - previous));
        previous = v;
    }
    // A 0.25 sine at the highest pitch (880 Hz) steps at most 0.25 * 2 pi 880 / 48000 = 0.029 per sample.
    std::cout << "ramp largest step " << largest << "\n";
    assert (largest < .05f);
}

// ---- Processor integration: mode, background analysis, budget, state and modulation ----

const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("borato-wave-spectral");

juce::File writeWav (const juce::String& name, const bw::Sample& sample)
{
    folder.createDirectory ();
    const auto file = folder.getChildFile (name + ".wav");
    file.deleteFile ();
    juce::AudioBuffer<float> audio (sample.numChannels (), sample.numFrames ());
    audio.copyFrom (0, 0, sample.data.data (), sample.numFrames ());
    if (sample.isStereo ()) audio.copyFrom (1, 0, sample.right.data (), sample.numFrames ());
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream ().release ());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get (), sample.rate, (unsigned) sample.numChannels (), 32, {}, 0));
    assert (writer != nullptr);
    stream.release ();
    assert (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples ()));
    return file;
}

void sampleOnly (Rig& r)
{
    r.set ("oscAOn", 0); r.set ("sampleOn", 1); r.set ("sampleLevel", .8f); r.set ("sampleRoute", 2);
}

void waitReady (WaveProcessor& processor)
{
    assert (processor.waitForSampleLoad (60000));
    if (processor.spectralState () != WaveProcessor::SpectralState::ready)
        std::cout << "spectral state " << (int) processor.spectralState () << " error '" << processor.spectralError () << "' data "
                  << (processor.spectralData () ? (long long) processor.spectralData ()->sourceHash : -1) << " sample "
                  << (processor.sample () ? (long long) processor.sample ()->contentHash : -1) << " mode "
                  << processor.apvts.getRawParameterValue ("sampleMode")->load () << "\n";
    assert (processor.spectralState () == WaveProcessor::SpectralState::ready);
}

// Renders in blocks, asserting finite output and no heap traffic inside processBlock.
std::vector<float> renderChecked (WaveProcessor& processor, juce::MidiBuffer& midi, int total, int block)
{
    std::vector<float> left;
    juce::AudioBuffer<float> audio (2, block);
    midi.ensureSize (256);
    for (int done = 0; done < total;)
    {
        const int chunk = juce::jmin (block, total - done);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers (), 2, 0, chunk);
        const size_t before = gAudioHeapOps.load ();
        {
            const ScopedAudioHeapGuard guard;
            processor.processBlock (view, midi);
        }
        assert (gAudioHeapOps.load () == before);
        midi.clear ();
        for (int i = 0; i < chunk; ++i)
        {
            assert (std::isfinite (view.getSample (0, i)) && std::isfinite (view.getSample (1, i)));
            left.push_back (view.getSample (0, i));
        }
        done += chunk;
    }
    return left;
}

double rmsError (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i) sum += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt (sum / (double) (to - from));
}

void testProcessorIdentityModesAndAllocation ()
{
    const auto file = writeWav ("spectral-identity", *makeSample (48000, sine (440), sine (660)));
    auto render = [&] (int mode)
    {
        Rig r; sampleOnly (r); r.set ("sampleMode", (float) mode);
        assert (r.processor.loadSample (file).isEmpty ());
        if (mode == kSampleModeSpectral) waitReady (r.processor);
        else assert (r.processor.spectralState () == WaveProcessor::SpectralState::off && r.processor.spectralData () == nullptr);
        r.send (on (60));
        return renderChecked (r.processor, r.pending, 24000, 64);
    };
    // At the root note, unit speed and pitch, Spectral equals the sample's own playback.
    const auto sampled = render (kSampleModePlayback), resynthesised = render (kSampleModeSpectral);
    const double error = dbfs (rmsError (sampled, resynthesised, 0, sampled.size ()));
    std::cout << "processor spectral vs sample: " << error << " dBFS (signal " << dbfs (rms (sampled, 4800, 24000)) << " dBFS)\n";
    assert (rms (sampled, 4800, 24000) > .05 && error < -90);

    // Sixteen notes with freeze, scrub and pitch modulation, and mode switches mid-note, allocate nothing.
    Rig r; sampleOnly (r); r.set ("sampleMode", kSampleModeSpectral);
    assert (r.processor.loadSample (file).isEmpty ());
    waitReady (r.processor);
    r.set ("mod1Enabled", 1); r.set ("mod1Source", 1); r.set ("mod1Destination", bw::kSpectralPositionDestination); r.set ("mod1Amount", .5f);
    r.set ("mod2Enabled", 1); r.set ("mod2Source", 3); r.set ("mod2Destination", bw::kSpectralPitchDestination); r.set ("mod2Amount", 1);
    r.set ("lfo1Rate", 8);
    r.set ("spectralStage1Type",1); r.set ("spectralStage1Amount",.13f);
    r.set ("spectralStage2Type",2); r.set ("spectralStage2Amount",.5f); r.set ("spectralTransients",1);
    r.set ("mod3Enabled",1); r.set ("mod3Source",1); r.set ("mod3Destination",bw::kSpectralStage1Destination); r.set ("mod3Amount",.2f);

    for (int note = 48; note < 64; ++note) r.send (on (note));
    renderChecked (r.processor, r.pending, 4800, 37);
    r.set ("spectralFreeze", 1); r.set ("macro1", 1); renderChecked (r.processor, r.pending, 4800, 128);
    r.set ("spectralSpeed", 2); r.set ("spectralPitch", -12); r.set ("macro1", 0); renderChecked (r.processor, r.pending, 4800, 512);
    r.set ("sampleMode", kSampleModeGranular); renderChecked (r.processor, r.pending, 2400, 64);
    // Leaving Spectral released the analysis, so returning reanalyses; held notes resume once it is ready.
    r.set ("sampleMode", kSampleModeSpectral); renderChecked (r.processor, r.pending, 256, 64);
    waitReady (r.processor); renderChecked (r.processor, r.pending, 2400, 64);
    assert (r.processor.spectralCursor.load () >= 0);
    // Leaving the mode releases the analysis once the voices let go of it.
    r.set ("sampleMode", kSampleModePlayback); renderChecked (r.processor, r.pending, 4800, 64);
    assert (r.processor.spectralData () == nullptr && r.processor.spectralState () == WaveProcessor::SpectralState::off);
    assert (r.processor.retiredMemoryBytes () == 0);
    std::cout << "ok: processor identity, 16 voices, freeze/scrub/pitch/speed modulation and mode switches without callback allocations\n";
}

void testBudgetRefusalKeepsPreviousAnalysis ()
{
    Rig r; sampleOnly (r); r.set ("sampleMode", kSampleModeSpectral);
    assert (r.processor.spectralState () == WaveProcessor::SpectralState::missing);
    const int frames = 4000000;
    auto bankOf = [] (std::shared_ptr<const bw::Sample> s) { bw::SampleBank bank; bank.zones.push_back ({}); bank.zones[0].sample = std::move (s); return bank; };
    const auto first = makeSample (frames, sine (220, .25), sine (330, .25));
    assert (r.processor.setSampleBank (bankOf (first)).isEmpty ());
    waitReady (r.processor);
    const auto analysis = r.processor.spectralData ();
    assert (analysis->bytes () == bw::SpectralData::estimateBytes (frames, 2) && analysis->bytes () > (size_t) 100 << 20);
    // A second long stereo sample: its analysis cannot coexist with the old one and both samples, so it is
    // refused before allocating, and the previous analysis stays published.
    const auto second = makeSample (frames, sine (440, .25), sine (550, .25));
    assert (r.processor.setSampleBank (bankOf (second)).isEmpty ());
    assert (r.processor.waitForSampleLoad (60000));
    std::cout << "refused: " << r.processor.spectralError () << "\n";
    assert (r.processor.spectralState () == WaveProcessor::SpectralState::refused);
    assert (r.processor.spectralError ().contains ("256 MB") && r.processor.spectralData () == analysis);
    assert (r.processor.assetMemoryBytes () <= bw::kMaxAssetMemoryBytes);
    // Leaving the mode frees the old analysis; returning retries within the budget.
    r.set ("sampleMode", kSampleModePlayback); r.run (.01);
    r.set ("sampleMode", kSampleModeSpectral);
    waitReady (r.processor);
    assert (r.processor.spectralData ()->sourceHash == second->contentHash);
    assert (r.processor.assetMemoryBytes () <= bw::kMaxAssetMemoryBytes);
    std::cout << "ok: budget refusal before allocation, previous analysis kept, retry after release\n";
}

void testPortableStateVersionAndModulation ()
{
    const auto file = writeWav ("spectral-portable", *makeSample (24000, sine (440, .4), sine (660, .4)));
    Rig setup; sampleOnly (setup); setup.set ("sampleMode", kSampleModeSpectral);
    setup.set ("spectralSpeed", 1.5f); setup.set ("spectralPitch", 3); setup.set ("sampleLoop", 1);
    assert (setup.processor.loadSample (file).isEmpty ());
    waitReady (setup.processor);
    setup.set ("mod1Enabled", 1); setup.set ("mod1Source", 3); setup.set ("mod1Destination", bw::kSpectralSpeedDestination); setup.set ("mod1Amount", .2f);
    setup.set ("macro1", .5f);
    juce::MemoryBlock state; setup.processor.getStateInformation (state);
    const auto preset = folder.getChildFile ("spectral.bwpreset"); assert (setup.processor.savePreset (preset));
    file.deleteFile (); // The state carries the audio; the analysis is regenerated, never stored.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData (), (int) state.getSize ());
    assert (xml != nullptr && (int) xml->getIntAttribute ("spectralAlgorithmVersion") == bw::kSpectralAlgorithmVersion);
    assert (xml->toString ().indexOf ("magnitude") < 0);
    auto render = [&] (const juce::MemoryBlock* from, const juce::File* presetFile, bool withEditor)
    {
        Rig r;
        if (from) r.processor.setStateInformation (from->getData (), (int) from->getSize ());
        else assert (r.processor.loadPreset (*presetFile));
        waitReady (r.processor);
        std::unique_ptr<juce::AudioProcessorEditor> editor (withEditor ? r.processor.createEditor () : nullptr);
        r.send (on (64));
        return renderChecked (r.processor, r.pending, 36000, 64);
    };
    const auto reference = render (&state, nullptr, false);
    assert (rms (reference, 4800, 36000) > .05);
    for (const auto& audio : { render (&state, nullptr, true), render (nullptr, &preset, false) })
        assert (dbfs (rmsError (reference, audio, 0, reference.size ())) < -80);
    // A state written by another algorithm version holds no cache to misread; it is simply reanalysed.
    auto other = *xml; other.setAttribute ("spectralAlgorithmVersion", 99);
    juce::MemoryBlock otherState; juce::AudioProcessor::copyXmlToBinary (other, otherState);
    assert (dbfs (rmsError (reference, render (&otherState, nullptr, false), 0, reference.size ())) < -80);

    // Macro to Spectral Pitch: half depth unipolar at full macro moves 0 st to +12 st.
    const auto toneFile = writeWav ("spectral-pitch", *makeSample (48000, sine (440, .4)));
    Rig r; sampleOnly (r); r.set ("sampleMode", kSampleModeSpectral); r.set ("spectralFreeze", 1); r.set ("spectralPos", .5f);
    assert (r.processor.loadSample (toneFile).isEmpty ());
    waitReady (r.processor);
    r.set ("mod1Enabled", 1); r.set ("mod1Source", 3); r.set ("mod1Destination", bw::kSpectralPitchDestination); r.set ("mod1Amount", .5f);
    r.set ("macro1", 1);
    r.send (on (60));
    const auto shifted = renderChecked (r.processor, r.pending, 48000, 64);
    const double hz = frequency (shifted, 9600, 48000, kRate);
    std::cout << "macro to spectral pitch: " << hz << " Hz\n";
    assert (std::abs (cents (hz, 880)) < 2);
    std::cout << "ok: portable spectral state and preset, version tag, editor invariance and matrix pitch\n";
}

void testSpectralFilterAndPhaseAlignedMix ()
{
    constexpr double binHz = kRate / bw::kSpectralFftSize;

    // 1. Cutoff precision: spectralFilterHigh = 1000 Hz, spectralFilterLow = 20 Hz, shift = 0 st.
    const auto harmonicSample = makeSample (24000, [] (int n)
    {
        return (float) (0.20 * std::sin (kTwoPi * 300.0 * n / kRate)
                      + 0.20 * std::sin (kTwoPi * 600.0 * n / kRate)
                      + 0.20 * std::sin (kTwoPi * 2500.0 * n / kRate)
                      + 0.20 * std::sin (kTwoPi * 5000.0 * n / kRate));
    });
    const auto data = analyze (*harmonicSample);
    auto cOpen = whole (*data);
    bw::SpectralVoicePlayer player;
    player.reset (data.get (), cOpen, kRate);
    const auto openAudio = render (player, cOpen, 24000).first;

    auto cLowpass = whole (*data);
    cLowpass.filter.lowHz = 20.0f;
    cLowpass.filter.highHz = 1000.0f;
    cLowpass.filter.shiftSemitones = 0.0f;
    player.reset (data.get (), cLowpass, kRate);
    const auto lpAudio = render (player, cLowpass, 24000).first;

    auto bandPowerDb = [] (const std::vector<float>& x, double hz)
    {
        double s = 0, c = 0;
        constexpr size_t from = 4096, to = 20480;
        for (size_t i = from; i < to; ++i)
        {
            s += x[i] * std::sin (kTwoPi * hz * (double) i / kRate);
            c += x[i] * std::cos (kTwoPi * hz * (double) i / kRate);
        }
        const double mag = 2.0 * std::sqrt (s * s + c * c) / (double) (to - from);
        return 20.0 * std::log10 (std::max (mag, 1.0e-12));
    };
    const double pass300Diff = std::abs (bandPowerDb (lpAudio, 300.0) - bandPowerDb (openAudio, 300.0));
    const double pass600Diff = std::abs (bandPowerDb (lpAudio, 600.0) - bandPowerDb (openAudio, 600.0));
    const double stop2500Atten = bandPowerDb (openAudio, 2500.0) - bandPowerDb (lpAudio, 2500.0);
    const double stop5000Atten = bandPowerDb (openAudio, 5000.0) - bandPowerDb (lpAudio, 5000.0);
    std::cout << "spectral filter LP 1kHz: passband diff " << pass300Diff << " / " << pass600Diff
              << " dB, stopband atten " << stop2500Atten << " / " << stop5000Atten << " dB\n";
    assert (pass300Diff <= 0.5 && pass600Diff <= 0.5);
    assert (stop2500Atten >= 60.0 && stop5000Atten >= 60.0);

    // 2. Inverted / equal cutoffs (lowHz >= highHz): deterministic total attenuation, zero NaN/Inf.
    for (std::pair<float, float> inv : { std::pair { 5000.0f, 500.0f }, std::pair { 1000.0f, 1000.0f } })
    {
        bw::SpectralFilter f;
        f.lowHz = inv.first; f.highHz = inv.second;
        for (int k = 0; k < bw::kSpectralBins; ++k)
        {
            const float g = bw::spectralFilterBinGain (k, binHz, f);
            assert (std::isfinite (g) && g == 0.0f);
        }
        auto cInv = whole (*data);
        cInv.filter = f;
        player.reset (data.get (), cInv, kRate);
        const auto invAudio = render (player, cInv, 12000).first;
        assert (rms (invAudio, 0, invAudio.size ()) < 1.0e-7);
    }

    // Edges that cross mid-note (e.g. matrix modulation) also attenuate fully, without a stage-layout change.
    {
        auto cLive = whole (*data);
        cLive.loop = true;
        cLive.filter.lowHz = 200.0f; cLive.filter.highHz = 8000.0f;
        player.reset (data.get (), cLive, kRate);
        const auto before = render (player, cLive, 8000).first;
        cLive.filter.lowHz = 6000.0f; cLive.filter.highHz = 900.0f;
        const auto after = render (player, cLive, 12000).first;
        assert (rms (before, 4000, 8000) > 1.0e-3 && rms (after, 6000, 12000) < 1.0e-6);
    }

    // 3. Filter shift (+12 st doubles effective cutoff from 1000 Hz to 2000 Hz; custom curve shifts safely).
    bw::SpectralFilter fShiftedUp { 20.0f, 1000.0f, 12.0f, false, false, nullptr };
    bw::SpectralFilter fRef2k { 40.0f, 2000.0f, 0.0f, false, false, nullptr };
    for (int k = 1; k < bw::kSpectralBins - 1; ++k)
    {
        const float gShifted = bw::spectralFilterBinGain (k, binHz, fShiftedUp);
        const float gRef = bw::spectralFilterBinGain (k, binHz, fRef2k);
        assert (std::abs (gShifted - gRef) < 1.0e-5f);
    }
    bw::SpectralFilterCurve customCurve;
    customCurve.custom = true;
    for (int k = 0; k < bw::kSpectralBins; ++k)
        customCurve.gains[(size_t) k] = (k >= 20 && k <= 60) ? 1.0f : 0.1f;
    for (float shiftSt : { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f })
    {
        bw::SpectralFilter fc { 20.0f, 20000.0f, shiftSt, false, false, &customCurve };
        for (int k = 0; k < bw::kSpectralBins; ++k)
        {
            const float g = bw::spectralFilterBinGain (k, binHz, fc);
            assert (std::isfinite (g) && g >= 0.0f && g <= 1.0f);
        }
    }

    // 4. Pre vs Post stage ordering with an active spectral stage (binShift +32 bins = +750 Hz, cutoff = 2000 Hz).
    const auto tone1500 = makeSample (24000, sine (1500.0, 0.3));
    const auto data1500 = analyze (*tone1500);
    auto cPre = whole (*data1500);
    cPre.stages[0] = { bw::SpectralStageType::binShift, 0.75f, false };
    cPre.filter.lowHz = 20.0f; cPre.filter.highHz = 2000.0f; cPre.filter.postOrder = false;
    auto cPost = cPre;
    cPost.filter.postOrder = true;
    player.reset (data1500.get (), cPre, kRate);
    const auto preOut = render (player, cPre, 24000).first;
    player.reset (data1500.get (), cPost, kRate);
    const auto postOut = render (player, cPost, 24000).first;
    const double preHighDb = dbfs (rms (preOut, 4096, 20480));
    const double postHighDb = dbfs (rms (postOut, 4096, 20480));
    std::cout << "spectral filter Pre vs Post (>2kHz shifted band): Pre " << preHighDb << " dBFS, Post " << postHighDb << " dBFS\n";
    assert (preHighDb - postHighDb > 18.0);

    // Mid-note Pre/Post toggle and cutoff/mix sweeps remain click-free.
    auto cToggle = cPre; cToggle.loop = true;
    player.reset (data1500.get (), cToggle, kRate);
    float prevSample = 0.0f, maxStep = 0.0f;
    for (int n = 0; n < 16000; ++n)
    {
        if (n == 4000) cToggle.filter.postOrder = true;
        if (n == 8000) cToggle.filter.postOrder = false;
        if (n == 12000) cToggle.wet = 0.25f;
        const float s = player.process (cToggle).first;
        assert (std::isfinite (s));
        if (n > 0) maxStep = std::max (maxStep, std::abs (s - prevSample));
        prevSample = s;
    }
    assert (maxStep < 0.25f);

    // 5. Phase-aligned Wet/Dry mix: 0.0 == dry (<-95 dBFS), 1.0 == wet (<-95 dBFS), 0.5 with neutral stages has zero comb filtering (<0.1 dB ripple).
    auto cWetOnly = cPre; cWetOnly.wet = 1.0f;
    auto cDryOnly = cPre; cDryOnly.wet = 0.0f;
    auto cRefDry = whole (*data1500);
    player.reset (data1500.get (), cWetOnly, kRate);
    const auto wetSignal = render (player, cWetOnly, 24000).first;
    player.reset (data1500.get (), cDryOnly, kRate);
    const auto drySignal = render (player, cDryOnly, 24000).first;
    player.reset (data1500.get (), cRefDry, kRate);
    const auto refDrySignal = render (player, cRefDry, 24000).first;
    assert (dbfs (errorRms (drySignal, refDrySignal)) < -95.0);
    assert (dbfs (errorRms (wetSignal, preOut)) < -95.0);

    auto cHalfNeutral = whole (*data);
    cHalfNeutral.wet = 0.5f;
    player.reset (data.get (), cHalfNeutral, kRate);
    const auto halfNeutralAudio = render (player, cHalfNeutral, 24000).first;
    assert (dbfs (errorRms (halfNeutralAudio, openAudio)) < -95.0);
    for (double testHz : { 300.0, 600.0, 2500.0, 5000.0 })
        assert (std::abs (bandPowerDb (halfNeutralAudio, testHz) - bandPowerDb (openAudio, testHz)) < 0.05);

    // 6. Processor integration: custom curve state round-trip, corrupted XML rejection, and modulation destinations without heap allocs.
    {
        const auto wavFile = writeWav ("spectral-filter-proc", *harmonicSample);
        Rig rig; sampleOnly (rig); rig.set ("sampleMode", kSampleModeSpectral);
        assert (rig.processor.loadSample (wavFile).isEmpty ());
        waitReady (rig.processor);
        rig.set ("spectralFilterLow", 120.0f);
        rig.set ("spectralFilterHigh", 4200.0f);
        rig.set ("spectralFilterShift", -3.0f);
        rig.set ("spectralFilterOrder", 1.0f);
        rig.set ("spectralMix", 0.65f);
        assert (rig.processor.setSpectralFilterCurve (customCurve));
        assert (rig.processor.getSpectralFilterCurve () == customCurve);

        juce::MemoryBlock savedState;
        rig.processor.getStateInformation (savedState);
        Rig restored;
        restored.processor.setStateInformation (savedState.getData (), (int) savedState.getSize ());
        waitReady (restored.processor);
        assert (restored.processor.getSpectralFilterCurve () == customCurve);
        assert (std::abs (restored.processor.apvts.getRawParameterValue ("spectralFilterHigh")->load () - 4200.0f) < 1.0f);
        assert (std::abs (restored.processor.apvts.getRawParameterValue ("spectralMix")->load () - 0.65f) < 1.0e-4f);

        // Corrupted SPECTRAL_FILTER XML (wrong bins or invalid base64) is rejected without overwriting existing curve.
        auto badXml = juce::AudioProcessor::getXmlFromBinary (savedState.getData (), (int) savedState.getSize ());
        assert (badXml != nullptr);
        if (auto* filtEl = badXml->getChildByName ("SPECTRAL_FILTER"))
            filtEl->setAttribute ("bins", 999);
        juce::MemoryBlock badBlock;
        juce::AudioProcessor::copyXmlToBinary (*badXml, badBlock);
        restored.processor.clearSpectralFilterCurve ();
        assert (! restored.processor.getSpectralFilterCurve ().custom);
        restored.processor.setStateInformation (badBlock.getData (), (int) badBlock.getSize ());
        assert (! restored.processor.getSpectralFilterCurve ().custom);

        // Matrix modulation of Spectral Low, High, Shift and Mix runs with zero heap allocations in processBlock.
        rig.set ("mod1Enabled", 1); rig.set ("mod1Source", 1); rig.set ("mod1Destination", bw::kSpectralFilterLowDestination); rig.set ("mod1Amount", 0.25f);
        rig.set ("mod2Enabled", 1); rig.set ("mod2Source", 2); rig.set ("mod2Destination", bw::kSpectralFilterHighDestination); rig.set ("mod2Amount", -0.35f);
        rig.set ("mod3Enabled", 1); rig.set ("mod3Source", 3); rig.set ("mod3Destination", bw::kSpectralFilterShiftDestination); rig.set ("mod3Amount", 0.5f);
        rig.set ("mod4Enabled", 1); rig.set ("mod4Source", 4); rig.set ("mod4Destination", bw::kSpectralMixDestination); rig.set ("mod4Amount", -0.5f);
        rig.set ("macro1", 0.8f); rig.set ("macro2", 0.6f);
        rig.send (on (60));
        const auto modRendered = renderChecked (rig.processor, rig.pending, 12000, 64);
        assert (rms (modRendered, 2048, 12000) > 1.0e-4);
    }
    std::cout << "ok: spectral filter cutoffs, inverted policy, shift, Pre/Post ordering, phase-aligned wet/dry mix and custom curve persistence\n";
}
} // namespace

int main (int argc, char** argv)
{
#ifdef _WIN32
    // Failed asserts print and exit instead of opening a modal dialog.
    _set_error_mode (_OUT_TO_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--filter") { testSpectralFilterAndPhaseAlignedMix (); folder.deleteRecursively (); return 0; }
    if (argc > 1) { testTransientIdentity (); testSpectralStages (); testPhaseLockFixtures (); testTransientRatesAndCloseAttacks (); testSpectralFilterAndPhaseAlignedMix (); folder.deleteRecursively (); return 0; }
    testSleepingMixBranches ();
    testIdentityReconstruction ();
    testCancellation ();
    testTransientIdentity ();
    testSpectralStages ();
    testPhaseLockFixtures ();
    testTransientRatesAndCloseAttacks ();
    testFreezeAndScrub ();
    testTimeAndPitchSeparation ();
    testProcessorIdentityModesAndAllocation ();
    testBudgetRefusalKeepsPreviousAnalysis ();
    testPortableStateVersionAndModulation ();
    testSpectralFilterAndPhaseAlignedMix ();
    folder.deleteRecursively ();
    std::cout << "ok\n";
    return 0;
}
