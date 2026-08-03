/* -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*- */
// $Id$
/*
 * MIOS Studio Main
 *
 * ==========================================================================
 *
 *  Copyright (C) 2010 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#include "includes.h"
#include "version.h"
#include "gui/MiosStudio.h"

juce_ImplementSingleton (MiosStudioProperties)

class MiosStudioWindow
#if JUCE_IOS
    : public Component,
      private Timer
#else
    : public DocumentWindow
#endif
{
public:

    //==============================================================================
    MiosStudioWindow()
#if JUCE_IOS
    {
        contentComponent.reset(new MiosStudio());
        viewport.reset(new Viewport(T("MIOS Studio Viewport")));
        viewport->setViewedComponent(contentComponent.get(), false);
        viewport->setScrollBarsShown(false, false);
        viewport->addMouseListener(this, true);
        configureNestedScrollControls(*contentComponent);
        addAndMakeVisible(*viewport);
        Component::SafePointer<Viewport> safeViewport(viewport.get());
        Timer::callAfterDelay(100, [safeViewport] {
            if( safeViewport != nullptr )
                safeViewport->setViewPosition(0, 0);
        });
        startTimerHz(60);
        setSize(1024, 768);
        setVisible(true);
    }
#else
        : DocumentWindow(String(T("MIOS Studio ")) + String(T(MIOS_STUDIO_VERSION)),
                         Colours::lightgrey,
                         DocumentWindow::allButtons,
                         true)
    {
        // Create an instance of our main content component, and add it 
        // to our window.
        MiosStudio* const contentComponent = new MiosStudio();
        setContentOwned(contentComponent, true);
        setUsingNativeTitleBar(true);
        centreWithSize(getWidth(), getHeight());

        int guiX = (contentComponent->initialGuiX >= 0) ? contentComponent->initialGuiX : getX();
        int guiY = (contentComponent->initialGuiY >= 0) ? contentComponent->initialGuiY : getY();
        setTopLeftPosition(guiX, guiY);

        if( contentComponent->initialGuiTitle.length() )
            setName(contentComponent->initialGuiTitle);

        setMenuBar(contentComponent);

        if( !contentComponent->runningInBatchMode() )
            setVisible(true);
    }
#endif

    ~MiosStudioWindow()
    {
#if ! JUCE_IOS
        setMenuBar(0);
#endif
        // (the content component will be deleted automatically, so no need to do it here)
    }

    //==============================================================================
#if JUCE_IOS
    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xffc1d0ff));
    }

    void paintOverChildren(Graphics& g) override
    {
        if( headerChromeAlpha <= 0.0f )
            return;

        auto header = getHeaderBounds();
        g.setColour(Colour(0xffedf1ff).withAlpha(0.65f * headerChromeAlpha));
        g.fillRect(header);
        g.setColour(Colour(0xff9aa7d8).withAlpha(0.35f * headerChromeAlpha));
        g.drawHorizontalLine(header.getBottom() - 1, 0.0f, (float)getWidth());
        g.setColour(Colours::black.withAlpha(headerChromeAlpha));
        g.setFont(Font(FontOptions(18.0f).withStyle(T("Bold"))));
        g.drawText(getHeaderTitle(),
                   getHeaderTitleBounds(header),
                   Justification::centred);
    }

    void timerCallback() override
    {
        if( viewport == 0 )
            return;

        const float targetAlpha = (viewport->getViewPositionY() > 0 || headerDragActive) ? 1.0f : 0.0f;
        const String title = getHeaderTitle();
        const bool titleChanged = title != lastHeaderTitle;
        lastHeaderTitle = title;

        if( headerChromeAlpha != targetAlpha ) {
            const float delta = targetAlpha > headerChromeAlpha ? 0.12f : -0.16f;
            headerChromeAlpha = jlimit(0.0f, 1.0f, headerChromeAlpha + delta);
            if( std::abs(headerChromeAlpha - targetAlpha) < 0.02f )
                headerChromeAlpha = targetAlpha;
            repaint(getHeaderBounds());
        } else if( titleChanged && headerChromeAlpha > 0.0f ) {
            repaint(getHeaderBounds());
        }

        updateKeyboardAvoidance();
    }

    void resized() override
    {
        if( viewport == 0 || contentComponent == 0 )
            return;

        auto bounds = getLocalBounds();
        bounds.removeFromTop(getSafeAreaTop());
        viewport->setBounds(bounds);

        const bool phoneWidth = bounds.getWidth() < 760;
        const bool phoneLandscape = bounds.getWidth() >= 760 && bounds.getHeight() < 560;
        const int contentHeight = phoneWidth
            ? jmax(bounds.getHeight(), 1180)
            : phoneLandscape
            ? jmax(bounds.getHeight(), 720)
            : bounds.getHeight();
        contentComponent->setSize(bounds.getWidth(), contentHeight);
    }

    void mouseDown(const MouseEvent& e) override
    {
        headerDragActive = false;
    }

    void mouseDrag(const MouseEvent& e) override
    {
        if( headerDragActive || e.getDistanceFromDragStart() > 4 ) {
            headerDragActive = true;
            repaint(getHeaderBounds());
        }
    }

    void mouseUp(const MouseEvent&) override
    {
        headerDragActive = false;
    }
#else
    void closeButtonPressed()
    {
        // When the user presses the close button, we'll tell the app to quit. This
        // window will be deleted by our MiosStudioApplication::shutdown() method
        // 
        JUCEApplication::quit();
    }
#endif

#if JUCE_IOS
private:
    Rectangle<int> getHeaderBounds() const
    {
        return getLocalBounds().removeFromTop(getSafeAreaTop() + 44);
    }

    Rectangle<int> getHeaderTitleBounds(Rectangle<int> header) const
    {
        return header.withTrimmedTop(getSafeAreaTop()).withTrimmedBottom(4);
    }

    int getSafeAreaTop() const
    {
        if( auto* display = Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()) ) {
            const int topInset = display->safeAreaInsets.getTop();
            if( topInset > 0 )
                return topInset;
        }

        return getWidth() < 760 ? 59 : 24;
    }

    const String getHeaderTitle() const
    {
        if( contentComponent != nullptr )
            return contentComponent->getIosActiveToolPageName();

        return T("MIOS Studio");
    }

    void configureNestedScrollControls(Component& component)
    {
        for( int i = 0; i < component.getNumChildComponents(); ++i ) {
            if( Component* child = component.getChildComponent(i) ) {
                if( Viewport* nestedViewport = dynamic_cast<Viewport*>(child) ) {
                    nestedViewport->setViewportIgnoreDragFlag(true);
                    nestedViewport->setScrollOnDragMode(Viewport::ScrollOnDragMode::all);
                }

                configureNestedScrollControls(*child);
            }
        }
    }

    void updateKeyboardAvoidance()
    {
        if( viewport == 0 || contentComponent == 0 )
            return;

        Component* focused = Component::getCurrentlyFocusedComponent();
        if( focused == nullptr || focused == contentComponent.get() || !contentComponent->isParentOf(focused) )
            return;

        if( dynamic_cast<TextEditor*>(focused) == nullptr )
            return;

        const int keyboardBottom = getKeyboardAvoidanceInsetBottom();
        if( keyboardBottom <= 0 )
            return;

        const Rectangle<int> focusedBounds = contentComponent->getLocalArea(focused, focused->getLocalBounds()).expanded(0, 10);
        const int visibleBottom = viewport->getViewPositionY() + viewport->getHeight() - keyboardBottom - 8;

        if( focusedBounds.getBottom() > visibleBottom ) {
            viewport->setViewPosition(viewport->getViewPositionX(),
                                      focusedBounds.getBottom() - viewport->getHeight() + keyboardBottom + 8);
        } else if( focusedBounds.getY() < viewport->getViewPositionY() + 8 ) {
            viewport->setViewPosition(viewport->getViewPositionX(),
                                      jmax(0, focusedBounds.getY() - 8));
        }
    }

    int getKeyboardAvoidanceInsetBottom() const
    {
        if( auto* display = Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()) )
            if( const int keyboardInset = display->keyboardInsets.getBottom() )
                return keyboardInset;

        const int viewportHeight = viewport != 0 ? viewport->getHeight() : getHeight();
        return getWidth() < 760
            ? jlimit(280, 390, roundToInt((float)viewportHeight * 0.45f))
            : jlimit(220, 320, roundToInt((float)viewportHeight * 0.38f));
    }

    std::unique_ptr<MiosStudio> contentComponent;
    std::unique_ptr<Viewport> viewport;
    float headerChromeAlpha = 0.0f;
    String lastHeaderTitle;
    bool headerDragActive = false;
#endif
};

//==============================================================================
/** This is the application object that is started up when Juce starts. It handles
    the initialisation and shutdown of the whole application.
*/
class JUCEMiosStudioApplication : public JUCEApplication
{
    /* Important! NEVER embed objects directly inside your JUCEApplication class! Use
       ONLY pointers to objects, which you should create during the initialise() method
       (NOT in the constructor!) and delete in the shutdown() method (NOT in the
       destructor!)

       This is because the application object gets created before Juce has been properly
       initialised, so any embedded objects would also get constructed too soon.
   */
    MiosStudioWindow* miosStudioWindow;

public:
    //==============================================================================
    JUCEMiosStudioApplication()
        : miosStudioWindow (0)
    {
        // NEVER do anything in here that could involve any Juce function being called
        // - leave all your startup tasks until the initialise() method.
    }

