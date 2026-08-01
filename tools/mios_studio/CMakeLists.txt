cmake_minimum_required(VERSION 3.22)

project(MIOS_Studio VERSION 2.4.9 LANGUAGES C CXX)

set(JUCE_PATH "" CACHE PATH "Path to a JUCE source checkout")
if(NOT JUCE_PATH AND DEFINED ENV{JUCE_PATH})
    file(TO_CMAKE_PATH "$ENV{JUCE_PATH}" JUCE_PATH)
endif()
if(NOT JUCE_PATH)
    message(FATAL_ERROR "Set JUCE_PATH or the JUCE_PATH environment variable to a JUCE source checkout")
endif()

string(MD5 JUCE_BUILD_ID "${JUCE_PATH}")
add_subdirectory("${JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE-${JUCE_BUILD_ID}" EXCLUDE_FROM_ALL)

juce_add_gui_app(MIOS_Studio
    PRODUCT_NAME "MIOS Studio"
    COMPANY_NAME "MIDIbox"
    BUNDLE_ID "org.midibox.MIOSStudio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icon/MIOS_Studio.iconset/icon_128x128.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/icon/MIOS_Studio.iconset/icon_16x16.png")

juce_generate_juce_header(MIOS_Studio)

target_sources(MIOS_Studio PRIVATE
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
    src/gui/MiosStudio.cpp
    src/gui/MiosTerminal.cpp
    src/gui/OscMonitor.cpp
    src/gui/OscTextEditor.cpp
    src/gui/OscTool.cpp
    src/gui/SysexLibrarian.cpp
    src/gui/SysexTool.cpp
    src/gui/UploadWindow.cpp
    src/HexFileLoader.cpp
    src/Main.cpp
    src/OscHelper.cpp
    src/SysexHelper.cpp
    src/SysexPatchDb.cpp
    src/UdpSocket.cpp
    src/UploadHandler.cpp)

target_compile_features(MIOS_Studio PRIVATE cxx_std_17)
target_compile_definitions(MIOS_Studio PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

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
