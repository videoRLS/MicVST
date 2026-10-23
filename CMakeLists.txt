cmake_minimum_required(VERSION 3.22)
project(MicVST VERSION 1.2.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Quelldateien sind UTF-8 (deutsche Kommentare mit Umlauten). /utf-8 lässt MSVC sie
# korrekt parsen und vermeidet C4819-Warnungen.
if(MSVC)
    add_compile_options(/utf-8)
    # Funktionsweise COMDATs (/Gy) + der Linker entfernt Unreferenziertes (/OPT:REF, /OPT:ICF).
    # Nötig, weil JUCEs juce_recommended_config_flags "/Ox" setzen: das tritt im generierten
    # Projekt an die Stelle von CMakes "/O2" - und /Ox enthält, anders als /O2, kein /Gy. Ohne /Gy
    # blieb toter Code im exe, u. a. JUCEs komplette WinINet- (WebInputStream) und Winsock-
    # Implementierung, die MicVST nie aufruft (siehe Kapalı-kutu-Prüfung, docs/RAPOR.md).
    add_compile_options(/Gy)
    add_link_options($<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    # Statische MSVC-Runtime -> voll-portable exe ohne VC++-Redistributable-Abhängigkeit.
    # Gilt auch für JUCE, weil vor FetchContent gesetzt (CMP0091/NEW ab CMake 3.15).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE 9.0.4 (12.10.2026, aktuelle stabile Version): u. a. Fix für ein WASAPI-Start/Stop-Race
# (Callback konnte nach stop() noch laufen), VST3-Hosting-Fixes, Härtung gegen fehlerhafte
# String-Eingaben. Das mitgelieferte VST3 SDK ist 3.8.0 - warum es nicht gegen 3.8.1 getauscht
# wird, steht in docs/RAPOR.md.
# cmake/PatchJuce.cmake setzt drei kleine Echtzeit-Fixes im WASAPI-Backend von JUCE ein und bricht
# das Konfigurieren ab, falls sich JUCE ändert (statt stillschweigend ungepatcht zu bauen).
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuce.cmake
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- LAME 4.0, the MP3 encoder of the recorder (third_party/lame-4.0, LGPL; see MICVST.md there) ---
file(GLOB MICVST_LAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame-4.0/libmp3lame/*.c)
add_library(MicVSTLame STATIC ${MICVST_LAME_SOURCES})
target_include_directories(MicVSTLame
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame-4.0/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame-4.0
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame-4.0/libmp3lame)
target_compile_definitions(MicVSTLame PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    target_compile_options(MicVSTLame PRIVATE /W0)
else()
    target_compile_options(MicVSTLame PRIVATE -w)
    if (NOT WIN32)
        target_link_libraries(MicVSTLame PRIVATE m)
    endif()
endif()

# --- Bildirim sesleri: the built-in mute / unmute sounds (FLAC, in the exe) ---
file(GLOB MICVST_SOUND_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/*.flac)
juce_add_binary_data(MicVSTSounds HEADER_NAME MicVSTSounds.h NAMESPACE MicVSTSoundData SOURCES ${MICVST_SOUND_FILES})

# --- Shared logic library (engine, state, audio helpers) ---
add_library(MicVSTCore STATIC
    src/audio/AudioBridge.cpp
    src/audio/BuiltInAutoGain.cpp
    src/audio/BuiltInGain.cpp
    src/audio/BuiltInEffects.cpp
    src/audio/fx/EqEffects.cpp
    src/audio/fx/ColourEffects.cpp
    src/audio/fx/LevelEffects.cpp
    src/audio/fx/DynamicsEffects.cpp
    src/audio/fx/MeterEffects.cpp
    src/ui/BuiltInEffectEditor.cpp
    src/audio/RealtimeSupport.cpp
    src/audio/DeviceFormat.cpp
    src/audio/Recorder.cpp
    src/audio/CueSounds.cpp
    src/audio/Metering.cpp
    src/audio/GraphConnections.cpp
    src/audio/ScanCoordinator.cpp
    src/state/Persistence.cpp
    src/state/Presets.cpp
    src/state/PluginScanCache.cpp
    src/ui/PluginSearch.cpp
    src/Text.cpp
    src/i18n/Translations.cpp
)
target_compile_features(MicVSTCore PUBLIC cxx_std_17)
# user sounds may be MP3 (Bildirim sesleri): JUCE's decoder, in every target that builds the module
target_compile_definitions(MicVSTCore PUBLIC JUCE_USE_MP3AUDIOFORMAT=1)
target_include_directories(MicVSTCore PUBLIC src)
target_link_libraries(MicVSTCore PUBLIC
    MicVSTLame
    MicVSTSounds
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
)

# --- GUI application ---
juce_add_gui_app(MicVST
    PRODUCT_NAME "MicVST"
    COMPANY_NAME "MicVST"
    VERSION      ${PROJECT_VERSION}
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/appIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/appIconSmall.png"
)

# Beide Icon-Varianten als Binärdaten einbetten (für das theme-adaptive Tray-Icon).
juce_add_binary_data(MicVSTData SOURCES
    resources/iconDarkMode.png
    resources/iconLightMode.png
)

target_sources(MicVST PRIVATE
    src/Main.cpp
    src/audio/AudioEngine.cpp
    src/audio/MonitorOutput.cpp
    src/audio/PluginChain.cpp
    src/ui/MainComponent.cpp
    src/ui/DevicePanel.cpp
    src/ui/PluginListView.cpp
    src/ui/PluginPicker.cpp
    src/ui/LevelMeterComponent.cpp
    src/ui/TrayIcon.cpp
    src/ui/DarkTitleBar.cpp
    src/ui/GlobalHotkeys.cpp
    src/ui/HotkeyDialog.cpp
    src/ui/SettingsComponent.cpp
    src/ui/RecordingPanel.cpp
    src/ui/CuePanel.cpp
    src/state/AutostartRegistry.cpp
)
target_compile_definitions(MicVST PRIVATE
    MICVST_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_WIN_WEBVIEW2=0
)
target_link_libraries(MicVST PRIVATE
    MicVSTCore
    MicVSTData
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- Unit tests (console app, JUCE UnitTestRunner) ---
juce_add_console_app(MicVSTTests PRODUCT_NAME "MicVSTTests")
target_sources(MicVSTTests PRIVATE
    tests/TestMain.cpp
    tests/ScanTests.cpp
    tests/PickerTests.cpp
    tests/BridgeTests.cpp
    tests/TextTests.cpp
    tests/GainTests.cpp
    tests/AutoGainTests.cpp
    tests/EffectTests.cpp
    tests/PresetTests.cpp
    tests/HotkeyTests.cpp
    tests/DeviceFormatTests.cpp
    tests/RecorderTests.cpp
    tests/RecorderSweepTests.cpp
    tests/CounterWrapTests.cpp
    tests/CueTests.cpp
    src/audio/PluginChain.cpp
    src/ui/GlobalHotkeys.cpp
)
target_include_directories(MicVSTTests PRIVATE tests)
target_compile_definitions(MicVSTTests PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
)
target_link_libraries(MicVSTTests PRIVATE
    MicVSTCore
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- Measurements: old vs. new audio path (simulation) + real-thread scheduling on Windows ---
juce_add_console_app(MicVSTStress PRODUCT_NAME "MicVSTStress")
target_sources(MicVSTStress PRIVATE tests/StressMain.cpp)
target_include_directories(MicVSTStress PRIVATE tests)
target_compile_definitions(MicVSTStress PRIVATE JUCE_USE_CURL=0)
target_link_libraries(MicVSTStress PRIVATE
    MicVSTCore
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- UI snapshots (CI): the real windows rendered with the Windows fonts into PNG files, to check
# the Turkish texts (fit, characters). Uses two test devices instead of WASAPI (CI machines have
# no audio hardware): tests/snapshot/audio/MicVSTDeviceManager.h shadows the real one.
juce_add_console_app(MicVSTSnapshot PRODUCT_NAME "MicVSTSnapshot")
target_sources(MicVSTSnapshot PRIVATE
    tests/SnapshotMain.cpp
    src/audio/AudioEngine.cpp
    src/audio/MonitorOutput.cpp
    src/audio/PluginChain.cpp
    src/ui/MainComponent.cpp
    src/ui/DevicePanel.cpp
    src/ui/PluginListView.cpp
    src/ui/PluginPicker.cpp
    src/ui/LevelMeterComponent.cpp
    src/ui/DarkTitleBar.cpp
    src/ui/GlobalHotkeys.cpp
    src/ui/HotkeyDialog.cpp
    src/ui/SettingsComponent.cpp
    src/ui/RecordingPanel.cpp
    src/ui/CuePanel.cpp
    src/state/AutostartRegistry.cpp
)
target_include_directories(MicVSTSnapshot BEFORE PRIVATE tests/snapshot tests)
target_compile_definitions(MicVSTSnapshot PRIVATE
    MICVST_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_WIN_WEBVIEW2=0
)
target_link_libraries(MicVSTSnapshot PRIVATE
    MicVSTCore
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- Engine test (CI): MicVST's real AudioEngine with real threads between virtual devices with
# WASAPI-like timing (tests/enginetest), on Windows with MMCSS. Records everything the virtual
# cable plays: clicks, gaps, latency, chain edits, device changes, unplugging, faulty plug-ins.
juce_add_console_app(MicVSTEngineTest PRODUCT_NAME "MicVSTEngineTest")
target_sources(MicVSTEngineTest PRIVATE
    tests/EngineTestMain.cpp
    tests/enginetest/MonotonicPump.cpp
    src/audio/AudioEngine.cpp
    src/audio/MonitorOutput.cpp
    src/audio/PluginChain.cpp
)
target_include_directories(MicVSTEngineTest BEFORE PRIVATE tests/enginetest tests)
target_compile_definitions(MicVSTEngineTest PRIVATE
    MICVST_VERSION_STRING="${PROJECT_VERSION}"
    MICVST_NEW_ENGINE=1
    MICVST_ENGINE_LABEL="${PROJECT_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_WIN_WEBVIEW2=0
)
target_link_libraries(MicVSTEngineTest PRIVATE
    MicVSTCore
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
