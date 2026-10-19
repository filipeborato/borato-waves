cmake_minimum_required(VERSION 3.22)
project(BoratoWave VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_DIR "C:/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} juce)

juce_add_plugin(BoratoWave
    COMPANY_NAME borato
    PLUGIN_MANUFACTURER_CODE Brto
    PLUGIN_CODE BWav
    PRODUCT_NAME "Borato Wave"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    FORMATS VST3 Standalone)

target_sources(BoratoWave PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp
    Source/UI/Controls.cpp Source/UI/Displays.cpp Source/UI/WavetableBrowser.cpp Source/UI/ModulationUI.cpp
    Source/UI/TableEditor.cpp Source/UI/SamplePanel.cpp Source/UI/ImportDialog.cpp
    Source/Library/borato_preset_search.cpp Source/FactoryPresets.cpp)
target_compile_definitions(BoratoWave PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# Match Borato Morphium: use JUCE's bundled ASIO headers on Windows.
# PUBLIC keeps the shared audio-device module and Standalone wrapper consistent.
if(WIN32)
    target_compile_definitions(BoratoWave PUBLIC JUCE_ASIO=1)
endif()
target_link_libraries(BoratoWave
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

juce_add_console_app(test_wavetable)
target_sources(test_wavetable PRIVATE tests/test_wavetable.cpp)
target_include_directories(test_wavetable PRIVATE Source)
target_compile_definitions(test_wavetable PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(test_wavetable PRIVATE juce::juce_dsp)

juce_add_console_app(test_interface)
target_sources(test_interface PRIVATE tests/test_interface.cpp)
target_include_directories(test_interface PRIVATE Source)
target_link_libraries(test_interface PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_sources)
target_sources(test_sources PRIVATE tests/test_sources.cpp)
target_include_directories(test_sources PRIVATE Source)
target_link_libraries(test_sources PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_modulation)
target_sources(test_modulation PRIVATE tests/test_modulation.cpp)
target_include_directories(test_modulation PRIVATE Source)
target_link_libraries(test_modulation PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_effects)
target_sources(test_effects PRIVATE tests/test_effects.cpp)
target_include_directories(test_effects PRIVATE Source)
target_link_libraries(test_effects PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_presets)
target_sources(test_presets PRIVATE tests/test_presets.cpp)
target_include_directories(test_presets PRIVATE Source)
target_link_libraries(test_presets PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_atelier)
target_sources(test_atelier PRIVATE tests/test_atelier.cpp)
target_include_directories(test_atelier PRIVATE Source)
target_link_libraries(test_atelier PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_voices)
target_sources(test_voices PRIVATE tests/test_voices.cpp)
target_include_directories(test_voices PRIVATE Source)
target_link_libraries(test_voices PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_sample)
target_sources(test_sample PRIVATE tests/test_sample.cpp)
target_include_directories(test_sample PRIVATE Source)
target_link_libraries(test_sample PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_table_editor)
target_sources(test_table_editor PRIVATE tests/test_table_editor.cpp)
target_include_directories(test_table_editor PRIVATE Source)
target_link_libraries(test_table_editor PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_history)
target_sources(test_history PRIVATE tests/test_history.cpp)
target_include_directories(test_history PRIVATE Source)
target_link_libraries(test_history PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)

juce_add_console_app(test_tuned_import)
target_sources(test_tuned_import PRIVATE tests/test_tuned_import.cpp)
target_include_directories(test_tuned_import PRIVATE Source)
target_link_libraries(test_tuned_import PRIVATE BoratoWave juce::juce_audio_utils juce::juce_dsp)