    ~JUCEMiosStudioApplication()
    {
        // Your shutdown() method should already have done all the things necessary to
        // clean up this app object, so you should never need to put anything in
        // the destructor.

        // Making any Juce calls in here could be very dangerous...
    }

    //==============================================================================
    void initialise (const String& commandLine)
    {
        // create the main window...
        miosStudioWindow = new MiosStudioWindow();
#if JUCE_IOS
        miosStudioWindow->addToDesktop(0);
        miosStudioWindow->setBounds(Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt());
        miosStudioWindow->toFront(true);
#endif

        /*  ..and now return, which will fall into to the main event
            dispatch loop, and this will run until something calls
            JUCEAppliction::quit().

            In this case, JUCEAppliction::quit() will be called by the
            hello world window being clicked.
        */
    }

    void shutdown()
    {
        // clear up..
        if( miosStudioWindow != 0 )
            deleteAndZero(miosStudioWindow);
    }


    //==============================================================================
    const String getApplicationName()
    {
        return T("MIOS Studio");
    }

    const String getApplicationVersion()
    {
        return T(MIOS_STUDIO_VERSION);
    }

    bool moreThanOneInstanceAllowed()
    {
        return true;
    }

    void anotherInstanceStarted (const String& commandLine)
    {
    }
};


//==============================================================================
// This macro creates the application's main() function..
START_JUCE_APPLICATION (JUCEMiosStudioApplication)
