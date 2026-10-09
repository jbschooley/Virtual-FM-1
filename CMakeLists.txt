cmake_minimum_required(VERSION 3.22)
# these must be set before project() to take effect
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(VirtualFM1 VERSION 0.7.0 LANGUAGES C CXX)

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
    firmwares/felucca/FeluccaSync.cpp
    firmwares/felucca/FeluccaSeq.cpp)
set(FM1_DEVICE_INCLUDES core firmwares firmwares/felucca)
if(NOT MSVC)
    target_compile_options(fm1_sync PRIVATE -Wall -Wextra)
endif()

# ---- Felucca's engines (engines/felucca: Felucca 1.0, GPL-3.0-only) -------------
# Each firmware (Felucca, SLOOP, Melodee) is one compilation unit with all its state in
# file-level statics. It is compiled once, and engines/felucca/fel_state.py moves that state into
# a block each instance owns (felucca_core.h bind / state_size / state_init): one copy of the code,
# any number of independent instances. That uses Clang's section pragma and LLVM IR, so the
# firmwares are built only with Clang (macOS and iOS always; Linux with CC=clang; Android's NDK;
# Windows with clang-cl: Visual Studio's ClangCL toolset, -T ClangCL), and with Python 3.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(FM1_FELUCCA_DEFAULT ON)
else()
    set(FM1_FELUCCA_DEFAULT OFF)
endif()
option(FM1_BUILD_FELUCCA "Build Felucca's engines into the plugin (needs Clang)" ${FM1_FELUCCA_DEFAULT})
if(FM1_BUILD_FELUCCA AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "Felucca's engines need Clang (CC=clang); building without them")
    set(FM1_BUILD_FELUCCA OFF)
