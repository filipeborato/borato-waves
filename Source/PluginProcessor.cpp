#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout ()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<P> (juce::ParameterID { "wtPos", 1 }, "WT Pos", R (0, 1), 0.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "level", 1 }, "Level", R (0, 1), 0.75f));
    l.add (std::make_unique<P> (juce::ParameterID { "cutoff", 1 }, "Cutoff", R (20, 20000, 0, 0.25f), 8000.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "reso", 1 }, "Reso", R (0.5f, 10, 0, 0.4f), 0.707f));
    l.add (std::make_unique<P> (juce::ParameterID { "attack", 1 }, "Attack", R (0.001f, 5, 0, 0.3f), 0.005f));
    l.add (std::make_unique<P> (juce::ParameterID { "decay", 1 }, "Decay", R (0.001f, 5, 0, 0.3f), 0.3f));
    l.add (std::make_unique<P> (juce::ParameterID { "sustain", 1 }, "Sustain", R (0, 1), 0.7f));
    l.add (std::make_unique<P> (juce::ParameterID { "release", 1 }, "Release", R (0.001f, 8, 0, 0.3f), 0.3f));
    l.add (std::make_unique<P> (juce::ParameterID { "gain", 1 }, "Gain", R (-36, 6), -6.0f));
    // Append to preserve existing parameter IDs/order and legacy Basic Shapes patches.
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "wtTable", 1 },
                                                       "Wavetable", bw::WavetableBank::names (), 0));
    auto continuous = [&] (const char* id, const char* name, float lo, float hi, float initial)
    {
        l.add (std::make_unique<P> (juce::ParameterID { id, 1 }, name, R (lo, hi), initial));
    };
    auto toggle = [&] (const char* id, const char* name, bool initial)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, initial));
    };
    toggle ("oscAOn", "Osc A On", true);
    continuous ("oscAPitch", "Osc A Semitones", -24, 24, 0);
    continuous ("oscAFine", "Osc A Fine", -100, 100, 0);
    continuous ("oscAPan", "Osc A Pan", -1, 1, 0);
    toggle ("oscBOn", "Osc B On", false);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "wtTableB", 1 },
                                                       "Osc B Wavetable", bw::WavetableBank::names (), 0));
    continuous ("wtPosB", "Osc B WT Pos", 0, 1, 0);
    continuous ("levelB", "Osc B Level", 0, 1, 0.5f);
    continuous ("oscBPitch", "Osc B Semitones", -24, 24, 0);
    continuous ("oscBFine", "Osc B Fine", -100, 100, 0);
    continuous ("oscBPan", "Osc B Pan", -1, 1, 0);
    toggle ("subOn", "Sub On", false);
    continuous ("subLevel", "Sub Level", 0, 1, 0.25f);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "subOctave", 1 }, "Sub Octave", -2, 0, -1));
    toggle ("noiseOn", "Noise On", false);
    continuous ("noiseLevel", "Noise Level", 0, 1, 0.1f);
    for (auto osc : { "oscA", "oscB" })
    {
        const juce::String prefix (osc);
        l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { prefix + "Unison", 1 }, prefix + " Unison", 1, 8, 1));
        continuous ((prefix + "Detune").toRawUTF8 (), (prefix + " Detune").toRawUTF8 (), 0, 50, 0);
        continuous ((prefix + "Spread").toRawUTF8 (), (prefix + " Spread").toRawUTF8 (), 0, 1, 0);
        continuous ((prefix + "Phase").toRawUTF8 (), (prefix + " Initial Phase").toRawUTF8 (), 0, 360, 0);
        continuous ((prefix + "Random").toRawUTF8 (), (prefix + " Phase Random").toRawUTF8 (), 0, 1, 0);
    }
    auto choice = [&] (const juce::String& id, const juce::StringArray& names, int initial = 0)
    { l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, id, names, initial)); };
    for (int i = 1; i <= 2; ++i)
    {
        const auto prefix = "lfo" + juce::String (i);
        l.add (std::make_unique<P> (juce::ParameterID { prefix+"Rate", 1 }, prefix+" Rate", R (0.01f, 40, 0, .35f), 1.0f));
        toggle ((prefix+"Sync").toRawUTF8 (), (prefix + " Sync").toRawUTF8 (), false);
        choice (prefix+"Division", bw::divisionNames (), 12);
        choice (prefix+"Mode", { "Note", "Free", "Transport" });
    }
    continuous ("internalBpm", "Internal BPM", 20, 300, 120);
    for (int i = 1; i <= 4; ++i) continuous (("macro"+juce::String (i)).toRawUTF8 (), ("Macro "+juce::String (i)).toRawUTF8 (), 0, 1, 0);
    for (int i = 1; i <= 16; ++i)
    {
        const auto prefix = "mod"+juce::String (i);
        toggle ((prefix+"Enabled").toRawUTF8 (), (prefix + " Enabled").toRawUTF8 (), false);
        choice (prefix+"Source", bw::sourceNames ());
        choice (prefix+"Destination", bw::destinationNames ());
        continuous ((prefix+"Amount").toRawUTF8 (), (prefix + " Depth").toRawUTF8 (), -1, 1, 0);
        toggle ((prefix+"Bipolar").toRawUTF8 (), (prefix + " Bipolar").toRawUTF8 (), true);
    }
    // Filters, routing, user tables and effects: appended so existing IDs and order stay stable.
    choice ("filterType", bw::VoiceFilter::modeNames ());
    toggle ("filter2On", "Filter 2 On", false);
    choice ("filter2Type", bw::VoiceFilter::modeNames ());
    l.add (std::make_unique<P> (juce::ParameterID { "cutoff2", 1 }, "Cutoff 2", R (20, 20000, 0, 0.25f), 1000.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "reso2", 1 }, "Reso 2", R (0.5f, 10, 0, 0.4f), 0.707f));
    choice ("filterRouting", { "Serial", "Parallel" });
    for (auto id : { "oscARoute", "oscBRoute", "subRoute", "noiseRoute" }) choice (id, routeNames ());
    toggle ("oscAUser", "Osc A User Table", false);
    toggle ("oscBUser", "Osc B User Table", false);
    toggle ("distOn", "Distortion On", false);
    choice ("distMode", bw::distortionModes ());
    continuous ("distDrive", "Distortion Drive", 0, 36, 12);
    continuous ("distMix", "Distortion Mix", 0, 1, 1);
    toggle ("delayOn", "Delay On", false);
    l.add (std::make_unique<P> (juce::ParameterID { "delayTime", 1 }, "Delay Time", R (1, 2000, 0, 0.4f), 375.0f));
    toggle ("delaySync", "Delay Sync", false);
    choice ("delayDivision", bw::divisionNames (), 16);
    continuous ("delayFeedback", "Delay Feedback", 0, .95f, .35f);
    toggle ("delayPingPong", "Delay Ping-Pong", false);
    continuous ("delayMix", "Delay Mix", 0, 1, .25f);
    toggle ("reverbOn", "Reverb On", false);
    continuous ("reverbSize", "Reverb Size", 0, 1, .6f);
    continuous ("reverbDamping", "Reverb Damping", 0, 1, .4f);
    continuous ("reverbWidth", "Reverb Width", 0, 1, 1);
    continuous ("reverbMix", "Reverb Mix", 0, 1, .25f);
    l.add (std::make_unique<P> (juce::ParameterID { "env2Attack", 1 }, "Env 2 Attack", R (0.001f, 5, 0, 0.3f), 0.001f));
    l.add (std::make_unique<P> (juce::ParameterID { "env2Decay", 1 }, "Env 2 Decay", R (0.001f, 5, 0, 0.3f), 0.4f));
    l.add (std::make_unique<P> (juce::ParameterID { "env2Sustain", 1 }, "Env 2 Sustain", R (0, 1), 0.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "env2Release", 1 }, "Env 2 Release", R (0.001f, 8, 0, 0.3f), 0.3f));
    choice ("voiceMode", { "Poly", "Mono", "Legato" });
    for (auto osc : { "oscA", "oscB" })
    {
        const juce::String prefix (osc), name = prefix == "oscA" ? "Osc A" : "Osc B";
        toggle ((prefix + "WarpOn").toRawUTF8 (), (name + " Warp On").toRawUTF8 (), false);
        continuous ((prefix + "Warp").toRawUTF8 (), (name + " Warp").toRawUTF8 (), -1, 1, 0);
    }
    l.add (std::make_unique<P> (juce::ParameterID { "glide", 1 }, "Glide", R (0, 2, 0, 0.4f), 0.0f));
    continuous ("fmDepth", "FM Depth", 0, 1, 0);
    toggle ("sampleOn", "Sample On", false);
    continuous ("sampleLevel", "Sample Level", 0, 1, .5f);
    continuous ("samplePitch", "Sample Semitones", -24, 24, 0);
    continuous ("sampleStart", "Sample Start", 0, 1, 0);
    continuous ("sampleEnd", "Sample End", 0, 1, 1);
    toggle ("sampleLoop", "Sample Loop", false);
    toggle ("chorusOn", "Chorus On", false);
    l.add (std::make_unique<P> (juce::ParameterID { "chorusRate", 1 }, "Chorus Rate", R (0.05f, 5, 0, 0.5f), 0.6f));
    continuous ("chorusDepth", "Chorus Depth", 0, 1, .5f);
    continuous ("chorusMix", "Chorus Mix", 0, 1, .5f);
    toggle ("eqOn", "EQ On", false);
    continuous ("eqLow", "EQ Low", -18, 18, 0);
    l.add (std::make_unique<P> (juce::ParameterID { "eqFreq", 1 }, "EQ Frequency", R (40, 16000, 0, 0.25f), 1000.0f));
    continuous ("eqGain", "EQ Gain", -18, 18, 0);
    l.add (std::make_unique<P> (juce::ParameterID { "eqQ", 1 }, "EQ Q", R (0.3f, 8, 0, 0.4f), 1.0f));
    continuous ("eqHigh", "EQ High", -18, 18, 0);
    toggle ("compOn", "Compressor On", false);
    continuous ("compThreshold", "Comp Threshold", -48, 0, -18);
    l.add (std::make_unique<P> (juce::ParameterID { "compRatio", 1 }, "Comp Ratio", R (1, 20, 0, 0.4f), 4.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "compAttack", 1 }, "Comp Attack", R (0.1f, 100, 0, 0.3f), 10.0f));
    l.add (std::make_unique<P> (juce::ParameterID { "compRelease", 1 }, "Comp Release", R (10, 1000, 0, 0.4f), 150.0f));
    continuous ("compMakeup", "Comp Makeup", 0, 24, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "sampleRoot", 1 }, "Sample Root Note", 0, 127, bw::kSampleRootNote));
    continuous ("samplePan", "Sample Pan", -1, 1, 0);
    choice ("sampleRoute", routeNames ());
    return l;
}

