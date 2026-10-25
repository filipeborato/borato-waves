#pragma once
#include <array>
#include <utility>
#include <vector>
#include <optional>
#include "Modulation.h"
#include "Wavetable.h"
#include "Sample.h"

namespace bw
{
// One matrix slot; slots are filled in order from mod1.
struct PresetRoute { int source = 0, destination = 0; float amount = 0; bool bipolar = false; };
struct PresetValue { const char* id; float value; };

// Original factory content. Values are plain (Hz, dB, semitones, choice index) and only list
// what differs from the parameter defaults; later entries override earlier ones.
struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* tags;
    const char* description;
    std::vector<PresetValue> values;
    std::vector<PresetRoute> routes;
    std::array<int, 2> lfoShapes { 0, 0 };   // LfoCurve::factory indices
    std::array<const char*, 4> macros { "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    std::array<std::optional<LfoCurve>, 2> curves {};
    std::array<std::shared_ptr<const Wavetable>, 2> tables {};
    std::array<const char*, 2> tableNames { "", "" };
    std::shared_ptr<const Sample> sample {};
    std::shared_ptr<const SampleBank> sampleBank {};

    LfoCurve curve (int index) const { return curves[(size_t) index].value_or (LfoCurve::factory (lfoShapes[(size_t) index])); }
};

const std::vector<FactoryPreset>& factoryPresets();
} // namespace bw
