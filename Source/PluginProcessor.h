#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "TableOscillator.h"
#include "Modulation.h"
#include "Filter.h"
#include "Effects.h"
#include "UserWavetable.h"
#include "FactoryPresets.h"
#include <array>

struct OscParams
{
    std::atomic<float> *on, *table, *position, *level, *pitch, *fine, *pan, *unison, *detune, *spread, *phase, *random, *user, *route;
};

struct SynthParams
{
    std::atomic<float> *cutoff, *reso, *attack, *decay, *sustain, *release;
    std::array<OscParams, 2> oscillators;
    std::atomic<float> *subOn, *subLevel, *subOctave, *noiseOn, *noiseLevel;
    std::atomic<float> *filterType, *filter2On, *filter2Type, *cutoff2, *reso2, *filterRouting, *subRoute, *noiseRoute;
    std::atomic<float> *env2Attack, *env2Decay, *env2Sustain, *env2Release;
    // Written by the audio thread at block start; null when no user table is loaded.
    std::array<const bw::Wavetable*, 2> userTables {};
};

inline juce::StringArray routeNames() { return { "Filter 1", "Filter 2", "Direct" }; }

class WaveSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class WaveVoice : public juce::SynthesiserVoice
{
public:
    WaveVoice (const bw::WavetableBank& b, const SynthParams& p, bw::ModulationContext& m, int id) : oscillators { bw::TableOscillator (b), bw::TableOscillator (b) }, bank (b), params (p), modulation (m), voiceId (id) {}

    void prepare (double sampleRate, int blockSize);
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<WaveSound*> (s) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int value) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int start, int num) override;

private:
    float clampCutoff (float hz) const { return juce::jmin (hz, (float) getSampleRate () * 0.45f); }

    void updateTargets (bool reset);
    std::array<bw::TableOscillator, 2> oscillators;
    const bw::WavetableBank& bank;
    const SynthParams& params;
    juce::ADSR adsr, env2;
    std::array<bw::VoiceFilter, 2> filters;
    struct Lane
    {
        juce::SmoothedValue<float> gain, offset;
        double phase = 0, frequency = 440;
        float cachedPitch = 10000, cachedPan = 10000, leftGain = 1, rightGain = 1;
        int mip = 0;
    };
    struct OscState
    {
        juce::SmoothedValue<float> position, amplitude, pitch, fine, pan, detune, spread, enabled;
        std::array<Lane, 8> lanes;
    };
    std::array<OscState, 2> sources;
    bw::ModulationContext& modulation;
    int voiceId;
    std::array<double, 2> notePhase {};
    struct CurveTransition
    {
        bw::LfoCurve current, target;
        juce::SmoothedValue<float> fade;
        uint64_t generation = 0;
        bool switching = false;
    };
    std::array<CurveTransition, 2> curveTransitions;
    void beginCurveTransition (size_t index);

    std::array<juce::SmoothedValue<float>, 4> macroValues;
    struct RouteState
    {
        struct Assignment { int source = 0, destination = 0; bool bipolar = true; bool operator== (const Assignment&) const = default; };
        Assignment current, target, requested;
        juce::SmoothedValue<float> fade, amount;
        bool switching = false;
    };
    std::array<RouteState, 16> routeStates;
    void updateModulation (bool reset);
    juce::SmoothedValue<float> cutoff, resonance, subAmplitude, subPitch, noiseAmplitude, bend, subEnabled, noiseEnabled, cutoff2, resonance2, filter2Enabled;
    float cachedCutoff = -1, cachedResonance = -1, cachedCutoff2 = -1, cachedResonance2 = -1;
    double fundamental = 440, subPhase = 0, subFrequency = 220;
    float cachedSubPitch = 10000;
    uint32_t noiseState = 0x12345678u;
    float velocity = 0;
};

class WaveProcessor : public juce::AudioProcessor
{
public:
    WaveProcessor ();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources () override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor () override;
    bool hasEditor () const override { return true; }

    const juce::String getName () const override { return JucePlugin_Name; }
    bool acceptsMidi () const override { return true; }
    bool producesMidi () const override { return false; }
    double getTailLengthSeconds () const override;
    int getNumPrograms () override { return 1; }
    int getCurrentProgram () override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    const bw::WavetableBank wavetableBank;
    int selectedTable (bool oscillatorB = false) const
    {
        return juce::jlimit (0, bw::WavetableBank::kCount - 1,
                            juce::roundToInt (apvts.getRawParameterValue (oscillatorB ? "wtTableB" : "wtTable")->load ()));
    }
    const bw::Wavetable& currentWavetable (bool oscillatorB = false) const { return wavetableBank[selectedTable (oscillatorB)]; }
    juce::MidiKeyboardState keyboardState;
    // Audio publishes block peaks; the UI owns display ballistics.
    std::array<std::atomic<float>, 2> outputPeak {};
    std::array<std::atomic<float>, 2> lfoDisplayPhase {};
    std::atomic<unsigned> curveRevision { 0 };
    bw::LfoCurve getLfoCurve (int index) const;
    void setLfoCurve (int index, const bw::LfoCurve& curve);
    juce::String getMacroName (int index) const;
    void setMacroName (int index, const juce::String& name);

    // User wavetables: decode/build on the calling (non-audio) thread, then publish immutably.
    juce::String importWavetable (int oscillator, const juce::File& file);
    void setUserTable (int oscillator, std::shared_ptr<const bw::Wavetable> table, const juce::String& name);
    std::shared_ptr<const bw::Wavetable> userTable (int oscillator) const;
    juce::String userTableName (int oscillator) const;
    bool usingUserTable (int oscillator) const { return params.oscillators[(size_t) oscillator].user->load () >= .5f && userTable (oscillator) != nullptr; }
    std::atomic<unsigned> tableRevision { 0 };

    // Preset files hold the same XML as host state; newer format versions are refused.
    static constexpr int kStateVersion = 1;
    // Factory presets reset every parameter, LFO curve, macro name and user table.
    void loadFactoryPreset (int index);
    // Index of the factory preset the current state equals, or -1.
    int matchingFactoryPreset () const;
    static juce::File presetDirectory ();
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);


private:
    std::unique_ptr<juce::XmlElement> createStateXml ();
    void restoreState (const juce::XmlElement& xml);
    mutable std::mutex extraStateMutex;
    std::array<bw::UserTableSlot, 2> userSlots;
    std::vector<std::vector<float>> factoryNormalised; // per preset, in getParameters() order
    std::array<juce::String, 2> userTableNames;
    bw::Effects effects;
    bw::EffectParams effectParams {};
    bw::CurveExchange::Curves savedCurves { bw::LfoCurve::factory (0), bw::LfoCurve::factory (0) };
    std::array<juce::String, 4> macroNames { "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    bw::CurveExchange curveExchange;
    bw::ModulationContext modulation;
    double processingRate = 44100;
    std::array<double, 2> freePhases {};
    SynthParams params;
    juce::Synthesiser synth;
    juce::SmoothedValue<float> gain;
    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* internalBpm = nullptr;
};