//==============================================================================
void WaveVoice::prepare (double sampleRate, int blockSize)
{
    adsr.setSampleRate (sampleRate);
    adsr.reset ();
    env2.setSampleRate (sampleRate);
    env2.reset ();
    clearCurrentNote ();
    juce::ignoreUnused (blockSize);
    residualLength = swapLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));
    residualLeft = 0; lastOut = residual = {};
    for (auto& f : filters) f.reset ();
    for (auto* smoother : { &cutoff, &resonance, &subAmplitude, &subPitch, &noiseAmplitude, &bend, &subEnabled, &noiseEnabled, &cutoff2, &resonance2, &filter2Enabled, &fmDepth,
                           &sampleAmplitude, &sampleEnabled, &samplePitch, &samplePan })
        smoother->reset (sampleRate, 0.02);
    playingSample = fadingSample = nullptr; swapLeft = 0;
    cachedSampleRoot = -1; cachedSampleSemitones = cachedSamplePan = 10000; sampleLeftGain = sampleRightGain = 1;
    glideOffset.setCurrentAndTargetValue (0); pendingGlide = 0;
    for (size_t i = 0; i < sources.size (); ++i)
    {
        auto& source = sources[i];
        for (auto* smoother : { &source.position, &source.amplitude, &source.pitch, &source.fine, &source.pan, &source.detune, &source.spread, &source.enabled, &source.warp, &source.warpEnabled })
            smoother->reset (sampleRate, 0.02);
        for (auto& lane : source.lanes) { lane.gain.reset (sampleRate, .02); lane.offset.reset (sampleRate, .02); }
        oscillators[i].prepare (sampleRate);
    }
    for (auto& value : macroValues) value.reset (sampleRate, .02);
    for (auto& curve : curveTransitions) curve.fade.reset (sampleRate, .02);
    for (auto& route : routeStates) { route.fade.reset (sampleRate, .02); route.amount.reset (sampleRate, .02); }
}

void WaveVoice::updateTargets (bool reset)
{
    auto set = [reset] (juce::SmoothedValue<float>& value, float target)
    {
        if (reset) value.setCurrentAndTargetValue (target);
        else value.setTargetValue (target);
    };
    set (cutoff, clampCutoff (params.cutoff->load ()));
    set (resonance, params.reso->load ());
    set (cutoff2, clampCutoff (params.cutoff2->load ()));
    set (resonance2, params.reso2->load ());
    set (filter2Enabled, params.filter2On->load () >= .5f ? 1.0f : 0.0f);
    set (fmDepth, params.fmDepth->load ());
    set (sampleAmplitude, params.sampleLevel->load ());
    set (sampleEnabled, params.sampleOn->load () >= .5f ? 1.0f : 0.0f);
    set (samplePitch, params.samplePitch->load ());
    set (samplePan, params.samplePan->load ());
    filters[0].setMode (juce::roundToInt (params.filterType->load ()));
    filters[1].setMode (juce::roundToInt (params.filter2Type->load ()));
    for (size_t i = 0; i < sources.size (); ++i)
    {
        const auto& p = params.oscillators[i];
        auto& source = sources[i];
        set (source.position, p.position->load ());
        set (source.amplitude, p.level->load ());
        set (source.enabled, p.on->load () >= .5f ? 1.0f : 0.0f);
        set (source.pitch, p.pitch->load ());
        set (source.fine, p.fine->load () * 0.01f);
        set (source.pan, p.pan->load ());
        set (source.detune, p.detune->load ());
        set (source.spread, p.spread->load ());
        set (source.warp, p.warp->load ());
        set (source.warpEnabled, p.warpOn->load () >= .5f ? 1.0f : 0.0f);
        const int count = juce::jlimit (1, 8, juce::roundToInt (p.unison->load ()));
        for (int n = 0; n < 8; ++n)
        {
            auto& lane = source.lanes[(size_t)n];
            set (lane.gain, n < count ? 1.0f / count : 0);
            set (lane.offset, n < count && count > 1 ? -1.0f + 2.0f*n/(count-1) : 0);
            if (reset)
            {
                noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                lane.phase = p.phase->load ()/360.0 + (n < count ? (double)n/count : 0) + p.random->load () * (double)noiseState/4294967296.0;
                lane.phase -= std::floor (lane.phase);
                lane.cachedPitch = lane.cachedPan = 10000;
            }
        }
        const bw::Wavetable* table = p.user->load () >= .5f ? params.userTables[i] : nullptr;
        if (table == nullptr) table = &bank[juce::roundToInt (p.table->load ())];
        if (reset) oscillators[i].reset (table);
        else oscillators[i].setTable (table);
    }
    set (subAmplitude, params.subLevel->load ());
    set (subEnabled, params.subOn->load () >= .5f ? 1.0f : 0.0f);
    set (subPitch, params.subOctave->load () * 12.0f);
    set (noiseAmplitude, params.noiseLevel->load ());
    set (noiseEnabled, params.noiseOn->load () >= .5f ? 1.0f : 0.0f);
}

void WaveVoice::pitchWheelMoved (int value)
{
    const int delta = juce::jlimit (0, 16383, value) - 8192;
    bend.setTargetValue (2.0f * (float) delta / (delta >= 0 ? 8191.0f : 8192.0f));
}

void WaveVoice::startNote (int note, float vel, juce::SynthesiserSound*, int wheel)
{
    if (getSampleRate () <= 0.0)
    {
        clearCurrentNote ();
        return;
    }
    glideTo (note, pendingGlide);
    pendingGlide = 0;
    velocity = vel;
    released = false;
    notePhase = {};
    modulation.latestVoice.store (voiceId, std::memory_order_relaxed);
    cachedSubPitch = 10000;
    subPhase = 0;
    // Each voice owns its PRNG. Advance it rather than reseeding every note.
    noiseState ^= (uint32_t) (note + 1) * 2654435761u;
    if (noiseState == 0) noiseState = 1;
    pitchWheelMoved (wheel);
    bend.setCurrentAndTargetValue (bend.getTargetValue ());
    updateTargets (true);
    updateModulation (true);
    playingSample = params.sample; fadingSample = nullptr; swapLeft = 0;
    if (playingSample != nullptr) player.reset (playingSample->region (params.sampleStart->load (), params.sampleEnd->load ()));
    for (auto& f : filters) f.reset ();
    cachedCutoff = cutoff.getCurrentValue (); cachedResonance = resonance.getCurrentValue ();
    cachedCutoff2 = cutoff2.getCurrentValue (); cachedResonance2 = resonance2.getCurrentValue ();
    filters[0].set (cachedCutoff, cachedResonance, getSampleRate ());
    filters[1].set (cachedCutoff2, cachedResonance2, getSampleRate ());
    adsr.setParameters ({ params.attack->load (), params.decay->load (), params.sustain->load (), params.release->load () });
    adsr.reset ();
    adsr.noteOn ();
    env2.setParameters ({ params.env2Attack->load (), params.env2Decay->load (), params.env2Sustain->load (), params.env2Release->load () });
    env2.reset ();
    env2.noteOn ();
}

void WaveVoice::glideTo (int note, float seconds)
{
    const float from = (float) noteNumber + glideOffset.getCurrentValue ();
    noteNumber = note;
    fundamental = juce::MidiMessage::getMidiNoteInHertz (note);
    glideOffset.reset (getSampleRate (), juce::jmax (0.0f, seconds));
    glideOffset.setCurrentAndTargetValue (seconds > 0 ? from - (float) note : 0.0f);
    glideOffset.setTargetValue (0);
    // The fundamental moved, so lane, sub and sample frequencies must be recomputed.
    cachedSubPitch = cachedSampleSemitones = 10000;
    for (auto& source : sources) for (auto& lane : source.lanes) lane.cachedPitch = 10000;
}

void WaveVoice::legato (int note, float glideSeconds)
{
    glideTo (note, glideSeconds);
    modulation.latestVoice.store (voiceId, std::memory_order_relaxed);
}

void WaveVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff) { adsr.noteOff (); env2.noteOff (); released = true; }
    else
    {
        const float fade = (float) residualLeft / (float) residualLength;
        for (size_t ch = 0; ch < 2; ++ch) residual[ch] = lastOut[ch] + residual[ch] * fade;
        residualLeft = residualLength; lastOut = {};
        playingSample = fadingSample = nullptr; swapLeft = 0;
        adsr.reset ();
        env2.reset ();
        clearCurrentNote ();
    }
}

void WaveVoice::beginCurveTransition (size_t index)
{
    auto& transition = curveTransitions[index];
    transition.target = modulation.curves[index];
    transition.generation = modulation.curveGeneration;
    transition.fade.setCurrentAndTargetValue (0);
    transition.fade.setTargetValue (1);
    transition.switching = true;
}

void WaveVoice::updateModulation (bool reset)
{
    for (size_t i = 0; i < curveTransitions.size (); ++i)
    {
        auto& transition = curveTransitions[i];
        if (reset)
        {
            transition.current = transition.target = modulation.curves[i];
            transition.generation = modulation.curveGeneration;
            transition.fade.setCurrentAndTargetValue (1);
            transition.switching = false;
        }
        else if (! transition.switching && transition.generation != modulation.curveGeneration)
            beginCurveTransition (i);
    }
    for (size_t i = 0; i < macroValues.size (); ++i)
        if (reset) macroValues[i].setCurrentAndTargetValue (modulation.macros[i]->load ());
        else macroValues[i].setTargetValue (modulation.macros[i]->load ());
    for (size_t i = 0; i < routeStates.size (); ++i)
    {
        auto& state = routeStates[i]; const auto& p = modulation.routes[i];
        state.requested = { juce::jlimit (0, bw::kSources - 1, juce::roundToInt (p.source->load ())), juce::jlimit (0, bw::kDestinations - 1, juce::roundToInt (p.destination->load ())), p.bipolar->load () >= .5f };
        const float amount = p.enabled->load () >= .5f ? p.amount->load () : 0;
        if (reset) { state.current = state.target = state.requested; state.amount.setCurrentAndTargetValue (amount); state.fade.setCurrentAndTargetValue (1); state.switching = false; }
        else
        {
            state.amount.setTargetValue (amount);
            if (! state.switching && ! (state.current == state.requested)) { state.target = state.requested; state.fade.setCurrentAndTargetValue (0); state.fade.setTargetValue (1); state.switching = true; }
        }
    }
}

void WaveVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    for (int i = 0; i < num && residualLeft > 0; ++i, --residualLeft)
        for (int ch = 0; ch < juce::jmin (2, out.getNumChannels ()); ++ch)
            out.addSample (ch, start+i, residual[(size_t) ch] * (float) residualLeft / (float) residualLength);
    if (! isVoiceActive ()) return;
    if (! released)
    {
        adsr.setParameters ({ params.attack->load (), params.decay->load (), params.sustain->load (), params.release->load () });
        env2.setParameters ({ params.env2Attack->load (), params.env2Decay->load (), params.env2Sustain->load (), params.env2Release->load () });
    }
    updateTargets (false);
    updateModulation (false);
    const double sampleRate = getSampleRate ();
    const std::array<bool, 2> noteModes { modulation.lfo[0].mode->load () < .5f, modulation.lfo[1].mode->load () < .5f };
    const bool displayVoice = modulation.latestVoice.load (std::memory_order_relaxed) == voiceId;
    auto routeOf = [] (std::atomic<float>* r) { return (size_t) juce::jlimit (0, 2, juce::roundToInt (r->load ())); };
    const std::array<size_t, 2> sourceRoutes { routeOf (params.oscillators[0].route), routeOf (params.oscillators[1].route) };
    const size_t subRoute = routeOf (params.subRoute), noiseRoute = routeOf (params.noiseRoute), sampleRoute = routeOf (params.sampleRoute);
    const int sampleRoot = juce::jlimit (0, 127, juce::roundToInt (params.sampleRoot->load ()));
    const bool serial = params.filterRouting->load () < .5f, sampleLoop = params.sampleLoop->load () >= .5f;
    const float regionStart = params.sampleStart->load (), regionEnd = params.sampleEnd->load ();
    if (params.sample != playingSample && swapLeft == 0) // a second swap waits for the first fade
    {
        fadingSample = playingSample; fadingPlayer = player;
        swapLength = juce::jmax (1, juce::roundToInt (sampleRate * params.sampleSwapSeconds));
        swapLeft = swapLength;
        playingSample = params.sample;
        if (playingSample != nullptr) player.reset (playingSample->region (regionStart, regionEnd));
    }
    for (int i = 0; i < num; ++i)
    {
        const float envelopeValue = adsr.getNextSample ();
        std::array<float, bw::kSources> values { 0, 0, 0, 0, 0, 0, 0, envelopeValue, velocity, env2.getNextSample () };
        for (size_t l = 0; l < 2; ++l)
        {
            const bool noteMode = noteModes[l];
            double phase = noteMode ? notePhase[l] : modulation.phase[l] + (start+i)*modulation.increment[l];
            phase -= std::floor (phase);
            auto& transition = curveTransitions[l];
            const float previous = transition.current.evaluate (phase);
            values[l+1] = previous;
            if (transition.switching)
            {
                const float mix = transition.fade.getNextValue ();
                values[l+1] += mix * (transition.target.evaluate (phase) - previous);
                if (! transition.fade.isSmoothing ())
                {
                    transition.current = transition.target;
                    transition.switching = false;
                    if (transition.generation != modulation.curveGeneration)
                        beginCurveTransition (l);
                }
            }
            notePhase[l] += modulation.increment[l]; notePhase[l] -= std::floor (notePhase[l]);
            if (displayVoice) (*modulation.display)[l].store ((float)phase, std::memory_order_relaxed);
        }
        for (size_t m = 0; m < 4; ++m) values[m+3] = macroValues[m].getNextValue ();
        std::array<float, bw::kDestinations> mod {};
        for (auto& route : routeStates)
        {
            const float depth = route.amount.getNextValue ();
            const float mix = route.fade.getNextValue ();
            auto add = [&] (const RouteState::Assignment& a, float scale)
            { if (a.source != 0 && scale != 0) mod[(size_t)a.destination] += scale * (a.bipolar ? values[(size_t)a.source]*2-1 : values[(size_t)a.source]); };
            add (route.current, depth * (route.switching ? 1-mix : 1));
            if (route.switching)
            {
                add (route.target, depth*mix);
                if (! route.fade.isSmoothing ())
                {
                    route.current = route.target; route.switching = false;
                    if (! (route.current == route.requested)) { route.target = route.requested; route.fade.setCurrentAndTargetValue (0); route.fade.setTargetValue (1); route.switching = true; }
                }
            }
        }
        auto effective = [&] (int destination, float base)
        {
            if (mod[(size_t)destination] == 0) return base;
            const auto& range = modulation.ranges[(size_t)destination];
            return range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (juce::jlimit (range.start, range.end, base))+mod[(size_t)destination]));
        };
        const float hz = clampCutoff (effective (12, cutoff.getNextValue ())), q = effective (13, resonance.getNextValue ());
        if (hz != cachedCutoff || q != cachedResonance) { cachedCutoff = hz; cachedResonance = q; filters[0].set (hz, q, sampleRate); }
        const float hz2 = clampCutoff (effective (16, cutoff2.getNextValue ())), q2 = effective (17, resonance2.getNextValue ());
        if (hz2 != cachedCutoff2 || q2 != cachedResonance2) { cachedCutoff2 = hz2; cachedResonance2 = q2; filters[1].set (hz2, q2, sampleRate); }
        const float pitchBend = bend.getNextValue () + glideOffset.getNextValue ();
        // Buses: Filter 1, Filter 2, Direct.
        std::array<float, 3> busL {}, busR {};
        // B renders first: its raw signal (before level and on/off) phase-modulates A, so B can be a silent modulator.
        const float fmCycles = effective (bw::kFmDestination, fmDepth.getNextValue ());
        float modulator = 0;
        double modulatorHz = 0;
        for (const size_t index : { (size_t) 1, (size_t) 0 })
        {
            auto& source = sources[index]; const int d = (int)index*6;
            const float basePitch = source.pitch.getNextValue ();
            const float semitones = effective (d+3, basePitch) + source.fine.getNextValue () + pitchBend;
            const float position = effective (d, source.position.getNextValue ());
            const float amplitude = effective (d+1, source.amplitude.getNextValue ()) * source.enabled.getNextValue ();
            const float pan = effective (d+2, source.pan.getNextValue ());
            const float detune = effective (d+4, source.detune.getNextValue ());
            const float spread = effective (d+5, source.spread.getNextValue ());
            const float warp = effective (bw::kFirstWarpDestination+(int)index, source.warp.getNextValue ()) * source.warpEnabled.getNextValue ();
            // Bending the phase by w raises the top harmonic by up to (1 + |w|), so read a narrower band.
            const float warpWidth = 1 + std::abs (warp);
            // Phase modulation by B gives carrier harmonic k sidebands out to about k*I + 4 modulator multiples
            // (Carson's rule plus a -40 dB margin). Cap the index I so the fundamental's sidebands stay below
            // Nyquist, then read only the harmonics h with h*(f*w + I*fB) <= Nyquist - 4*fB.
            double fmShift = 0, fmSpread = 0, bandRate = sampleRate;
            if (index == 0 && fmCycles > 0 && modulatorHz > 0)
            {
                if (semitones != source.cachedSemitones) { source.cachedSemitones = semitones; source.hz = fundamental * std::exp2 (semitones / 12.0); }
                const double twoPi = juce::MathConstants<double>::twoPi;
                const double fmIndex = std::min (twoPi * fmCycles, (0.5 * sampleRate - source.hz * warpWidth) / modulatorHz - 4);
                if (fmIndex > 0) { fmShift = fmIndex / twoPi; fmSpread = fmIndex * modulatorHz; bandRate = sampleRate - 8 * modulatorHz; }
            }
            float left = 0, right = 0;
            for (auto& lane : source.lanes)
            {
                const float laneGain = lane.gain.getNextValue (), offset = lane.offset.getNextValue ();
                if (laneGain == 0) continue;
                const float pitch = semitones + offset*detune*.01f;
                if (pitch != lane.cachedPitch) { lane.cachedPitch = pitch; lane.frequency = fundamental * std::exp2 (pitch/12.0); lane.cachedWidth = 0; }
                if (warpWidth != lane.cachedWidth || fmSpread != lane.cachedSpread)
                {
                    lane.cachedWidth = warpWidth; lane.cachedSpread = fmSpread;
                    lane.mip = bw::Wavetable::levelFor (lane.frequency * warpWidth + fmSpread, bandRate);
                }
                const float lanePan = juce::jlimit (-1.0f, 1.0f, pan+offset*spread);
                if (lanePan != lane.cachedPan) { lane.cachedPan = lanePan; const float angle = (lanePan+1)*juce::MathConstants<float>::pi*.25f; lane.leftGain = std::cos (angle)*juce::MathConstants<float>::sqrt2; lane.rightGain = std::sin (angle)*juce::MathConstants<float>::sqrt2; }
                const float nyquistGain = (float)juce::jlimit (0.0, 1.0, (0.5-lane.frequency/sampleRate)/.025);
                double phase = lane.phase;
                // Periodic, monotonic phase bend (slope 1 +/- w, a parabolic sine shape): ends and period stay fixed,
                // so tuning and continuity hold. Polynomial rather than std::sin: this runs per lane per sample.
                if (warp != 0) { const double u = 2*phase - 1; phase -= .5*warp * u * (1 - std::abs (u)); phase -= std::floor (phase); }
                if (fmShift != 0) { phase += fmShift * modulator; phase -= std::floor (phase); }
                const float raw = oscillators[index].readCurrent (phase, position, lane.mip)*laneGain*nyquistGain, sample = raw*amplitude;
                if (index == 1) { modulator += raw; modulatorHz = std::max (modulatorHz, lane.frequency); }
                lane.phase += lane.frequency/sampleRate; lane.phase -= std::floor (lane.phase);
                left += sample*lane.leftGain; right += sample*lane.rightGain;
            }
            oscillators[index].advance ();
            busL[sourceRoutes[index]] += left; busR[sourceRoutes[index]] += right;
        }
        const float subSemitones = subPitch.getNextValue ()+pitchBend;
        if (subSemitones != cachedSubPitch) { cachedSubPitch = subSemitones; subFrequency = fundamental*std::exp2 (subSemitones/12.0); }
        const float subGain = (float)juce::jlimit (0.0, 1.0, (0.5-subFrequency/sampleRate)/.025);
        const float subLevel = effective (14, subAmplitude.getNextValue ()) * subEnabled.getNextValue ();
        const float sub = (float)std::sin (subPhase*juce::MathConstants<double>::twoPi)*subLevel*subGain;
        subPhase += subFrequency/sampleRate; subPhase -= std::floor (subPhase);
        noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
        const float noiseLevel = effective (15, noiseAmplitude.getNextValue ()) * noiseEnabled.getNextValue ();
        const float noise = ((float)(noiseState>>8)/8388608.0f-1)*noiseLevel;
        // The sample source routes to Filter 1, Filter 2 or Direct. While its level is zero it pauses rather than reading.
        const float sampleGain = sampleAmplitude.getNextValue () * sampleEnabled.getNextValue ();
        const float sampleSemitones = samplePitch.getNextValue () + pitchBend;
        const float currentSamplePan = juce::jlimit (-1.0f, 1.0f, samplePan.getNextValue ());
        if ((sampleGain != 0 || swapLeft > 0) && (playingSample != nullptr || fadingSample != nullptr))
        {
            if (sampleSemitones != cachedSampleSemitones || sampleRoot != cachedSampleRoot)
            {
                cachedSampleSemitones = sampleSemitones;
                cachedSampleRoot = sampleRoot;
                sampleRatio = fundamental / juce::MidiMessage::getMidiNoteInHertz (sampleRoot) * std::exp2 (sampleSemitones / 12.0) / sampleRate;
            }
            if (currentSamplePan != cachedSamplePan)
            {
                cachedSamplePan = currentSamplePan;
                const float angle = (currentSamplePan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                sampleLeftGain = std::cos (angle) * juce::MathConstants<float>::sqrt2;
                sampleRightGain = std::sin (angle) * juce::MathConstants<float>::sqrt2;
            }
            auto play = [&] (bw::SamplePlayer& head, const bw::Sample& source)
            { return head.nextStereo (source, source.region (regionStart, regionEnd), sampleLoop, sampleRatio * source.rate); };
            auto [sampledL, sampledR] = playingSample != nullptr ? play (player, *playingSample) : std::pair<float, float> { 0.0f, 0.0f };
            if (swapLeft > 0)
            {
                const float t = (float) swapLeft / (float) swapLength;
                const auto [fadeL, fadeR] = fadingSample != nullptr ? play (fadingPlayer, *fadingSample) : std::pair<float, float> { 0.0f, 0.0f };
                sampledL = sampledL * (1.0f - t) + fadeL * t;
                sampledR = sampledR * (1.0f - t) + fadeR * t;
                if (--swapLeft == 0) fadingSample = nullptr;
            }
            busL[sampleRoute] += sampledL * sampleGain * sampleLeftGain;
            busR[sampleRoute] += sampledR * sampleGain * sampleRightGain;
        }
        busL[subRoute] += sub; busR[subRoute] += sub;
        busL[noiseRoute] += noise; busR[noiseRoute] += noise;
        const float envelope = envelopeValue*velocity, filter2Mix = filter2Enabled.getNextValue ();
        // Serial: Filter 1 feeds Filter 2. Parallel: their outputs sum. A disabled Filter 2 passes through.
        auto voiceOut = [&] (int ch, float in1, float in2, float direct)
        {
            const float y1 = filters[0].process (ch, in1);
            const float x2 = serial ? y1 + in2 : in2;
            const float y2 = filter2Mix == 0 ? x2 : x2 + filter2Mix * (filters[1].process (ch, x2) - x2);
            return (serial ? y2 : y1 + y2) + direct;
        };
        if (out.getNumChannels () == 1) { lastOut[0] = voiceOut (0, .5f*(busL[0]+busR[0]), .5f*(busL[1]+busR[1]), .5f*(busL[2]+busR[2]))*envelope; out.addSample (0, start+i, lastOut[0]); }
        else if (out.getNumChannels () >= 2)
        {
            lastOut = { voiceOut (0, busL[0], busL[1], busL[2])*envelope, voiceOut (1, busR[0], busR[1], busR[2])*envelope };
            out.addSample (0, start+i, lastOut[0]); out.addSample (1, start+i, lastOut[1]);
        }
        if (! adsr.isActive ()) { playingSample = fadingSample = nullptr; swapLeft = 0; clearCurrentNote (); break; }
    }
}

