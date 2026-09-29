cmake_minimum_required(VERSION 3.22)
project(FM1Companion VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

option(FM1_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(FM1_BUILD_AU "Also build the AudioUnit" ON)
option(FM1_COPY_PLUGIN "Copy the built plugins into the user plug-in folders" ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
endif()

# ---- msfa engine (Apache-2.0, see engine/msfa/NOTICE.md) ----------------------
add_library(fm1_engine STATIC
    engine/msfa/dx7note.cc engine/msfa/env.cc engine/msfa/exp2.cc engine/msfa/fm_core.cc
    engine/msfa/fm_op_kernel.cc engine/msfa/freqlut.cc engine/msfa/lfo.cc engine/msfa/pitchenv.cc
    engine/msfa/porta.cpp engine/msfa/sin.cc engine/msfa/tuning.cc
    engine/FmSynth.cpp)
target_include_directories(fm1_engine PUBLIC engine)
target_compile_options(fm1_engine PRIVATE -O3 -Wno-deprecated-declarations)

# ---- sync codec (no JUCE) -----------------------------------------------------
add_library(fm1_sync STATIC sync/Fm1Codec.cpp sync/Fm1Seq.cpp sync/Fm1Record.cpp)
target_include_directories(fm1_sync PUBLIC sync)
target_compile_options(fm1_sync PRIVATE -Wall -Wextra)

# ---- tests ----------------------------------------------------------------------
enable_testing()
add_executable(codec_test tests/codec_test.cpp)
target_link_libraries(codec_test PRIVATE fm1_sync)
add_test(NAME codec_test COMMAND codec_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

add_executable(engine_test tests/engine_test.cpp)
target_link_libraries(engine_test PRIVATE fm1_engine fm1_sync)
add_test(NAME engine_test COMMAND engine_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

# ---- plugin ---------------------------------------------------------------------
if(FM1_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)

    set(FM1_FORMATS VST3 Standalone)
    if(FM1_BUILD_AU AND APPLE)
        list(APPEND FM1_FORMATS AU)
    endif()

    file(GLOB FM1_ASSETS ${CMAKE_SOURCE_DIR}/plugin/dexed_ui/assets/*)
    juce_add_binary_data(FM1Assets HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES ${FM1_ASSETS})

    juce_add_plugin(FM1Companion
        COMPANY_NAME "Jacob Schooley"
        BUNDLE_ID "com.schooley.fm1companion"
        PLUGIN_MANUFACTURER_CODE Jscn
        PLUGIN_CODE Fm1c
        FORMATS ${FM1_FORMATS}
        PRODUCT_NAME "FM-1 Companion"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD ${FM1_COPY_PLUGIN}
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(FM1Companion PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/Panels.cpp
        plugin/Params.cpp
        plugin/Effects.cpp
        plugin/Sequencer.cpp
        plugin/Arpeggiator.cpp
        plugin/BankModel.cpp
        plugin/dexed_ui/DXLookNFeel.cpp
        plugin/dexed_ui/DXComponents.cpp
        plugin/dexed_ui/AlgoDisplay.cpp
        plugin/dexed_ui/OperatorPanel.cpp
        plugin/dexed_ui/GlobalPanel.cpp
        sync/Fm1Link.cpp
        sync/Fm1Session.cpp)

    target_include_directories(FM1Companion PRIVATE plugin plugin/dexed_ui sync engine)

    target_compile_definitions(FM1Companion PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(FM1Companion PRIVATE
        fm1_engine fm1_sync FM1Assets
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    # end-to-end sync test over virtual CoreMIDI ports (no hardware; macOS only, JUCE cannot create virtual ports on Windows)
    if(APPLE)
    juce_add_console_app(sync_test PRODUCT_NAME "sync_test")
    target_sources(sync_test PRIVATE tests/sync_test.cpp sync/Fm1Link.cpp sync/Fm1Session.cpp)
    target_include_directories(sync_test PRIVATE sync)
    target_compile_definitions(sync_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(sync_test PRIVATE fm1_sync juce::juce_audio_devices juce::juce_events
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME sync_test COMMAND sync_test ${CMAKE_SOURCE_DIR}/tests/golden.json)
    endif()

    # sequencer and arpeggiator timing
    juce_add_console_app(seq_test PRODUCT_NAME "seq_test")
    target_sources(seq_test PRIVATE tests/seq_test.cpp plugin/Sequencer.cpp plugin/Arpeggiator.cpp)
    target_include_directories(seq_test PRIVATE sync plugin)
    target_compile_definitions(seq_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(seq_test PRIVATE fm1_sync juce::juce_audio_processors juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME seq_test COMMAND seq_test)
endif()
