/* -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*- */
// $Id$
/*
 * MIDI Monitor Component
 *
 * ==========================================================================
 *
 *  Copyright (C) 2010 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#include "MiosStudio.h"
#include "../version.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <fstream>
#endif

namespace
{
MidiDeviceInfo findMidiDevice(const Array<MidiDeviceInfo>& devices,
                              const String& preferredName,
                              bool connectToBootloader,
                              bool allowApplicationNameForBootloader)
{
    if( preferredName.isNotEmpty() &&
        (connectToBootloader || !preferredName.containsIgnoreCase("MIOS32 Bootloader")) ) {
        for( const MidiDeviceInfo& device : devices )
            if( device.name == preferredName && (!connectToBootloader || allowApplicationNameForBootloader) )
                return device;
    }

    for( const MidiDeviceInfo& device : devices ) {
        const bool isBootloader = device.name.containsIgnoreCase("MIOS32 Bootloader");
        if( connectToBootloader == isBootloader ) {
            if( isBootloader || device.name.startsWithIgnoreCase("MIDIbox") ||
                device.name.containsIgnoreCase("MIOS32") )
                return device;
        }
    }

    return MidiDeviceInfo();
}

Component* findEditTarget()
{
#if JUCE_IOS
    return dynamic_cast<TextEditor*>(Component::getCurrentlyFocusedComponent());
#else
    for( Component* component = Component::getCurrentlyFocusedComponent();
         component != nullptr;
         component = component->getParentComponent() ) {
        if( dynamic_cast<TextEditor*>(component) != nullptr ||
            dynamic_cast<LogBox*>(component) != nullptr )
            return component;
    }

    return nullptr;
#endif
}

#if JUCE_IOS
struct IosStudioLayout
{
    Rectangle<int> title;
    Rectangle<int> midiIn;
    Rectangle<int> midiOut;
    Rectangle<int> upload;
    Rectangle<int> terminal;
    Rectangle<int> keyboard;
    bool compact = false;
    bool shortWide = false;
    bool stackedUploadLogs = false;
};

IosStudioLayout getIosStudioLayout(Rectangle<int> bounds)
{
    IosStudioLayout layout;

    bounds = bounds.reduced(8);
    layout.title = Rectangle<int>();

    layout.shortWide = bounds.getWidth() >= 760 && bounds.getHeight() < 560;
    layout.compact = bounds.getWidth() < 760;
    const bool shortScreen = bounds.getHeight() < 560;
    layout.stackedUploadLogs = bounds.getWidth() < 560;

    const int gap = 8;
    const int monitorHeight = layout.shortWide
        ? jlimit(120, 170, bounds.getHeight() / 3)
        : layout.compact
        ? jlimit(220, 320, bounds.getHeight() / (shortScreen ? 3 : 4))
        : jlimit(170, 240, bounds.getHeight() / 4);
    const int uploadHeight = layout.shortWide
        ? 0
        : layout.compact
        ? (layout.stackedUploadLogs
           ? jlimit(220, 300, bounds.getHeight() / 3)
           : jlimit(150, 210, bounds.getHeight() / 4))
        : jlimit(170, 230, bounds.getHeight() / 4);
    const int keyboardHeight = layout.shortWide
        ? jlimit(110, 160, bounds.getHeight() / 3)
        : layout.compact
        ? jlimit(140, 230, bounds.getHeight() / (shortScreen ? 3 : 4))
        : jlimit(200, 280, bounds.getHeight() / 4);

    layout.midiIn = bounds.removeFromTop(monitorHeight).reduced(0, 4);
    layout.midiOut = Rectangle<int>();

    bounds.removeFromTop(gap);

    layout.keyboard = bounds.removeFromBottom(keyboardHeight).reduced(0, 4);
    bounds.removeFromBottom(gap);

    if( layout.shortWide ) {
        layout.upload = bounds.removeFromLeft(bounds.getWidth() / 2).reduced(0, 4);
        layout.terminal = bounds.reduced(4, 4);
    } else {
        layout.upload = bounds.removeFromTop(uploadHeight).reduced(0, 4);
        bounds.removeFromTop(gap);
        layout.terminal = bounds.reduced(0, 4);
    }

    return layout;
}
#endif
}

//==============================================================================
MiosStudio::MiosStudio()
    : batchMode(false)
    , duggleMode(false)
#if JUCE_IOS
    , iosQueryActive(false)
    , iosRepeatQueriesRemaining(0)
    , iosReceivedTerminalMessage(false)
#else
    , uploadWindow(0)
    , midiInMonitor(0)
    , midiOutMonitor(0)
    , miosTerminal(0)
    , midiKeyboard(0)
    , initialMidiScanCounter(1) // start step-wise MIDI port scan
    , midiScanRetriesRemaining(20)
    , midiInputCallbackRegistered(false)
    , batchWaitCounter(0)
    , initialGuiX(-1) // centered
    , initialGuiY(-1) // centered
#endif
{
#if JUCE_IOS
    initialGuiX = -1;
    initialGuiY = -1;
    batchWaitCounter = 0;
    runningStatus = 0;
    initialMidiScanCounter = 0;
    midiScanRetriesRemaining = 0;
    midiInputCallbackRegistered = false;

    uploadHandler = new UploadHandler(this);
    sysexPatchDb = new SysexPatchDb();

    LookAndFeel::setDefaultLookAndFeel(&myLookAndFeel);
    initialiseIosUi();

    audioDeviceManager.addMidiInputDeviceCallback(String(), this);
    midiInputCallbackRegistered = true;

    scanIosMidiDevices();
    Timer::startTimer(20);
    setSize(1024, 768);
#else
    bool hideMonitors = false;
    bool hideUpload = false;
    bool hideTerminal = false;
    bool hideKeyboard = false;
    int  guiWidth = 800;
    int  guiHeight = 650;
    int  firstDeviceId = -1;

    // parse the command line
    {
        int numErrors = 0;
        bool quitIfBatch = false;
        StringArray commandLineArray = JUCEApplication::getCommandLineParameterArray();

        // first search for --batch option
        for(int i=0; i<commandLineArray.size(); ++i) {
            if( commandLineArray[i].compare("--batch") == 0 ) {
                batchMode = true;
                redirectIOToConsole();
            }
        }

        // now for the remaining options
        for(int i=0; i<commandLineArray.size(); ++i) {
            if( commandLineArray[i].compare("--help") == 0 ) {
                commandLineInfoMessages += "Command Line Parameters:\n";
                commandLineInfoMessages += "--help                  this page\n";
                commandLineInfoMessages += "--version               shows version number\n";
                commandLineInfoMessages += "--batch                 don't open GUI\n";
                commandLineInfoMessages += "--in=<port>             optional search string for MIDI IN port\n";
                commandLineInfoMessages += "--out=<port>            optional search string for MIDI OUT port\n";
                commandLineInfoMessages += "--device_id=<id>        sets the device id, should be done before upload if necessary\n";
                commandLineInfoMessages += "--query                 queries the selected core\n";
                commandLineInfoMessages += "--upload_hex=<file>     upload specified .hex file to core. Multiple --upload_hex allowed!\n";
                commandLineInfoMessages += "--upload_file=<file>    upload specified file to SD Card. Multiple --upload_file allowed!\n";
                commandLineInfoMessages += "--send_syx=<file>       send specified .syx file to core. Multiple --send_syx allowed!\n";
                commandLineInfoMessages += "--terminal=<command>    send a MIOS terminal command. Multiple --terminal allowed!\n";
                commandLineInfoMessages += "--wait=<seconds>        Waits for the given seconds.\n";
                commandLineInfoMessages += "--gui_x=<x>             specifies the initial window X position\n";
                commandLineInfoMessages += "--gui_y=<y>             specifies the initial window Y position\n";
                commandLineInfoMessages += "--gui_width=<width>     specifies the initial window width\n";
                commandLineInfoMessages += "--gui_height=<height>   specifies the initial window height\n";
                commandLineInfoMessages += "--gui_title=<name>      changes the name of the application in the title bar\n";
                commandLineInfoMessages += "--gui_hide_monitors     disables the MIDI IN/OUT monitor when the GUI is started\n";
                commandLineInfoMessages += "--gui_hide_upload       disables the upload panel when the GUI is started\n";
                commandLineInfoMessages += "--gui_hide_terminal     disables the terminal panel when the GUI is started\n";
                commandLineInfoMessages += "--gui_hide_keyboard     disables the virtual keyboard panel when the GUI is started\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "Usage Examples:\n";
                commandLineInfoMessages += "  MIOS_Studio --in=MIOS32 --out=MIOS32\n";
                commandLineInfoMessages += "    starts MIOS Studio with MIDI IN/OUT port matching with 'MIOS32'\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "  MIOS_Studio --upload_hex=project.hex\n";
                commandLineInfoMessages += "    starts MIOS Studio and uploads the project.hex file immediately\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "  MIOS_Studio --batch --upload_hex=project.hex\n";
                commandLineInfoMessages += "    starts MIOS Studio without GUI and uploads the project.hex file\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "  MIOS_Studio --batch --upload_file=default.ngc --upload_file=default.ngl\n";
                commandLineInfoMessages += "    starts MIOS Studio without GUI and uploads two files to SD Card (MIOS32 only)\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "  MIOS_Studio --batch --terminal=\"help\" --wait=1\n";
                commandLineInfoMessages += "    starts MIOS Studio, executes the \"help\" command in MIOS Terminal and waits 1 second for response\n";
                commandLineInfoMessages += "\n";
                commandLineInfoMessages += "NOTE: most parameters can be combined to a sequence of operations.\n";
                commandLineInfoMessages += "      E.g. upload a .hex file, upload files to SD Card, execute a terminal command and wait some seconds before exit.\n";
                quitIfBatch = true;
            } else if( commandLineArray[i].compare("--version") == 0 ) {
                commandLineInfoMessages += String("MIOS Studio ") + String(MIOS_STUDIO_VERSION) + String("\n");
            } else if( commandLineArray[i].compare("--batch") == 0 ) {
                // already handled above
            } else if( commandLineArray[i].startsWith("--in=") ) {
                inPortFromCommandLine = commandLineArray[i].substring(5);
                inPortFromCommandLine.trimCharactersAtStart(" \t\"'");
                inPortFromCommandLine.trimCharactersAtEnd(" \t\"'");
                std::cout << "Preselected MIDI IN Port: " << inPortFromCommandLine << std::endl;
            } else if( commandLineArray[i].startsWith("--out=") ) {
                outPortFromCommandLine = commandLineArray[i].substring(6);
                outPortFromCommandLine.trimCharactersAtStart(" \t\"'");
                outPortFromCommandLine.trimCharactersAtEnd(" \t\"'");
                std::cout << "Preselected MIDI OUT Port: " << outPortFromCommandLine << std::endl;
            } else if( commandLineArray[i].startsWith("--device_id") ) {
                String id = commandLineArray[i].substring(12);
                id.trimCharactersAtStart(" \t\"'");
                id.trimCharactersAtEnd(" \t\"'");
                int idValue = id.getIntValue();
                if( idValue < 0 || idValue > 127 ) {
                    commandLineErrorMessages += String("ERROR: device ID should be within 0..127!\n");
                    ++numErrors;
                } else {
                    if( firstDeviceId < 0 ) {
                        firstDeviceId = idValue;
                    } else {
                        batchJobs.add(String("device_id ") + id);
                    }
                }
            } else if( commandLineArray[i].startsWith("--query") ) {
                batchJobs.add(String("query"));
            } else if( commandLineArray[i].startsWith("--duggle") ) {
                duggleMode = true;
            } else if( commandLineArray[i].startsWith("--upload_hex") ) {
                String file = commandLineArray[i].substring(13);
                file.trimCharactersAtStart(" \t\"'");
                file.trimCharactersAtEnd(" \t\"'");
                batchJobs.add(String("upload_hex ") + file);
            } else if( commandLineArray[i].startsWith("--upload_file") ) {
                String file = commandLineArray[i].substring(14);
                file.trimCharactersAtStart(" \t\"'");
                file.trimCharactersAtEnd(" \t\"'");
                batchJobs.add(String("upload_file ") + file);
            } else if( commandLineArray[i].startsWith("--send_syx") ) {
                String file = commandLineArray[i].substring(11);
                file.trimCharactersAtStart(" \t\"'");
                file.trimCharactersAtEnd(" \t\"'");
                batchJobs.add(String("send_syx ") + file);
            } else if( commandLineArray[i].startsWith("--terminal") ) {
                String command = commandLineArray[i].substring(11);
                command.trimCharactersAtStart(" \t\"'");
                command.trimCharactersAtEnd(" \t\"'");
                batchJobs.add(String("terminal ") + command);
            } else if( commandLineArray[i].startsWith("--wait") ) {
                String command = commandLineArray[i].substring(7);
                command.trimCharactersAtStart(" \t\"'");
                command.trimCharactersAtEnd(" \t\"'");
                batchJobs.add(String("wait ") + command);
            } else if( commandLineArray[i].startsWith("--gui_x") ) {
                int value = commandLineArray[i].substring(8).getIntValue();
                if( value >= 0 )
                    initialGuiX = value;
            } else if( commandLineArray[i].startsWith("--gui_y") ) {
                int value = commandLineArray[i].substring(8).getIntValue();
                if( value >= 0 )
                    initialGuiY = value;
            } else if( commandLineArray[i].startsWith("--gui_width") ) {
                int value = commandLineArray[i].substring(12).getIntValue();
                if( value > 0 )
                    guiWidth = value;
            } else if( commandLineArray[i].startsWith("--gui_height") ) {
                int value = commandLineArray[i].substring(13).getIntValue();
                if( value > 0 )
                    guiHeight = value;
            } else if( commandLineArray[i].startsWith("--gui_title") ) {
                initialGuiTitle = commandLineArray[i].substring(12);
                initialGuiTitle.trimCharactersAtStart(" \t\"'");
                initialGuiTitle.trimCharactersAtEnd(" \t\"'");
            } else if( commandLineArray[i].startsWith("--gui_hide_monitors") ) {
                hideMonitors = true;
            } else if( commandLineArray[i].startsWith("--gui_hide_upload") ) {
                hideUpload = true;
            } else if( commandLineArray[i].startsWith("--gui_hide_terminal") ) {
                hideTerminal = true;
            } else if( commandLineArray[i].startsWith("--gui_hide_keyboard") ) {
                hideKeyboard = true;
            } else if( commandLineArray[i].startsWith("-psn") ) {
                // ignore for MacOS
            } else if( commandLineArray[i].startsWith("-NSDocumentRevisionsDebugMode") ) {
                // ignore for MacOS
            } else if( commandLineArray[i].startsWith("YES") ) {
                // ignore for MacOS
            } else {
                commandLineErrorMessages += String("ERROR: unknown command line parameter: ") + commandLineArray[i] + String("\n");
                commandLineErrorMessages += String("Enter '--help' to get a list of all available options!\n");
                ++numErrors;
            }
        }

        std::cout << commandLineInfoMessages;

        if( numErrors ) {
            quitIfBatch = true;

            if( runningInBatchMode() ) {
                std::cerr << commandLineErrorMessages;
            } else {
                // AlertWindow will be shown from timerCallback() once MIOS Studio is running
            }
        }

        if( runningInBatchMode() && quitIfBatch ) {
#ifdef _WIN32
            std::cout << "Press <enter> to quit console." << std::endl;
            while (GetAsyncKeyState(VK_RETURN) & 0x8000) {}
            while (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) {}
#endif
            JUCEApplication::getInstance()->setApplicationReturnValue(1); // error
            JUCEApplication::quit();
        }

        if( runningInBatchMode() ) {
            batchJobs.add("quit");
        }
    }

    // instantiate components
    uploadHandler = new UploadHandler(this);
    sysexPatchDb = new SysexPatchDb();

    // default look and feel
    LookAndFeel::setDefaultLookAndFeel(&myLookAndFeel);
    
    addAndMakeVisible(uploadWindow = new UploadWindow(this));
    addAndMakeVisible(midiInMonitor = new MidiMonitor(this, true));
    addAndMakeVisible(midiOutMonitor = new MidiMonitor(this, false));
    addAndMakeVisible(miosTerminal = new MiosTerminal(this));
    addAndMakeVisible(midiKeyboard = new MidiKeyboard(this));

    // tools are created and made visible via tools button in Upload Window
    sysexToolWindow = 0;
    oscToolWindow = 0;
    midio128ToolWindow = 0;
    mbCvToolWindow = 0;
    mbhpMfToolWindow = 0;
    sysexLibrarianWindow = 0;
    miosFileBrowserWindow = 0;

    addAndMakeVisible(horizontalDividerBar1 = new StretchableLayoutResizerBar(&horizontalLayout, 1, false));
    addAndMakeVisible(horizontalDividerBar2 = new StretchableLayoutResizerBar(&horizontalLayout, 3, false));
    addAndMakeVisible(horizontalDividerBar3 = new StretchableLayoutResizerBar(&horizontalLayout, 5, false));
    addAndMakeVisible(verticalDividerBarMonitors = new StretchableLayoutResizerBar(&verticalLayoutMonitors, 1, true));
    addAndMakeVisible(resizer = new ResizableCornerComponent(this, &resizeLimits));
    resizeLimits.setSizeLimits(200, 100, 2048, 2048);

    commandManager = new ApplicationCommandManager();
    commandManager->registerAllCommandsForTarget(this);
    commandManager->registerAllCommandsForTarget(JUCEApplication::getInstance());
    addKeyListener(commandManager->getKeyMappings());
    setApplicationCommandManagerToWatch(commandManager);

    if( hideMonitors ) {
        midiInMonitor->setVisible(false);
        verticalDividerBarMonitors->setVisible(false);
        midiOutMonitor->setVisible(false);
    }
    if( hideUpload ) {
        horizontalDividerBar1->setVisible(false);
        uploadWindow->setVisible(false);
    }
    if( hideTerminal ) {
        horizontalDividerBar2->setVisible(false);
        miosTerminal->setVisible(false);
    }
    if( hideKeyboard ) {
        horizontalDividerBar3->setVisible(false);
        midiKeyboard->setVisible(false);
    }

    updateLayout();

    if( firstDeviceId >= 0 ) {
        std::cout << "Setting Device ID=" << firstDeviceId << std::endl;
        uploadWindow->setDeviceId(firstDeviceId);
    }

    // Some MIDI backends discover devices asynchronously. Give them time to
    // populate before the first scan; timerCallback() retries if necessary.
    Timer::startTimer(250);

    setSize(guiWidth, guiHeight);
#endif
}

MiosStudio::~MiosStudio()
{
    if( midiInputCallbackRegistered ) {
        audioDeviceManager.removeMidiInputDeviceCallback(String(), this);
        midiInputCallbackRegistered = false;
    }

    if( uploadHandler )
        deleteAndZero(uploadHandler);
#if JUCE_IOS
    if( sysexPatchDb )
        deleteAndZero(sysexPatchDb);
#else
    if( sysexToolWindow )
        deleteAndZero(sysexToolWindow);
    if( oscToolWindow )
        deleteAndZero(oscToolWindow);
    if( midio128ToolWindow )
        deleteAndZero(midio128ToolWindow);
    if( mbCvToolWindow )
        deleteAndZero(mbCvToolWindow);
    if( mbhpMfToolWindow )
        deleteAndZero(mbhpMfToolWindow);
    if( sysexLibrarianWindow )
        deleteAndZero(sysexLibrarianWindow);
    if( miosFileBrowserWindow )
        deleteAndZero(miosFileBrowserWindow);

    // try: avoid crash under Windows by disabling all MIDI INs/OUTs
#endif
    closeMidiPorts();
}

//==============================================================================
#ifdef _WIN32
// see http://www.rawmaterialsoftware.com/viewtopic.php?f=2&t=9868

// Code taken from here: http://dslweb.nwnexus.com/~ast/dload/guicon.htm
// Modified to support attaching to an owner console.
void MiosStudio::redirectIOToConsole()
{
    int hConHandle;
    long lStdHandle;
    FILE *fp;
    if (1) // TK: crashes the application: AttachConsole(ATTACH_PARENT_PROCESS) == 0)
    {
      // We couldn't obtain a parent console.  Probably application was launched 
      // from inside Explorer (E.G. the run prompt, or a shortcut).
      // We'll spawn a new console window instead then!
      CONSOLE_SCREEN_BUFFER_INFO coninfo;
      AllocConsole();
      GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
      coninfo.dwSize.Y = 500;
      SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);
    }
    // redirect unbuffered STDOUT to the console
    lStdHandle = (long)GetStdHandle(STD_OUTPUT_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);
    fp = _fdopen( hConHandle, "w" );
    *stdout = *fp;
    setvbuf( stdout, NULL, _IONBF, 0 );
    // redirect unbuffered STDIN to the console
    lStdHandle = (long)GetStdHandle(STD_INPUT_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);
    fp = _fdopen( hConHandle, "r" );
    *stdin = *fp;
    setvbuf( stdin, NULL, _IONBF, 0 );
    // redirect unbuffered STDERR to the console
    lStdHandle = (long)GetStdHandle(STD_ERROR_HANDLE);
    hConHandle = _open_osfhandle(lStdHandle, _O_TEXT);
    fp = _fdopen( hConHandle, "w" );
    *stderr = *fp;
    setvbuf( stderr, NULL, _IONBF, 0 );
    // make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog
    // point to console as well
    std::ios::sync_with_stdio();
}

#else

// Empty function avoid ifdefs elsewhere.
void MiosStudio::redirectIOToConsole() {}

#endif


//==============================================================================
void MiosStudio::paint (Graphics& g)
{
#if JUCE_IOS
    g.fillAll(Colour(0xffc1d0ff));

    const IosStudioLayout layout = getIosStudioLayout(getLocalBounds());
    paintIosPanel(g, layout.midiIn, String());
    paintIosPanel(g, layout.upload, T("Upload"));
    paintIosPanel(g, layout.terminal, T("MIOS Terminal"));
    paintIosPanel(g, layout.keyboard, T("MIDI Keyboard"));
    paintIosKeyboard(g, layout.keyboard.reduced(8));
#else
    g.fillAll(Colour(0xffc1d0ff));
#endif
}

void MiosStudio::resized()
{
#if JUCE_IOS
    const IosStudioLayout layout = getIosStudioLayout(getLocalBounds());
    titleLabel.setBounds(layout.title);

    auto midiInner = layout.midiIn.reduced(8);
    auto refreshArea = midiInner.removeFromBottom(34);
    refreshButton.setBounds(refreshArea.removeFromLeft(96).reduced(0, 4));
    midiInner.removeFromBottom(4);

    auto layoutMidiPort = [](Rectangle<int> bounds,
                             Label& portLabel,
                             ComboBox& selector,
                             TextEditor& log) {
        auto row = bounds.removeFromTop(32);
        portLabel.setBounds(row.removeFromLeft(78));
        selector.setBounds(row.reduced(0, 3));
        bounds.removeFromTop(4);
        log.setBounds(bounds);
    };

    if( layout.midiIn.getWidth() < 560 ) {
        auto inBlock = midiInner.removeFromTop(midiInner.getHeight() / 2).withTrimmedBottom(4);
        auto outBlock = midiInner.withTrimmedTop(4);
        layoutMidiPort(inBlock, inputLabel, inputSelector, midiInLog);
        layoutMidiPort(outBlock, outputLabel, outputSelector, midiOutLog);
    } else {
        auto inBlock = midiInner.removeFromLeft(midiInner.getWidth() / 2).withTrimmedRight(4);
        auto outBlock = midiInner.withTrimmedLeft(4);
        layoutMidiPort(inBlock, inputLabel, inputSelector, midiInLog);
        layoutMidiPort(outBlock, outputLabel, outputSelector, midiOutLog);
    }

    auto uploadInner = layout.upload.reduced(8);
    const bool narrowUpload = layout.upload.getWidth() < 560;
    if( narrowUpload ) {
        auto topRow = uploadInner.removeFromTop(32);
        deviceIdLabel.setBounds(topRow.removeFromLeft(82));
        deviceIdSlider.setBounds(topRow.removeFromLeft(132));
        topRow.removeFromLeft(8);
        queryButton.setBounds(topRow.removeFromLeft(86).reduced(0, 4));
        uploadInner.removeFromTop(4);
    } else {
        auto header = uploadInner.removeFromTop(34);
        deviceIdLabel.setBounds(header.removeFromLeft(82));
        deviceIdSlider.setBounds(header.removeFromLeft(134));
        header.removeFromLeft(8);
        queryButton.setBounds(header.removeFromLeft(86).reduced(0, 4));
        uploadInner.removeFromTop(8);
    }

    uploadStatusLog.setVisible(true);

    auto uploadStatusPane = uploadInner.removeFromBottom(jlimit(86, 130, uploadInner.getHeight() / 3));
    auto uploadControlPane = uploadInner.removeFromBottom(narrowUpload ? 64 : 58);
    auto deviceStatusPane = uploadInner.withTrimmedBottom(6);

    uploadQueryLog.setBounds(deviceStatusPane);

    uploadFileLabel.setBounds(uploadControlPane.removeFromTop(22));
    auto fileRow = uploadControlPane.removeFromTop(34);
    uploadFileButton.setBounds(fileRow.removeFromLeft(narrowUpload ? 86 : 92).reduced(0, 4));
    fileRow.removeFromLeft(8);
    uploadStartButton.setBounds(fileRow.removeFromLeft(narrowUpload ? 74 : 82).reduced(0, 4));
    fileRow.removeFromLeft(8);
    uploadStopButton.setBounds(fileRow.removeFromLeft(narrowUpload ? 74 : 82).reduced(0, 4));

    uploadStatusPane.removeFromTop(6);
    uploadStatusLog.setBounds(uploadStatusPane);

    keyboardBounds = layout.keyboard.reduced(8);

    auto terminalInner = layout.terminal.reduced(8);
    auto terminalRow = terminalInner.removeFromBottom(34);
    const int sendWidth = terminalRow.getWidth() < 420 ? 76 : 92;
    sendTerminalButton.setBounds(terminalRow.removeFromRight(sendWidth));
    terminalRow.removeFromRight(8);
    terminalInput.setBounds(terminalRow);
    terminalLog.setBounds(terminalInner.withTrimmedBottom(6));
#else
    horizontalLayout.layOutComponents(layoutHComps.getRawDataPointer(), layoutHComps.size(),
                                       4, 4,
                                       getWidth() - 8, getHeight() - 8,
                                       true,  // lay out above each other
                                       true); // resize the components' heights as well as widths

    if( layoutVComps.size() ) {
        verticalLayoutMonitors.layOutComponents(layoutVComps.getRawDataPointer(), layoutVComps.size(),
                                                4,
                                                4 + horizontalLayout.getItemCurrentPosition(0),
                                                getWidth() - 8,
                                                horizontalLayout.getItemCurrentAbsoluteSize(0),
                                                false, // lay out side-by-side
                                                true); // resize the components' heights as well as widths
    }

    resizer->setBounds(getWidth()-16, getHeight()-16, 16, 16);
#endif
}


//==============================================================================
bool MiosStudio::runningInBatchMode(void)
{
    return batchMode;
}

//==============================================================================
void MiosStudio::handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message)
{
    uint8 *data = (uint8 *)message.getRawData();
    uint32 size = message.getRawDataSize();

    // TK: the Juce specific "MidiBuffer" sporatically throws an assertion when overloaded
    // therefore I'm using a std::queue instead
    

    // ugly fix for reduced buffer size under windows...
    if( size > 2 && data[0] == 0xf0 && data[size-1] != 0xf7 ) {
        // first message without F7 at the end
        sysexReceiveBuffer.clear();
        for(int pos=0; pos<size; ++pos)
            sysexReceiveBuffer.add(data[pos]);
        return; // hopefully we will receive F7 with the next call

    } else if( sysexReceiveBuffer.size() && !(data[0] & 0x80) && data[size-1] != 0xf7 ) {
        // continued message without F7 at the end
        for(int pos=0; pos<size; ++pos)
            sysexReceiveBuffer.add(data[pos]);
        return; // hopefully we will receive F7 with the next call

    } else if( sysexReceiveBuffer.size() && data[size-1] == 0xf7 ) {
        // finally we received F7
        for(int pos=0; pos<size; ++pos)
            sysexReceiveBuffer.add(data[pos]);

        // propagate combined message
        uint8 *bufferedData = &sysexReceiveBuffer.getReference(0);        
        MidiMessage combinedMessage(bufferedData, sysexReceiveBuffer.size());
        sysexReceiveBuffer.clear();

        const ScopedLock sl(midiInQueueLock); // lock will be released at end of function
        midiInQueue.push(combinedMessage);

        // propagate to upload handler
        uploadHandler->handleIncomingMidiMessage(source, combinedMessage);

    } else {
        sysexReceiveBuffer.clear();

        const ScopedLock sl(midiInQueueLock); // lock will be released at end of function
        midiInQueue.push(message);

        // propagate to upload handler
        uploadHandler->handleIncomingMidiMessage(source, message);
    }
}


//==============================================================================
void MiosStudio::sendMidiMessage(MidiMessage &message)
{
    MidiOutput *out = audioDeviceManager.getDefaultMidiOutput();

    // if timestamp isn't set, to this now to ensure a plausible MIDI Out monitor output
    if( message.getTimeStamp() == 0 )
        message.setTimeStamp((double)Time::getMillisecondCounter() / 1000.0);

    if( out )
        out->sendMessageNow(message);

    const ScopedLock sl(midiOutQueueLock); // lock will be released at end of function
    midiOutQueue.push(message);
}


//==============================================================================
void MiosStudio::closeMidiPorts(void)
{
    String inputIdentifier;
    {
        const ScopedLock sl(midiPortStateLock);
        inputIdentifier = activeMidiInputIdentifier;
        activeMidiInputName.clear();
        activeMidiInputIdentifier.clear();
        activeMidiOutputName.clear();
        activeMidiOutputIdentifier.clear();
    }

    if( inputIdentifier.isNotEmpty() )
        audioDeviceManager.setMidiInputDeviceEnabled(inputIdentifier, false);

    audioDeviceManager.setDefaultMidiOutputDevice(String());
}

String MiosStudio::getActiveMidiInput(void)
{
    const ScopedLock sl(midiPortStateLock);
    return activeMidiInputName;
}

String MiosStudio::getActiveMidiOutput(void)
{
    const ScopedLock sl(midiPortStateLock);
    return activeMidiOutputName;
}

bool MiosStudio::runMidiPortOperationOnMessageThread(
    const std::function<void(MiosStudio&)>& operation,
    int timeoutMs)
{
    MessageManager* messageManager = MessageManager::getInstanceWithoutCreating();
    if( messageManager == 0 )
        return false;

    if( messageManager->isThisTheMessageThread() ) {
        operation(*this);
        return true;
    }

    struct PendingOperation
    {
        WaitableEvent completed;
        std::atomic<bool> cancelled { false };
    };

    std::shared_ptr<PendingOperation> pending = std::make_shared<PendingOperation>();
    Component::SafePointer<MiosStudio> safeThis(this);
    if( !MessageManager::callAsync([safeThis, pending, operation]() {
            if( !pending->cancelled.load() && safeThis != 0 )
                operation(*safeThis.getComponent());
            pending->completed.signal();
        }) )
        return false;

    if( !pending->completed.wait(timeoutMs) ) {
        pending->cancelled.store(true);
        return false;
    }

    return true;
}

bool MiosStudio::reconnectMidiPortsForUpload(const String &applicationInput,
                                             const String &applicationOutput,
                                             bool connectToBootloader,
                                             int timeoutMs)
{
    String previousInputName;
    String previousInputIdentifier;
    String previousOutputName;
    String previousOutputIdentifier;
    {
        const ScopedLock sl(midiPortStateLock);
        previousInputName = activeMidiInputName;
        previousInputIdentifier = activeMidiInputIdentifier;
        previousOutputName = activeMidiOutputName;
        previousOutputIdentifier = activeMidiOutputIdentifier;
    }

    // AudioDeviceManager sends change notifications and owns backend objects.
    // Keep all of its mutations on JUCE's message thread so that a device
    // notification cannot deadlock against the upload worker.
    if( !runMidiPortOperationOnMessageThread([](MiosStudio& studio) {
            studio.closeMidiPorts();
        }, 500) )
        return false;

    const uint32 startTime = Time::getMillisecondCounter();
    bool previousInputDisappeared = previousInputIdentifier.isEmpty();
    bool previousOutputDisappeared = previousOutputIdentifier.isEmpty();
    while( !Thread::currentThreadShouldExit() &&
           Time::getMillisecondCounter() - startTime < (uint32)timeoutMs ) {
        const uint32 elapsed = Time::getMillisecondCounter() - startTime;
        // Windows can retain the application product name and device
        // identifier across the application/bootloader transition. Prefer a
        // real remove/add transition, but permit the cached endpoint quickly
        // enough to fit inside the legacy bootloader's short timeout.
        const bool allowCachedEndpoint = elapsed >= 300;

        struct ScanResult
        {
            bool previousInputPresent = false;
            bool previousOutputPresent = false;
            bool connected = false;
        };
        std::shared_ptr<ScanResult> result = std::make_shared<ScanResult>();

        const bool operationCompleted = runMidiPortOperationOnMessageThread(
            [result, applicationInput, applicationOutput, connectToBootloader,
             allowCachedEndpoint, previousInputName, previousInputIdentifier,
             previousOutputName, previousOutputIdentifier,
             previousInputDisappeared, previousOutputDisappeared](MiosStudio& studio) {
                const Array<MidiDeviceInfo> inputs = MidiInput::getAvailableDevices();
                const Array<MidiDeviceInfo> outputs = MidiOutput::getAvailableDevices();

                for( const MidiDeviceInfo& device : inputs )
                    if( device.identifier == previousInputIdentifier )
                        result->previousInputPresent = true;
                for( const MidiDeviceInfo& device : outputs )
                    if( device.identifier == previousOutputIdentifier )
                        result->previousOutputPresent = true;

                const bool inputDisappeared = previousInputDisappeared ||
                                              !result->previousInputPresent;
                const bool outputDisappeared = previousOutputDisappeared ||
                                               !result->previousOutputPresent;
                MidiDeviceInfo input = findMidiDevice(inputs, applicationInput,
                                                      connectToBootloader,
                                                      inputDisappeared || allowCachedEndpoint);
                MidiDeviceInfo output = findMidiDevice(outputs, applicationOutput,
                                                       connectToBootloader,
                                                       outputDisappeared || allowCachedEndpoint);

                const bool inputTransitionObserved = inputDisappeared ||
                                                     input.identifier != previousInputIdentifier ||
                                                     input.name != previousInputName ||
                                                     allowCachedEndpoint;
                const bool outputTransitionObserved = outputDisappeared ||
                                                      output.identifier != previousOutputIdentifier ||
                                                      output.name != previousOutputName ||
                                                      allowCachedEndpoint;
                if( !inputTransitionObserved )
                    input = MidiDeviceInfo();
                if( !outputTransitionObserved )
                    output = MidiDeviceInfo();

                if( input.identifier.isEmpty() || output.identifier.isEmpty() )
                    return;

                studio.audioDeviceManager.setMidiInputDeviceEnabled(input.identifier, true);
                studio.audioDeviceManager.setDefaultMidiOutputDevice(output.identifier);

                if( studio.audioDeviceManager.isMidiInputDeviceEnabled(input.identifier) &&
                    studio.audioDeviceManager.getDefaultMidiOutput() != 0 ) {
                    const ScopedLock sl(studio.midiPortStateLock);
                    studio.activeMidiInputName = input.name;
                    studio.activeMidiInputIdentifier = input.identifier;
                    studio.activeMidiOutputName = output.name;
                    studio.activeMidiOutputIdentifier = output.identifier;
                    result->connected = true;
                    return;
                }

                studio.audioDeviceManager.setMidiInputDeviceEnabled(input.identifier, false);
                studio.audioDeviceManager.setDefaultMidiOutputDevice(String());
            }, 500);

        if( !operationCompleted )
            return false;

        previousInputDisappeared = previousInputDisappeared || !result->previousInputPresent;
        previousOutputDisappeared = previousOutputDisappeared || !result->previousOutputPresent;
        if( result->connected )
            return true;

        Thread::sleep(25);
    }

    return false;
}

#if JUCE_IOS
void MiosStudio::initialiseIosUi()
{
    titleLabel.setText(T("MIOS Studio"), dontSendNotification);
    titleLabel.setFont(Font(FontOptions(24.0f).withStyle("Bold")));
    titleLabel.setJustificationType(Justification::centredLeft);
    addChildComponent(titleLabel);

    for( Label* heading : { &midiInHeader, &midiOutHeader, &deviceStatusHeader, &uploadStatusHeader, &terminalHeader, &keyboardHeader } )
        addChildComponent(*heading);

    inputLabel.setText(T("MIDI In"), dontSendNotification);
    outputLabel.setText(T("MIDI Out"), dontSendNotification);
    deviceIdLabel.setText(T("Device ID"), dontSendNotification);
    uploadFileLabel.setText(T("No upload file selected"), dontSendNotification);
    for( Label* label : { &inputLabel, &outputLabel, &deviceIdLabel, &uploadFileLabel } ) {
        label->setJustificationType(Justification::centredLeft);
        label->setFont(Font(FontOptions(14.0f)));
        addAndMakeVisible(*label);
    }

    inputSelector.addListener(this);
    outputSelector.addListener(this);
    addAndMakeVisible(inputSelector);
    addAndMakeVisible(outputSelector);

    deviceIdSlider.setRange(0, 127, 1);
    deviceIdSlider.setSliderStyle(Slider::IncDecButtons);
    deviceIdSlider.setTextBoxStyle(Slider::TextBoxLeft, false, 48, 28);
    deviceIdSlider.setValue(uploadHandler->getDeviceId(), dontSendNotification);
    deviceIdSlider.onValueChange = [this]() {
        uploadHandler->setDeviceId((uint8)deviceIdSlider.getValue());
    };
    addAndMakeVisible(deviceIdSlider);

    refreshButton.setButtonText(T("Refresh"));
    queryButton.setButtonText(T("Query"));
    repeatQueryButton.setButtonText(T("Query x10"));
    uploadFileButton.setButtonText(T("File..."));
    uploadStartButton.setButtonText(T("Start"));
    uploadStopButton.setButtonText(T("Stop"));
    sendTerminalButton.setButtonText(T("Send"));
    for( TextButton* button : { &refreshButton, &queryButton, &uploadFileButton, &uploadStartButton, &uploadStopButton, &sendTerminalButton } ) {
        button->addListener(this);
        addAndMakeVisible(*button);
    }
    repeatQueryButton.addListener(this);
    addChildComponent(repeatQueryButton);
    uploadStartButton.setEnabled(false);
    uploadStopButton.setEnabled(false);

    terminalInput.setTextToShowWhenEmpty(T("MIOS32 terminal command"), Colours::grey);
    terminalInput.addListener(this);
    addAndMakeVisible(terminalInput);

    for( TextEditor* editor : { &midiInLog, &midiOutLog, &uploadQueryLog, &uploadStatusLog, &terminalLog } )
        configureIosLog(*editor);

    addIosLogEntry(uploadQueryLog, T("Waiting for first query."));
    addIosLogEntry(uploadStatusLog, T("Connect a Core MIDI interface, select matching input/output ports, then query."));
    addIosLogEntry(terminalLog, T("MIOS Terminal ready."));
}

void MiosStudio::scanIosMidiDevices()
{
    inputPortNames.clear();
    outputPortNames.clear();
    inputSelector.clear(dontSendNotification);
    outputSelector.clear(dontSendNotification);

    int itemId = 1;
    for( const MidiDeviceInfo& device : MidiInput::getAvailableDevices() ) {
        inputPortNames.add(device.name);
        inputSelector.addItem(device.name, itemId++);
    }

    itemId = 1;
    for( const MidiDeviceInfo& device : MidiOutput::getAvailableDevices() ) {
        outputPortNames.add(device.name);
        outputSelector.addItem(device.name, itemId++);
    }

    addIosLogEntry(uploadStatusLog, String::formatted(T("Core MIDI scan: %d input(s), %d output(s)."),
                                                      inputPortNames.size(),
                                                      outputPortNames.size()));

    if( inputSelector.getNumItems() > 0 && inputSelector.getSelectedId() == 0 )
        inputSelector.setSelectedId(1, sendNotificationSync);
    if( outputSelector.getNumItems() > 0 && outputSelector.getSelectedId() == 0 )
        outputSelector.setSelectedId(1, sendNotificationSync);
}

void MiosStudio::buttonClicked(Button* buttonThatWasClicked)
{
    if( buttonThatWasClicked == &refreshButton ) {
        scanIosMidiDevices();
    } else if( buttonThatWasClicked == &queryButton ) {
        startIosQuery(1);
    } else if( buttonThatWasClicked == &repeatQueryButton ) {
        startIosQuery(10);
    } else if( buttonThatWasClicked == &uploadFileButton ) {
        iosUploadFileChooser.reset(new FileChooser(T("Choose MIOS upload file"),
                                                   File(),
                                                   T("*.hex;*.syx"),
                                                   true));
        iosUploadFileChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                          [this](const FileChooser& chooser) {
            const URL result = chooser.getURLResult();
            if( result.isEmpty() )
                return;

            iosUploadFileName = result.isLocalFile()
                ? result.getLocalFile().getFileName()
                : result.toString(false);
            uploadFileLabel.setText(iosUploadFileName, dontSendNotification);
            uploadStartButton.setEnabled(true);
            addIosLogEntry(uploadStatusLog, T("Selected upload file: ") + iosUploadFileName);
        });
    } else if( buttonThatWasClicked == &uploadStartButton ) {
        addIosLogEntry(uploadStatusLog, iosUploadFileName.isNotEmpty()
            ? T("Firmware upload is not wired in this UI milestone yet.")
            : T("Select an upload file before starting."));
    } else if( buttonThatWasClicked == &uploadStopButton ) {
        addIosLogEntry(uploadStatusLog, T("No upload is currently running."));
    } else if( buttonThatWasClicked == &sendTerminalButton ) {
        sendIosTerminalCommand(terminalInput.getText());
    }
}

void MiosStudio::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if( comboBoxThatHasChanged == &inputSelector ) {
        const int index = inputSelector.getSelectedId() - 1;
        if( isPositiveAndBelow(index, inputPortNames.size()) ) {
            setMidiInput(inputPortNames[index]);
            addIosLogEntry(uploadStatusLog, T("Input: ") + inputPortNames[index]);
        }
    } else if( comboBoxThatHasChanged == &outputSelector ) {
        const int index = outputSelector.getSelectedId() - 1;
        if( isPositiveAndBelow(index, outputPortNames.size()) ) {
            setMidiOutput(outputPortNames[index]);
            addIosLogEntry(uploadStatusLog, T("Output: ") + outputPortNames[index]);
        }
    }
}

void MiosStudio::textEditorReturnKeyPressed(TextEditor& editor)
{
    if( &editor == &terminalInput )
        sendIosTerminalCommand(terminalInput.getText());
}

void MiosStudio::textEditorEscapeKeyPressed(TextEditor& editor)
{
    editor.setText(String(), dontSendNotification);
}

void MiosStudio::startIosQuery(int repeatCount)
{
    if( getActiveMidiInput().isEmpty() || getActiveMidiOutput().isEmpty() ) {
        addIosLogEntry(uploadQueryLog, T("Select both a MIDI input and MIDI output before querying."));
        return;
    }

    if( iosQueryActive || uploadHandler->busy() ) {
        addIosLogEntry(uploadQueryLog, T("Query already in progress."));
        return;
    }

    iosRepeatQueriesRemaining = jmax(1, repeatCount);
    iosQueryActive = true;
    queryButton.setEnabled(false);
    repeatQueryButton.setEnabled(false);
    deviceIdSlider.setEnabled(false);
    uploadQueryLog.clear();
    addIosLogEntry(uploadQueryLog,
                   String::formatted(T("Starting MIOS application query (%d run%s)."),
                                     iosRepeatQueriesRemaining,
                                     iosRepeatQueriesRemaining == 1 ? "" : "s"));
    if( !uploadHandler->startQuery() )
        finishIosQuery();
}

void MiosStudio::finishIosQuery()
{
    const String errorMessage = uploadHandler->finish();
    if( errorMessage.isNotEmpty() ) {
        addIosLogEntry(uploadQueryLog, T("Query failed: ") + errorMessage);
        iosRepeatQueriesRemaining = 0;
    } else {
        addIosLogEntry(uploadQueryLog, T("Query completed."));
        appendIosCoreInfo();
        --iosRepeatQueriesRemaining;
    }

    if( iosRepeatQueriesRemaining > 0 ) {
        addIosLogEntry(uploadQueryLog, String::formatted(T("Repeating query, %d remaining."), iosRepeatQueriesRemaining));
        if( uploadHandler->startQuery() )
            return;
        addIosLogEntry(uploadQueryLog, T("Could not start repeated query."));
        iosRepeatQueriesRemaining = 0;
    }

    iosQueryActive = false;
    queryButton.setEnabled(true);
    repeatQueryButton.setEnabled(true);
    deviceIdSlider.setEnabled(true);
}

void MiosStudio::sendIosTerminalCommand(const String& command)
{
    const String trimmed = command.trim();
    if( trimmed.isEmpty() )
        return;

    Array<uint8> dataArray = SysexHelper::createMios32DebugMessage(uploadHandler->getDeviceId());
    dataArray.add(0x00);
    for(int i=0; i<trimmed.length(); ++i)
        dataArray.add(trimmed[i] & 0x7f);
    dataArray.add('\n');
    dataArray.add(0xf7);
    MidiMessage message = SysexHelper::createMidiMessage(dataArray);
    sendMidiMessage(message);
    terminalInput.setText(String(), dontSendNotification);
    addIosLogEntry(terminalLog, T("> ") + trimmed);
}

void MiosStudio::addIosLogEntry(const String& textLine)
{
    addIosLogEntry(uploadStatusLog, textLine);
}

void MiosStudio::addIosLogEntry(TextEditor& editor, const String& textLine)
{
    const double timeStamp = Time::getMillisecondCounter() / 1000.0;
    editor.setText(editor.getText() + String::formatted(T("[%8.3f] "), timeStamp) + textLine + T("\n"),
                   dontSendNotification);
    editor.moveCaretToEnd();
    editor.setHighlightedRegion(Range<int>());
    editor.giveAwayKeyboardFocus();
}

void MiosStudio::appendIosCoreInfo()
{
    String str;
    if( !(str=uploadHandler->coreOperatingSystem).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Operating System: ") + str);
    if( !(str=uploadHandler->coreBoard).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Board: ") + str);
    if( !(str=uploadHandler->coreFamily).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Core Family: ") + str);
    if( !(str=uploadHandler->coreChipId).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Chip ID: 0x") + str);
    if( !(str=uploadHandler->coreSerialNumber).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Serial: #") + str);
    if( !(str=uploadHandler->coreFlashSize).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("Flash Memory Size: ") + str + T(" bytes"));
    if( !(str=uploadHandler->coreRamSize).isEmpty() )
        addIosLogEntry(uploadQueryLog, T("RAM Size: ") + str + T(" bytes"));
    if( !(str=uploadHandler->coreAppHeader1).isEmpty() )
        addIosLogEntry(uploadQueryLog, str);
    if( !(str=uploadHandler->coreAppHeader2).isEmpty() )
        addIosLogEntry(uploadQueryLog, str);
}

void MiosStudio::configureIosHeaderLabel(Label& label, const String& text)
{
    label.setText(text, dontSendNotification);
    label.setFont(Font(FontOptions(14.0f).withStyle("Bold")));
    label.setJustificationType(Justification::centredLeft);
    addAndMakeVisible(label);
}

void MiosStudio::configureIosLog(TextEditor& editor)
{
    editor.setMultiLine(true, false);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setCaretVisible(false);
    editor.setWantsKeyboardFocus(false);
    editor.setMouseClickGrabsKeyboardFocus(false);
    editor.setPopupMenuEnabled(false);
    editor.setColour(TextEditor::backgroundColourId, Colours::white);
    editor.setColour(TextEditor::outlineColourId, Colour(0xff8a8a8a));
    editor.setColour(TextEditor::shadowColourId, Colour(0x33000000));
    editor.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), String(), 13.0f)));
    addAndMakeVisible(editor);
}

void MiosStudio::paintIosPanel(Graphics& g, Rectangle<int> bounds, const String&)
{
    g.setColour(Colours::white);
    g.fillRect(bounds);
    g.setColour(Colour(0xff9aa7d8));
    g.drawRect(bounds, 1);
    g.setColour(Colour(0x18000000));
    g.drawRect(bounds.expanded(1), 1);
}

void MiosStudio::paintIosKeyboard(Graphics& g, Rectangle<int> bounds)
{
    if( bounds.isEmpty() )
        return;

    const int whiteKeys = 24;
    const float keyWidth = (float)bounds.getWidth() / (float)whiteKeys;

    g.setColour(Colours::white);
    g.fillRect(bounds);
    g.setColour(Colours::black);
    for(int i=0; i<=whiteKeys; ++i) {
        const int x = bounds.getX() + roundToInt(i * keyWidth);
        g.drawVerticalLine(x, (float)bounds.getY(), (float)bounds.getBottom());
    }
    g.drawRect(bounds, 1);

    const int blackPattern[] = { 0, 1, 3, 4, 5 };
    const int blackHeight = roundToInt(bounds.getHeight() * 0.62f);
    const int blackWidth = jmax(8, roundToInt(keyWidth * 0.58f));
    g.setColour(Colours::black);
    for(int octave=0; octave<4; ++octave) {
        for(int key : blackPattern) {
            const int whiteIndex = octave * 7 + key;
            if( whiteIndex + 1 >= whiteKeys )
                continue;
            const int x = bounds.getX() + roundToInt((whiteIndex + 1) * keyWidth - blackWidth / 2.0f);
            g.fillRect(x, bounds.getY(), blackWidth, blackHeight);
        }
    }
}
#endif


//==============================================================================
void MiosStudio::timerCallback()
{
#if JUCE_IOS
    for(int checkLoop=0; checkLoop<10; ++checkLoop) {
        if( !midiInQueue.empty() ) {
            const ScopedLock sl(midiInQueueLock);
            MidiMessage &message = midiInQueue.front();
            uint8 *data = (uint8 *)message.getRawData();
            if( data[0] >= 0x80 && data[0] < 0xf8 )
                runningStatus = data[0];

            if( runningStatus == 0xf0 &&
                SysexHelper::isValidMios32DebugMessage(data, message.getRawDataSize(), -1) &&
                (data[7] == 0x40 || data[7] == 0x00) ) {
                String text;
                for(int i=8; i<message.getRawDataSize(); ++i)
                    if( data[i] < 0x80 && (data[i] != '\n' || i + 1 < message.getRawDataSize()) )
                        text += String::formatted(T("%c"), data[i] & 0x7f);
                if( !iosReceivedTerminalMessage ) {
                    iosReceivedTerminalMessage = true;
                    addIosLogEntry(terminalLog, T("Terminal response stream started."));
                }
                addIosLogEntry(terminalLog, T("< ") + text);
            }

            addIosLogEntry(midiInLog, String::toHexString(message.getRawData(),
                                                          message.getRawDataSize()));

            midiInQueue.pop();
        }

        if( !midiOutQueue.empty() ) {
            const ScopedLock sl(midiOutQueueLock);
            MidiMessage &message = midiOutQueue.front();
            addIosLogEntry(midiOutLog, String::toHexString(message.getRawData(),
                                                           message.getRawDataSize()));
            midiOutQueue.pop();
        }
    }

    if( iosQueryActive && !uploadHandler->busy() )
        finishIosQuery();
#else
    // step-wise MIDI port scan after startup
    if( initialMidiScanCounter ) {
        switch( initialMidiScanCounter ) {
        case 1:
            Timer::stopTimer();

            // Rebuilding a ComboBox while its popup is open dismisses the
            // menu.  Startup retries can otherwise make the port selector
            // appear to close every 250 ms while a backend is settling.
            if( midiInMonitor->isPortSelectorPopupActive() ||
                midiOutMonitor->isPortSelectorPopupActive() ) {
                Timer::startTimer(100);
                break;
            }

            midiInMonitor->scanMidiDevices(inPortFromCommandLine);
            ++initialMidiScanCounter;

            Timer::startTimer(1);
            break;

        case 2:
            Timer::stopTimer();

            if( midiInMonitor->isPortSelectorPopupActive() ||
                midiOutMonitor->isPortSelectorPopupActive() ) {
                Timer::startTimer(100);
                break;
            }

            midiOutMonitor->scanMidiDevices(outPortFromCommandLine);
            ++initialMidiScanCounter;

            Timer::startTimer(1);
            break;

        case 3:
            Timer::stopTimer();

            {
                const String wantedInput = inPortFromCommandLine.isNotEmpty()
                    ? inPortFromCommandLine : getMidiInput();
                const String wantedOutput = outPortFromCommandLine.isNotEmpty()
                    ? outPortFromCommandLine : getMidiOutput();
                const bool inputMissing = wantedInput.isNotEmpty() && getActiveMidiInput().isEmpty();
                const bool outputMissing = wantedOutput.isNotEmpty() && getActiveMidiOutput().isEmpty();

                if( (inputMissing || outputMissing) && midiScanRetriesRemaining > 0 ) {
                    --midiScanRetriesRemaining;
                    initialMidiScanCounter = 1;
                    Timer::startTimer(250);
                    break;
                }
            }

            if( !runningInBatchMode() ) {
                // and check for infos
                if( commandLineInfoMessages.length() ) {
                    AlertWindow::showMessageBox(AlertWindow::InfoIcon,
                                                T("Info"),
                                                commandLineInfoMessages,
                                                String());
                    commandLineInfoMessages = String();
                }

                // now also check for command line errors
                if( commandLineErrorMessages.length() ) {
                    AlertWindow::showMessageBox(AlertWindow::WarningIcon,
                                                T("Command Line Error"),
                                                commandLineErrorMessages,
                                                String());
                    commandLineErrorMessages = String();
                }
            }

            // try to query selected core
            if( !midiInputCallbackRegistered ) {
                audioDeviceManager.addMidiInputDeviceCallback(String(), this);
                midiInputCallbackRegistered = true;
            }

            if( getActiveMidiInput().isNotEmpty() && getActiveMidiOutput().isNotEmpty() )
                uploadWindow->queryCore();

            initialMidiScanCounter = 0; // stop scan

            Timer::startTimer(1);
            break;
        }
    } else {
        // important: only broadcast 1..5 messages per timer tick to avoid GUI hangups when
        // a large bulk of data is received

        for(int checkLoop=0; checkLoop<5; ++checkLoop) {
            if( !midiInQueue.empty() ) {
                const ScopedLock sl(midiInQueueLock); // lock will be released at end of this scope

                MidiMessage &message = midiInQueue.front();

                uint8 *data = (uint8 *)message.getRawData();
                if( data[0] >= 0x80 && data[0] < 0xf8 )
                    runningStatus = data[0];

                // propagate incoming event to MIDI components
                midiInMonitor->handleIncomingMidiMessage(message, runningStatus);

                // filter runtime events for following components to improve performance
                if( data[0] < 0xf8 ) {
                    if( sysexToolWindow )
                        sysexToolWindow->handleIncomingMidiMessage(message, runningStatus);
                    if( midio128ToolWindow )
                        midio128ToolWindow->handleIncomingMidiMessage(message, runningStatus);
                    if( mbCvToolWindow )
                        mbCvToolWindow->handleIncomingMidiMessage(message, runningStatus);
                    if( mbhpMfToolWindow )
                        mbhpMfToolWindow->handleIncomingMidiMessage(message, runningStatus);
                    if( sysexLibrarianWindow )
                        sysexLibrarianWindow->handleIncomingMidiMessage(message, runningStatus);
                    if( miosFileBrowserWindow ) {
                        miosFileBrowserWindow->handleIncomingMidiMessage(message, runningStatus);
                    }
                    miosTerminal->handleIncomingMidiMessage(message, runningStatus);
                    midiKeyboard->handleIncomingMidiMessage(message, runningStatus);
                }

                midiInQueue.pop();
            }

            if( !midiOutQueue.empty() ) {
                const ScopedLock sl(midiOutQueueLock); // lock will be released at end of this scope

                MidiMessage &message = midiOutQueue.front();

                midiOutMonitor->handleIncomingMidiMessage(message, message.getRawData()[0]);

                midiOutQueue.pop();
            }
        }

        if( batchJobs.size() ) {
            if( batchWaitCounter ) {
                --batchWaitCounter;
            } else if( uploadWindow->uploadInProgress() ||
                       (sysexToolWindow && sysexToolWindow->sendSyxInProgress()) ||
                       (miosFileBrowserWindow && miosFileBrowserWindow->uploadFileInProgress()) ) {
                // wait...
            } else {
                String job(batchJobs[0]);
                batchJobs.remove(0);

                if( job.startsWithIgnoreCase("device_id ") ) {
                    int id = job.substring(10).getIntValue();
                    if( id < 0 || id > 127 ) {
                        std::cerr << "ERROR: device ID should be within 0..127!" << std::endl;
                    } else {
                        std::cout << "Setting Device ID=" << id << std::endl;
                        uploadWindow->setDeviceId(id);
                    }
                } else if( job.startsWithIgnoreCase("query") ) {
                    std::cout << "Query Core..." << std::endl;
                    uploadWindow->queryFromExternal();
                } else if( job.startsWithIgnoreCase("upload_hex ") ) {
                    String filename = job.substring(11);

                    std::cout << "Uploading " << filename << "..." << std::endl;
                    uploadWindow->uploadFileFromExternal(filename);
                } else if( job.startsWithIgnoreCase("upload_file ") ) {
                    String filename = job.substring(12);

                    std::cout << "Uploading " << filename << "..." << std::endl;
                    if( !miosFileBrowserWindow ) {
                        miosFileBrowserWindow = new MiosFileBrowserWindow(this);
                        if( !runningInBatchMode() ) {
                            miosFileBrowserWindow->setVisible(true);
                        }
                    }
                    miosFileBrowserWindow->uploadFileFromExternal(filename);
                } else if( job.startsWithIgnoreCase("send_syx ") ) {
                    String filename = job.substring(9);

                    std::cout << "Sending SysEx " << filename << "..." << std::endl;
                    if( !sysexToolWindow ) {
                        sysexToolWindow = new SysexToolWindow(this);
                        if( !runningInBatchMode() ) {
                            sysexToolWindow->setVisible(true);
                        }
                    }
                    sysexToolWindow->sendSyxFile(filename);
                } else if( job.startsWithIgnoreCase("terminal ") ) {
                    String command = job.substring(9);

                    std::cout << "MIOS Terminal command: " << command << std::endl;
                    miosTerminal->execCommand(command);
                } else if( job.startsWithIgnoreCase("wait ") ) {
                    int counter = job.substring(5).getIntValue();
                    if( counter < 0 ) {
                        counter = 0;
                    }
                    std::cout << "Waiting for " << counter << " second" << ((counter == 1) ? "" : "s") << "..." << std::endl;
                    batchWaitCounter = counter*1000;
                } else if( job.startsWithIgnoreCase("quit") ) {
                    if( runningInBatchMode() ) {
#ifdef _WIN32
                        std::cout << "Press <enter> to quit console." << std::endl;
                        while (GetAsyncKeyState(VK_RETURN) & 0x8000) {}
                        while (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) {}
#endif
                        JUCEApplication::getInstance()->setApplicationReturnValue(0); // no error
                        JUCEApplication::quit();
                    } else {
                        AlertWindow::showMessageBox(AlertWindow::InfoIcon,
                                                    T("Info"),
                                                    T("All batch jobs executed."),
                                                    String());
                    }
                } else {
                    std::cerr << "ERROR: unknown batch job: '" << job << "'!" << std::endl;
                }
            }
        }
    }
#endif
}


//==============================================================================
void MiosStudio::setMidiInput(const String &port)
{
    String previousIdentifier;
    {
        const ScopedLock sl(midiPortStateLock);
        previousIdentifier = activeMidiInputIdentifier;
    }

    const Array<MidiDeviceInfo> allMidiIns(MidiInput::getAvailableDevices());
    MidiDeviceInfo selectedInput;
    for( const MidiDeviceInfo& midiInput : allMidiIns ) {
        if( midiInput.name == port ) {
            selectedInput = midiInput;
            break;
        }
    }

    if( previousIdentifier.isNotEmpty() && previousIdentifier != selectedInput.identifier )
        audioDeviceManager.setMidiInputDeviceEnabled(previousIdentifier, false);
    if( selectedInput.identifier.isNotEmpty() )
        audioDeviceManager.setMidiInputDeviceEnabled(selectedInput.identifier, true);

    {
        const ScopedLock sl(midiPortStateLock);
        activeMidiInputName = selectedInput.name;
        activeMidiInputIdentifier = selectedInput.identifier;
    }

    // propagate port change
#if ! JUCE_IOS
    if( uploadWindow && initialMidiScanCounter == 0 && port != String() )
        uploadWindow->midiPortChanged();
#endif

    // store setting if MIDI input selected
    if( port != String() ) {
        PropertiesFile *propertiesFile = MiosStudioProperties::getInstance()->getCommonSettings(true);
        if( propertiesFile ) {
            propertiesFile->setValue(T("midiIn"), port);
            propertiesFile->setValue(T("midiInIdentifier"), selectedInput.identifier);
        }
    }
}

String MiosStudio::getMidiInput(void)
{
    // restore setting
    PropertiesFile *propertiesFile = MiosStudioProperties::getInstance()->getCommonSettings(true);
    return propertiesFile ? propertiesFile->getValue(T("midiIn"), String()) : String();
}

void MiosStudio::setMidiOutput(const String &port)
{
    MidiDeviceInfo selectedOutput;
    for( const MidiDeviceInfo& midiOutput : MidiOutput::getAvailableDevices() ) {
        if( midiOutput.name == port ) {
            selectedOutput = midiOutput;
            break;
        }
    }
    audioDeviceManager.setDefaultMidiOutputDevice(selectedOutput.identifier);

    {
        const ScopedLock sl(midiPortStateLock);
        activeMidiOutputName = selectedOutput.name;
        activeMidiOutputIdentifier = selectedOutput.identifier;
    }

    // propagate port change
#if ! JUCE_IOS
    if( uploadWindow && initialMidiScanCounter == 0 && port != String() )
        uploadWindow->midiPortChanged();
#endif

    // store setting if MIDI output selected
    if( port != String() ) {
        PropertiesFile *propertiesFile = MiosStudioProperties::getInstance()->getCommonSettings(true);
        if( propertiesFile ) {
            propertiesFile->setValue(T("midiOut"), port);
            propertiesFile->setValue(T("midiOutIdentifier"), selectedOutput.identifier);
        }
    }
}

String MiosStudio::getMidiOutput(void)
{
    // restore setting
    PropertiesFile *propertiesFile = MiosStudioProperties::getInstance()->getCommonSettings(true);
    return propertiesFile ? propertiesFile->getValue(T("midiOut"), String()) : String();
}




//==============================================================================
#if ! JUCE_IOS
StringArray MiosStudio::getMenuBarNames()
{
    const char* const names[] = { "Application", "Edit", "Tools", "Help", 0 };

    return StringArray ((const char**) names);
}

PopupMenu MiosStudio::getMenuForIndex(int topLevelMenuIndex, const String& menuName)
{
    PopupMenu menu;

    if( topLevelMenuIndex == 0 ) {
        // "Application" menu
        menu.addCommandItem(commandManager, enableMonitors);
        menu.addCommandItem(commandManager, enableUpload);
        menu.addCommandItem(commandManager, enableTerminal);
        menu.addCommandItem(commandManager, enableKeyboard);
        menu.addSeparator();
        menu.addCommandItem(commandManager, rescanDevices);
        menu.addSeparator();
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::quit);
    } else if( topLevelMenuIndex == 1 ) {
        // "Edit" menu
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::cut);
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::copy);
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::paste);
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::del);
        menu.addSeparator();
        menu.addCommandItem(commandManager, StandardApplicationCommandIDs::selectAll);
    } else if( topLevelMenuIndex == 2 ) {
        // "Tools" menu
        menu.addCommandItem(commandManager, showSysexTool);
        menu.addCommandItem(commandManager, showSysexLibrarian);
        menu.addCommandItem(commandManager, showOscTool);
        menu.addCommandItem(commandManager, showMidio128Tool);
        menu.addCommandItem(commandManager, showMbCvTool);
        menu.addCommandItem(commandManager, showMbhpMfTool);
        menu.addCommandItem(commandManager, showMiosFileBrowser);
    } else if( topLevelMenuIndex == 3 ) {
        // "Help" menu
        menu.addCommandItem(commandManager, showMiosStudioPage);
        menu.addCommandItem(commandManager, showTroubleshootingPage);
        menu.addSeparator();
        menu.addCommandItem(commandManager, showAbout);
    }

    return menu;
}

void MiosStudio::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
}


//==============================================================================
// The following methods implement the ApplicationCommandTarget interface, allowing
// this window to publish a set of actions it can perform, and which can be mapped
// onto menus, keypresses, etc.

ApplicationCommandTarget* MiosStudio::getNextCommandTarget()
{
    // this will return the next parent component that is an ApplicationCommandTarget (in this
    // case, there probably isn't one, but it's best to use this method in your own apps).
    return findFirstTargetParentComponent();
}

void MiosStudio::getAllCommands(Array <CommandID>& commands)
{
    // this returns the set of all commands that this target can perform..
    const CommandID ids[] = { showSysexTool,
                              showOscTool,
                              showMidio128Tool,
                              showMbCvTool,
                              showMbhpMfTool,
                              showSysexLibrarian,
                              showMiosFileBrowser,
                              enableMonitors,
                              enableUpload,
                              enableTerminal,
                              enableKeyboard,
                              rescanDevices,
                              StandardApplicationCommandIDs::cut,
                              StandardApplicationCommandIDs::copy,
                              StandardApplicationCommandIDs::paste,
                              StandardApplicationCommandIDs::del,
                              StandardApplicationCommandIDs::selectAll,
                              showMiosStudioPage,
                              showTroubleshootingPage,
                              showAbout
    };

    commands.addArray (ids, numElementsInArray (ids));
}

// This method is used when something needs to find out the details about one of the commands
// that this object can perform..
void MiosStudio::getCommandInfo(const CommandID commandID, ApplicationCommandInfo& result)
{
    const String applicationCategory (T("Application"));
    const String editCategory(T("Edit"));
    const String toolsCategory(T("Tools"));
    const String helpCategory (T("Help"));

    switch( commandID ) {
    case StandardApplicationCommandIDs::cut: {
        Component* target = findEditTarget();
        TextEditor* editor = dynamic_cast<TextEditor*>(target);
        LogBox* log = dynamic_cast<LogBox*>(target);
        result.setInfo(T("Cut"), T("Cuts the selection to the clipboard"), editCategory, 0);
        result.setActive((editor && !editor->isReadOnly() && !editor->getHighlightedRegion().isEmpty()) ||
                         (log && log->hasSelection()));
        result.addDefaultKeypress('X', ModifierKeys::commandModifier);
    } break;

    case StandardApplicationCommandIDs::copy: {
        Component* target = findEditTarget();
        TextEditor* editor = dynamic_cast<TextEditor*>(target);
        LogBox* log = dynamic_cast<LogBox*>(target);
        result.setInfo(T("Copy"), T("Copies the selection to the clipboard"), editCategory, 0);
        result.setActive((editor && !editor->getHighlightedRegion().isEmpty()) ||
                         (log && log->hasSelection()));
        result.addDefaultKeypress('C', ModifierKeys::commandModifier);
    } break;

    case StandardApplicationCommandIDs::paste: {
        TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget());
        result.setInfo(T("Paste"), T("Pastes text from the clipboard"), editCategory, 0);
        result.setActive(editor && !editor->isReadOnly() &&
                         SystemClipboard::getTextFromClipboard().isNotEmpty());
        result.addDefaultKeypress('V', ModifierKeys::commandModifier);
    } break;

    case StandardApplicationCommandIDs::del: {
        Component* target = findEditTarget();
        TextEditor* editor = dynamic_cast<TextEditor*>(target);
        LogBox* log = dynamic_cast<LogBox*>(target);
        result.setInfo(T("Delete"), T("Deletes the selection"), editCategory, 0);
        result.setActive((editor && !editor->isReadOnly() && !editor->getHighlightedRegion().isEmpty()) ||
                         (log && log->hasSelection()));
        result.addDefaultKeypress(KeyPress::deleteKey, ModifierKeys::noModifiers);
    } break;

    case StandardApplicationCommandIDs::selectAll: {
        Component* target = findEditTarget();
        TextEditor* editor = dynamic_cast<TextEditor*>(target);
        LogBox* log = dynamic_cast<LogBox*>(target);
        result.setInfo(T("Select All"), T("Selects all available content"), editCategory, 0);
        result.setActive((editor && editor->getTotalNumChars() > 0) ||
                         (log && log->hasEntries()));
        result.addDefaultKeypress('A', ModifierKeys::commandModifier);
    } break;

    case enableMonitors:
        result.setInfo(T("Show MIDI Monitors"), T("Enables/disables the MIDI IN/OUT Monitors"), applicationCategory, 0);
        result.setTicked(verticalDividerBarMonitors->isVisible());
        result.addDefaultKeypress('M', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case enableUpload:
        result.setInfo(T("Show Upload Window"), T("Enables/disables the Upload Window"), applicationCategory, 0);
        result.setTicked(uploadWindow->isVisible());
        result.addDefaultKeypress('U', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case enableTerminal:
        result.setInfo(T("Show MIOS Terminal"), T("Enables/disables the MIOS Terminal"), applicationCategory, 0);
        result.setTicked(miosTerminal->isVisible());
        result.addDefaultKeypress('T', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case enableKeyboard:
        result.setInfo(T("Show Virtual Keyboard"), T("Enables/disables the virtual Keyboard"), applicationCategory, 0);
        result.setTicked(midiKeyboard->isVisible());
        result.addDefaultKeypress('K', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case rescanDevices:
        result.setInfo(T("Rescan MIDI Devices"), T("Updates the MIDI In/Out port lists"), applicationCategory, 0);
        result.addDefaultKeypress('R', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showSysexTool:
        result.setInfo(T("SysEx Tool"), T("Allows to send and receive SysEx dumps"), toolsCategory, 0);
        result.setTicked(sysexToolWindow && sysexToolWindow->isVisible());
        result.addDefaultKeypress('1', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showSysexLibrarian:
        result.setInfo(T("SysEx Librarian"), T("Allows to manage SysEx files"), toolsCategory, 0);
        result.setTicked(sysexLibrarianWindow && sysexLibrarianWindow->isVisible());
        result.addDefaultKeypress('2', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showOscTool:
        result.setInfo(T("OSC Tool"), T("Allows to send and receive OSC messages"), toolsCategory, 0);
        result.setTicked(oscToolWindow && oscToolWindow->isVisible());
        result.addDefaultKeypress('3', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showMidio128Tool:
        result.setInfo(T("MIDIO128 V2 Tool"), T("Allows to configure a MIDIO128 V2"), toolsCategory, 0);
        result.setTicked(midio128ToolWindow && midio128ToolWindow->isVisible());
        result.addDefaultKeypress('4', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showMbCvTool:
        result.setInfo(T("MIDIbox CV V1 Tool"), T("Allows to configure a MIDIbox CV V1"), toolsCategory, 0);
        result.setTicked(mbCvToolWindow && mbCvToolWindow->isVisible());
        result.addDefaultKeypress('5', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showMbhpMfTool:
        result.setInfo(T("MBHP_MF_NG Tool"), T("Allows to configure the MBHP_MF_NG firmware"), toolsCategory, 0);
        result.setTicked(mbhpMfToolWindow && mbhpMfToolWindow->isVisible());
        result.addDefaultKeypress('6', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showMiosFileBrowser:
        result.setInfo(T("MIOS32 File Browser"), T("Allows to send and receive files to/from MIOS32 applications"), toolsCategory, 0);
        result.setTicked(miosFileBrowserWindow && miosFileBrowserWindow->isVisible());
        result.addDefaultKeypress('7', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showMiosStudioPage:
        result.setInfo(T("MIOS Studio Page (Web)"), T("Opens the MIOS Studio page on uCApps.de"), helpCategory, 0);
        result.addDefaultKeypress ('H', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showTroubleshootingPage:
        result.setInfo(T("MIDI Troubleshooting Page (Web)"), T("Opens the MIDI Troubleshooting page on uCApps.de"), helpCategory, 0);
        result.addDefaultKeypress ('I', ModifierKeys::commandModifier|ModifierKeys::shiftModifier);
        break;

    case showAbout:
        result.setInfo(T("About MIOS Studio"), T("Displays information about MIOS Studio"), helpCategory, 0);
        break;
    }
}

// this is the ApplicationCommandTarget method that is used to actually perform one of our commands..
bool MiosStudio::perform(const InvocationInfo& info)
{
    switch( info.commandID ) {
    case StandardApplicationCommandIDs::cut:
        if( TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget()) )
            editor->cut();
        else if( LogBox* log = dynamic_cast<LogBox*>(findEditTarget()) )
            log->cut();
        else
            return false;
        break;

    case StandardApplicationCommandIDs::copy:
        if( TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget()) )
            editor->copy();
        else if( LogBox* log = dynamic_cast<LogBox*>(findEditTarget()) )
            log->copy();
        else
            return false;
        break;

    case StandardApplicationCommandIDs::paste:
        if( TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget()) )
            editor->paste();
        else
            return false;
        break;

    case StandardApplicationCommandIDs::del:
        if( TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget()) )
            editor->insertTextAtCaret(String());
        else if( LogBox* log = dynamic_cast<LogBox*>(findEditTarget()) )
            log->deleteSelection();
        else
            return false;
        break;

    case StandardApplicationCommandIDs::selectAll:
        if( TextEditor* editor = dynamic_cast<TextEditor*>(findEditTarget()) )
            editor->selectAll();
        else if( LogBox* log = dynamic_cast<LogBox*>(findEditTarget()) )
            log->selectAll();
        else
            return false;
        break;

    case enableMonitors:
        if( verticalDividerBarMonitors->isVisible() ) {
            midiInMonitor->setVisible(false);
            verticalDividerBarMonitors->setVisible(false);
            midiOutMonitor->setVisible(false);
        } else {
            midiInMonitor->setVisible(true);
            verticalDividerBarMonitors->setVisible(true);
            midiOutMonitor->setVisible(true);
        }
        updateLayout();
        break;

    case enableUpload:        
        if( horizontalDividerBar1->isVisible() ) {
            horizontalDividerBar1->setVisible(false);
            uploadWindow->setVisible(false);
        } else {
            horizontalDividerBar1->setVisible(true);
            uploadWindow->setVisible(true);
        }
        updateLayout();
        break;

    case enableTerminal:
        if( horizontalDividerBar2->isVisible() ) {
            horizontalDividerBar2->setVisible(false);
            miosTerminal->setVisible(false);
        } else {
            horizontalDividerBar2->setVisible(true);
            miosTerminal->setVisible(true);
        }
        updateLayout();
        break;

    case enableKeyboard:
        if( horizontalDividerBar3->isVisible() ) {
            horizontalDividerBar3->setVisible(false);
            midiKeyboard->setVisible(false);
        } else {
            horizontalDividerBar3->setVisible(true);
            midiKeyboard->setVisible(true);
        }
        updateLayout();
        break;

    case rescanDevices:
        closeMidiPorts();
        initialMidiScanCounter = 1;
        midiScanRetriesRemaining = 20;
        Timer::startTimer(250);
        break;

    case showSysexTool:
        if( !sysexToolWindow )
            sysexToolWindow = new SysexToolWindow(this);
        sysexToolWindow->setVisible(true);
        sysexToolWindow->toFront(true);
        break;

    case showOscTool:
        if( !oscToolWindow )
            oscToolWindow = new OscToolWindow(this);
        oscToolWindow->setVisible(true);
        oscToolWindow->toFront(true);
        break;

    case showMidio128Tool:
        if( !midio128ToolWindow )
            midio128ToolWindow = new Midio128ToolWindow(this);
        midio128ToolWindow->setVisible(true);
        midio128ToolWindow->toFront(true);
        break;

    case showMbCvTool:
        if( !mbCvToolWindow )
            mbCvToolWindow = new MbCvToolWindow(this);
        mbCvToolWindow->setVisible(true);
        mbCvToolWindow->toFront(true);
        break;

    case showMbhpMfTool:
        if( !mbhpMfToolWindow )
            mbhpMfToolWindow = new MbhpMfToolWindow(this);
        mbhpMfToolWindow->setVisible(true);
        mbhpMfToolWindow->toFront(true);
        break;

    case showSysexLibrarian:
        if( !sysexLibrarianWindow )
            sysexLibrarianWindow = new SysexLibrarianWindow(this);
        sysexLibrarianWindow->setVisible(true);
        sysexLibrarianWindow->toFront(true);
        break;

    case showMiosFileBrowser:
        if( !miosFileBrowserWindow )
            miosFileBrowserWindow = new MiosFileBrowserWindow(this);
        miosFileBrowserWindow->setVisible(true);
        miosFileBrowserWindow->toFront(true);
        break;

    case showMiosStudioPage: {
        URL webpage(T("http://www.uCApps.de/mios_studio.html"));
        webpage.launchInDefaultBrowser();
    }  break;

    case showTroubleshootingPage: {
        URL webpage(T("http://www.uCApps.de/howto_debug_midi.html"));
        webpage.launchInDefaultBrowser();
    } break;

    case showAbout: {
        const String message =
            T("Version ") + String(T(MIOS_STUDIO_VERSION)) + T("\n\n")
            + T("MIOS Studio is a cross-platform utility for configuring, monitoring ")
            + T("and updating MIDIbox devices running MIOS or MIOS32. It provides ")
            + T("firmware upload, MIDI and OSC monitors, a MIOS terminal and ")
            + T("device-specific tools.\n\n")
            + T("Original application Copyright (C) 2010 Thorsten Klose\n")
            + T("Modernisation Copyright (C) 2026 Phil Taylor");

        AlertWindow::showMessageBox(AlertWindow::InfoIcon,
                                    T("About MIOS Studio"),
                                    message,
                                    T("OK"),
                                    this);
    } break;

    default:
        return false;
    }

    return true;
};


void MiosStudio::updateLayout(void)
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    layoutHComps.clear();
    horizontalLayout.clearAllItems();

    int itemIx = 0;
    if( verticalDividerBarMonitors->isVisible() ) {
        horizontalLayout.setItemLayout(itemIx++,    50,   -1, -1); // MIDI In/Out Monitors
        layoutHComps.add(0);
    }

    if( uploadWindow->isVisible() ) {
        if( itemIx ) {
            horizontalLayout.setItemLayout(itemIx++,    8,      8,     8); // Resizer
            layoutHComps.add(horizontalDividerBar1);
        }

        horizontalLayout.setItemLayout(itemIx++,   186,    186,  186); // Upload Window
        layoutHComps.add(uploadWindow);
    }

    if( miosTerminal->isVisible() ) {
        if( itemIx ) {
            horizontalLayout.setItemLayout(itemIx++,    8,      8,     8); // Resizer
            layoutHComps.add(horizontalDividerBar2);
        }

        horizontalLayout.setItemLayout(itemIx++,   50,   -1, -1); // MIOS Terminal
        layoutHComps.add(miosTerminal);

    }

    if( midiKeyboard->isVisible() ) {
        if( itemIx ) {
            horizontalLayout.setItemLayout(itemIx++,    8,      8,     8); // Resizer
            layoutHComps.add(horizontalDividerBar3);
        }

        horizontalLayout.setItemLayout(itemIx++,   124,    124,  124); // MIDI Keyboard
        layoutHComps.add(midiKeyboard);
    }

    // dummy to ensure that MIDI keyboard or upload window is displayed with right size if all other components invisible
    horizontalLayout.setItemLayout(itemIx++,   0, 0, 0);
    layoutHComps.add(0);

    ////////////////////////////////////////////////////////////////////////////////////////////////
    layoutVComps.clear();
    verticalLayoutMonitors.clearAllItems();
    if( verticalDividerBarMonitors->isVisible() ) {
        //                                   num  min   max   prefered  
        verticalLayoutMonitors.setItemLayout(0, -0.2, -0.8, -0.5); // MIDI In Monitor
        layoutVComps.add(midiInMonitor);

        verticalLayoutMonitors.setItemLayout(1,    8,    8,    8); // resizer
        layoutVComps.add(verticalDividerBarMonitors);

        verticalLayoutMonitors.setItemLayout(2, -0.2, -0.8, -0.5); // MIDI Out Monitor
        layoutVComps.add(midiOutMonitor);
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////
    resized();
}
#endif