//==============================================================================
WaveProcessor::WaveProcessor ()
    : AudioProcessor (BusesProperties ().withOutput ("Output", juce::AudioChannelSet::stereo (), true)),
      apvts (*this, nullptr, "STATE", createLayout ()), wavetableBank (bw::WavetableBank::shared ())
{
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    params = { p ("cutoff"), p ("reso"), p ("attack"), p ("decay"), p ("sustain"), p ("release"),
        { OscParams { p ("oscAOn"), p ("wtTable"), p ("wtPos"), p ("level"), p ("oscAPitch"), p ("oscAFine"), p ("oscAPan"), p ("oscAUnison"), p ("oscADetune"), p ("oscASpread"), p ("oscAPhase"), p ("oscARandom"), p ("oscAUser"), p ("oscARoute"), p ("oscAWarpOn"), p ("oscAWarp") },
          OscParams { p ("oscBOn"), p ("wtTableB"), p ("wtPosB"), p ("levelB"), p ("oscBPitch"), p ("oscBFine"), p ("oscBPan"), p ("oscBUnison"), p ("oscBDetune"), p ("oscBSpread"), p ("oscBPhase"), p ("oscBRandom"), p ("oscBUser"), p ("oscBRoute"), p ("oscBWarpOn"), p ("oscBWarp") } },
        p ("subOn"), p ("subLevel"), p ("subOctave"), p ("noiseOn"), p ("noiseLevel"),
        p ("filterType"), p ("filter2On"), p ("filter2Type"), p ("cutoff2"), p ("reso2"), p ("filterRouting"), p ("subRoute"), p ("noiseRoute"),
        p ("env2Attack"), p ("env2Decay"), p ("env2Sustain"), p ("env2Release"), p ("fmDepth"),
        p ("sampleOn"), p ("sampleLevel"), p ("samplePitch"), p ("sampleStart"), p ("sampleEnd"), p ("sampleLoop"), p ("sampleRoot"), p ("samplePan"), p ("sampleRoute") };
    effectParams = { p ("distOn"), p ("distMode"), p ("distDrive"), p ("distMix"),
        p ("delayOn"), p ("delayTime"), p ("delaySync"), p ("delayDivision"), p ("delayFeedback"), p ("delayPingPong"), p ("delayMix"),
        p ("reverbOn"), p ("reverbSize"), p ("reverbDamping"), p ("reverbWidth"), p ("reverbMix"),
        p ("chorusOn"), p ("chorusRate"), p ("chorusDepth"), p ("chorusMix"),
        p ("eqOn"), p ("eqLow"), p ("eqFreq"), p ("eqGain"), p ("eqQ"), p ("eqHigh"),
        p ("compOn"), p ("compThreshold"), p ("compRatio"), p ("compAttack"), p ("compRelease"), p ("compMakeup") };
    gainDb = p ("gain");
    internalBpm = p ("internalBpm");
    voiceMode = p ("voiceMode");
    glideTime = p ("glide");

    modulation.display = &lfoDisplayPhase;
    for (int i = 0; i < 2; ++i)
    {
        const auto prefix = "lfo"+juce::String (i+1);
        modulation.lfo[(size_t)i] = { p ((prefix+"Rate").toRawUTF8 ()), p ((prefix+"Sync").toRawUTF8 ()), p ((prefix+"Division").toRawUTF8 ()), p ((prefix+"Mode").toRawUTF8 ()) };
    }
    for (int i = 0; i < 4; ++i) modulation.macros[(size_t)i] = p (("macro"+juce::String (i+1)).toRawUTF8 ());
    for (int i = 0; i < 16; ++i)
    {
        const auto prefix = "mod"+juce::String (i+1);
        modulation.routes[(size_t)i] = { p ((prefix+"Enabled").toRawUTF8 ()), p ((prefix+"Source").toRawUTF8 ()), p ((prefix+"Destination").toRawUTF8 ()), p ((prefix+"Amount").toRawUTF8 ()), p ((prefix+"Bipolar").toRawUTF8 ()) };
    }
    for (int i = 0; i < bw::kDestinations; ++i)
        modulation.ranges[(size_t)i] = apvts.getParameter (bw::destinationIds ()[(size_t)i])->getNormalisableRange ();
    for (const auto& preset : bw::factoryPresets ())
    {
        std::map<juce::String, float> plain;
        for (const auto& [id, value] : preset.values)
        {
            jassert (apvts.getParameter (id) != nullptr);
            plain[id] = value;
        }
        for (size_t r = 0; r < preset.routes.size (); ++r)
        {
            const auto prefix = "mod"+juce::String ((int) r+1);
            const auto& route = preset.routes[r];
            plain[prefix+"Enabled"] = 1; plain[prefix+"Source"] = (float) route.source; plain[prefix+"Destination"] = (float) route.destination;
            plain[prefix+"Amount"] = route.amount; plain[prefix+"Bipolar"] = route.bipolar ? 1.0f : 0.0f;
        }
        auto& normalised = factoryNormalised.emplace_back ();
        for (auto* parameter : getParameters ())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            {
                const auto found = plain.find (ranged->paramID);
                normalised.push_back (found != plain.end () ? ranged->convertTo0to1 (found->second) : ranged->getDefaultValue ());
            }
    }
    for (auto* parameter : getParameters ())
        parameter->addListener (this);
    synth.setMinimumRenderingSubdivisionSize (1);
    synth.addSound (new WaveSound ());
    for (int i = 0; i < 16; ++i)
        synth.addVoice (new WaveVoice (wavetableBank, params, modulation, i));
    sampleLoadIdleEvent.signal ();
}

WaveProcessor::~WaveProcessor ()
{
    shuttingDown.store (true, std::memory_order_release);
    const uint64_t id = latestSampleLoadId.fetch_add (1, std::memory_order_acq_rel) + 1;
    cancelledUpToLoadId.store (id, std::memory_order_release);
    sampleLoadPool.removeAllJobs (true, 10000);
    for (auto* parameter : getParameters ())
        parameter->removeListener (this);
}

void WaveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    processingRate = sampleRate;
    freePhases = {};
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices (); ++i)
        static_cast<WaveVoice*> (synth.getVoice (i))->prepare (sampleRate, samplesPerBlock);
    effects.prepare (sampleRate, samplesPerBlock);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load ()));
}

bool WaveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet ();
    return out == juce::AudioChannelSet::stereo () || out == juce::AudioChannelSet::mono ();
}

void WaveProcessor::releaseResources ()
{
    for (auto& peak : outputPeak)
        peak.store (0.0f, std::memory_order_relaxed);
}

void WaveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear ();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples (), true);
    if (curveExchange.consume (modulation.curves))
        ++modulation.curveGeneration;
    for (size_t i = 0; i < userSlots.size (); ++i)
        params.userTables[i] = userSlots[i].acquire (buffer.getNumSamples ());
    params.sample = sampleSlot.acquire (buffer.getNumSamples ());
    params.sampleSwapSeconds = sampleSwapSeconds.load (std::memory_order_relaxed);
    double bpm = internalBpm->load ();
    double ppq = 0; bool hasPpq = false, playing = true;
    if (auto* hostPlayHead = getPlayHead ())
        if (const auto position = hostPlayHead->getPosition ())
        {
            if (const auto tempo = position->getBpm (); tempo && std::isfinite (*tempo) && *tempo > 0) bpm = *tempo;
            if (const auto beats = position->getPpqPosition (); beats && std::isfinite (*beats)) { ppq = *beats; hasPpq = true; playing = position->getIsPlaying (); }
        }
    for (size_t l = 0; l < 2; ++l)
    {
        const auto& settings = modulation.lfo[l];
        const double beats = bw::divisionBeats (juce::roundToInt (settings.division->load ()));
        const bool sync = settings.sync->load () >= .5f;
        const double hz = sync ? bpm/(60*beats) : settings.rate->load ();
        const bool transport = settings.mode->load () >= 1.5f && hasPpq;
        modulation.phase[l] = transport ? ppq * (sync ? 1/beats : hz*60/bpm) : freePhases[l];
        modulation.phase[l] -= std::floor (modulation.phase[l]);
        modulation.increment[l] = transport && ! playing ? 0 : hz/processingRate;
        freePhases[l] += buffer.getNumSamples ()*hz/processingRate;
        freePhases[l] -= std::floor (freePhases[l]);
    }
    synth.setMode (juce::roundToInt (voiceMode->load ()), glideTime->load ());
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples ());
    // Once no voice reads an older user table or sample, the replaced ones can be freed.
    auto allVoices = [this] (auto&& settled)
    {
        for (int v = 0; v < synth.getNumVoices (); ++v)
            if (! settled (*static_cast<WaveVoice*> (synth.getVoice (v)))) return false;
        return true;
    };
    for (size_t i = 0; i < userSlots.size (); ++i)
        if (allVoices ([i] (const WaveVoice& voice) { return voice.settled (i); })) userSlots[i].settle ();
    if (allVoices ([] (const WaveVoice& voice) { return voice.sampleSettled (); })) sampleSlot.settle ();
    // Global FX destinations take macro sources only; per-voice sources have no single value here.
    std::array<float, 3> fxOffsets {};
    for (const auto& route : modulation.routes)
    {
        const int source = juce::roundToInt (route.source->load ()), destination = juce::roundToInt (route.destination->load ());
        if (route.enabled->load () < .5f || source < 3 || source > 6 || destination < bw::kFirstGlobalDestination
            || destination >= bw::kFirstGlobalDestination + bw::kGlobalDestinations) continue;
        const float value = modulation.macros[(size_t) (source - 3)]->load ();
        fxOffsets[(size_t) (destination - bw::kFirstGlobalDestination)] += route.amount->load () * (route.bipolar->load () >= .5f ? value * 2 - 1 : value);
    }
    effects.process (buffer, effectParams, bpm, fxOffsets);
    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load ()));
    gain.applyGain (buffer, buffer.getNumSamples ());
    static_assert (std::atomic<float>::is_always_lock_free);
    for (size_t ch = 0; ch < outputPeak.size (); ++ch)
        outputPeak[ch].store (buffer.getNumSamples () > 0 && buffer.getNumChannels () > 0
            ? buffer.getMagnitude (juce::jmin ((int) ch, buffer.getNumChannels () - 1), 0, buffer.getNumSamples ()) : 0.0f,
            std::memory_order_relaxed);
}

//==============================================================================
void WaveSynth::setMode (int newMode, float glideSeconds)
{
    glide = glideSeconds;
    if (newMode == mode) return;
    // Held keys are forgotten on a mode change; sounding notes release normally. Not allNotesOff:
    // that would also forget a sustain pedal that is still down.
    const juce::ScopedLock sl (lock);
    heldCount = 0; current = -1; gate = false;
    for (auto* voice : voices) voice->stopNote (1.0f, true);
    mode = newMode;
}

void WaveSynth::noteOn (int channel, int note, float velocity)
{
    if (mode == poly) return Synthesiser::noteOn (channel, note, velocity);
    const juce::ScopedLock sl (lock);
    forget (note);
    held[(size_t) heldCount++] = note;
    velocities[(size_t) note] = velocity;
    play (channel, note);
}

void WaveSynth::noteOff (int channel, int note, float velocity, bool allowTailOff)
{
    if (mode == poly) return Synthesiser::noteOff (channel, note, velocity, allowTailOff);
    const juce::ScopedLock sl (lock);
    forget (note);
    if (note != current) return;
    if (heldCount > 0) play (channel, held[(size_t) heldCount - 1]); // last-note priority
    else if (! pedal) release (velocity, allowTailOff);
}

void WaveSynth::allNotesOff (int channel, bool allowTailOff)
{
    const juce::ScopedLock sl (lock);
    heldCount = 0; current = -1; gate = false; pedal = false; // the base forgets the pedal too
    Synthesiser::allNotesOff (channel, allowTailOff);
}

void WaveSynth::handleSustainPedal (int channel, bool isDown)
{
    // The base keeps its per-channel pedal state; it never stops the mono voice, whose key stays down for JUCE.
    Synthesiser::handleSustainPedal (channel, isDown);
    const juce::ScopedLock sl (lock);
    pedal = isDown; // tracked in poly too, for a later switch to mono/legato
    if (mode != poly && ! isDown && heldCount == 0 && gate) release (0, true);
}

void WaveSynth::play (int channel, int note)
{
    auto* voice = static_cast<WaveVoice*> (getVoice (0));
    const bool sounding = voice->isVoiceActive ();
    current = note;
    if (mode == legato && gate && sounding)
        voice->legato (note, glide);
    else
    {
        voice->setPendingGlide (sounding && mode == mono ? glide : 0);
        startVoice (voice, getSound (0).get (), channel, note, velocities[(size_t) note]);
    }
    gate = true;
}

void WaveSynth::release (float velocity, bool allowTailOff)
{
    gate = false; current = -1;
    stopVoice (getVoice (0), velocity, allowTailOff);
}

void WaveSynth::forget (int note)
{
    const auto end = held.begin () + heldCount;
    heldCount = (int) (std::remove (held.begin (), end, note) - held.begin ());
}

juce::AudioProcessorEditor* WaveProcessor::createEditor () { return new WaveEditor (*this); }

bw::LfoCurve WaveProcessor::getLfoCurve (int index) const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return savedCurves[(size_t)juce::jlimit (0, 1, index)];
}
void WaveProcessor::setLfoCurve (int index, const bw::LfoCurve& curve)
{
    const ScopedEdit edit (*this);
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        savedCurves[(size_t) juce::jlimit (0, 1, index)] = curve.valid () ? curve : bw::LfoCurve::factory (0);
        curveExchange.publish (savedCurves);
        curveRevision.fetch_add (1, std::memory_order_relaxed);
    }
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}
juce::String WaveProcessor::getMacroName (int index) const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return macroNames[(size_t)juce::jlimit (0, 3, index)];
}
void WaveProcessor::setMacroName (int index, const juce::String& name)
{
    const ScopedEdit edit (*this);
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        index = juce::jlimit (0, 3, index);
        macroNames[(size_t) index] = name.trim ().substring (0, 32);
        if (macroNames[(size_t) index].isEmpty ())
            macroNames[(size_t) index] = "Macro " + juce::String (index + 1);
        curveRevision.fetch_add (1, std::memory_order_relaxed);
    }
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}
double WaveProcessor::getTailLengthSeconds () const
{
    // ponytail: fixed worst case; derive from feedback/size if hosts cut tails short.
    return effectParams.delayOn->load () >= .5f || effectParams.reverbOn->load () >= .5f ? 10.0 : 0.0;
}

namespace
{
struct UniqueAssetBytes
{
    std::vector<const void*> seen;
    size_t bytes = 0;

    template <typename T>
    void add (const T* item)
    {
        if (item == nullptr) return;
        if (std::find (seen.begin (), seen.end (), static_cast<const void*> (item)) != seen.end ()) return;
        seen.push_back (item);
        bytes += item->bytes ();
    }
    template <typename T>
    void add (const std::shared_ptr<const T>& item) { add (item.get ()); }
};
} // namespace

void WaveProcessor::collectAssetsLocked ()
{
    for (auto& slot : userSlots) slot.collect ();
    sampleSlot.collect ();
}

size_t WaveProcessor::nonEvictableAssetBytesLocked (const bw::Wavetable* nextTable, const bw::Sample* nextSample) const
{
    UniqueAssetBytes set;
    for (const auto& slot : userSlots)
    {
        set.add (slot.get ());
        for (const auto& r : slot.retiredItems ()) set.add (r);
    }
    set.add (sampleSlot.get ());
    for (const auto& r : sampleSlot.retiredItems ()) set.add (r);
    set.add (nextTable);
    set.add (nextSample);
    return set.bytes + pendingSampleJobBytes;
}

size_t WaveProcessor::totalAssetBytesLocked (const bw::Wavetable* nextTable, const bw::Sample* nextSample) const
{
    UniqueAssetBytes set;
    for (const auto& slot : userSlots)
    {
        set.add (slot.get ());
        for (const auto& r : slot.retiredItems ()) set.add (r);
    }
    set.add (sampleSlot.get ());
    for (const auto& r : sampleSlot.retiredItems ()) set.add (r);
    set.add (nextTable);
    set.add (nextSample);
    auto addSnapshot = [&] (const Snapshot& s)
    {
        for (const auto& t : s.tables) set.add (t);
        set.add (s.sample);
    };
    if (editDepth > 0) addSnapshot (editStart);
    for (const auto& step : history)
    {
        addSnapshot (step.before);
        addSnapshot (step.after);
    }
    return set.bytes + pendingSampleJobBytes;
}

