cmake_minimum_required(VERSION 3.22)
project(BoratoWave VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_DIR "C:/JUCE" CACHE PATH "JUCE checkout")
option(BORATOWAVE_FETCH_JUCE "Download the pinned JUCE revision" OFF)
option(BORATOWAVE_BUILD_TESTS "Build and register the offline test suite" ON)

if(BORATOWAVE_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 3fb5cbc0340758596dac39f7c8de47d562b4896e) # JUCE 9.0.4
    FetchContent_MakeAvailable(juce)
elseif(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "JUCE checkout not found. Set JUCE_DIR or enable BORATOWAVE_FETCH_JUCE.")
endif()

juce_add_plugin(BoratoWave
    COMPANY_NAME "Borato Company"
    BUNDLE_ID com.boratoaudio.waves
    PLUGIN_MANUFACTURER_CODE Bora
    PLUGIN_CODE BWav
    PRODUCT_NAME "Borato Waves"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
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

if(BORATOWAVE_BUILD_TESTS)
    enable_testing()
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

    foreach(test_name IN ITEMS wavetable interface sources modulation effects presets atelier voices sample table_editor history tuned_import)
        add_test(NAME ${test_name} COMMAND $<TARGET_FILE:test_${test_name}>)
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 1800)
    endforeach()
endif()
