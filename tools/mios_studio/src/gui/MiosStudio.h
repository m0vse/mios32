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

#ifndef _MIOS_STUDIO_H
#define _MIOS_STUDIO_H

#include "../includes.h"
#include <atomic>
#include <functional>
#include <memory>
#include <queue>

#include "UploadWindow.h"
#include "MidiMonitor.h"
#include "MiosTerminal.h"
#include "MidiKeyboard.h"
#include "SysexTool.h"
#include "OscTool.h"
#include "Midio128Tool.h"
#include "MbCvTool.h"
#include "MbhpMfTool.h"
#include "SysexLibrarian.h"
#include "MiosFileBrowser.h"
#include "../SysexPatchDb.h"
#include "../UploadHandler.h"
#include "../SysexHelper.h"

#if JUCE_IOS
class IosClipboardTextEditor
    : public TextEditor
    , private Timer
{
public:
    void setLongPressClipboardMenuEnabled(bool shouldEnable);

    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    void timerCallback() override;
    void showClipboardMenu();

    bool longPressClipboardMenuEnabled = false;
    bool longPressMenuActive = false;
};

class IosDrawerRowButton
    : public Button
{
public:
    IosDrawerRowButton();

    void paintButton(Graphics& g, bool isMouseOverButton, bool isButtonDown) override;
};

class IosStepperLookAndFeel
    : public LookAndFeel_V2
{
public:
    Button* createSliderButton(Slider&, bool isIncrement) override;
};
#endif

class MiosStudio
    : public Component
    , public MidiInputCallback
#if ! JUCE_IOS
    , public MenuBarModel
    , public ApplicationCommandTarget
#else
    , public Button::Listener
    , public ComboBox::Listener
    , public TextEditor::Listener
#endif
    , public Timer
{
public:
    enum CommandIDs {
        enableMonitors             = 0x1010,
        enableUpload               = 0x1011,
        enableTerminal             = 0x1012,
        enableKeyboard             = 0x1013,
        rescanDevices              = 0x1100,
        showSysexTool              = 0x2000,
        showOscTool                = 0x2001,
        showMidio128Tool           = 0x2002,
        showMbCvTool               = 0x2003,
        showMbhpMfTool             = 0x2004,
        showSysexLibrarian         = 0x2005,
        showMiosFileBrowser        = 0x2006,
        showMiosStudioPage         = 0x3000,
        showTroubleshootingPage    = 0x3001,
        showAbout                  = 0x3002,
    };

    //==============================================================================
    MiosStudio();
    ~MiosStudio();

    //==============================================================================
    LookAndFeel_V2 myLookAndFeel;
    
    //==============================================================================
    void redirectIOToConsole();

    //==============================================================================
    void paint (Graphics& g);
    void resized();

    //==============================================================================
    bool runningInBatchMode(void);

    //==============================================================================
    void handleIncomingMidiMessage(MidiInput* source, const MidiMessage& message);

    void timerCallback();

    void sendMidiMessage(MidiMessage &message);
    void closeMidiPorts(void);

    void setMidiInput(const String &port);
    String getMidiInput(void);
    void setMidiOutput(const String &port);
    String getMidiOutput(void);
    String getActiveMidiInput(void);
    String getActiveMidiOutput(void);
    bool reconnectMidiPortsForUpload(const String &applicationInput,
                                     const String &applicationOutput,
                                     bool connectToBootloader,
                                     int timeoutMs);

#if JUCE_IOS
    void buttonClicked(Button* buttonThatWasClicked);
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged);
    void mouseDown(const MouseEvent& e);
    void mouseDrag(const MouseEvent& e);
    void mouseUp(const MouseEvent& e);
    void textEditorReturnKeyPressed(TextEditor& editor);
    void textEditorEscapeKeyPressed(TextEditor& editor);
    void textEditorTextChanged(TextEditor&) {}
    void textEditorFocusLost(TextEditor&) {}
#else
    StringArray getMenuBarNames();
    PopupMenu getMenuForIndex(int topLevelMenuIndex, const String& menuName);
    void menuItemSelected(int menuItemID, int topLevelMenuIndex);

    ApplicationCommandTarget* getNextCommandTarget();
    void getAllCommands(Array <CommandID>& commands);
#if JUCE_MAJOR_VERSION==1 && JUCE_MINOR_VERSION<51
    void getCommandInfo(const CommandID commandID, ApplicationCommandInfo& result);
#else
    void getCommandInfo(CommandID commandID, ApplicationCommandInfo& result);
#endif
	bool perform(const InvocationInfo& info);
#endif

    void updateLayout(void);

    AudioDeviceManager audioDeviceManager;

    UploadHandler *uploadHandler;

#if ! JUCE_IOS
    // Windows opened by Tools button in Upload Window
    SysexToolWindow *sysexToolWindow;
    OscToolWindow *oscToolWindow;
    Midio128ToolWindow *midio128ToolWindow;
    MbCvToolWindow *mbCvToolWindow;
    MbhpMfToolWindow *mbhpMfToolWindow;
    SysexLibrarianWindow *sysexLibrarianWindow;
    MiosFileBrowserWindow *miosFileBrowserWindow;
#endif

    //==============================================================================
    SysexPatchDb *sysexPatchDb;

    //==============================================================================
    int initialGuiX; // if -1: centered
    int initialGuiY; // if -1: centered
    String initialGuiTitle;

    //==============================================================================
	// This is needed by MSVC in debug mode (please #ifdef if it causes Mac problems)
    juce_UseDebuggingNewOperator