void WaveProcessor::evictHistoryForBudgetLocked (const bw::Wavetable* nextTable, const bw::Sample* nextSample)
{
    while (! history.empty () && (history.size () > kHistorySteps || totalAssetBytesLocked (nextTable, nextSample) > kHistoryBytes))
    {
        history.erase (history.begin ());
        if (historyIndex > 0) --historyIndex;
    }
    if (editDepth > 0 && totalAssetBytesLocked (nextTable, nextSample) > kHistoryBytes)
    {
        editStart.sample.reset ();
        editStart.tables = {};
    }
}

size_t WaveProcessor::assetMemoryBytes ()
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    collectAssetsLocked ();
    return totalAssetBytesLocked ();
}

size_t WaveProcessor::retiredMemoryBytes ()
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    collectAssetsLocked ();
    UniqueAssetBytes set;
    for (const auto& slot : userSlots)
        for (const auto& r : slot.retiredItems ()) set.add (r);
    for (const auto& r : sampleSlot.retiredItems ()) set.add (r);
    return set.bytes;
}

juce::String WaveProcessor::importWavetable (int oscillator, const juce::File& file)
{
    juce::String error;
    const auto cycles = bw::loadCycles (file, error);
    if (cycles.empty ()) return error;
    return publishCycles (oscillator, cycles, file.getFileNameWithoutExtension ());
}
juce::String WaveProcessor::importTuned (int oscillator, const bw::TunedSource& source, double period, const juce::String& name)
{
    const auto cycles = bw::cyclesFromPeriod (source.mono, period);
    if (cycles.empty ()) return "The period is longer than the audio";
    return publishCycles (oscillator, cycles, name);
}
juce::String WaveProcessor::publishCycles (int oscillator, const std::vector<std::vector<float>>& cycles, const juce::String& name)
{
    // ponytail: built on the calling thread (~tens of ms for 256 frames); move to a worker if imports stall the UI.
    auto table = std::make_shared<bw::Wavetable> ();
    table->setFrames (cycles);
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        collectAssetsLocked ();
        if (nonEvictableAssetBytesLocked (table.get (), nullptr) > kHistoryBytes)
            return "Wavetable exceeds 256 MB memory budget";
        evictHistoryForBudgetLocked (table.get (), nullptr);
    }
    setUserTable (oscillator, std::move (table), name);
    return {};
}
void WaveProcessor::setUserTable (int oscillator, std::shared_ptr<const bw::Wavetable> table, const juce::String& name)
{
    const ScopedEdit edit (*this);
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        collectAssetsLocked ();
        evictHistoryForBudgetLocked (table.get (), nullptr);
        const auto index = (size_t) juce::jlimit (0, 1, oscillator);
        userTableNames[index] = table != nullptr ? name.substring (0, 64) : juce::String ();
        userSlots[index].publish (std::move (table));
        tableRevision.fetch_add (1, std::memory_order_relaxed);
    }
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}
bool WaveProcessor::userTableSettled (int oscillator)
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    auto& slot = userSlots[(size_t) juce::jlimit (0, 1, oscillator)];
    slot.collect ();
    return slot.idle ();
}
std::shared_ptr<const bw::Wavetable> WaveProcessor::userTable (int oscillator) const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return userSlots[(size_t) juce::jlimit (0, 1, oscillator)].get ();
}
juce::String WaveProcessor::userTableName (int oscillator) const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return userTableNames[(size_t) juce::jlimit (0, 1, oscillator)];
}

juce::String WaveProcessor::loadSample (const juce::File& file)
{
    juce::String error;
    auto loaded = bw::loadSample (file, error);
    if (loaded == nullptr)
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        lastSampleLoadError = error;
        return error;
    }
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        collectAssetsLocked ();
        if (nonEvictableAssetBytesLocked (nullptr, loaded.get ()) > kHistoryBytes)
        {
            lastSampleLoadError = "Sample exceeds 256 MB memory budget";
            return lastSampleLoadError;
        }
        evictHistoryForBudgetLocked (nullptr, loaded.get ());
    }
    publishSample (std::move (loaded), file);
    return {};
}

juce::String WaveProcessor::relinkSample (const juce::File& file)
{
    juce::String error;
    auto loaded = bw::loadSample (file, error);
    if (loaded == nullptr)
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        lastSampleLoadError = error;
        return error;
    }
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        collectAssetsLocked ();
        if (nonEvictableAssetBytesLocked (nullptr, loaded.get ()) > kHistoryBytes)
        {
            lastSampleLoadError = "Sample exceeds 256 MB memory budget";
            return lastSampleLoadError;
        }
        evictHistoryForBudgetLocked (nullptr, loaded.get ());
    }
    publishSample (std::move (loaded), file, "SAMPLE RELINK");
    return {};
}

bool WaveProcessor::isRequestCancelledLocked (uint64_t requestId) const noexcept
{
    if (shuttingDown.load (std::memory_order_acquire)) return true;
    if (requestId < latestSampleLoadId.load (std::memory_order_acquire)) return true;
    if (requestId <= cancelledUpToLoadId.load (std::memory_order_acquire)) return true;
    if (requestId == specificCancelledLoadId.load (std::memory_order_acquire)) return true;
    return false;
}

uint64_t WaveProcessor::startSampleLoad (const juce::File& file, SampleReaderFactory readerFactory)
{
    const uint64_t requestId = latestSampleLoadId.fetch_add (1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        lastSampleLoadError.clear ();
        sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
        sampleLoadingFlag.store (true, std::memory_order_release);
    }
    // Drop queued jobs that have not started yet so rapid request bursts remain bounded.
    sampleLoadPool.removeAllJobs (false, 0);

    struct LoadJob final : juce::ThreadPoolJob
    {
        WaveProcessor& processor;
        uint64_t requestId;
        juce::File file;
        SampleReaderFactory readerFactory;

        LoadJob (WaveProcessor& p, uint64_t id, const juce::File& f, SampleReaderFactory rf)
            : juce::ThreadPoolJob ("SampleLoad"), processor (p), requestId (id), file (f), readerFactory (std::move (rf))
        {
            processor.activeSampleJobs.fetch_add (1, std::memory_order_acq_rel);
            processor.sampleLoadIdleEvent.reset ();
        }

        ~LoadJob () override
        {
            if (processor.activeSampleJobs.fetch_sub (1, std::memory_order_acq_rel) == 1)
                processor.sampleLoadIdleEvent.signal ();
        }

        JobStatus runJob () override
        {
            auto shouldCancel = [this] () noexcept -> bool
            {
                return shouldExit () || processor.isRequestCancelledLocked (requestId);
            };

            if (shouldCancel ())
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Sample load cancelled");
                return jobHasFinished;
            }

            std::unique_ptr<juce::AudioFormatReader> reader;
            if (readerFactory)
            {
                reader = readerFactory (file);
            }
            else if (file != juce::File ())
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats ();
                reader.reset (formats.createReaderFor (file));
            }

            if (reader == nullptr)
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Could not read the audio file");
                return jobHasFinished;
            }
            if (reader->sampleRate <= 0.0 || ! std::isfinite (reader->sampleRate))
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Sample has an invalid sample rate");
                return jobHasFinished;
            }
            if (reader->numChannels == 0 || reader->numChannels > bw::kMaxSampleChannels)
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Sample has an unsupported channel count");
                return jobHasFinished;
            }
            if (reader->lengthInSamples <= 0)
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Sample is empty");
                return jobHasFinished;
            }
            if ((uint64_t) reader->lengthInSamples > (uint64_t) bw::kMaxSampleFrames)
            {
                processor.completeSampleLoad (requestId, nullptr, file, "Sample exceeds 60 seconds");
                return jobHasFinished;
            }

            const size_t channels = reader->numChannels >= 2 ? 2u : 1u;
            const size_t reservationBytes = (size_t) reader->lengthInSamples * channels * sizeof (float);
            {
                std::lock_guard<std::mutex> lock (processor.extraStateMutex);
                if (processor.isRequestCancelledLocked (requestId))
                {
                    processor.completeSampleLoad (requestId, nullptr, file, "Sample load cancelled");
                    return jobHasFinished;
                }
                processor.collectAssetsLocked ();
                if (processor.nonEvictableAssetBytesLocked () + reservationBytes > WaveProcessor::kHistoryBytes)
                {
                    processor.completeSampleLoad (requestId, nullptr, file, "Sample exceeds 256 MB memory budget");
                    return jobHasFinished;
                }
                processor.pendingSampleJobBytes += reservationBytes;
            }

            struct ScopedReservation
            {
                WaveProcessor& p;
                size_t bytes;
                ~ScopedReservation ()
                {
                    std::lock_guard<std::mutex> lock (p.extraStateMutex);
                    p.pendingSampleJobBytes -= bytes;
                }
            };

            juce::String error;
            std::shared_ptr<const bw::Sample> loaded;
            {
                const ScopedReservation reservation { processor, reservationBytes };
                loaded = bw::decodeSample (*reader, error, -1,
                    shouldCancel,
                    [this] (float progress)
                    {
                        if (! processor.isRequestCancelledLocked (requestId)
                            && requestId == processor.latestSampleLoadId.load (std::memory_order_acquire))
                        {
                            processor.sampleLoadProgressValue.store (progress, std::memory_order_relaxed);
                        }
                    },
                    reservationBytes);
            }

            processor.completeSampleLoad (requestId, std::move (loaded), file, error);
            return jobHasFinished;
        }
    };

    sampleLoadPool.addJob (new LoadJob (*this, requestId, file, std::move (readerFactory)), true);
    return requestId;
}

void WaveProcessor::cancelSampleLoad (uint64_t requestId)
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    const uint64_t latest = latestSampleLoadId.load (std::memory_order_acquire);
    if (requestId == 0 || requestId == latest)
    {
        cancelledUpToLoadId.store (latest, std::memory_order_release);
        sampleLoadingFlag.store (false, std::memory_order_release);
        sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
    }
    else
    {
        specificCancelledLoadId.store (requestId, std::memory_order_release);
    }
}

bool WaveProcessor::waitForSampleLoad (int timeoutMs)
{
    return sampleLoadIdleEvent.wait (timeoutMs);
}