endif()
if(FM1_BUILD_FELUCCA)
    find_package(Python3 COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        message(WARNING "Felucca's engines need Python 3 (engines/felucca/fel_state.py); building without them")
        set(FM1_BUILD_FELUCCA OFF)
    endif()
endif()
# SLOOP (engines/sloop: SLOOP 2.4.1) and Melodee (engines/melodee: Melodee 0.13), both built from
# Felucca and GPL-3.0-only, are built the same way, behind the same API, into the same library.

# fm1_core(<kind> <tag>): engines/<kind>/<kind>_core.c compiled once through fel_state_build.py
# (frontend IR, fel_state.py, the object), its functions prefixed <tag>_; <kind>_core.inc declares
# them and lists them (FeluccaEngine.cpp). Its sample sets (SMP_DATA) and empty user sample slots,
# if it has them, are compiled apart in <kind>_shared.c (<tag>_smp_data, <tag>_no_samples), read
# only, so not state; the core includes a felucca_samples.h written here from the vendored one, with
# SMP_DATA declared, not defined (its contents left as an unused array the compiler drops).
# Sets <kind>Objects in the caller.
function(fm1_core kind tag)
    set(samplesIn ${CMAKE_SOURCE_DIR}/engines/${kind}/generated/felucca_samples.h)
    if(NOT EXISTS ${samplesIn})   # (Melodee: no samples, no user sample slots)
        file(WRITE ${CMAKE_BINARY_DIR}/${kind}/shared/${kind}_shared.c
             "/* written by CMakeLists.txt: ${kind} has no sample sets */\nconst int ${tag}_shared_none = 0;\n")
    else()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${samplesIn})
        file(READ ${samplesIn} smp)
        set(smpDecl "static const uint8_t SMP_DATA[")
        string(FIND "${smp}" "${smpDecl}" at)
        if(at LESS 0)
            message(FATAL_ERROR "${kind}: felucca_samples.h has no SMP_DATA array (a new generator? see CMakeLists.txt)")
        endif()
        string(SUBSTRING "${smp}" ${at} -1 tail)
        string(FIND "${tail}" "};" arrEnd)
        math(EXPR arrEnd "${arrEnd} + 2")
        string(SUBSTRING "${tail}" 0 ${arrEnd} array)
        string(REPLACE "${smpDecl}" "const uint8_t ${tag}_smp_data[" array "${array}")
        if(kind STREQUAL "sloop")
            set(userSlots 4)   # (SLOOP 2.4: USR1-4; Felucca: 3)
        else()
            set(userSlots 3)
        endif()
        file(WRITE ${CMAKE_BINARY_DIR}/${kind}/shared/${kind}_shared.c
             "/* written by CMakeLists.txt: what the ${kind} core reads and never writes (not its state) */\n#include <stdint.h>\n"
             "const uint8_t ${tag}_no_samples[${userSlots}][0x14000] = {{0}};\n${array}\n")
        string(REPLACE "${smpDecl}" "extern const uint8_t ${tag}_smp_data[];\n#define SMP_DATA ${tag}_smp_data\nstatic const uint8_t ${tag}_smp_data_unused[" header "${smp}")
        file(WRITE ${CMAKE_BINARY_DIR}/${kind}/shared/felucca_samples.h "${header}")
    endif()
    set(includes ${CMAKE_BINARY_DIR}/${kind}/shared ${CMAKE_SOURCE_DIR}/engines/${kind}/generated
                 ${CMAKE_SOURCE_DIR}/engines/felucca ${CMAKE_SOURCE_DIR}/engines/${kind})
    set(args --cc ${FEL_CLANG} --src ${CMAKE_SOURCE_DIR}/engines/${kind}/${kind}_core.c --prefix ${tag}_
             -DFEL_PREFIX=${tag}_)
    foreach(d ${includes})
        list(APPEND args -I${d})
    endforeach()
    # the pragma's marks (only their presence matters: fel_state.py takes the marked variables out)
    if(APPLE)
        list(APPEND args "-DFEL_BSS_SECTION=\"__DATA,__felbss\"" "-DFEL_DATA_SECTION=\"__DATA,__feldata\"")
    else()
        list(APPEND args "-DFEL_BSS_SECTION=\"felbss\"" "-DFEL_DATA_SECTION=\"feldata\"")
    endif()
    if(WIN32)
        list(APPEND args --target ${felTarget})
    elseif(APPLE)
        if(NOT CMAKE_GENERATOR STREQUAL "Xcode")   # (Xcode: its environment says, per build)
            set(archs ${CMAKE_OSX_ARCHITECTURES})
            if(NOT archs)
                set(archs ${CMAKE_HOST_SYSTEM_PROCESSOR})
            endif()
            foreach(a ${archs})
                list(APPEND args --arch ${a})
            endforeach()
            if(CMAKE_OSX_SYSROOT)
                list(APPEND args --isysroot ${CMAKE_OSX_SYSROOT})
            endif()
            if(CMAKE_OSX_DEPLOYMENT_TARGET)
                list(APPEND args --min ${CMAKE_OSX_DEPLOYMENT_TARGET})
            endif()
        endif()
    else()   # Linux, Android: into a shared library
        list(APPEND args --pic)
        if(CMAKE_C_COMPILER_TARGET)
            list(APPEND args --target ${CMAKE_C_COMPILER_TARGET})
        endif()
        if(CMAKE_SYSROOT)
            list(APPEND args --sysroot ${CMAKE_SYSROOT})
        endif()
    endif()
    # (a multi-configuration generator: one per configuration and, in Xcode, per SDK)
    set(dir ${CMAKE_BINARY_DIR}/${kind}/core/${CMAKE_CFG_INTDIR})
    set(obj ${dir}/${kind}_core${CMAKE_C_OUTPUT_EXTENSION})
    add_custom_command(OUTPUT ${obj}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/engines/felucca/fel_state_build.py ${args}
                --out ${obj} --work ${dir}/work --depfile ${dir}/${kind}_core.d
        DEPENDS engines/${kind}/${kind}_core.c engines/felucca/fel_state.py engines/felucca/fel_state_build.py
        DEPFILE ${dir}/${kind}_core.d
        COMMENT "${kind}: one core, its state per instance (fel_state.py)"
        VERBATIM)
    set_source_files_properties(${obj} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    file(WRITE ${CMAKE_BINARY_DIR}/${kind}/${kind}_core.inc.new
         "// written by CMakeLists.txt: ${kind}_core.c's functions\n"
         "#ifdef FELUCCA_CORE_DECLARE\n#define FEL_PREFIX ${tag}_\n#include \"felucca_core_api.h\"\n#undef FEL_PREFIX\n#endif\n"
         "#ifdef FELUCCA_CORE_TABLE\n#define FEL_PREFIX ${tag}_\n{ FELUCCA_API(FEL_ENTRY) }\n#undef FEL_PREFIX\n#endif\n")
    configure_file(${CMAKE_BINARY_DIR}/${kind}/${kind}_core.inc.new ${CMAKE_BINARY_DIR}/${kind}/${kind}_core.inc COPYONLY)
    set(${kind}Objects ${obj} ${CMAKE_BINARY_DIR}/${kind}/shared/${kind}_shared.c PARENT_SCOPE)
endfunction()

if(FM1_BUILD_FELUCCA)
    if(MSVC)   # clang-cl: the clang driver beside it, for fel_state_build.py
        get_filename_component(felClangDir "${CMAKE_C_COMPILER}" DIRECTORY)
        find_program(FEL_CLANG NAMES clang clang.exe HINTS "${felClangDir}" NO_DEFAULT_PATH)
        if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
            set(felTarget aarch64-pc-windows-msvc)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(felTarget i686-pc-windows-msvc)
        else()
            set(felTarget x86_64-pc-windows-msvc)
        endif()
        if(NOT FEL_CLANG)
            message(FATAL_ERROR "Felucca on Windows: no clang beside ${CMAKE_C_COMPILER}")
        endif()
    else()
        set(FEL_CLANG "${CMAKE_C_COMPILER}")
        set(felTarget "${CMAKE_C_COMPILER_TARGET}")
    endif()
    fm1_core(felucca fel)
    fm1_core(sloop slp)
    fm1_core(melodee mel)
    add_library(fm1_felucca STATIC engines/felucca/FeluccaEngine.cpp ${feluccaObjects} ${sloopObjects} ${melodeeObjects})
    target_include_directories(fm1_felucca PUBLIC engines/felucca PRIVATE ${CMAKE_BINARY_DIR}/felucca ${CMAKE_BINARY_DIR}/sloop ${CMAKE_BINARY_DIR}/melodee)
    target_compile_definitions(fm1_felucca PUBLIC FM1_FELUCCA=1)
endif()

# ---- tests (desktop only) -------------------------------------------------------
enable_testing()
if(NOT IOS AND NOT ANDROID)
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
        # Sloop's (engines/sloop: Sloop 2.4.1, a fork of Felucca): its golden renders. Its CPU
        # budget is measured per host, so it is given no baseline (a missing file: notes only)
        add_executable(sloop_regress engines/sloop/upstream/tests/regress.c)
        target_include_directories(sloop_regress PRIVATE engines/sloop/generated engines/sloop/upstream/firmware/src)
        target_compile_options(sloop_regress PRIVATE -w -O2)
        target_link_libraries(sloop_regress PRIVATE m)
        add_test(NAME sloop_regress COMMAND sloop_regress ${CMAKE_SOURCE_DIR}/engines/sloop/upstream/tests/golden.txt no-cpu-budget)
        # Melodee's (engines/melodee: Melodee 0.13, from Felucca 1.0): 340 golden renders, no CPU baseline
        add_executable(melodee_regress engines/melodee/upstream/tests/regress.c)
        target_include_directories(melodee_regress PRIVATE engines/melodee/generated engines/melodee/upstream/firmware/src)
        target_compile_options(melodee_regress PRIVATE -w -O2)
        target_link_libraries(melodee_regress PRIVATE m)
        add_test(NAME melodee_regress COMMAND melodee_regress ${CMAKE_SOURCE_DIR}/engines/melodee/upstream/tests/golden.txt no-cpu-budget)
    endif()
    # the copies: independent, and playing
    add_executable(felucca_test tests/felucca_test.cpp)
    target_link_libraries(felucca_test PRIVATE fm1_felucca)
    target_compile_definitions(felucca_test PRIVATE FELUCCA_FROZEN="${CMAKE_SOURCE_DIR}/tests/felucca-frozen.txt")
    add_test(NAME felucca_test COMMAND felucca_test)
    add_executable(sloop_test tests/sloop_test.cpp)
    target_link_libraries(sloop_test PRIVATE fm1_felucca)
    target_compile_definitions(sloop_test PRIVATE SLOOP_FROZEN="${CMAKE_SOURCE_DIR}/tests/sloop-frozen.txt")
    add_test(NAME sloop_test COMMAND sloop_test)
    add_executable(melodee_test tests/melodee_test.cpp)
    target_link_libraries(melodee_test PRIVATE fm1_felucca)
    target_compile_definitions(melodee_test PRIVATE MELODEE_FROZEN="${CMAKE_SOURCE_DIR}/tests/melodee-frozen.txt")
    add_test(NAME melodee_test COMMAND melodee_test)
endif()
endif()

# ---- plugin ---------------------------------------------------------------------
if(FM1_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)

    set(FM1_FORMATS VST3 Standalone)
    if(IOS)
        set(FM1_FORMATS Standalone AUv3)
    elseif(ANDROID)   # the app only (built by android/'s Gradle project)
        set(FM1_FORMATS Standalone)
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

    # iOS rounds an app's icon itself and shows any transparency as white: its icon fills the square
    if(IOS)
        set(fm1Icon icon-ios)
    else()
        set(fm1Icon icon)
    endif()
    juce_add_plugin(VirtualFM1
        COMPANY_NAME "Bockage"
        BUNDLE_ID "com.bockage.virtualfm1"
        # the AU/VST3 identity codes stay as first released, so saved host projects still find the plugin
        PLUGIN_MANUFACTURER_CODE Jscn
        PLUGIN_CODE Fm1c
        FORMATS ${FM1_FORMATS}
        PRODUCT_NAME "Virtual FM-1"
        ICON_BIG "${CMAKE_SOURCE_DIR}/assets/${fm1Icon}-1024.png"
        ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/${fm1Icon}-256.png"
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
        plugin/FeluccaSeqPanel.cpp
        plugin/FeluccaMidi.cpp
        plugin/ChosenFile.cpp
        plugin/IconButton.cpp
        plugin/Fm1SeqPanel.cpp
        plugin/FeluccaParams.cpp
        plugin/FeluccaDevice.cpp
        plugin/dexed_ui/DXLookNFeel.cpp
        plugin/dexed_ui/DXComponents.cpp
        plugin/dexed_ui/AlgoDisplay.cpp
        plugin/dexed_ui/OperatorPanel.cpp
        plugin/dexed_ui/GlobalPanel.cpp
        ${FM1_DEVICE_SOURCES})

    if(IOS)
        target_sources(VirtualFM1 PRIVATE plugin/SafeArea_ios.mm)
    endif()
    if(ANDROID)   # JUCE's Java side loads the app as libjuce_jni.so, and finds it by its exported
                  # juce_CreateApplication (so not hidden, as JUCE's own Android exporter has it)
        set_target_properties(VirtualFM1_Standalone PROPERTIES OUTPUT_NAME juce_jni
            C_VISIBILITY_PRESET default CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
    endif()

    target_include_directories(VirtualFM1 PRIVATE plugin plugin/dexed_ui engines/fm1_fx ${FM1_DEVICE_INCLUDES})

    # (no WebP images: JUCE's libwebp SSE4.1 code does not compile with clang-cl 19)
    target_compile_definitions(VirtualFM1 PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_WEBP=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(VirtualFM1 PRIVATE
        fm1_engine fm1_sync FM1Assets
        $<$<BOOL:${FM1_BUILD_FELUCCA}>:fm1_felucca>
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    # Link-time optimisation where the linker reads what the compiler leaves: Apple's, and GCC's
    # with its own plugin. Not with Clang on Linux (GNU ld reads LLVM bitcode only when LLVM's
    # plugin is installed for it, which GitHub's runner lacks) nor clang-cl (Microsoft's linker
    # cannot read it).
    if(APPLE OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_link_libraries(VirtualFM1 PRIVATE juce::juce_recommended_lto_flags)
    endif()

    if(NOT IOS AND NOT ANDROID)
    # The JUCE modules the test programs use, compiled once into one library they
    # all link, rather than once per program. The tests share these JUCE settings.
    add_library(fm1_test_juce STATIC)
    target_link_libraries(fm1_test_juce
        PRIVATE juce::juce_audio_processors juce::juce_audio_devices juce::juce_audio_formats
                juce::juce_gui_basics juce::juce_events juce::juce_core
        PUBLIC juce::juce_recommended_config_flags)
    target_compile_definitions(fm1_test_juce
        PUBLIC JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WEBP=0 JUCE_MODAL_LOOPS_PERMITTED=1
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
    target_link_libraries(fm1_probe PRIVATE fm1_sync fm1_engine fm1_test_juce $<$<BOOL:${FM1_BUILD_FELUCCA}>:fm1_felucca>)
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
    if(MSVC)   # its checks keep whole processors and banks in one function: macOS's 8 MB stack, not Windows' 1 MB
        target_link_options(plugin_test PRIVATE /STACK:8388608)
    endif()
    # the shared code's definitions, less those that say which plugin wrapper is being built
    target_compile_definitions(plugin_test PRIVATE
        $<FILTER:$<TARGET_PROPERTY:VirtualFM1,COMPILE_DEFINITIONS>,EXCLUDE,^JucePlugin_Build_|^JUCE_SHARED_CODE>)
    target_include_directories(plugin_test PRIVATE $<TARGET_PROPERTY:VirtualFM1,INCLUDE_DIRECTORIES>)
    # fm1_felucca directly too: its FM1_FELUCCA definition changes FM1Processor's members
    target_link_libraries(plugin_test PRIVATE VirtualFM1 $<$<BOOL:${FM1_BUILD_FELUCCA}>:fm1_felucca> juce::juce_recommended_config_flags)
    add_test(NAME state_test COMMAND plugin_test state-check ${CMAKE_SOURCE_DIR}/tests/states/v3 ${CMAKE_SOURCE_DIR}/tests/states/v4)
    add_test(NAME plugin_checks COMMAND plugin_test checks)
    add_test(NAME library_test COMMAND plugin_test library ${CMAKE_SOURCE_DIR}/tests/golden.json)
    if(FM1_BUILD_FELUCCA)
        add_test(NAME felucca_library_test COMMAND plugin_test felucca-library)
    endif()
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
    target_compile_definitions(seq_test PRIVATE FM1_096_STEPS="${CMAKE_SOURCE_DIR}/tests/fm1-096-steps.txt")
    add_test(NAME seq_test COMMAND seq_test)

    # presets and patterns as JSON: exact round trips, single-byte edits, error places
    juce_add_console_app(json_test PRODUCT_NAME "json_test")
    target_sources(json_test PRIVATE tests/json_test.cpp plugin/Fm1Json.cpp)
    target_include_directories(json_test PRIVATE plugin)
    target_link_libraries(json_test PRIVATE fm1_sync fm1_test_juce juce::juce_recommended_warning_flags)
    add_test(NAME json_test COMMAND json_test ${CMAKE_SOURCE_DIR}/tests/golden.json)
    endif()
endif()