protected:
    //==============================================================================
    bool batchMode;
    bool duggleMode;
    String commandLineErrorMessages;
    String commandLineInfoMessages;
    String inPortFromCommandLine;
    String outPortFromCommandLine;

    StringArray batchJobs;
    unsigned batchWaitCounter;

    //==============================================================================
#if JUCE_IOS
    enum IosToolPage {
        iosToolStudio = 0,
        iosToolSysexTool,
        iosToolSysexLibrarian,
        iosToolOsc,
        iosToolMidio128,
        iosToolMbCv,
        iosToolMbhpMf,
        iosToolFileBrowser,
        iosToolCount
    };

    Label inputLabel;
    Label outputLabel;
    Label deviceIdLabel;
    Label drawerHintLabel;
    Label drawerScrim;
    Label drawerBackground;
    Label midiInHeader;
    Label midiOutHeader;
    Label deviceStatusHeader;
    Label uploadFileLabel;
    Label uploadStatusHeader;
    Label terminalHeader;
    Label keyboardHeader;
    ComboBox inputSelector;
    ComboBox outputSelector;
    Slider deviceIdSlider;
    TextButton refreshButton;
    TextButton queryButton;
    TextButton repeatQueryButton;
    TextButton uploadFileButton;
    TextButton uploadStartButton;
    TextButton uploadStopButton;
    TextButton sendTerminalButton;
    IosDrawerRowButton toolButtons[iosToolCount];
    IosStepperLookAndFeel iosStepperLookAndFeel;
    IosClipboardTextEditor terminalInput;
    IosClipboardTextEditor midiInLog;
    IosClipboardTextEditor midiOutLog;
    IosClipboardTextEditor uploadQueryLog;
    IosClipboardTextEditor uploadStatusLog;
    IosClipboardTextEditor terminalLog;
    std::unique_ptr<SysexTool> iosSysexTool;
    std::unique_ptr<SysexLibrarian> iosSysexLibrarian;
    std::unique_ptr<OscTool> iosOscTool;
    std::unique_ptr<Midio128Tool> iosMidio128Tool;
    std::unique_ptr<MbCvTool> iosMbCvTool;
    std::unique_ptr<MbhpMfTool> iosMbhpMfTool;
    std::unique_ptr<MiosFileBrowser> iosMiosFileBrowser;
    Rectangle<int> keyboardBounds;
    StringArray inputPortNames;
    StringArray outputPortNames;
    std::unique_ptr<FileChooser> iosUploadFileChooser;
    String iosUploadFileName;
    bool iosQueryActive;
    int iosRepeatQueriesRemaining;
    bool iosReceivedTerminalMessage;
    bool iosDrawerOpen;
    bool iosDrawerEdgeDragActive;
    float iosDrawerAnimation;
    Point<int> iosDrawerDragStart;
    IosToolPage iosActiveToolPage;

    void initialiseIosUi();
    void scanIosMidiDevices();
    void startIosQuery(int repeatCount);
    void finishIosQuery();
    void sendIosTerminalCommand(const String& command);
    void addIosLogEntry(const String& textLine);
    void addIosLogEntry(TextEditor& editor, const String& textLine);
    void appendIosCoreInfo();
    void configureIosHeaderLabel(Label& label, const String& text);
    void configureIosLog(IosClipboardTextEditor& editor);
    void configureIosDrawerButton(IosDrawerRowButton& button, const String& text);
    void setIosDrawerOpen(bool shouldBeOpen);
    void setIosActiveToolPage(IosToolPage page);
    Component* getIosToolPageComponent(IosToolPage page) const;
    const String getIosToolPageName(IosToolPage page) const;
    void paintIosPanel(Graphics& g, Rectangle<int> bounds, const String& title);
    void paintIosKeyboard(Graphics& g, Rectangle<int> bounds);
#else
    UploadWindow *uploadWindow;
    MidiMonitor *midiInMonitor;
    MidiMonitor *midiOutMonitor;
    MiosTerminal *miosTerminal;
    MidiKeyboard *midiKeyboard;

    Array<Component *> layoutHComps;
    StretchableLayoutManager horizontalLayout;
    StretchableLayoutResizerBar* horizontalDividerBar1;
    StretchableLayoutResizerBar* horizontalDividerBar2;
    StretchableLayoutResizerBar* horizontalDividerBar3;

    Array<Component *> layoutVComps;
    StretchableLayoutManager verticalLayoutMonitors;
    StretchableLayoutResizerBar* verticalDividerBarMonitors;

    ResizableCornerComponent *resizer;
    ComponentBoundsConstrainer resizeLimits;
#endif

    // TK: the Juce specific "MidiBuffer" sporatically throws an assertion when overloaded
    // therefore I'm using a std::queue instead
    std::queue<MidiMessage> midiInQueue;
    CriticalSection midiInQueueLock;
    uint8 runningStatus;

    std::queue<MidiMessage> midiOutQueue;
    CriticalSection midiOutQueueLock;

    Array<uint8> sysexReceiveBuffer;

    int initialMidiScanCounter;
    int midiScanRetriesRemaining;
    bool midiInputCallbackRegistered;

    CriticalSection midiPortStateLock;
    String activeMidiInputName;
    String activeMidiInputIdentifier;
    String activeMidiOutputName;
    String activeMidiOutputIdentifier;

    bool runMidiPortOperationOnMessageThread(const std::function<void(MiosStudio&)>& operation,
                                             int timeoutMs);

    // the command manager object used to dispatch command events
#if ! JUCE_IOS
    ApplicationCommandManager* commandManager;
#endif

    //==============================================================================
    // (prevent copy constructor and operator= being generated..)
    MiosStudio (const MiosStudio&);
    const MiosStudio& operator= (const MiosStudio&);
};

#endif /* _MIOS_STUDIO_H */