juce::String WaveProcessor::sampleLoadError () const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return lastSampleLoadError;
}

void WaveProcessor::setRestoreReaderFactory (SampleReaderFactory factory)
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    restoreReaderFactory = std::move (factory);
}

bool WaveProcessor::completeSampleLoad (uint64_t requestId, std::shared_ptr<const bw::Sample> loaded, const juce::File& path, const juce::String& error)
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    const bool isLatest = (requestId == latestSampleLoadId.load (std::memory_order_acquire));
    if (isRequestCancelledLocked (requestId))
    {
        if (isLatest)
        {
            sampleLoadingFlag.store (false, std::memory_order_release);
            sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
        }
        return false;
    }

    if (loaded == nullptr)
    {
        lastSampleLoadError = error.isNotEmpty () ? error : juce::String ("Could not read the audio file");
        if (isLatest)
        {
            sampleLoadingFlag.store (false, std::memory_order_release);
            sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
        }
        return false;
    }

    collectAssetsLocked ();
    if (nonEvictableAssetBytesLocked (nullptr, loaded.get ()) > kHistoryBytes)
    {
        lastSampleLoadError = "Sample exceeds 256 MB memory budget";
        if (isLatest)
        {
            sampleLoadingFlag.store (false, std::memory_order_release);
            sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
        }
        return false;
    }

    evictHistoryForBudgetLocked (nullptr, loaded.get ());
    sampleFile = path;
    sampleSlot.publish (std::move (loaded));
    sampleRevision.fetch_add (1, std::memory_order_relaxed);
    lastSampleLoadError.clear ();
    if (isLatest)
    {
        sampleLoadProgressValue.store (1.0f, std::memory_order_relaxed);
        sampleLoadingFlag.store (false, std::memory_order_release);
    }
    return true;
}

void WaveProcessor::clearSample () { publishSample (nullptr, {}); }
void WaveProcessor::publishSample (std::shared_ptr<const bw::Sample> loaded, const juce::File& path, const juce::String& editStepName)
{
    const ScopedEdit edit (*this, editStepName);
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        const uint64_t id = latestSampleLoadId.fetch_add (1, std::memory_order_acq_rel) + 1;
        cancelledUpToLoadId.store (id, std::memory_order_release);
        sampleLoadingFlag.store (false, std::memory_order_release);
        sampleLoadProgressValue.store (0.0f, std::memory_order_relaxed);
        lastSampleLoadError.clear ();
        collectAssetsLocked ();
        evictHistoryForBudgetLocked (nullptr, loaded.get ());
        sampleFile = path;
        sampleSlot.publish (std::move (loaded));
        sampleRevision.fetch_add (1, std::memory_order_relaxed);
    }
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}
std::shared_ptr<const bw::Sample> WaveProcessor::sample () const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return sampleSlot.get ();
}
bool WaveProcessor::sampleSettled ()
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    sampleSlot.collect ();
    return sampleSlot.idle ();
}
juce::File WaveProcessor::samplePath () const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return sampleFile;
}
bool WaveProcessor::sampleMissing () const
{
    std::lock_guard<std::mutex> lock (extraStateMutex);
    return ! sampleLoadingFlag.load (std::memory_order_acquire) && sampleFile != juce::File () && sampleSlot.get () == nullptr;
}

void WaveProcessor::loadFactoryPreset (int index)
{
    index = juce::jlimit (0, (int) factoryNormalised.size () - 1, index);
    const auto& preset = bw::factoryPresets ()[(size_t) index];
    const ScopedEdit edit (*this, "PRESET " + juce::String (preset.name).toUpperCase ());
    const auto& values = factoryNormalised[(size_t) index];
    const auto& parameters = getParameters ();
    for (int i = 0; i < parameters.size (); ++i)
    {
        parameters[i]->beginChangeGesture ();
        parameters[i]->setValueNotifyingHost (values[(size_t) i]);
        parameters[i]->endChangeGesture ();
    }
    for (int i = 0; i < 2; ++i) setLfoCurve (i, preset.curve (i));
    for (int i = 0; i < 4; ++i) setMacroName (i, preset.macros[(size_t) i]);
    for (int i = 0; i < 2; ++i) setUserTable (i, preset.tables[(size_t) i], preset.tableNames[(size_t) i]);
    clearSample ();
}
static bool sameCurve (const bw::LfoCurve& a, const bw::LfoCurve& b)
{
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i)
    {
        const auto& p = a.points[(size_t) i]; const auto& q = b.points[(size_t) i];
        if (std::abs (p.x - q.x) > 1.0e-5f || std::abs (p.y - q.y) > 1.0e-5f || std::abs (p.curve - q.curve) > 1.0e-5f) return false;
    }
    return true;
}
int WaveProcessor::matchingFactoryPreset () const
{
    if (sample () != nullptr || samplePath () != juce::File () || sampleLoading ()) return -1; // factory presets carry no sample
    const auto& parameters = getParameters ();
    std::array<bw::LfoCurve, 2> curves { getLfoCurve (0), getLfoCurve (1) };
    std::array<juce::String, 4> macros { getMacroName (0), getMacroName (1), getMacroName (2), getMacroName (3) };
    std::array<std::shared_ptr<const bw::Wavetable>, 2> tables { userTable (0), userTable (1) };
    for (size_t n = 0; n < factoryNormalised.size (); ++n)
    {
        const auto& preset = bw::factoryPresets ()[n];
        bool same = true;
        for (int i = 0; same && i < parameters.size (); ++i)
            same = std::abs (parameters[i]->getValue () - factoryNormalised[n][(size_t) i]) <= 1.0e-4f;
        for (int l = 0; same && l < 2; ++l) same = sameCurve (curves[(size_t) l], preset.curve (l));
        for (int m = 0; same && m < 4; ++m) same = macros[(size_t) m] == preset.macros[(size_t) m];
        for (int t = 0; same && t < 2; ++t)
        {
            const auto& actual = tables[(size_t) t];
            const auto& expected = preset.tables[(size_t) t];
            if (actual == expected) continue;
            same = actual != nullptr && expected != nullptr && actual->numFrames == expected->numFrames;
            // Saved cycles pass through FFT reconstruction; pointer identity is insufficient.
            for (int f = 0; same && f < actual->numFrames; ++f)
                for (int s = 0; same && s < bw::Wavetable::kSize; ++s)
                    same = std::abs (actual->ptr (f, 0)[s] - expected->ptr (f, 0)[s]) < 2.0e-5f;
        }
        if (same) return (int) n;
    }
    return -1;
}

juce::File WaveProcessor::presetDirectory ()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Borato").getChildFile ("Borato Waves").getChildFile ("Presets");
}
bool WaveProcessor::savePreset (const juce::File& file)
{
    if (! file.getParentDirectory ().createDirectory ()) return false;
    const auto xml = createStateXml ();
    if (xml == nullptr) return false;
    xml->setAttribute ("presetCategory", "USER");
    juce::TemporaryFile temp (file);
    {
        std::unique_ptr<juce::FileOutputStream> stream (temp.getFile ().createOutputStream ());
        if (stream == nullptr || ! stream->openedOk ()) return false;
        xml->writeTo (*stream);
        stream->flush ();
        if (stream->getStatus ().failed ()) return false;
    }
    return temp.overwriteTargetFileWithTemporary ();
}
bool WaveProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType ().toString ())
        || xml->getIntAttribute ("formatVersion", 1) > kStateVersion) return false;
    const ScopedEdit edit (*this, "PRESET " + file.getFileNameWithoutExtension ().toUpperCase ());
    if (! restoreState (*xml)) return false;
    std::fill (touched.begin (), touched.end (), true); // the state replaces every parameter, without gestures
    return true;
}

void WaveProcessor::parameterGestureChanged (int index, bool starting)
{
    if (! starting) return endEdit ();
    beginEdit ();
    if (! replaying) touched[(size_t) index] = true;
}
void WaveProcessor::beginEdit (const juce::String& name)
{
    if (replaying || editDepth++ > 0) return;
    editStart = capture ();
    touched.assign ((size_t) getParameters ().size (), false);
    editName = name;
}
void WaveProcessor::endEdit ()
{
    if (replaying || editDepth == 0 || --editDepth > 0) return;
    Step step { std::move (editStart), capture (), std::vector<bool> (touched.size ()), std::move (editName) };
    const auto& before = step.before;
    const auto& after = step.after;
    juce::StringArray changes;
    const auto& parameters = getParameters ();
    for (size_t i = 0; i < touched.size (); ++i)
        if (touched[i] && before.values[i] != after.values[i])
        {
            step.parameters[i] = true;
            changes.add (parameters[(int) i]->getName (64).toUpperCase ());
        }
    for (int l = 0; l < 2; ++l)
        if (! sameCurve (before.curves[(size_t) l], after.curves[(size_t) l])) changes.add ("LFO " + juce::String (l + 1) + " SHAPE");
    if (before.macros != after.macros) changes.add ("MACRO NAME");
    for (size_t t = 0; t < 2; ++t)
        if (before.tables[t] != after.tables[t] || before.tableNames[t] != after.tableNames[t]) changes.add (t == 0 ? "OSC A TABLE" : "OSC B TABLE");
    if (before.sample != after.sample || before.samplePath != after.samplePath) changes.add ("SAMPLE");
    if (changes.isEmpty ()) return;
    if (step.name.isEmpty ()) step.name = changes.size () == 1 ? changes[0] : "EDIT";
    // Wheel ticks and arrow steps are a gesture each: on the same control within half a second, they extend the last step.
    const auto now = juce::Time::getMillisecondCounter ();
    if (historyIndex == (int) history.size () && historyIndex > 0 && now - lastStepTime < 500
        && before.sample == after.sample && before.tables == after.tables
        && history.back ().name == step.name && history.back ().parameters == step.parameters && changes.size () == 1)
    {
        history.back ().after = std::move (step.after);
        lastStepTime = now;
        return;
    }
    lastStepTime = now;
    history.resize ((size_t) historyIndex);
    history.push_back (std::move (step));
    historyIndex = (int) history.size ();
    std::lock_guard<std::mutex> lock (extraStateMutex);
    collectAssetsLocked ();
    evictHistoryForBudgetLocked ();
}
bool WaveProcessor::undo ()
{
    if (editDepth > 0 || ! canUndo ()) return false;
    const auto& step = history[(size_t) --historyIndex];
    restore (step.before, step.parameters);
    return true;
}
bool WaveProcessor::redo ()
{
    if (editDepth > 0 || ! canRedo ()) return false;
    const auto& step = history[(size_t) historyIndex++];
    restore (step.after, step.parameters);
    return true;
}
WaveProcessor::Snapshot WaveProcessor::capture () const
{
    Snapshot s;
    for (auto* parameter : getParameters ()) s.values.push_back (parameter->getValue ());
    std::lock_guard<std::mutex> lock (extraStateMutex);
    s.curves = savedCurves;
    s.macros = macroNames;
    s.tableNames = userTableNames;
    for (size_t t = 0; t < 2; ++t) s.tables[t] = userSlots[t].get ();
    s.sample = sampleSlot.get ();
    s.samplePath = sampleFile;
    return s;
}
// Through the same setters as an edit, so the editor, host and audio thread follow.
void WaveProcessor::restore (const Snapshot& s, const std::vector<bool>& parameters)
{
    const juce::ScopedValueSetter<bool> quiet (replaying, true);
    const auto& all = getParameters ();
    for (int i = 0; i < all.size (); ++i)
        if (parameters[(size_t) i] && all[i]->getValue () != s.values[(size_t) i])
        {
            all[i]->beginChangeGesture ();
            all[i]->setValueNotifyingHost (s.values[(size_t) i]);
            all[i]->endChangeGesture ();
        }
    for (int l = 0; l < 2; ++l)
        if (! sameCurve (getLfoCurve (l), s.curves[(size_t) l])) setLfoCurve (l, s.curves[(size_t) l]);
    for (int m = 0; m < 4; ++m)
        if (getMacroName (m) != s.macros[(size_t) m]) setMacroName (m, s.macros[(size_t) m]);
    for (int t = 0; t < 2; ++t)
        if (userTable (t) != s.tables[(size_t) t] || userTableName (t) != s.tableNames[(size_t) t])
            setUserTable (t, s.tables[(size_t) t], s.tableNames[(size_t) t]);
    if (sample () != s.sample || samplePath () != s.samplePath) publishSample (s.sample, s.samplePath);
}

