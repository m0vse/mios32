cmake_minimum_required(VERSION 3.22)

set(MIOS_STUDIO_BASE_VERSION "2.4.10")
set(MIOS_STUDIO_VERSION_OVERRIDE "" CACHE STRING
    "Version embedded in CI and release builds")
if(MIOS_STUDIO_VERSION_OVERRIDE)
    set(MIOS_STUDIO_BUILD_VERSION "${MIOS_STUDIO_VERSION_OVERRIDE}")
else()
    set(MIOS_STUDIO_BUILD_VERSION "${MIOS_STUDIO_BASE_VERSION}")
endif()

project(MIOS_Studio VERSION "${MIOS_STUDIO_BUILD_VERSION}" LANGUAGES C CXX)

set(JUCE_PATH "" CACHE PATH "Path to a JUCE source checkout")
if(NOT JUCE_PATH AND DEFINED ENV{JUCE_PATH})
    file(TO_CMAKE_PATH "$ENV{JUCE_PATH}" JUCE_PATH)
endif()
if(NOT JUCE_PATH)
    message(FATAL_ERROR "Set JUCE_PATH or the JUCE_PATH environment variable to a JUCE source checkout")
endif()

string(MD5 JUCE_BUILD_ID "${JUCE_PATH}")
set(MIOS_STUDIO_IOS FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MIOS_STUDIO_IOS TRUE)
endif()

if(WIN32)
    include(cmake/PatchJuceWinmm.cmake)
    patch_juce_winmm_sysex_wait("${JUCE_PATH}")
endif()

add_subdirectory("${JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE-${JUCE_BUILD_ID}" EXCLUDE_FROM_ALL)

set(MIOS_STUDIO_APP_OPTIONS)
if(MIOS_STUDIO_IOS)
    list(APPEND MIOS_STUDIO_APP_OPTIONS
        LOCAL_NETWORK_PERMISSION_ENABLED TRUE
        LOCAL_NETWORK_PERMISSION_TEXT "MIOS Studio uses the local network only when OSC is enabled to communicate with MIDIbox devices."
        FILE_SHARING_ENABLED TRUE
        DOCUMENT_BROWSER_ENABLED TRUE
        TARGETED_DEVICE_FAMILY "1,2"
        IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown
        IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)
endif()

juce_add_gui_app(MIOS_Studio
    PRODUCT_NAME "MIOS Studio"
    COMPANY_NAME "MIDIbox"
    BUNDLE_ID "org.midibox.MIOSStudio"
    ${MIOS_STUDIO_APP_OPTIONS}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/MIOS_Studio.iconset/icon_128x128.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icon/MIOS_Studio.iconset/icon_16x16.png")

# JUCE 9.0.0 does not make its generated Windows resource file depend on
# Info.txt.  Without this dependency, changing the release version in an
# existing build tree can leave the executable's file properties stale.
if(WIN32)
    get_target_property(MIOS_STUDIO_GENERATED_SOURCES_DIR
        MIOS_Studio JUCE_GENERATED_SOURCES_DIRECTORY)
    add_custom_command(
        OUTPUT "${MIOS_STUDIO_GENERATED_SOURCES_DIR}/MIOS_Studio_resources.rc"
        APPEND
        DEPENDS "${MIOS_STUDIO_GENERATED_SOURCES_DIR}/Info.txt")
endif()

juce_generate_juce_header(MIOS_Studio)

set(MIOS_STUDIO_DESKTOP_GUI_SOURCES
    src/gui/CommandLineEditor.cpp
    src/gui/ConfigTableComponents.cpp
    src/gui/HexTextEditor.cpp
    src/gui/LogBox.cpp
    src/gui/MbCvTool.cpp
    src/gui/MbhpMfTool.cpp
    src/gui/MidiKeyboard.cpp
    src/gui/MidiMonitor.cpp
    src/gui/Midio128Tool.cpp
    src/gui/MidiSlider.cpp
    src/gui/MiosFileBrowser.cpp
    src/gui/MiosTerminal.cpp
    src/gui/OscMonitor.cpp
    src/gui/OscTextEditor.cpp
    src/gui/OscTool.cpp
    src/gui/SysexLibrarian.cpp
    src/gui/SysexTool.cpp
    src/gui/UploadWindow.cpp)

target_sources(MIOS_Studio PRIVATE
    src/gui/MiosStudio.cpp
    src/HexFileLoader.cpp
    src/Main.cpp
    src/OscHelper.cpp
    src/SysexHelper.cpp
    src/SysexPatchDb.cpp
    src/UdpSocket.cpp
    src/UploadHandler.cpp)

if(NOT MIOS_STUDIO_IOS)
    target_sources(MIOS_Studio PRIVATE ${MIOS_STUDIO_DESKTOP_GUI_SOURCES})
endif()

target_compile_features(MIOS_Studio PRIVATE cxx_std_17)
target_compile_definitions(MIOS_Studio PRIVATE
    MIOS_STUDIO_VERSION="${PROJECT_VERSION}"
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

if(NOT MIOS_STUDIO_IOS)
    target_compile_definitions(MIOS_Studio PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
endif()

if(WIN32)
    # Do not enable JUCE's WinRT MIDI backend here.  Its asynchronous device
    # discovery misses the LPC17 bootloader's short USB enumeration window and
    # does not reliably restore the application connection afterward.  MIOS
    # Studio requires WinMM for firmware handover; PatchJuceWinmm.cmake bounds
    # WinMM's otherwise indefinite SysEx completion wait.
    target_compile_definitions(MIOS_Studio PRIVATE JUCE_USE_WINRT_MIDI=0)
endif()

if(MSVC)
    target_compile_options(MIOS_Studio PRIVATE
        /wd4018
        /wd4100
        /wd4189
        /wd4244)
endif()

target_link_libraries(MIOS_Studio PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)
