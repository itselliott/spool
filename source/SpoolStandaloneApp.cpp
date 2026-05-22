// =============================================================================
// Custom standalone entry point.
//
// JUCE's default StandaloneFilterApp ships an "Options" hamburger menu with
// Audio/MIDI Settings, Save/Load state, Reset — but it's hardcoded inside
// StandaloneFilterWindow with no public hook to add items. To extend it,
// we replace the entire JUCEApplication entry point:
//
//   1. Define JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 in CMake to suppress
//      JUCE's default app definition.
//   2. Subclass StandaloneFilterWindow → hide JUCE's "Options" button and
//      add our own that pops up the full menu (JUCE's items + SPOOL items
//      for credits / bug report / GitHub / Ko-fi).
//   3. Define our own juce_CreateApplication() returning this subclass.
//   4. START_JUCE_APPLICATION wires it.
// =============================================================================

// JUCE's StandaloneFilterWindow header depends on a stack of other JUCE
// modules — include them BEFORE the standalone window so all types it
// references (StandalonePluginHolder, AudioIODeviceCallback, etc.) are
// fully declared. PluginEditor.h pulls in the processors / utils chain.
#include "PluginEditor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    constexpr const char* kSpoolVersion = "v1.0.1";

    // Hide JUCE's hardcoded "Options" TextButton so we can put our own in
    // the same spot. JUCE doesn't expose the button publicly, but the
    // window adds it as a child component — we find it by text match.
    juce::TextButton* findJuceOptionsButton (juce::StandaloneFilterWindow& w)
    {
        for (int i = 0; i < w.getNumChildComponents(); ++i)
            if (auto* b = dynamic_cast<juce::TextButton*> (w.getChildComponent (i)))
                if (b->getButtonText() == "Options")
                    return b;
        return nullptr;
    }
}

//==============================================================================
class SpoolStandaloneWindow  : public juce::StandaloneFilterWindow
{
public:
    SpoolStandaloneWindow (const juce::String& title,
                           juce::Colour backgroundColour,
                           std::unique_ptr<juce::StandalonePluginHolder> pluginHolderIn)
        : juce::StandaloneFilterWindow (title, backgroundColour, std::move (pluginHolderIn))
    {
        // Hide JUCE's hardcoded options button and slot our own into its place.
        if (auto* juceBtn = findJuceOptionsButton (*this))
            juceBtn->setVisible (false);

        spoolOptionsButton.setButtonText ("Options");
        spoolOptionsButton.setTriggeredOnMouseDown (true);
        spoolOptionsButton.onClick = [this] { showOptionsMenu(); };
        juce::Component::addAndMakeVisible (spoolOptionsButton);
    }

    void resized() override
    {
        juce::StandaloneFilterWindow::resized();
        // Mirror JUCE's original options-button placement so the title bar
        // layout doesn't shift visually.
        spoolOptionsButton.setBounds (8, 6, 60, getTitleBarHeight() - 8);
    }

private:
    void showOptionsMenu()
    {
        juce::PopupMenu m;
        // ---- JUCE's standard items first --------------------------------
        m.addItem (1, "Audio/MIDI Settings...");
        m.addSeparator();
        m.addItem (2, "Save current state...");
        m.addItem (3, "Load a saved state...");
        m.addSeparator();
        m.addItem (4, "Reset to default state");
        // ---- SPOOL's additions ------------------------------------------
        m.addSeparator();
        m.addItem (5, "About / Credits...");
        m.addItem (6, "Report a Bug (email)");
        m.addItem (7, "GitHub (source + releases)");
        m.addItem (8, "Tip on Ko-fi");
        m.addSeparator();
        m.addItem (9, juce::String ("SPOOL ") + kSpoolVersion, false /* disabled label */);

        m.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (&spoolOptionsButton),
            [this] (int choice)
            {
                switch (choice)
                {
                    case 1: pluginHolder->showAudioSettingsDialog(); break;
                    case 2: pluginHolder->askUserToSaveState();      break;
                    case 3: pluginHolder->askUserToLoadState();      break;
                    case 4: resetToDefaultState();                   break;
                    case 5: showCreditsOverlay();                    break;
                    case 6: juce::URL ("mailto:elliottdevs@gmail.com"
                                       "?subject=SPOOL%20Bug%20Report"
                                       "&body=SPOOL%20version%3A%20" + juce::String (kSpoolVersion) +
                                       "%0AOS%3A%20%0A%0A"
                                       "What%20happened%3A%0A%0A"
                                       "Steps%20to%20reproduce%3A%0A").launchInDefaultBrowser();
                            break;
                    case 7: juce::URL ("https://github.com/itselliott/spool")
                                 .launchInDefaultBrowser(); break;
                    case 8: juce::URL ("https://ko-fi.com/itselliott")
                                 .launchInDefaultBrowser(); break;
                    default: break;
                }
            });
    }

    void showCreditsOverlay()
    {
        // Reach into the plugin editor and reopen the welcome / credits card.
        if (auto* proc = getAudioProcessor())
            if (auto* editor = proc->getActiveEditor())
                if (auto* spoolEditor = dynamic_cast<SpoolAudioProcessorEditor*> (editor))
                    spoolEditor->showWelcomeOverlay();
    }

    juce::TextButton spoolOptionsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpoolStandaloneWindow)
};

//==============================================================================
// JUCEApplication subclass — clones StandaloneFilterApp and only overrides
// createWindow() to return our subclass.
//
class SpoolStandaloneApp  : public juce::JUCEApplication
{
public:
    SpoolStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        mainWindow = createWindow();
        if (mainWindow != nullptr)
            mainWindow->setVisible (true);
        else
            pluginHolder = createPluginHolder();
    }

    void shutdown() override
    {
        pluginHolder.reset();
        mainWindow.reset();
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)        pluginHolder->savePluginState();
        if (mainWindow   != nullptr)        mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    std::unique_ptr<SpoolStandaloneWindow> createWindow()
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            return nullptr;

        return std::make_unique<SpoolStandaloneWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel()
                .findColour (juce::ResizableWindow::backgroundColourId),
            createPluginHolder());
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] {
            JucePlugin_PreferredChannelConfigurations
        };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (
            channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif
        return std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(), false, juce::String{}, nullptr,
            channelConfig, false);
    }

    juce::ApplicationProperties             appProperties;
    std::unique_ptr<SpoolStandaloneWindow>  mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};

//==============================================================================
// JUCE picks this up when JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 is defined.
// START_JUCE_APPLICATION expands to the platform-specific main()/WinMain()
// plus the juce_CreateApplication() factory, so we don't need to define
// it manually.
#if JucePlugin_Build_Standalone
 START_JUCE_APPLICATION (SpoolStandaloneApp)
#endif