std::unique_ptr<juce::XmlElement> WaveProcessor::createStateXml ()
{
    auto state = apvts.copyState ();
    for (auto name : { "MODULATION", "USERTABLES", "SAMPLE" })
        if (const auto old = state.getChildWithName (name); old.isValid ()) state.removeChild (old, nullptr);
    state.setProperty ("formatVersion", kStateVersion, nullptr);
    juce::ValueTree extra ("MODULATION"); extra.setProperty ("version", 1, nullptr);
    juce::ValueTree tables ("USERTABLES");
    std::shared_ptr<const bw::Sample> currentSample;
    juce::File currentSamplePath;
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        for (int i = 0; i < 2; ++i)
        {
            juce::ValueTree curve ("CURVE"); curve.setProperty ("index", i, nullptr);
            const auto& data = savedCurves[(size_t)i];
            for (int n = 0; n < data.count; ++n) { juce::ValueTree point ("POINT"); const auto& p = data.points[(size_t)n]; point.setProperty ("x", p.x, nullptr); point.setProperty ("y", p.y, nullptr); point.setProperty ("curve", p.curve, nullptr); curve.addChild (point, -1, nullptr); }
            extra.addChild (curve, -1, nullptr);
        }
        for (int i = 0; i < 4; ++i) extra.setProperty ("macro"+juce::String (i+1), macroNames[(size_t)i], nullptr);
        // Level 0 holds the normalised full-band cycles, enough to rebuild every mip level.
        for (int i = 0; i < 2; ++i)
            if (const auto& table = userSlots[(size_t) i].get ())
            {
                juce::MemoryBlock samples ((size_t) table->numFrames * bw::Wavetable::kSize * sizeof (float));
                for (int f = 0; f < table->numFrames; ++f)
                    samples.copyFrom (table->ptr (f, 0), (int) ((size_t) f * bw::Wavetable::kSize * sizeof (float)), bw::Wavetable::kSize * sizeof (float));
                juce::ValueTree entry ("TABLE");
                entry.setProperty ("oscillator", i, nullptr); entry.setProperty ("name", userTableNames[(size_t) i], nullptr);
                entry.setProperty ("frames", table->numFrames, nullptr); entry.setProperty ("data", samples.toBase64Encoding (), nullptr);
                tables.addChild (entry, -1, nullptr);
            }
        currentSample = sampleSlot.get ();
        currentSamplePath = sampleFile;
    }
    state.addChild (extra, -1, nullptr);
    state.addChild (tables, -1, nullptr);
    if (currentSample != nullptr || currentSamplePath != juce::File ())
        state.addChild (bw::serializeSampleContainer (currentSample.get (), currentSamplePath), -1, nullptr);
    return state.createXml ();
}
void WaveProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createStateXml ()) copyXmlToBinary (*xml, dest);
}
bool WaveProcessor::restoreState (const juce::XmlElement& xml)
{
    auto state = juce::ValueTree::fromXml (xml);
    if (! state.isValid ()) return false;

    const auto sampleTree = state.getChildWithName ("SAMPLE");
    std::shared_ptr<const bw::Sample> embeddedSample;
    juce::File parsedSamplePath;
    bool hasSampleContainer = false;
    {
        juce::String error;
        if (! bw::deserializeSampleContainer (sampleTree, embeddedSample, parsedSamplePath, hasSampleContainer, error, kHistoryBytes))
        {
            std::lock_guard<std::mutex> lock (extraStateMutex);
            lastSampleLoadError = error;
            return false;
        }
        if (hasSampleContainer && embeddedSample != nullptr)
        {
            std::lock_guard<std::mutex> lock (extraStateMutex);
            collectAssetsLocked ();
            UniqueAssetBytes retiredSet;
            for (const auto& slot : userSlots)
                for (const auto& r : slot.retiredItems ()) retiredSet.add (r);
            for (const auto& r : sampleSlot.retiredItems ()) retiredSet.add (r);
            retiredSet.add (embeddedSample);
            if (retiredSet.bytes + pendingSampleJobBytes > kHistoryBytes)
            {
                lastSampleLoadError = "Sample exceeds 256 MB memory budget";
                return false;
            }
        }
    }

    for (auto* parameter : getParameters ())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! state.getChildWithProperty ("id", ranged->paramID).isValid ())
            {
                juce::ValueTree value ("PARAM"); value.setProperty ("id", ranged->paramID, nullptr);
                value.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue ()), nullptr); state.addChild (value, -1, nullptr);
            }
    const auto extra = state.getChildWithName ("MODULATION");
    {
        std::lock_guard<std::mutex> lock (extraStateMutex);
        savedCurves = { bw::LfoCurve::factory (0), bw::LfoCurve::factory (0) };
        for (int i = 0; i < 4; ++i) { auto name = extra.getProperty ("macro"+juce::String (i+1), "Macro "+juce::String (i+1)).toString ().trim ().substring (0, 32); macroNames[(size_t)i] = name.isEmpty () ? "Macro "+juce::String (i+1) : name; }
        if ((int)extra.getProperty ("version", 0) == 1)
            for (const auto& child : extra)
                if (child.hasType ("CURVE"))
                {
                    const int index = (int)child.getProperty ("index", -1); if (index < 0 || index > 1) continue;
                    bw::LfoCurve curve; curve.count = child.getNumChildren ();
                    if (curve.count <= 32) for (int n = 0; n < curve.count; ++n) { const auto point = child.getChild (n); curve.points[(size_t)n] = { (float)point.getProperty ("x", -1), (float)point.getProperty ("y", -1), (float)point.getProperty ("curve", 0) }; }
                    if (curve.valid ()) savedCurves[(size_t)index] = curve;
                }
        curveExchange.publish (savedCurves); curveRevision.fetch_add (1, std::memory_order_relaxed);
    }
    // States without a table clear the slot, so a user table never leaks into another patch.
    const auto tables = state.getChildWithName ("USERTABLES");
    for (int i = 0; i < 2; ++i)
    {
        const auto entry = tables.getChildWithProperty ("oscillator", i);
        const int frames = entry.getProperty ("frames", 0);
        juce::MemoryBlock samples;
        std::shared_ptr<bw::Wavetable> table;
        if (frames > 0 && frames <= bw::kMaxUserFrames && samples.fromBase64Encoding (entry.getProperty ("data").toString ())
            && samples.getSize () == (size_t) frames * bw::Wavetable::kSize * sizeof (float))
        {
            std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> (bw::Wavetable::kSize));
            for (int f = 0; f < frames; ++f)
                samples.copyTo (cycles[(size_t) f].data (), (int) ((size_t) f * bw::Wavetable::kSize * sizeof (float)), bw::Wavetable::kSize * sizeof (float));
            const bool finite = std::all_of (cycles.begin (), cycles.end (), [] (const auto& cycle)
            {
                return std::all_of (cycle.begin (), cycle.end (), [] (float sample) { return std::isfinite (sample) && std::abs (sample) <= 1.001f; });
            });
            if (finite)
            {
                table = std::make_shared<bw::Wavetable> ();
                table->setFrames (cycles);
            }
        }
        setUserTable (i, std::move (table), entry.getProperty ("name").toString ());
    }

    if (hasSampleContainer)
    {
        publishSample (std::move (embeddedSample), parsedSamplePath);
    }
    else if (parsedSamplePath != juce::File ())
    {
        // A missing or unreadable file keeps its reference, so saving again does not lose it.
        SampleReaderFactory factoryCopy;
        {
            std::lock_guard<std::mutex> lock (extraStateMutex);
            factoryCopy = restoreReaderFactory;
        }
        publishSample (nullptr, parsedSamplePath);
        if (factoryCopy != nullptr || parsedSamplePath.existsAsFile ())
            startSampleLoad (parsedSamplePath, std::move (factoryCopy));
    }
    else
    {
        clearSample ();
    }

    for (auto name : { "MODULATION", "USERTABLES", "SAMPLE" })
        if (const auto child = state.getChildWithName (name); child.isValid ()) state.removeChild (child, nullptr);
    apvts.replaceState (state);
    return true;
}
void WaveProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml && xml->hasTagName (apvts.state.getType ()))
    {
        bool ok = false;
        {
            const juce::ScopedValueSetter<bool> quiet (replaying, true);
            ok = restoreState (*xml);
        }
        if (ok)
        {
            history.clear (); // a new project or patch from the host
            historyIndex = 0;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter () { return new WaveProcessor (); }
