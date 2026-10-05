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
set(FM1_SANITIZE "" CACHE STRING "Build with sanitizers for testing, e.g. address,undefined or thread (not for release builds)")
if(FM1_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=${FM1_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${FM1_SANITIZE})
endif()
option(FM1_BUILD_TOOLS "Build fm1_probe, the check against a connected FM-1 (CI does not need it)" ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
endif()

# ---- the DX7 engine: msfa (Apache-2.0, see engines/dx7/msfa/NOTICE.md) ------------
add_library(fm1_engine STATIC
    engines/dx7/msfa/dx7note.cc engines/dx7/msfa/env.cc engines/dx7/msfa/exp2.cc engines/dx7/msfa/fm_core.cc
    engines/dx7/msfa/fm_op_kernel.cc engines/dx7/msfa/freqlut.cc engines/dx7/msfa/lfo.cc engines/dx7/msfa/pitchenv.cc
    engines/dx7/msfa/porta.cpp engines/dx7/msfa/sin.cc engines/dx7/msfa/tuning.cc
    engines/dx7/FmSynth.cpp)
target_include_directories(fm1_engine PUBLIC engines/dx7)
if(NOT MSVC)
    target_compile_options(fm1_engine PRIVATE -O3 -Wno-deprecated-declarations)
endif()

# ---- sync codec (no JUCE) -----------------------------------------------------
add_library(fm1_sync STATIC
    firmwares/fm1_common/Fm1Codec.cpp firmwares/fm1_common/Fm1Seq.cpp
    firmwares/fm1_common/Fm1Record.cpp firmwares/fm1_common/Fm1Edit.cpp)
target_include_directories(fm1_sync PUBLIC firmwares/fm1_common)

# talking to a synth: the MIDI link and session (core/) and one profile per firmware (firmwares/)
set(FM1_DEVICE_SOURCES
    core/Fm1Link.cpp core/Fm1Session.cpp core/Firmware.cpp
    firmwares/Firmwares.cpp
    firmwares/fm1_stock/StockFirmware.cpp
    firmwares/baudgirl_fm1va/FmVaFirmware.cpp
    firmwares/felucca/FeluccaFirmware.cpp
    firmwares/felucca/FeluccaSync.cpp)
set(FM1_DEVICE_INCLUDES core firmwares firmwares/felucca)
if(NOT MSVC)
    target_compile_options(fm1_sync PRIVATE -Wall -Wextra)
endif()

# ---- Felucca's engines (engines/felucca: Felucca 1.0, GPL-3.0-only) -------------
# Felucca's synth is one compilation unit with all its state in file-level statics, so
# one compiled copy plays one instance. felucca_core.c is compiled FELUCCA_COPIES times,
# each copy with its own symbol prefix; an instance takes a free copy while it plays
# Felucca (FeluccaEngine). Each copy keeps its writable state in sections of its own,
# so that a copy can be put back to how it started before the next instance takes it;
# that uses Clang's section pragma, so Felucca is built only with Clang (macOS and iOS
# always; Linux with CC=clang). MSVC also cannot parse Felucca's inline assembly.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    set(FM1_FELUCCA_DEFAULT ON)
else()
    set(FM1_FELUCCA_DEFAULT OFF)
endif()
option(FM1_BUILD_FELUCCA "Build Felucca's engines into the plugin (needs Clang)" ${FM1_FELUCCA_DEFAULT})
if(FM1_BUILD_FELUCCA AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "Felucca's engines need Clang (CC=clang); building without them")
    set(FM1_BUILD_FELUCCA OFF)
endif()
set(FELUCCA_COPIES 8 CACHE STRING "How many instances can play Felucca at once")
if(FM1_BUILD_FELUCCA)
    math(EXPR felLast "${FELUCCA_COPIES} - 1")
    set(felDecl "")
    set(felTable "")
    set(felObjects "")
    foreach(i RANGE 0 ${felLast})
        add_library(felucca_core_${i} OBJECT engines/felucca/felucca_core.c)
        # the copy's own sections, and the linker's symbols for where they start and stop
        if(APPLE)
            set(felB "__DATA,__felb${i}")
            set(felD "__DATA,__feld${i}")
            set(felBS "section$start$__DATA$__felb${i}")
            set(felBE "section$end$__DATA$__felb${i}")
            set(felDS "section$start$__DATA$__feld${i}")
            set(felDE "section$end$__DATA$__feld${i}")
        else()
            set(felB "felb${i}")
            set(felD "feld${i}")
            set(felBS "__start_felb${i}")
            set(felBE "__stop_felb${i}")
            set(felDS "__start_feld${i}")
            set(felDE "__stop_feld${i}")
        endif()
        target_compile_definitions(felucca_core_${i} PRIVATE FEL_PREFIX=fel${i}_
            "FEL_BSS_SECTION=\"${felB}\"" "FEL_DATA_SECTION=\"${felD}\""
            "FEL_BSS_START=\"${felBS}\"" "FEL_BSS_STOP=\"${felBE}\""
            "FEL_DATA_START=\"${felDS}\"" "FEL_DATA_STOP=\"${felDE}\"")
        target_include_directories(felucca_core_${i} PRIVATE engines/felucca/generated engines/felucca)
        set_target_properties(felucca_core_${i} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        target_compile_options(felucca_core_${i} PRIVATE -w -O2)   # Felucca's own code, as upstream builds it
        string(APPEND felDecl "#define FEL_PREFIX fel${i}_\n#include \"felucca_core_api.h\"\n#undef FEL_PREFIX\n")
        string(APPEND felTable "#define FEL_PREFIX fel${i}_\n    { FELUCCA_API(FEL_ENTRY) },\n#undef FEL_PREFIX\n")
        list(APPEND felObjects $<TARGET_OBJECTS:felucca_core_${i}>)
    endforeach()
    file(WRITE ${CMAKE_BINARY_DIR}/felucca/felucca_copies.inc.new
         "// written by CMakeLists.txt: the ${FELUCCA_COPIES} copies of felucca_core.c\n"
         "#ifdef FELUCCA_COPIES_DECLARE\n${felDecl}#endif\n"
         "#ifdef FELUCCA_COPIES_TABLE\n${felTable}#endif\n")
    configure_file(${CMAKE_BINARY_DIR}/felucca/felucca_copies.inc.new ${CMAKE_BINARY_DIR}/felucca/felucca_copies.inc COPYONLY)
    add_library(fm1_felucca STATIC engines/felucca/FeluccaEngine.cpp ${felObjects})
    target_include_directories(fm1_felucca PUBLIC engines/felucca PRIVATE ${CMAKE_BINARY_DIR}/felucca)
    target_compile_definitions(fm1_felucca PUBLIC FM1_FELUCCA=1)
endif()

# ---- tests (desktop only) -------------------------------------------------------
enable_testing()
if(NOT IOS)
add_executable(codec_test tests/codec_test.cpp)
target_link_libraries(codec_test PRIVATE fm1_sync)
add_test(NAME codec_test COMMAND codec_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

add_executable(engine_test tests/engine_test.cpp)
target_link_libraries(engine_test PRIVATE fm1_engine fm1_sync)
target_include_directories(engine_test PRIVATE engines/fm1_fx)   # HardwareCharacter.h
add_test(NAME engine_test COMMAND engine_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

if(FM1_BUILD_FELUCCA)
    # Felucca's own regression (87 golden renders) on the vendored sources and generated tables
    if(NOT WIN32)
        add_executable(felucca_regress engines/felucca/upstream/tests/regress.c)
        target_include_directories(felucca_regress PRIVATE engines/felucca/generated engines/felucca/upstream/firmware/src)
        target_compile_options(felucca_regress PRIVATE -w -O2)
        target_link_libraries(felucca_regress PRIVATE m)
        add_test(NAME felucca_regress COMMAND felucca_regress ${CMAKE_SOURCE_DIR}/engines/felucca/upstream/tests/golden.txt no-cpu-budget)
    endif()
    # the copies: independent, and playing
    add_executable(felucca_test tests/felucca_test.cpp)
    target_link_libraries(felucca_test PRIVATE fm1_felucca)
    add_test(NAME felucca_test COMMAND felucca_test)
endif()
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
        engines/fm1_fx/Effects.cpp
        plugin/Sequencer.cpp
        plugin/SeqMidi.cpp
        plugin/Arpeggiator.cpp
        plugin/BankModel.cpp
        plugin/LibraryStore.cpp
        plugin/FeluccaPanel.cpp
        plugin/FeluccaParams.cpp
        plugin/FeluccaDevice.cpp
        plugin/dexed_ui/DXLookNFeel.cpp
        plugin/dexed_ui/DXComponents.cpp
        plugin/dexed_ui/AlgoDisplay.cpp
        plugin/dexed_ui/OperatorPanel.cpp
        plugin/dexed_ui/GlobalPanel.cpp
        ${FM1_DEVICE_SOURCES})

    target_include_directories(VirtualFM1 PRIVATE plugin plugin/dexed_ui engines/fm1_fx ${FM1_DEVICE_INCLUDES})

    target_compile_definitions(VirtualFM1 PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(VirtualFM1 PRIVATE
        fm1_engine fm1_sync FM1Assets
        $<$<BOOL:${FM1_BUILD_FELUCCA}>:fm1_felucca>
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
    target_sources(sync_test PRIVATE tests/sync_test.cpp ${FM1_DEVICE_SOURCES})
    target_include_directories(sync_test PRIVATE ${FM1_DEVICE_INCLUDES})
    target_link_libraries(sync_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME sync_test COMMAND sync_test ${CMAKE_SOURCE_DIR}/tests/golden.json)

    # hardware probe (not a ctest: needs a connected FM-1)
    if(FM1_BUILD_TOOLS)
    juce_add_console_app(fm1_probe PRODUCT_NAME "fm1_probe")
    target_sources(fm1_probe PRIVATE tests/fm1_probe.cpp ${FM1_DEVICE_SOURCES})
    target_include_directories(fm1_probe PRIVATE ${FM1_DEVICE_INCLUDES})
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
    # the loaded plugin keeps its library here, not in the user's
    set_tests_properties(host_test PROPERTIES ENVIRONMENT "FM1_DATA_DIR=${CMAKE_BINARY_DIR}/test-data;FM1_NO_DEVICE=1")
    if(FM1_BUILD_LV2 AND UNIX AND NOT APPLE)
        # LV2 hosts find plugins by URI on LV2_PATH
        add_dependencies(host_test VirtualFM1_LV2)
        add_test(NAME host_test_lv2 COMMAND host_test "urn:bockage:virtualfm1" 2)
        set_tests_properties(host_test_lv2 PROPERTIES ENVIRONMENT "LV2_PATH=${CMAKE_BINARY_DIR}/VirtualFM1_artefacts/$<CONFIG>/LV2;FM1_DATA_DIR=${CMAKE_BINARY_DIR}/test-data;FM1_NO_DEVICE=1")
    endif()

    # the whole plugin without a host: renders to compare builds, saved states that must keep loading
    add_executable(plugin_test tests/plugin_test.cpp)
    # the shared code's definitions, less those that say which plugin wrapper is being built
    target_compile_definitions(plugin_test PRIVATE
        $<FILTER:$<TARGET_PROPERTY:VirtualFM1,COMPILE_DEFINITIONS>,EXCLUDE,^JucePlugin_Build_|^JUCE_SHARED_CODE>)
    target_include_directories(plugin_test PRIVATE $<TARGET_PROPERTY:VirtualFM1,INCLUDE_DIRECTORIES>)
    # fm1_felucca directly too: its FM1_FELUCCA definition changes FM1Processor's members
    target_link_libraries(plugin_test PRIVATE VirtualFM1 $<$<BOOL:${FM1_BUILD_FELUCCA}>:fm1_felucca> juce::juce_recommended_config_flags)
    add_test(NAME state_test COMMAND plugin_test state-check ${CMAKE_SOURCE_DIR}/tests/states/v3 ${CMAKE_SOURCE_DIR}/tests/states/v4)
    add_test(NAME plugin_checks COMMAND plugin_test checks)
    add_test(NAME library_test COMMAND plugin_test library ${CMAKE_SOURCE_DIR}/tests/golden.json)
    add_test(NAME render_smoke COMMAND plugin_test render ${CMAKE_SOURCE_DIR}/tests/golden.json ${CMAKE_BINARY_DIR}/render.txt)

    # editor parameters: load/commit round trip
    juce_add_console_app(params_test PRODUCT_NAME "params_test")
    target_sources(params_test PRIVATE tests/params_test.cpp plugin/Params.cpp plugin/FeluccaParams.cpp)
    target_include_directories(params_test PRIVATE plugin)
    target_link_libraries(params_test PRIVATE fm1_sync fm1_test_juce)
    target_compile_definitions(params_test PRIVATE FM1_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME params_test COMMAND params_test)

    # sequencer and arpeggiator timing
    juce_add_console_app(seq_test PRODUCT_NAME "seq_test")
    target_sources(seq_test PRIVATE tests/seq_test.cpp plugin/Sequencer.cpp plugin/SeqMidi.cpp plugin/Arpeggiator.cpp)
    target_include_directories(seq_test PRIVATE plugin)
    target_link_libraries(seq_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME seq_test COMMAND seq_test)

    # presets and patterns as JSON: exact round trips, single-byte edits, error places
    juce_add_console_app(json_test PRODUCT_NAME "json_test")
    target_sources(json_test PRIVATE tests/json_test.cpp plugin/Fm1Json.cpp)
    target_include_directories(json_test PRIVATE plugin)
    target_link_libraries(json_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME json_test COMMAND json_test ${CMAKE_SOURCE_DIR}/tests/golden.json)
    endif()
endif()
