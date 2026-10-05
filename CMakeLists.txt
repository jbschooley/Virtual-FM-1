cmake_minimum_required(VERSION 3.22)
# these must be set before project() to take effect
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(VirtualFM1 VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the engine and sync libraries end up in the plugins, which are shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(FM1_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(FM1_BUILD_AU "Also build the AudioUnit" ON)
option(FM1_BUILD_LV2 "Build the LV2 plugin (Linux)" ON)
option(FM1_BUILD_AAX "Also build the AAX plugin (Pro Tools; macOS and Windows; off until signing is set up, see docs/AAX.md)" OFF)
option(FM1_COPY_PLUGIN "Copy the built plugins into the user plug-in folders" ON)
option(FM1_BUILD_TOOLS "Build fm1_probe, the check against a connected FM-1 (CI does not need it)" ON)

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
if(NOT MSVC)
    target_compile_options(fm1_engine PRIVATE -O3 -Wno-deprecated-declarations)
endif()

# ---- sync codec (no JUCE) -----------------------------------------------------
add_library(fm1_sync STATIC sync/Fm1Codec.cpp sync/Fm1Seq.cpp sync/Fm1Record.cpp sync/Fm1Edit.cpp)
target_include_directories(fm1_sync PUBLIC sync)
if(NOT MSVC)
    target_compile_options(fm1_sync PRIVATE -Wall -Wextra)
endif()

# ---- tests (desktop only) -------------------------------------------------------
enable_testing()
if(NOT IOS)
add_executable(codec_test tests/codec_test.cpp)
target_link_libraries(codec_test PRIVATE fm1_sync)
add_test(NAME codec_test COMMAND codec_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

add_executable(engine_test tests/engine_test.cpp)
target_link_libraries(engine_test PRIVATE fm1_engine fm1_sync)
target_include_directories(engine_test PRIVATE plugin)   # HardwareCharacter.h
add_test(NAME engine_test COMMAND engine_test ${CMAKE_SOURCE_DIR}/tests/golden.json)
endif()

# ---- plugin ---------------------------------------------------------------------
if(FM1_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)

    set(FM1_FORMATS VST3 Standalone)
    if(IOS)
        set(FM1_FORMATS Standalone AUv3)
    elseif(FM1_BUILD_AU AND APPLE)
        list(APPEND FM1_FORMATS AU)
    endif()
    # AAX uses the AAX SDK bundled with JUCE (modules/juce_audio_plugin_client/AAX/SDK),
    # which Avid licenses under the GPLv3 as one option. Unsigned builds load in
    # Pro Tools Developer; other Pro Tools versions need a PACE signature (docs/AAX.md).
    if(FM1_BUILD_AAX AND (APPLE OR WIN32))
        list(APPEND FM1_FORMATS AAX)
    endif()
    if(FM1_BUILD_LV2 AND UNIX AND NOT APPLE)
        list(APPEND FM1_FORMATS LV2)
    endif()

    file(GLOB FM1_ASSETS ${CMAKE_SOURCE_DIR}/plugin/dexed_ui/assets/*)
    juce_add_binary_data(FM1Assets HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES ${FM1_ASSETS})

    juce_add_plugin(VirtualFM1
        COMPANY_NAME "Bockage"
        BUNDLE_ID "com.bockage.virtualfm1"
        # the AU/VST3 identity codes stay as first released, so saved host projects still find the plugin
        PLUGIN_MANUFACTURER_CODE Jscn
        PLUGIN_CODE Fm1c
        FORMATS ${FM1_FORMATS}
        PRODUCT_NAME "Virtual FM-1"
        ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon-1024.png"
        ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icon-256.png"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # iOS app: keeps playing when another app is in front (a sound module for other apps)
        BACKGROUND_AUDIO_ENABLED TRUE
        # Bluetooth MIDI: iOS shows no Bluetooth devices to an app without this permission
        BLUETOOTH_PERMISSION_ENABLED TRUE
        BLUETOOTH_PERMISSION_TEXT "Virtual FM-1 uses Bluetooth to connect to Bluetooth MIDI devices such as the FM-1."
        COPY_PLUGIN_AFTER_BUILD ${FM1_COPY_PLUGIN}
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
        # hosts identify the LV2 plugin by this URI: it must never change
        LV2URI "urn:bockage:virtualfm1")

    target_sources(VirtualFM1 PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/Panels.cpp
        plugin/Params.cpp
        plugin/Fm1Json.cpp
        plugin/Effects.cpp
        plugin/Sequencer.cpp
        plugin/SeqMidi.cpp
        plugin/Arpeggiator.cpp
        plugin/BankModel.cpp
        plugin/dexed_ui/DXLookNFeel.cpp
        plugin/dexed_ui/DXComponents.cpp
        plugin/dexed_ui/AlgoDisplay.cpp
        plugin/dexed_ui/OperatorPanel.cpp
        plugin/dexed_ui/GlobalPanel.cpp
        sync/Fm1Link.cpp
        sync/Fm1Session.cpp sync/Firmware.cpp)

    target_include_directories(VirtualFM1 PRIVATE plugin plugin/dexed_ui sync engine)

    target_compile_definitions(VirtualFM1 PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(VirtualFM1 PRIVATE
        fm1_engine fm1_sync FM1Assets
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    if(NOT IOS)
    # The JUCE modules the test programs use, compiled once into one library they
    # all link, rather than once per program. The tests share these JUCE settings.
    add_library(fm1_test_juce STATIC)
    target_link_libraries(fm1_test_juce
        PRIVATE juce::juce_audio_processors juce::juce_audio_devices juce::juce_audio_formats
                juce::juce_gui_basics juce::juce_events juce::juce_core
        PUBLIC juce::juce_recommended_config_flags)
    target_compile_definitions(fm1_test_juce
        PUBLIC JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
               JUCE_PLUGINHOST_VST3=1 $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1> $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LV2=1>
        INTERFACE $<TARGET_PROPERTY:fm1_test_juce,COMPILE_DEFINITIONS>)
    target_include_directories(fm1_test_juce INTERFACE $<TARGET_PROPERTY:fm1_test_juce,INCLUDE_DIRECTORIES>)
    if(APPLE)
        target_link_libraries(fm1_test_juce PUBLIC "-framework CoreAudioKit")
    endif()

    # end-to-end sync test over virtual CoreMIDI ports (no hardware; macOS only, JUCE cannot create virtual ports on Windows)
    if(APPLE)
    juce_add_console_app(sync_test PRODUCT_NAME "sync_test")
    target_sources(sync_test PRIVATE tests/sync_test.cpp sync/Fm1Link.cpp sync/Fm1Session.cpp sync/Firmware.cpp)
    target_include_directories(sync_test PRIVATE sync)
    target_link_libraries(sync_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME sync_test COMMAND sync_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

    # hardware probe (not a ctest: needs a connected FM-1)
    if(FM1_BUILD_TOOLS)
    juce_add_console_app(fm1_probe PRODUCT_NAME "fm1_probe")
    target_sources(fm1_probe PRIVATE tests/fm1_probe.cpp sync/Fm1Link.cpp sync/Fm1Session.cpp sync/Firmware.cpp)
    target_include_directories(fm1_probe PRIVATE sync)
    target_link_libraries(fm1_probe PRIVATE fm1_sync fm1_engine fm1_test_juce)
    endif()
    endif()

    # loads the built plugin (VST3, and AU on macOS) as a host would, opens and closes
    # the editor, and unloads it; CI also runs it on the installed plugins
    juce_add_console_app(host_test PRODUCT_NAME "host_test")
    target_sources(host_test PRIVATE tests/host_test.cpp)
    target_link_libraries(host_test PRIVATE fm1_test_juce)
    add_dependencies(host_test VirtualFM1_VST3)
    add_test(NAME host_test COMMAND host_test "${CMAKE_BINARY_DIR}/VirtualFM1_artefacts/$<CONFIG>/VST3/Virtual FM-1.vst3" 2)
    if(FM1_BUILD_LV2 AND UNIX AND NOT APPLE)
        # LV2 hosts find plugins by URI on LV2_PATH
        add_dependencies(host_test VirtualFM1_LV2)
        add_test(NAME host_test_lv2 COMMAND host_test "urn:bockage:virtualfm1" 2)
        set_tests_properties(host_test_lv2 PROPERTIES ENVIRONMENT "LV2_PATH=${CMAKE_BINARY_DIR}/VirtualFM1_artefacts/$<CONFIG>/LV2")
    endif()

    # editor parameters: load/commit round trip
    juce_add_console_app(params_test PRODUCT_NAME "params_test")
    target_sources(params_test PRIVATE tests/params_test.cpp plugin/Params.cpp)
    target_include_directories(params_test PRIVATE sync plugin)
    target_link_libraries(params_test PRIVATE fm1_sync fm1_test_juce)
    target_compile_definitions(params_test PRIVATE FM1_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME params_test COMMAND params_test)

    # sequencer and arpeggiator timing
    juce_add_console_app(seq_test PRODUCT_NAME "seq_test")
    target_sources(seq_test PRIVATE tests/seq_test.cpp plugin/Sequencer.cpp plugin/SeqMidi.cpp plugin/Arpeggiator.cpp)
    target_include_directories(seq_test PRIVATE sync plugin)
    target_link_libraries(seq_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME seq_test COMMAND seq_test)

    # presets and patterns as JSON: exact round trips, single-byte edits, error places
    juce_add_console_app(json_test PRODUCT_NAME "json_test")
    target_sources(json_test PRIVATE tests/json_test.cpp plugin/Fm1Json.cpp)
    target_include_directories(json_test PRIVATE sync plugin)
    target_link_libraries(json_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME json_test COMMAND json_test ${CMAKE_SOURCE_DIR}/tests/golden.json)
    endif()
endif()
