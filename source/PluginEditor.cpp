#include "PluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <BinaryData.h>     // bg.png embedded via juce_add_binary_data

namespace
{
    // Vertical handheld field-recorder proportions.
    constexpr int kWindowW = 540;
    constexpr int kWindowH = 920;
    constexpr int kBezel   = 18;       // outer aluminium frame inset

    // Machined-aluminium palette.
    const auto kAlumLight   = juce::Colour::fromRGB (0xd8, 0xd8, 0xdc);   // body highlight
    const auto kAlumMid     = juce::Colour::fromRGB (0xc4, 0xc4, 0xc8);   // body base
    const auto kAlumDark    = juce::Colour::fromRGB (0xa0, 0xa0, 0xa6);   // body shadow
    const auto kPanelDark   = juce::Colour::fromRGB (0x22, 0x22, 0x26);   // dark inner sub-panel
    const auto kPanelMid    = juce::Colour::fromRGB (0xa6, 0xa6, 0xac);   // mid grey for control bg
    const auto kSeam        = juce::Colour::fromRGB (0x70, 0x70, 0x76);   // precision seam line
    const auto kScrew       = juce::Colour::fromRGB (0x44, 0x44, 0x4a);   // screw head
    const auto kScrewDark   = juce::Colour::fromRGB (0x20, 0x20, 0x24);   // screw slot

    // OLED display.
    const auto kOledBg      = juce::Colour::fromRGB (0x08, 0x08, 0x0a);
    juce::Colour kOledRim   = juce::Colour::fromRGB (0x35, 0x1a, 0x05);   // (themable)
    juce::Colour kOledText  = juce::Colour::fromRGB (0xff, 0xa6, 0x4a);   // (themable)
    juce::Colour kOledDim   = juce::Colour::fromRGB (0x80, 0x4a, 0x18);   // (themable)

    // Accents — kAccent is mutable so the SP-L easter-egg can re-tint it.
    juce::Colour kAccent    = juce::Colour::fromRGB (0xff, 0x5a, 0x14);   // record orange
    const auto kRecord      = juce::Colour::fromRGB (0xe5, 0x3a, 0x3a);   // active red

    // Aliases used throughout the file.
    const auto kPanel = kPanelDark;
    const auto kDim   = juce::Colour::fromRGB (0x44, 0x44, 0x48);
    const auto kText  = juce::Colour::fromRGB (0x18, 0x18, 0x1c);
}

//==============================================================================
// JuiceKnob
//
JuiceKnob::JuiceKnob (const juce::String& displayName)
{
    nameLabel.setText (displayName, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, kText);
    nameLabel.setFont   (juce::Font (juce::FontOptions { 50.0f, juce::Font::bold }));
    // Let JuiceKnob receive mouse events over the icon area (for reordering).
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider.setColour (juce::Slider::rotarySliderFillColourId,    kAccent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, kPanel);
    slider.setColour (juce::Slider::thumbColourId,               kAccent.brighter (0.2f));
    addAndMakeVisible (slider);

    // Value label is added LAST so it paints on top of the slider (centred
    // inside the knob via resized()). Don't let it eat mouse events — clicks
    // on the centre of the knob should fall through to the slider.
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, kAccent);
    valueLabel.setFont   (juce::Font (juce::FontOptions { 14.0f, juce::Font::bold }));
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);
}

void JuiceKnob::setValueText (const juce::String& text)
{
    valueLabel.setText (text, juce::dontSendNotification);
}

void JuiceKnob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setColour (juce::Slider::thumbColourId,            c.brighter (0.2f));
    slider.setColour (juce::Slider::trackColourId,            c);
    valueLabel.setColour (juce::Label::textColourId, c);
    repaint();
}

void JuiceKnob::mouseDown (const juce::MouseEvent& e)
{
    // Only count clicks on the top icon strip when reorderable.
    if (reorderable && e.position.y < (float) nameLabel.getBottom())
        reorderDragging = true;
}

void JuiceKnob::mouseUp (const juce::MouseEvent& e)
{
    if (reorderDragging)
    {
        reorderDragging = false;
        if (onReorderDrop)
        {
            const auto p = e.getEventRelativeTo (getParentComponent()).getPosition();
            onReorderDrop (p);
        }
    }
}

void JuiceKnob::tickLed (float deltaSeconds) noexcept
{
    if (ledAlpha > 0.0f)
    {
        ledAlpha = juce::jmax (0.0f, ledAlpha - deltaSeconds * 6.0f);
        repaint();
    }
}

void JuiceKnob::paint (juce::Graphics& g)
{
    if (ledAlpha > 0.0f)
    {
        auto sa = slider.getBounds();
        const float cx = (float) sa.getRight() - 8.0f;
        const float cy = (float) sa.getY()     + 8.0f;
        g.setColour (kAccent.withAlpha (ledAlpha));
        g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    }

    // Vector icon (if set) drawn centred in the name slot, normalised so all
    // JUICE knob icons render at the same visual size.
    if (! iconPath.isEmpty())
    {
        auto area = nameLabel.getBounds().toFloat().reduced (8.0f);
        const auto pb = iconPath.getBounds();
        if (pb.getWidth() > 0.5f && pb.getHeight() > 0.5f)
        {
            const float scale = juce::jmin (area.getWidth()  / pb.getWidth(),
                                            area.getHeight() / pb.getHeight());
            auto t = juce::AffineTransform::translation (-pb.getCentreX(), -pb.getCentreY())
                         .scaled (scale)
                         .translated (area.getCentreX(), area.getCentreY());
            g.setColour (kText);
            g.strokePath (iconPath, juce::PathStrokeType (4.5f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded), t);
        }
    }
}

void JuiceKnob::setIconPath (juce::Path p)
{
    iconPath = std::move (p);
    nameLabel.setVisible (iconPath.isEmpty());
    repaint();
}

void JuiceKnob::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact) return;
    compact = shouldBeCompact;
    // 75% bigger icon font + bolder. Value sits INSIDE the knob now.
    nameLabel .setFont (juce::Font (juce::FontOptions { compact ? 28.0f : 50.0f, juce::Font::bold }));
    valueLabel.setFont (juce::Font (juce::FontOptions { compact ? 11.0f : 14.0f, juce::Font::bold }));
    resized();
}

void JuiceKnob::resized()
{
    auto r = getLocalBounds();
    const int topH = compact ? 34 : 58;        // bigger name slot for bigger icons
    nameLabel.setBounds (r.removeFromTop (topH));
    // Fill the entire area below nameLabel — no inner reduction — so the
    // slider's hit-box covers the full visible knob (including the corners
    // of the well around the metal body).
    slider   .setBounds (r);

    // Value label sits CENTRED inside the knob (over the dial). It must be
    // toFront() so it draws on top of the slider component.
    auto sb = slider.getBounds();
    const int valW = juce::jmin (sb.getWidth() - 10, compact ? 80 : 110);
    const int valH = compact ? 14 : 18;
    valueLabel.setBounds (sb.getCentreX() - valW / 2,
                          sb.getCentreY() - valH / 2,
                          valW, valH);
    valueLabel.toFront (false);
}

//==============================================================================
// BpmControl — small draggable BPM readout with tap-tempo
//
void SpoolAudioProcessorEditor::BpmControl::mouseDown (const juce::MouseEvent& e)
{
    dragStartBpm = proc.getBpm();
    dragStartY   = e.getPosition().getY();
}

void SpoolAudioProcessorEditor::BpmControl::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = dragStartY - e.getPosition().getY();   // up = positive
    const double scale = e.mods.isShiftDown() ? 0.1 : 0.5;
    const double newBpm = juce::jlimit (40.0, 240.0, dragStartBpm + (double) dy * scale);
    proc.setBpm (newBpm);
    repaint();
}

void SpoolAudioProcessorEditor::BpmControl::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Black OLED-style window, thin accent rim. Just the number — no tag.
    g.setColour (juce::Colour::fromRGB (0x07, 0x07, 0x0a));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (kOledRim);
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

    const juce::String bpmTxt = juce::String ((int) std::round (proc.getBpm()));

    g.setFont (juce::Font (juce::FontOptions { b.getHeight() * 0.78f, juce::Font::bold }));
    // Neon glow: cascade 3 wider passes underneath, then sharp on top.
    for (int i = 3; i > 0; --i)
    {
        g.setColour (kOledText.withAlpha (0.10f / (float) i));
        g.drawText (bpmTxt, b.expanded ((float) i, (float) i),
                    juce::Justification::centred, false);
    }
    g.setColour (kOledText);
    g.drawText (bpmTxt, b, juce::Justification::centred, false);
}

//==============================================================================
// TapeReelComponent — DJ scratch interaction
//
float TapeReelComponent::angleFromMouse (const juce::MouseEvent& e) const noexcept
{
    auto b = getLocalBounds().toFloat();
    return std::atan2 ((float) e.getPosition().getY() - b.getCentreY(),
                       (float) e.getPosition().getX() - b.getCentreX());
}

float TapeReelComponent::wrapPi (float a) noexcept
{
    while (a >   juce::MathConstants<float>::pi) a -= juce::MathConstants<float>::twoPi;
    while (a <= -juce::MathConstants<float>::pi) a += juce::MathConstants<float>::twoPi;
    return a;
}

void TapeReelComponent::mouseDown (const juce::MouseEvent& e)
{
    scratching        = true;
    lastMouseAngle    = angleFromMouse (e);
    lastScratchTimeMs = juce::Time::getMillisecondCounterHiRes();
}

void TapeReelComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! scratching) return;
    const float newAngle  = angleFromMouse (e);
    const float deltaAng  = wrapPi (newAngle - lastMouseAngle);
    angle += deltaAng;
    repaint();

    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt  = (now - lastScratchTimeMs) * 0.001;
    if (dt > 0.0005)
    {
        // Map angular velocity (rad/sec) → playback rate (1.0 = normal speed).
        // Base vinyl spin = 0.55 rotations/sec, so one full rotation == ~1.8 s
        // of audio at 1.0× — scratching at that rate equals normal playback.
        constexpr float kBaseRotPerSec = 0.55f;
        const float angVel = (float) (deltaAng / dt);
        const float rate   = angVel / (kBaseRotPerSec * juce::MathConstants<float>::twoPi);
        if (onScratchUpdate) onScratchUpdate (rate);
    }
    lastMouseAngle    = newAngle;
    lastScratchTimeMs = now;
}

void TapeReelComponent::mouseUp (const juce::MouseEvent&)
{
    scratching = false;
    if (onScratchEnd) onScratchEnd();
}

void TapeReelComponent::paint (juce::Graphics& g)
{
    // Vinyl record. Concentric grooves + central orange label + spindle.
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // Vinyl body — deep black.
    g.setColour (juce::Colour::fromRGB (0x07, 0x07, 0x0a));
    g.fillEllipse (bounds);

    // Concentric grooves — many thin rings, almost-but-not-quite black.
    g.setColour (juce::Colour::fromRGB (0x16, 0x16, 0x1a));
    const int   numGrooves   = 26;
    const float innerLabelR  = radius * 0.34f;
    const float outerGrooveR = radius * 0.98f;
    for (int i = 0; i < numGrooves; ++i)
    {
        const float t = (float) i / (float) (numGrooves - 1);
        const float r = innerLabelR + t * (outerGrooveR - innerLabelR);
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 0.6f);
    }

    // Rotating element: a thin radial sheen + the orange label-marker. Wrap
    // these in the rotation so they actually spin with the disc.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

        // Subtle radial sheen — implies motion.
        g.setColour (juce::Colour::fromRGB (0x2a, 0x2a, 0x2e).withAlpha (0.55f));
        g.drawLine (centre.x, centre.y - radius * 0.95f,
                    centre.x, centre.y + radius * 0.95f, 1.0f);

        // Label area — flat orange disc with subtle ring.
        g.setColour (kAccent);
        g.fillEllipse (centre.x - innerLabelR, centre.y - innerLabelR,
                       innerLabelR * 2.0f, innerLabelR * 2.0f);
        g.setColour (kAccent.darker (0.3f));
        g.drawEllipse (centre.x - innerLabelR + 1.0f, centre.y - innerLabelR + 1.0f,
                       innerLabelR * 2.0f - 2.0f, innerLabelR * 2.0f - 2.0f, 1.0f);

        // Small dark mark on the label so the spin is visible.
        const float markerR = innerLabelR * 0.55f;
        g.setColour (juce::Colour::fromRGB (0x10, 0x10, 0x12));
        g.fillEllipse (centre.x + markerR - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }

    // Spindle (centre hole) — always centred, doesn't rotate.
    const float spindleR = radius * 0.06f;
    g.setColour (juce::Colour::fromRGB (0x02, 0x02, 0x04));
    g.fillEllipse (centre.x - spindleR, centre.y - spindleR,
                   spindleR * 2.0f, spindleR * 2.0f);
}

//==============================================================================
// One-shot welcome / credits / donation overlay shown the first time SPOOL
// opens on a machine. Dismissed by clicking "Got it"; a persistent flag in
// %APPDATA%/SPOOL/preferences.settings stops it from reappearing.
//
// Defined here (not at file scope below) so the editor's resized() and
// destructor can see the full type when they touch the unique_ptr<WelcomeOverlay>.
//
class SpoolAudioProcessorEditor::WelcomeOverlay : public juce::Component
{
public:
    WelcomeOverlay (SpoolAudioProcessorEditor& ownerIn) : owner (ownerIn)
    {
        // OK / dismiss button
        okBtn.setButtonText ("Got it");
        okBtn.setColour (juce::TextButton::buttonColourId,   kAccent);
        okBtn.setColour (juce::TextButton::buttonOnColourId, kAccent.brighter (0.15f));
        okBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
        okBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        okBtn.onClick = [this] { owner.dismissWelcomeOverlay(); };
        addAndMakeVisible (okBtn);

        // Ko-fi support
        kofiBtn.setButtonText ("Tip on Ko-fi");
        kofiBtn.setColour (juce::TextButton::buttonColourId,   kPanelDark);
        kofiBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
        kofiBtn.setColour (juce::TextButton::textColourOffId,
                           juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
        kofiBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        kofiBtn.onClick = []
        {
            juce::URL ("https://ko-fi.com/itselliott").launchInDefaultBrowser();
        };
        addAndMakeVisible (kofiBtn);

        // GitHub link
        ghBtn.setButtonText ("GitHub");
        ghBtn.setColour (juce::TextButton::buttonColourId,   kPanelDark);
        ghBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
        ghBtn.setColour (juce::TextButton::textColourOffId,
                         juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
        ghBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        ghBtn.onClick = []
        {
            juce::URL ("https://github.com/itselliott/spool").launchInDefaultBrowser();
        };
        addAndMakeVisible (ghBtn);

        setInterceptsMouseClicks (true, true);
    }

    void paint (juce::Graphics& g) override
    {
        // Dim the panel behind the card so the popup reads as modal.
        g.fillAll (juce::Colour::fromRGB (0x05, 0x05, 0x07).withAlpha (0.78f));

        // Card body — silver bezel matching the chassis.
        const auto card = cardBounds().toFloat();
        juce::ColourGradient bezelGrad (
            juce::Colour::fromRGB (0xd8, 0xd8, 0xdc), card.getX(), card.getY(),
            juce::Colour::fromRGB (0x6d, 0x6d, 0x72), card.getX(), card.getBottom(),
            false);
        g.setGradientFill (bezelGrad);
        g.fillRoundedRectangle (card, 12.0f);

        // Inner dark panel.
        const auto inner = card.reduced (10.0f);
        g.setColour (juce::Colour::fromRGB (0x12, 0x12, 0x16));
        g.fillRoundedRectangle (inner, 8.0f);
        g.setColour (kAccent.withAlpha (0.4f));
        g.drawRoundedRectangle (inner.reduced (0.5f), 8.0f, 1.0f);

        // SP·L logo block.
        const float logoSize = 56.0f;
        const auto logoArea = juce::Rectangle<float> (
            inner.getX() + 20.0f, inner.getY() + 22.0f, logoSize, logoSize);
        juce::ColourGradient logoGrad (
            juce::Colour::fromRGB (0xd8, 0xd8, 0xdc),
            logoArea.getX(), logoArea.getY(),
            juce::Colour::fromRGB (0x4a, 0x4a, 0x50),
            logoArea.getRight(), logoArea.getBottom(),
            false);
        g.setGradientFill (logoGrad);
        g.fillRoundedRectangle (logoArea, 10.0f);
        g.setColour (juce::Colour::fromRGB (0x18, 0x18, 0x1c));
        g.setFont (juce::Font (juce::FontOptions {
            juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold }));
        g.drawText (juce::String::fromUTF8 ("SP\xC2\xB7L"),
                    logoArea, juce::Justification::centred, false);

        // Title "SPOOL v1.0.1"
        const auto titleArea = juce::Rectangle<float> (
            logoArea.getRight() + 14.0f, logoArea.getY(),
            inner.getWidth() - logoArea.getWidth() - 50.0f, 28.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions { 26.0f, juce::Font::bold }));
        g.drawText ("SPOOL", titleArea, juce::Justification::topLeft, false);
        g.setColour (juce::Colour::fromRGB (0x88, 0x88, 0x92));
        g.setFont (juce::Font (juce::FontOptions {
            juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain }));
        // Plain ASCII separators only — JUCE's default Windows font fallback
        // chain doesn't have glyphs for the unicode bullet / em-dash, so even
        // properly encoded UTF-8 ends up as tofu. Slash + double-hyphen read
        // cleanly and ship in every system font.
        g.drawText ("v1.0.1  /  GPL-3.0",
                    titleArea.translated (0.0f, 30.0f),
                    juce::Justification::topLeft, false);

        // Body copy
        const auto body = juce::Rectangle<float> (
            inner.getX() + 20.0f, logoArea.getBottom() + 22.0f,
            inner.getWidth() - 40.0f, 130.0f);
        g.setColour (juce::Colour::fromRGB (0xcc, 0xcc, 0xd0));
        g.setFont (juce::Font (juce::FontOptions { 14.0f, juce::Font::plain }));

        const juce::String bodyText =
            "A sampler / looper / field recorder in the spirit of the $1,499 "
            "Teenage Engineering TP-7.\n\n"
            "Built by itselliott. Free and open-source forever. "
            "If SPOOL earned a spot in your toolbox, a small tip keeps "
            "development going. No pressure -- the plugin stays free either way.";
        g.drawFittedText (bodyText, body.toNearestInt(),
                          juce::Justification::topLeft, 8);
    }

    void resized() override
    {
        // Layout buttons at the bottom of the card.
        const auto card  = cardBounds();
        const auto inner = card.reduced (10);
        const int  btnH  = 30;
        const int  pad   = 16;
        const int  btnY  = inner.getBottom() - btnH - pad;

        const int leftW  = 110;
        kofiBtn.setBounds (inner.getX() + pad,             btnY, leftW, btnH);
        ghBtn  .setBounds (inner.getX() + pad + leftW + 8, btnY, 86,    btnH);

        const int okW = 86;
        okBtn.setBounds (inner.getRight() - pad - okW, btnY, okW, btnH);
    }

private:
    juce::Rectangle<int> cardBounds() const
    {
        const int w = juce::jmin (getWidth() - 40, 440);
        const int h = 340;
        return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
    }

    SpoolAudioProcessorEditor& owner;
    juce::TextButton okBtn, kofiBtn, ghBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomeOverlay)
};

namespace
{
    juce::PropertiesFile::Options welcomePrefsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SPOOL";
        o.filenameSuffix      = "settings";
        o.folderName          = "SPOOL";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    constexpr const char* kWelcomeShownKey = "welcomeShown_v1";
}

//==============================================================================
SpoolAudioProcessorEditor::SpoolAudioProcessorEditor (SpoolAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    juce::Logger::writeToLog ("editor ctor");
    setSize (kWindowW, kWindowH);
    loadBackgroundImage();
    setWantsKeyboardFocus (true);   // so keys 1-8 fire slots

    auto styleButton = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,   kPanelDark);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId,  juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        addAndMakeVisible (b);
    };

    styleButton (loadButton);
    styleButton (recordButton);
    styleButton (overdubButton);
    styleButton (playButton);
    styleButton (stopButton);
    styleButton (loopButton);
    styleButton (exportButton);

    // Transport buttons draw vector shapes, no text.

    overdubButton.setClickingTogglesState (true);
    overdubButton.setToggleState (processorRef.isOverdubEnabled(), juce::dontSendNotification);
    overdubButton.onClick = [this]
    {
        processorRef.setOverdubEnabled (overdubButton.getToggleState());
    };

    // Transport buttons paint their own vector shapes — no font LnF needed.

    // Utility row icons get a beefier glyph (≈75% of button height).
    loadButton    .setLookAndFeel (&utilityIconLnf);
    overdubButton .setLookAndFeel (&utilityIconLnf);
    loopButton    .setLookAndFeel (&utilityIconLnf);
    exportButton  .setLookAndFeel (&utilityIconLnf);

    // Status indicator pills (comp voice + tape machine): dark pill with a
    // glowing coloured LED instead of a flat colour block.
    inputCompButton  .setLookAndFeel (&ledPillLnf);
    tapeMachineButton.setLookAndFeel (&ledPillLnf);
    // FREEZE: vector snowflake so the glyph is guaranteed regardless of font.
    freezeButton.setLookAndFeel (&snowflakeLnf);

    // All rotary knobs use the digital-dial flat appearance.
    inputKnob     .getSlider().setLookAndFeel (&brushedKnobLnf);
    speedKnob     .getSlider().setLookAndFeel (&brushedKnobLnf);
    sampleGainKnob.getSlider().setLookAndFeel (&brushedKnobLnf);
    filterKnob      .getSlider().setLookAndFeel (&brushedKnobLnf);
    ghostKnob   .getSlider().setLookAndFeel (&brushedKnobLnf);
    hazeKnob      .getSlider().setLookAndFeel (&brushedKnobLnf);

    // RECORD: red glyph always (so the button reads as "record" even at rest),
    // red panel when armed.
    recordButton.setColour (juce::TextButton::textColourOffId, kRecord);
    recordButton.setColour (juce::TextButton::buttonOnColourId, kRecord);
    recordButton.setClickingTogglesState (true);

    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (processorRef.isLooping(), juce::dontSendNotification);
    // Toggle-state utility buttons highlight in accent when on, dark otherwise,
    // so they follow the SP-L theme like everything else.
    loopButton    .setColour (juce::TextButton::buttonColourId,   kPanelDark);
    loopButton    .setColour (juce::TextButton::buttonOnColourId, kAccent);
    loopButton    .setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    overdubButton .setColour (juce::TextButton::buttonColourId,   kPanelDark);
    overdubButton .setColour (juce::TextButton::buttonOnColourId, kAccent);
    overdubButton .setColour (juce::TextButton::textColourOnId,   juce::Colours::black);

    loadButton  .onClick = [this] { openFileChooser(); };
    recordButton.onClick = [this]
    {
        if (recordButton.getToggleState())
        {
            processorRef.startRecording();
        }
        else
        {
            processorRef.stopRecording();
            // stopRecording() auto-plays with loop on (RC-505 style).
            loopButton.setToggleState (true, juce::dontSendNotification);
            repaint();
        }
    };
    playButton.onClick = [this] { processorRef.togglePlay(); };
    stopButton.onClick = [this] { processorRef.stop(); };
    loopButton  .onClick = [this] { processorRef.setLooping (loopButton.getToggleState()); };
    exportButton.onClick = [this] { openExportChooser(); };
    // Drag the EXPORT button to drop the current loop region (or whole
    // sample if no region) as a WAV onto a DAW track, Explorer, etc.
    exportButton.onDragOut = [this] { return processorRef.renderLoopToTempWav(); };

    // Status label is now redundant — OLED display in the top-right shows
    // filename, position, REC state, and NO INPUT warnings. Don't add it.
    (void) statusLabel;

    // ---- NUDGE slider: bare horizontal track, no label, no text box --------
    nudgeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    nudgeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    nudgeSlider.setRange (-(double) SpoolAudioProcessor::kMaxNudgeSeconds,
                          (double) SpoolAudioProcessor::kMaxNudgeSeconds, 0.001);
    nudgeSlider.setValue (0.0, juce::dontSendNotification);
    nudgeSlider.setDoubleClickReturnValue (true, 0.0);
    nudgeSlider.setColour (juce::Slider::trackColourId,       kAccent);
    nudgeSlider.setColour (juce::Slider::backgroundColourId,  kPanelDark);
    nudgeSlider.setColour (juce::Slider::thumbColourId,       kAccent.brighter (0.2f));
    nudgeSlider.onValueChange = [this]
    {
        processorRef.setLoopAnchorOffset ((float) nudgeSlider.getValue());
        repaint();   // highlight follows the slider live
    };
    // When the user lets go, bake the offset into the loop region and snap
    // the slider back to centre so they can keep nudging from the new spot.
    nudgeSlider.onDragEnd = [this]
    {
        processorRef.commitNudgeOffset();
        nudgeSlider.setValue (0.0, juce::dontSendNotification);
        repaint();
    };
    addChildComponent (nudgeSlider);

    addAndMakeVisible (reel);

    // Grab-the-vinyl scratching → drive playback rate from drag velocity.
    reel.onScratchUpdate = [this] (float rate)
    {
        processorRef.setScratching (true);
        processorRef.setScratchRate (rate);
    };
    reel.onScratchEnd = [this]
    {
        processorRef.setScratching (false);
        processorRef.setScratchRate (0.0f);
    };

    // JUCE Standalone auto-mutes input to prevent feedback loops on shared
    // devices (laptop mic + laptop speakers). For a sampler/recorder we
    // explicitly want the input — force-unmute on construction. We have to
    // set the juce::Value (not the underlying atomic) so the notification
    // banner's Value::Listener fires and hides the warning.
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->getMuteInputValue().setValue (false);

    // --- Clear-sample button (✕ in top-right of waveform) -------------------
    clearSampleButton.setButtonText (juce::String::fromUTF8 ("\xE2\x9C\x95")); // ✕
    clearSampleButton.setColour (juce::TextButton::buttonColourId,
                                 juce::Colours::black.withAlpha (0.55f));
    clearSampleButton.setColour (juce::TextButton::buttonOnColourId, kRecord);
    clearSampleButton.setColour (juce::TextButton::textColourOffId,
                                 juce::Colour::fromRGB (0xc0, 0xc0, 0xc4));
    clearSampleButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    clearSampleButton.setLookAndFeel (&utilityIconLnf);
    clearSampleButton.onClick = [this]
    {
        processorRef.clearSample();
        repaint();
    };
    addAndMakeVisible (clearSampleButton);

    // --- RESET LED: small red circle in the header, between wordmark and
    //     OLED. Matches the comp + tape LED indicators (LedPillLookAndFeel).
    //     One click resets every knob / button / effect to factory default;
    //     slots 1-8 and the loaded sample stay intact.
    resetParamsButton.setButtonText ({});
    resetParamsButton.setColour (juce::TextButton::buttonColourId, kRecord);
    resetParamsButton.setLookAndFeel (&ledPillLnf);
    resetParamsButton.onClick = [this]
    {
        processorRef.resetAllParameters();
        syncControlsFromProcessor();
        repaint();
    };
    addAndMakeVisible (resetParamsButton);

    // --- Folder browse + prev/next sample (in OLED area) -------------------
    auto styleOledBtn = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     juce::Colours::black.withAlpha (0.55f));
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId, kOledText);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.setLookAndFeel (&utilityIconLnf);
        addAndMakeVisible (b);
    };

    folderButton.setButtonText (juce::String::fromUTF8 ("\xE2\x97\xAB")); // ◫
    folderButton.onClick = [this]
    {
        folderChooser = std::make_unique<juce::FileChooser> (
            "Choose a folder of samples", juce::File{});
        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectDirectories;
        folderChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                processorRef.setFolder (folder);
                repaint();
            }
        });
    };
    styleOledBtn (folderButton);

    folderPrevButton.setButtonText ("-");
    folderPrevButton.onClick = [this]
    {
        processorRef.loadPrevInFolder();
        repaint();
    };
    styleOledBtn (folderPrevButton);

    folderNextButton.setButtonText ("+");
    folderNextButton.onClick = [this]
    {
        processorRef.loadNextInFolder();
        repaint();
    };
    styleOledBtn (folderNextButton);

    // --- Loop slot buttons (1..8) --------------------------------------------
    for (int i = 0; i < SpoolAudioProcessor::kNumSlots; ++i)
    {
        auto& b = slotButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.setColour (juce::TextButton::buttonColourId,   kPanelDark);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId,
                     juce::Colour::fromRGB (0x90, 0x90, 0x94));   // dim when empty
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        b.onClick = [this, i] { triggerSlot (i); };
        b.onRightClick = [this, i] { processorRef.clearSlot (i); };
        addAndMakeVisible (b);
    }

    // --- INPUT knob: wet/dry of HEAVY 12:1 preamp ---------------------------
    addAndMakeVisible (inputKnob);
    inputKnob.setCompact (true);
    {
        auto& s = inputKnob.getSlider();
        s.setRange (0.0, 1.0, 0.01);
        s.setValue ((double) processorRef.getInputMix(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.0);
        inputKnob.setValueText (juce::String ((int) (s.getValue() * 100.0)));
        s.onValueChange = [this]
        {
            const auto v = (float) inputKnob.getSlider().getValue();
            processorRef.setInputMix (v);
            inputKnob.setValueText (juce::String ((int) (v * 100.0)));
        };
    }

    // --- SPEED knob (varispeed + lo-fi degradation as you slow down) -------
    addAndMakeVisible (speedKnob);
    speedKnob.setCompact (true);
    {
        auto& s = speedKnob.getSlider();
        s.setRange (0.5, 2.0, 0.001);
        s.setSkewFactorFromMidPoint (1.0);
        s.setValue ((double) processorRef.getPlaybackSpeed(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 1.0);
        speedKnob.setValueText ("");   // icon-only knob — position implies value
        s.onValueChange = [this]
        {
            processorRef.setPlaybackSpeed ((float) speedKnob.getSlider().getValue());
        };
    }

    // --- SAMPLE GAIN knob (trim, next to the waveform — compact layout) -----
    addAndMakeVisible (sampleGainKnob);
    sampleGainKnob.setCompact (true);
    {
        auto& s = sampleGainKnob.getSlider();
        s.setRange (0.0, 2.0, 0.01);
        s.setSkewFactorFromMidPoint (1.0);
        s.setValue ((double) processorRef.getSampleGain(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 1.0);
        sampleGainKnob.setValueText (juce::String (juce::Decibels::gainToDecibels ((float) s.getValue(), -60.0f), 1));
        s.onValueChange = [this]
        {
            const auto v = (float) sampleGainKnob.getSlider().getValue();
            processorRef.setSampleGain (v);
            sampleGainKnob.setValueText (juce::String (juce::Decibels::gainToDecibels (v, -60.0f), 1));
        };
    }

    // --- INPUT comp cycle button: no label, just a colour-cycling lozenge ---
    {
        inputCompButton.setButtonText ("");
        inputCompButton.onClick = [this]
        {
            const int next = (processorRef.getInputCompMode() + 1) % SpoolAudioProcessor::kNumCompModes;
            processorRef.setInputCompMode (next);
            applyInputCompColour();
        };
        addAndMakeVisible (inputCompButton);
        applyInputCompColour();
    }

    // --- JUICE knobs ---------------------------------------------------------
    addAndMakeVisible (filterKnob);
    addAndMakeVisible (ghostKnob);
    addAndMakeVisible (hazeKnob);

    // Uniform vector icons so the three JUICE labels render at equal size.
    {
        // FILTER: smooth LP→HP slope arc.
        juce::Path filter;
        filter.startNewSubPath (0.0f, 30.0f);
        filter.cubicTo (15.0f, 30.0f, 25.0f, 6.0f, 40.0f, 6.0f);
        filterKnob.setIconPath (filter);

        // GHOST: two-cycle sine wave (echo trail).
        juce::Path ghost;
        ghost.startNewSubPath (0.0f, 18.0f);
        for (int i = 1; i <= 40; ++i)
            ghost.lineTo ((float) i,
                          18.0f - 12.0f * std::sin ((float) i / 10.0f * juce::MathConstants<float>::twoPi));
        ghostKnob.setIconPath (ghost);

        // HAZE: three stacked tildes (reverb wash).
        juce::Path haze;
        for (int row = 0; row < 3; ++row)
        {
            const float yBase = 4.0f + row * 12.0f;
            haze.startNewSubPath (0.0f, yBase);
            for (int i = 1; i <= 40; ++i)
                haze.lineTo ((float) i,
                             yBase - 4.0f * std::sin ((float) i / 7.5f * juce::MathConstants<float>::twoPi));
        }
        hazeKnob.setIconPath (haze);
    }

    // Drag-to-reorder: click the icon strip of a knob, release over another
    // knob's column to swap their position in the signal path.
    filterKnob.setReorderable (true);
    ghostKnob .setReorderable (true);
    hazeKnob  .setReorderable (true);
    filterKnob.onReorderDrop = [this] (juce::Point<int> p) { handleKnobReorder (SpoolAudioProcessor::EffectFilter, p.getX()); };
    ghostKnob .onReorderDrop = [this] (juce::Point<int> p) { handleKnobReorder (SpoolAudioProcessor::EffectGhost,  p.getX()); };
    hazeKnob  .onReorderDrop = [this] (juce::Point<int> p) { handleKnobReorder (SpoolAudioProcessor::EffectHaze,   p.getX()); };

    // FILTER: DJ-style LP/HP sweep. 0=full LP, 0.5=bypass, 1=full HP.
    {
        auto& s = filterKnob.getSlider();
        s.setRange (0.0, 1.0, 0.001);
        s.setValue ((double) processorRef.getFilterPos(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.5);
        auto formatFilter = [] (float p) -> juce::String
        {
            if (std::abs (p - 0.5f) < 0.02f) return "OFF";
            if (p < 0.5f)
            {
                const float hz = 200.0f + (p * 2.0f) * (20000.0f - 200.0f);
                return juce::String::formatted ("LP %.1fk", hz / 1000.0f);
            }
            const float hz = 20.0f + ((p - 0.5f) * 2.0f) * (4000.0f - 20.0f);
            return juce::String::formatted ("HP %.0f", hz);
        };
        filterKnob.setValueText (formatFilter (processorRef.getFilterPos()));
        s.onValueChange = [this, formatFilter]
        {
            const auto v = (float) filterKnob.getSlider().getValue();
            processorRef.setFilterPos (v);
            filterKnob.setValueText (formatFilter (v));
        };
    }

    // FILTER row — three smaller cycle pills replace the single Q button:
    //   Q     cycles LO/MD/HI resonance
    //   RATE  cycles OFF/1-2/1-4/.../1-128 tempo-synced LFO rate
    //   RANGE cycles SM/MD/LG LFO depth
    auto setupCyclePill = [this] (juce::TextButton& b, const juce::String& initial,
                                  std::function<void()> onClick)
    {
        b.setColour (juce::TextButton::buttonColourId,   kPanelDark);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId,
                     juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        b.setButtonText (initial);
        b.onClick = std::move (onClick);
        addAndMakeVisible (b);
    };

    setupCyclePill (filterQButton,
        SpoolAudioProcessor::getFilterQLabel (processorRef.getFilterQMode()),
        [this]
        {
            const int next = (processorRef.getFilterQMode() + 1) % SpoolAudioProcessor::kNumFilterQModes;
            processorRef.setFilterQMode (next);
            filterQButton.setButtonText (SpoolAudioProcessor::getFilterQLabel (next));
        });

    setupCyclePill (filterLfoRateButton,
        SpoolAudioProcessor::getFilterLfoRateLabel (processorRef.getFilterLfoRate()),
        [this]
        {
            const int next = (processorRef.getFilterLfoRate() + 1) % SpoolAudioProcessor::kNumFilterLfoRates;
            processorRef.setFilterLfoRate (next);
            filterLfoRateButton.setButtonText (SpoolAudioProcessor::getFilterLfoRateLabel (next));
        });

    setupCyclePill (filterLfoRangeButton,
        SpoolAudioProcessor::getFilterLfoRangeLabel (processorRef.getFilterLfoRange()),
        [this]
        {
            const int next = (processorRef.getFilterLfoRange() + 1) % SpoolAudioProcessor::kNumFilterLfoRanges;
            processorRef.setFilterLfoRange (next);
            filterLfoRangeButton.setButtonText (SpoolAudioProcessor::getFilterLfoRangeLabel (next));
        });

    // --- SQUEEZE knob (one-knob pumping compressor) + FILTER button --------
    {
        auto& s = ghostKnob.getSlider();
        s.setRange (0.0, 1.0, 0.01);
        s.setValue ((double) processorRef.getGhostAmount(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.0);
        ghostKnob.setValueText (juce::String ((int) (s.getValue() * 100.0)));
        s.onValueChange = [this]
        {
            const auto v = (float) ghostKnob.getSlider().getValue();
            processorRef.setGhostAmount (v);
            ghostKnob.setValueText (juce::String ((int) (v * 100.0)));
        };
    }

    // FILTER button — just the current mode in a tiny black box (OFF / LP / HP / BP).
    ghostTimeButton.setColour (juce::TextButton::buttonColourId,   kPanelDark);
    ghostTimeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    ghostTimeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
    ghostTimeButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    ghostTimeButton.setButtonText (SpoolAudioProcessor::getGhostTimeLabel (processorRef.getGhostTimeMode()));
    ghostTimeButton.onClick = [this]
    {
        const int next = (processorRef.getGhostTimeMode() + 1) % SpoolAudioProcessor::kNumGhostTimes;
        processorRef.setGhostTimeMode (next);
        ghostTimeButton.setButtonText (SpoolAudioProcessor::getGhostTimeLabel (next));
    };
    addAndMakeVisible (ghostTimeButton);

    // --- LOOP CUTOFF cycle: -12 / -24 / BKWL -------------------------------
    // Visually distinct from the size buttons — accent-filled (it's a MODE
    // selector, not a preset), with dark text + a thin darker rim.
    {
        loopCutoffButton.setColour (juce::TextButton::buttonColourId,   kAccent);
        loopCutoffButton.setColour (juce::TextButton::buttonOnColourId, kAccent.brighter (0.15f));
        loopCutoffButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
        loopCutoffButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        loopCutoffButton.setButtonText (SpoolAudioProcessor::getLoopCutoffLabel (processorRef.getLoopCutoffMode()));
        loopCutoffButton.onClick = [this]
        {
            const int next = (processorRef.getLoopCutoffMode() + 1) % SpoolAudioProcessor::kNumLoopCutoffModes;
            processorRef.setLoopCutoffMode (next);
            loopCutoffButton.setButtonText (SpoolAudioProcessor::getLoopCutoffLabel (next));
        };
        addAndMakeVisible (loopCutoffButton);
    }

    // --- Loop size buttons: 1/16 / 1/8 / 1/4 / 1/2 / 1B / 2B --------------
    {
        const char* labels[] = { "1/16", "1/8", "1/4", "1/2", "1B", "2B" };
        const float beats[]  = { 0.25f,  0.5f,  1.0f,  2.0f,  4.0f, 8.0f };
        for (int i = 0; i < 6; ++i)
        {
            auto& b = loopSizeButtons[(size_t) i];
            b.setButtonText (labels[i]);
            b.setColour (juce::TextButton::buttonColourId, kPanelDark);
            b.setColour (juce::TextButton::buttonOnColourId, kAccent);
            b.setColour (juce::TextButton::textColourOffId,
                         juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            const float beatVal = beats[i];
            b.onClick = [this, beatVal] { processorRef.setLoopSizeBeats (beatVal); repaint(); };
            addAndMakeVisible (b);
        }
    }

    // --- TAPE knob + machine cycle button ---------------------------------
    addAndMakeVisible (tapeKnob);
    tapeKnob.setCompact (true);
    tapeKnob.getSlider().setLookAndFeel (&brushedKnobLnf);
    {
        auto& s = tapeKnob.getSlider();
        s.setRange (0.0, 1.0, 0.01);
        s.setValue ((double) processorRef.getTapeMix(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.0);
        tapeKnob.setValueText (juce::String ((int) (s.getValue() * 100.0)));
        s.onValueChange = [this]
        {
            const auto v = (float) tapeKnob.getSlider().getValue();
            processorRef.setTapeMix (v);
            tapeKnob.setValueText (juce::String ((int) (v * 100.0)));
        };
    }
    {
        tapeMachineButton.setColour (juce::TextButton::buttonColourId, kPanelDark);
        tapeMachineButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
        tapeMachineButton.setColour (juce::TextButton::textColourOffId,
                                     juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
        tapeMachineButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        // No text — TP-7 style coloured pill. Colour = machine identity.
        tapeMachineButton.setButtonText ({});
        tapeMachineButton.onClick = [this]
        {
            const int next = (processorRef.getTapeMachine() + 1) % SpoolAudioProcessor::kNumTapeMachines;
            processorRef.setTapeMachine (next);
            applyTapeMachineColour();
        };
        addAndMakeVisible (tapeMachineButton);
        applyTapeMachineColour();
    }

    // --- HAZE knob (reverb size) + FREEZE button ---------------------------
    {
        auto& s = hazeKnob.getSlider();
        s.setRange (0.0, 1.0, 0.01);
        s.setValue ((double) processorRef.getHazeAmount(), juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, 0.0);
        hazeKnob.setValueText (juce::String ((int) (s.getValue() * 100.0)));
        s.onValueChange = [this]
        {
            const auto v = (float) hazeKnob.getSlider().getValue();
            processorRef.setHazeAmount (v);
            hazeKnob.setValueText (juce::String ((int) (v * 100.0)));
        };
    }

    // FREEZE button — snowflake icon, toggles via colour.
    freezeButton.setColour (juce::TextButton::buttonColourId,   kPanelDark);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    freezeButton.setColour (juce::TextButton::textColourOffId,  juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
    freezeButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    freezeButton.setButtonText (juce::String::fromUTF8 ("\xE2\x9D\x84")); // ❄
    freezeButton.setClickingTogglesState (true);
    freezeButton.setToggleState (processorRef.isHazeFrozen(), juce::dontSendNotification);
    freezeButton.onClick = [this] { processorRef.setHazeFrozen (freezeButton.getToggleState()); };
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (bpmControl);

    // TAP TEMPO button — bare dark pill matching the loop-row buttons,
    // accent-highlights on click.
    tapButton.setColour (juce::TextButton::buttonColourId,   kPanelDark);
    tapButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    tapButton.setColour (juce::TextButton::textColourOffId,  juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
    tapButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    tapButton.onClick = [this] { registerTap(); };
    addAndMakeVisible (tapButton);

    // HAZE preset cycle: HALL / PLATE / ROOM / CHMBR / CAVE / SHIMR.
    hazePresetButton.setColour (juce::TextButton::buttonColourId,   kPanelDark);
    hazePresetButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    hazePresetButton.setColour (juce::TextButton::textColourOffId,  juce::Colour::fromRGB (0xe6, 0xe6, 0xe8));
    hazePresetButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    hazePresetButton.setButtonText (SpoolAudioProcessor::getHazePresetLabel (processorRef.getHazePreset()));
    hazePresetButton.onClick = [this]
    {
        const int next = (processorRef.getHazePreset() + 1) % SpoolAudioProcessor::kNumHazePresets;
        processorRef.setHazePreset (next);
        hazePresetButton.setButtonText (SpoolAudioProcessor::getHazePresetLabel (next));
    };
    addAndMakeVisible (hazePresetButton);

    processorRef.getThumbnail().addChangeListener (this);

    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);

    // First-launch welcome / credits / donation card. Stays modal-feeling
    // (dims the panel behind it) until the user clicks "Got it" — never
    // shown again on subsequent launches.
    maybeShowWelcomeOverlay();
}

SpoolAudioProcessorEditor::~SpoolAudioProcessorEditor()
{
    juce::Logger::writeToLog ("editor dtor");
    loadButton   .setLookAndFeel (nullptr);
    overdubButton.setLookAndFeel (nullptr);
    loopButton   .setLookAndFeel (nullptr);
    exportButton .setLookAndFeel (nullptr);
    clearSampleButton.setLookAndFeel (nullptr);
    folderButton    .setLookAndFeel (nullptr);
    folderPrevButton.setLookAndFeel (nullptr);
    folderNextButton.setLookAndFeel (nullptr);
    inputKnob     .getSlider().setLookAndFeel (nullptr);
    speedKnob     .getSlider().setLookAndFeel (nullptr);
    sampleGainKnob.getSlider().setLookAndFeel (nullptr);
    filterKnob      .getSlider().setLookAndFeel (nullptr);
    ghostKnob   .getSlider().setLookAndFeel (nullptr);
    hazeKnob      .getSlider().setLookAndFeel (nullptr);
    tapeKnob      .getSlider().setLookAndFeel (nullptr);
    inputCompButton  .setLookAndFeel (nullptr);
    tapeMachineButton.setLookAndFeel (nullptr);
    freezeButton     .setLookAndFeel (nullptr);
    resetParamsButton.setLookAndFeel (nullptr);
    processorRef.getThumbnail().removeChangeListener (this);
}

//==============================================================================
void SpoolAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto outer = getLocalBounds();

    // 1) Body background: custom image cropped-to-fill if present, otherwise
    // fall back to the machined-aluminium gradient. The image keeps its
    // native resolution — we just show whichever portion fits.
    if (backgroundImage.isValid())
    {
        const juce::RectanglePlacement placement (juce::RectanglePlacement::centred
                                                | juce::RectanglePlacement::fillDestination);
        g.drawImage (backgroundImage, outer.toFloat(), placement, false);
        // Slight dark overlay so the controls stay readable on busy textures.
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (outer.toFloat(), 14.0f);
    }
    else
    {
        juce::ColourGradient bodyGrad (kAlumLight, 0.0f, 0.0f,
                                       kAlumDark, 0.0f, (float) getHeight(), false);
        bodyGrad.addColour (0.5, kAlumMid);
        g.setGradientFill (bodyGrad);
        g.fillRoundedRectangle (outer.toFloat(), 14.0f);
    }

    // Subtle precision seam tracing the body ~6px in.
    g.setColour (kSeam.withAlpha (0.55f));
    g.drawRoundedRectangle (outer.reduced (6).toFloat(), 10.0f, 0.7f);

    // 2) Inner sub-panel (dark inset where controls live) -------------------
    auto inner = outer.reduced (kBezel);
    juce::ColourGradient panelGrad (kPanelMid.darker (0.15f), 0.0f, (float) inner.getY(),
                                    kPanelMid.darker (0.05f), 0.0f, (float) inner.getBottom(), false);
    g.setGradientFill (panelGrad);
    g.fillRoundedRectangle (inner.toFloat(), 10.0f);

    g.setColour (kSeam.withAlpha (0.65f));
    g.drawRoundedRectangle (inner.toFloat(), 10.0f, 1.0f);

    // 3) Corner screws ------------------------------------------------------
    const int screwR = 5;
    const int screwInset = kBezel - 3;
    auto drawScrew = [&] (int cx, int cy)
    {
        g.setColour (kScrew);
        g.fillEllipse ((float) (cx - screwR), (float) (cy - screwR),
                       (float) (screwR * 2), (float) (screwR * 2));
        g.setColour (kScrewDark);
        g.drawEllipse ((float) (cx - screwR), (float) (cy - screwR),
                       (float) (screwR * 2), (float) (screwR * 2), 0.8f);
        // Slot (diagonal -45 degrees)
        const float off = (float) screwR * 0.7f;
        g.drawLine ((float) cx - off, (float) cy - off,
                    (float) cx + off, (float) cy + off, 1.2f);
    };
    drawScrew (screwInset,                 screwInset);
    drawScrew (getWidth() - screwInset,    screwInset);
    drawScrew (screwInset,                 getHeight() - screwInset);
    drawScrew (getWidth() - screwInset,    getHeight() - screwInset);

    // 4) Header strip: SP-L wordmark left, OLED display right --------------
    auto header = juce::Rectangle<int> (inner.getX() + 12, inner.getY() + 8,
                                        inner.getWidth() - 24, 78);

    // Big wordmark — visible "SP-L" (the app itself is still SPOOL internally).
    g.setColour (kText);
    g.setFont   (juce::Font (juce::FontOptions { 56.0f, juce::Font::bold }));
    g.drawText  ("SP-L", header.removeFromLeft (200), juce::Justification::centredLeft, false);

    // OLED screen.
    auto oled = header.removeFromRight (220).reduced (0, 12);
    g.setColour (kOledBg);
    g.fillRoundedRectangle (oled.toFloat(), 5.0f);
    g.setColour (kOledRim);
    g.drawRoundedRectangle (oled.toFloat(), 5.0f, 1.2f);

    auto oledInner = oled.reduced (8, 6);
    g.setFont   (juce::Font (juce::FontOptions { 11.5f, juce::Font::bold }));

    // Recording / no-input states take over the OLED.
    const int  chans   = processorRef.getInputChannelCount();
    const bool isRec   = processorRef.isRecording();
    const auto name    = processorRef.getLoadedSampleName();
    const double dur   = processorRef.getDurationSeconds();
    const double pos   = (double) processorRef.getNormalisedPosition() * dur;

    if (isRec)
    {
        g.setColour (kRecord);
        g.drawText ("● REC", oledInner.removeFromTop (16), juce::Justification::topLeft, false);
        g.setColour (kOledText);
        g.drawText (juce::String::formatted ("%d%% BUFFER", (int) (processorRef.getRecordProgress() * 100)),
                    oledInner.removeFromBottom (16), juce::Justification::bottomLeft, false);
    }
    else if (chans == 0)
    {
        g.setColour (kRecord);
        g.drawText ("NO INPUT", oledInner.removeFromTop (16), juce::Justification::topLeft, false);
        g.setColour (kOledDim);
        g.drawText ("PRESS SETUP", oledInner.removeFromBottom (16), juce::Justification::bottomLeft, false);
    }
    else
    {
        g.setColour (kOledText);
        const auto display = name.isEmpty() ? juce::String ("READY") : name.upToFirstOccurrenceOf (".", false, false);
        g.drawText (display, oledInner.removeFromTop (16), juce::Justification::topLeft, false);
        g.setColour (kOledDim);
        juce::String bottomLine = formatTime (pos) + " / " + formatTime (dur);
        if (processorRef.isFolderActive())
        {
            bottomLine += juce::String::formatted ("   %d/%d",
                                                   processorRef.getFolderIndex() + 1,
                                                   processorRef.getFolderFileCount());
        }
        g.drawText (bottomLine, oledInner.removeFromBottom (16),
                    juce::Justification::bottomLeft, false);
    }

    // 5) Waveform panel — flat, no shadow.
    g.setColour (fileBeingDragged ? kAccent.withAlpha (0.20f) : juce::Colour::fromRGB (0x18, 0x18, 0x1c));
    g.fillRoundedRectangle (waveformArea.toFloat(), 4.0f);

    drawWaveform (g, waveformArea);

    // Helper: map a full-sample 0..1 to a pixel x in the (possibly zoomed) view.
    auto fullNormToX = [this] (float fullN) -> int
    {
        const float viewN = (fullN - waveformOffset) * waveformZoom;
        return waveformArea.getX() + (int) (viewN * waveformArea.getWidth());
    };

    // Loop region highlight — translucent orange band showing the active loop.
    if (processorRef.hasCustomLoop() || processorRef.getLoopLength() > 0)
    {
        const float sN = processorRef.getLoopRegionStartNormalised();
        const float eN = processorRef.getLoopRegionEndNormalised();
        if (eN > sN)
        {
            const int x1c = juce::jlimit (waveformArea.getX(), waveformArea.getRight(), fullNormToX (sN));
            const int x2c = juce::jlimit (waveformArea.getX(), waveformArea.getRight(), fullNormToX (eN));
            if (x2c > x1c)
            {
                g.setColour (kAccent.withAlpha (0.28f));
                g.fillRect  (x1c, waveformArea.getY(), x2c - x1c, waveformArea.getHeight());
                g.setColour (kAccent.withAlpha (0.75f));
                g.drawRect  (x1c, waveformArea.getY(), x2c - x1c, waveformArea.getHeight(), 1);
            }
        }
    }

    // Playhead — light grey, distinct from the orange wave.
    const float norm = processorRef.getNormalisedPosition();
    if (norm > 0.0f || processorRef.isPlaying())
    {
        const int x = fullNormToX (norm);
        if (x >= waveformArea.getX() && x <= waveformArea.getRight())
        {
            g.setColour (juce::Colour::fromRGB (0xd8, 0xd8, 0xdc).withAlpha (0.85f));
            g.drawVerticalLine (x, (float) waveformArea.getY() + 2.0f,
                                (float) waveformArea.getBottom() - 2.0f);
        }
    }
}

void SpoolAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Subtle background-image overlay drawn ON TOP of all controls. Gives the
    // whole device a unified weathered/etched look — like the leaves and
    // grunge are printed into the surface of the device. Low opacity so
    // controls stay readable.
    if (! backgroundImage.isValid()) return;

    auto inner = getLocalBounds().reduced (kBezel);
    g.saveState();
    g.reduceClipRegion (inner);
    g.setOpacity (0.12f);
    const juce::RectanglePlacement placement (juce::RectanglePlacement::centred
                                            | juce::RectanglePlacement::fillDestination);
    g.drawImage (backgroundImage, getLocalBounds().toFloat(), placement, false);
    g.restoreState();
}

void SpoolAudioProcessorEditor::resized()
{
    // Welcome overlay (if showing) covers the whole window.
    if (welcomeOverlay != nullptr)
    {
        welcomeOverlay->setBounds (getLocalBounds());
        welcomeOverlay->resized();
    }

    // Vertical handheld layout. The aluminium frame consumes kBezel on every side.
    auto inner = getLocalBounds().reduced (kBezel + 4);

    // Header — SP-L + OLED drawn in paint(). Mirror the OLED rect here so we
    // can position folder/prev/next buttons relative to it.
    {
        auto header = juce::Rectangle<int> (inner.getX() + 12, inner.getY() + 8,
                                            inner.getWidth() - 24, 78);
        // Record the SP-L wordmark rect for the easter-egg double-click.
        wordmarkArea = header.removeFromLeft (200);
        auto oled = header.removeFromRight (220).reduced (0, 12);

        // Folder icon: top-right corner of the OLED (inside the box).
        const int fldSz = 18;
        folderButton.setBounds (oled.getRight() - fldSz - 4,
                                oled.getY()     + 4,
                                fldSz, fldSz);

        // − and + buttons: split the row directly UNDER the OLED.
        const int navH = 16;
        const int navY = oled.getBottom() + 2;
        const int navW = oled.getWidth() / 2 - 2;
        folderPrevButton.setBounds (oled.getX(),                  navY, navW, navH);
        folderNextButton.setBounds (oled.getX() + navW + 4,       navY, navW, navH);

        // RESET LED — small square (rendered as a red circle by ledPillLnf)
        // sitting in the empty header space between the SP-L wordmark and
        // the OLED. Right edge is clamped to stay clear of the OLED.
        const int resetSz       = 22;
        const int availStart    = wordmarkArea.getRight() + 8;
        const int availEnd      = oled.getX() - 8;
        const int availWidth    = juce::jmax (resetSz, availEnd - availStart);
        const int resetX        = availStart + (availWidth - resetSz) / 2;
        resetParamsButton.setBounds (resetX,
                                     wordmarkArea.getCentreY() - resetSz / 2,
                                     resetSz, resetSz);
    }
    inner.removeFromTop (78);                    // header: SP-L + OLED (drawn in paint())
    inner.removeFromTop (22);                    // spacer below header (was 4 — now leaves room for nav buttons)

    // ---- Bottom: three OVERSIZED transport buttons (REC / PLAY / STOP) -----
    auto bigTransport = inner.removeFromBottom (70);
    {
        const int gap = 10;
        const int w   = (bigTransport.getWidth() - gap * 2) / 3;
        recordButton.setBounds (bigTransport.removeFromLeft (w)); bigTransport.removeFromLeft (gap);
        playButton  .setBounds (bigTransport.removeFromLeft (w)); bigTransport.removeFromLeft (gap);
        stopButton  .setBounds (bigTransport);
    }
    inner.removeFromBottom (8);

    // ---- Slot row (1..8): above bigTransport ------------------------------
    {
        auto slotRow = inner.removeFromBottom (40);
        const int n = SpoolAudioProcessor::kNumSlots;
        const int gap = 4;
        const int w   = (slotRow.getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            slotButtons[(size_t) i].setBounds (slotRow.removeFromLeft (w));
            if (i < n - 1) slotRow.removeFromLeft (gap);
        }
    }
    inner.removeFromBottom (10);

    // ---- JUICE area: 3 knobs + cutoff companion under LOOP -----------------
    auto juiceArea = inner.removeFromBottom (210);
    auto cutoffRow = juiceArea.removeFromBottom (28);
    juiceArea.removeFromBottom (4);

    {
        const int gap   = 8;
        const int knobW = (juiceArea.getWidth() - gap * 2) / 3;
        // Knobs are placed based on the processor's signalPathOrder so
        // dragging them rearranges the chain (and vice versa).
        JuiceKnob*       knobs   [3] = { &filterKnob,    &ghostKnob,        &hazeKnob };
        juce::TextButton* btns   [3] = { &filterQButton, &ghostTimeButton,  &freezeButton };

        for (int slot = 0; slot < 3; ++slot)
        {
            const int effectIdx = processorRef.getSignalOrder (slot);
            auto col = juiceArea.removeFromLeft (knobW);
            if (slot < 2) juiceArea.removeFromLeft (gap);

            knobs[effectIdx]->setBounds (col);

            const int innerW = knobW - 24;          // companion-row margin
            const int innerX = col.getX() + 12;

            if (effectIdx == SpoolAudioProcessor::EffectFilter)
            {
                // FILTER gets THREE companion buttons evenly split:
                //   Q | LFO RATE | LFO RANGE
                const int btnGap = 3;
                const int btnW   = (innerW - btnGap * 2) / 3;
                filterQButton       .setBounds (innerX,                    cutoffRow.getY(), btnW, cutoffRow.getHeight());
                filterLfoRateButton .setBounds (innerX + btnW + btnGap,    cutoffRow.getY(), btnW, cutoffRow.getHeight());
                filterLfoRangeButton.setBounds (innerX + (btnW + btnGap) * 2, cutoffRow.getY(),
                                                innerW - (btnW + btnGap) * 2, cutoffRow.getHeight());
            }
            else if (effectIdx == SpoolAudioProcessor::EffectHaze)
            {
                // HAZE column gets TWO companion buttons: snowflake FREEZE
                // toggle on the left + the preset cycle on the right.
                const int btnGap = 4;
                const int freezeW = 30;                 // square-ish for the snowflake
                const int presetW = innerW - freezeW - btnGap;
                freezeButton    .setBounds (innerX, cutoffRow.getY(), freezeW, cutoffRow.getHeight());
                hazePresetButton.setBounds (innerX + freezeW + btnGap, cutoffRow.getY(),
                                             presetW, cutoffRow.getHeight());
            }
            else
            {
                btns[effectIdx]->setBounds (innerX, cutoffRow.getY(),
                                            innerW, cutoffRow.getHeight());
            }
        }
    }
    inner.removeFromBottom (8);                  // spacer above JUICE

    // ---- NUDGE row: thin slider centred under the sample window -----------
    // Waveform is 80% wide and left-aligned, so the centre of the sample
    // window sits at 40% of inner. NUDGE is narrower (≈55% of waveform width)
    // and centred horizontally on that point.
    {
        auto row = inner.removeFromBottom (14);
        const int wfWidth   = (int) (row.getWidth() * 0.80f);
        const int nudgeW    = (int) (wfWidth * 0.55f);
        const int nudgeX    = row.getX() + (wfWidth - nudgeW) / 2;
        nudgeSlider.setBounds (nudgeX, row.getY(), nudgeW, row.getHeight());
    }
    inner.removeFromBottom (8);

    // ---- Loop control row: 6 size buttons + 1 cutoff cycle, aligned with
    //      the waveform width (80%). BPM moved to its own dedicated row
    //      above the waveform.
    {
        auto row = inner.removeFromBottom (22);
        const int rowW = (int) (row.getWidth() * 0.80f);
        auto loopBar   = row.removeFromLeft (rowW);
        const int gap        = 3;
        const int groupGap   = 10;
        const int sizeBtnW   = (loopBar.getWidth() - groupGap - gap * 5 - 50) / 6;
        for (int i = 0; i < 6; ++i)
        {
            loopSizeButtons[(size_t) i].setBounds (loopBar.removeFromLeft (sizeBtnW));
            if (i < 5) loopBar.removeFromLeft (gap);
        }
        loopBar.removeFromLeft (groupGap);
        loopCutoffButton.setBounds (loopBar.removeFromLeft (50));
    }
    inner.removeFromBottom (6);

    // ---- Utility row: four matched icon buttons aligned with the waveform.
    // Spans 80% of the inner width (same horizontal range as the waveform
    // above), four equally-sized icon buttons: ↑  +  ↻  ↓
    {
        auto fullRow  = inner.removeFromBottom (32);
        const int rowW = (int) (fullRow.getWidth() * 0.80f);
        auto utility   = fullRow.removeFromLeft (rowW);

        const int gap  = 6;
        const int btnW = (utility.getWidth() - gap * 3) / 4;
        loadButton    .setBounds (utility.removeFromLeft (btnW)); utility.removeFromLeft (gap);
        overdubButton .setBounds (utility.removeFromLeft (btnW)); utility.removeFromLeft (gap);
        loopButton    .setBounds (utility.removeFromLeft (btnW)); utility.removeFromLeft (gap);
        exportButton  .setBounds (utility);
    }
    inner.removeFromBottom (8);

    // ---- Top area: big reel centred, INPUT knob to its left, waveform below
    const int reelSize = juce::jmin (220, inner.getHeight() - 110);
    auto reelBounds = juce::Rectangle<int> (0, 0, reelSize, reelSize)
                         .withCentre ({ inner.getCentreX(), inner.getY() + reelSize / 2 + 4 });
    reel.setBounds (reelBounds);

    // Tightened hierarchy:
    //   SECONDARY (INPUT / TAPE / SAMPLE GAIN) — all share one square footprint.
    //   MARQUEE   (SPEED)                       — slightly larger.
    const int secKnobW = 86;
    const int secKnobH = 86;
    const int pillH    = 18;          // matches both INPUT comp pill + TAPE machine pill

    // INPUT (left column, vertically centred against the reel). The comp
    // voice indicator below is now a discrete LED circle — the dark pill
    // surround has been removed entirely (just halo + bezel + dot).
    {
        const int pillW = pillH;            // square click area for the circle
        const int colH  = secKnobH + 4 + pillH;
        inputKnob.setBounds (inner.getX() + 4,
                             reelBounds.getCentreY() - colH / 2,
                             secKnobW, secKnobH);
        inputCompButton.setBounds (inputKnob.getX() + (secKnobW - pillW) / 2,
                                   inputKnob.getBottom() + 4,
                                   pillW, pillH);
    }

    // SPEED + TAPE stacked on the right column. SPEED is the marquee size;
    // TAPE matches the secondary square footprint below it.
    {
        const int spdKnobW = secKnobW + 22;          // 108
        const int spdKnobH = secKnobH + 22;          // 108
        const int tapePillH = pillH;                  // match INPUT comp pill
        const int tapePillW = tapePillH;              // square — discrete LED circle

        const int colH = spdKnobH + 8 + secKnobH + 4 + tapePillH;
        const int rightX = inner.getRight() - spdKnobW - 2;
        const int colTop = reelBounds.getCentreY() - colH / 2;

        speedKnob.setBounds (rightX, colTop, spdKnobW, spdKnobH);

        // TAPE knob centred horizontally within the SPEED column.
        const int tapeX = rightX + (spdKnobW - secKnobW) / 2;
        const int tapeY = speedKnob.getBottom() + 8;
        tapeKnob.setBounds (tapeX, tapeY, secKnobW, secKnobH);
        tapeMachineButton.setBounds (tapeX + (secKnobW - tapePillW) / 2,
                                     tapeY + secKnobH + 4,
                                     tapePillW, tapePillH);
    }

    inner.removeFromTop (reelSize + 14);

    // ---- BPM + TAP row, sitting just above the waveform, left-justified.
    //      BPM is here because the loop SIZE buttons are tempo-relative —
    //      change BPM and the loop windows scale with it.
    {
        const int bpmRowH = 22;
        auto bpmRow = inner.removeFromTop (bpmRowH);
        const int bpmW = 48;       // just enough for 3-digit number
        const int tapW = 38;
        const int gap  = 4;
        bpmControl.setBounds (bpmRow.getX(), bpmRow.getY(), bpmW, bpmRowH);
        tapButton .setBounds (bpmRow.getX() + bpmW + gap, bpmRow.getY(), tapW, bpmRowH);
    }
    inner.removeFromTop (4);     // small gap before the waveform

    // Waveform takes the left ~80%; SAMPLE GAIN sits in the right column,
    // vertically aligned with TAPE knob's X so SPEED → TAPE → SAMPLE GAIN
    // form a clean column on the right edge.
    auto wfRow = inner.reduced (0, 4);
    const int gainX = tapeKnob.getX();              // align with TAPE column
    const int wfRightEdge = gainX - 6;              // leave a small gutter
    const int wfWidth = juce::jmax (40, wfRightEdge - wfRow.getX());
    waveformArea = wfRow.removeFromLeft (wfWidth);
    {
        // Centre vertically inside the row.
        const int gy = wfRow.getCentreY() - secKnobH / 2;
        sampleGainKnob.setBounds (gainX, gy, secKnobW, secKnobH);
    }

    // Small ✕ clear-sample button in the top-right corner of the waveform.
    const int clearSz = 20;
    clearSampleButton.setBounds (waveformArea.getRight() - clearSz - 4,
                                 waveformArea.getY()     + 4,
                                 clearSz, clearSz);
}

//==============================================================================
// Mouse on the waveform:
//   • click (no drag)    → seek to that position + clear any custom loop
//   • click + DRAG       → highlight a loop region between drag-start and end
//   • double-click       → set LOOP anchor at clicked position
//
void SpoolAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getPosition()) && waveformArea.getWidth() > 0)
    {
        const float viewNorm = (float) (e.getPosition().getX() - waveformArea.getX())
                             / (float) waveformArea.getWidth();
        dragStartNorm = juce::jlimit (0.0f, 1.0f, viewToFullNorm (viewNorm));
    }
}

void SpoolAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! waveformArea.getWidth()) return;
    if (! e.mouseWasDraggedSinceMouseDown()) return;

    const float viewNorm = juce::jlimit (0.0f, 1.0f,
        (float) (e.getPosition().getX() - waveformArea.getX())
      / (float) waveformArea.getWidth());
    const float currentNorm = juce::jlimit (0.0f, 1.0f, viewToFullNorm (viewNorm));

    processorRef.setCustomLoopRegionNormalised (dragStartNorm, currentNorm);
    if (processorRef.getLoopLength() == 0)
        processorRef.setLoopLength (1);

    repaint (waveformArea);
}

void SpoolAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! waveformArea.contains (e.getMouseDownPosition())) return;

    if (! e.mouseWasDraggedSinceMouseDown())
    {
        processorRef.clearCustomLoop();
        processorRef.seekNormalised (dragStartNorm);
        repaint (waveformArea);
    }
}

void SpoolAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e,
                                                const juce::MouseWheelDetails& w)
{
    // Ctrl + scroll over the waveform → zoom in/out, mouse-centred.
    if (! (e.mods.isCtrlDown() && waveformArea.contains (e.getPosition()))) return;
    if (waveformArea.getWidth() <= 0) return;

    const float factor    = w.deltaY > 0 ? 1.25f : 1.0f / 1.25f;
    const float newZoom   = juce::jlimit (1.0f, 50.0f, waveformZoom * factor);
    const float viewNorm  = (float) (e.getPosition().getX() - waveformArea.getX())
                          / (float) waveformArea.getWidth();
    const float fullNorm  = viewToFullNorm (viewNorm);

    waveformZoom    = newZoom;
    // Maintain the mouse-anchored full-sample position at the same view norm.
    const float maxOff = juce::jmax (0.0f, 1.0f - 1.0f / waveformZoom);
    waveformOffset  = juce::jlimit (0.0f, maxOff, fullNorm - viewNorm / waveformZoom);

    repaint (waveformArea);
}

void SpoolAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Easter egg: double-click the SP-L wordmark to randomise the accent theme.
    if (wordmarkArea.contains (e.getPosition()))
    {
        applyRandomTheme();
        return;
    }

    if (waveformArea.contains (e.getPosition()) && waveformArea.getWidth() > 0)
    {
        const float norm = juce::jlimit (0.0f, 1.0f,
                                         (float) (e.getPosition().getX() - waveformArea.getX())
                                       / (float) waveformArea.getWidth());

        // Set the LOOP anchor at the clicked spot and snap the playhead there
        // so the loop window kicks in immediately from that position.
        processorRef.setLoopAnchorFromNormalised (norm);
        processorRef.seekNormalised (norm);
        repaint (waveformArea);
    }
}

//==============================================================================
// FileDragAndDropTarget
//
bool SpoolAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3" || ext == ".ogg")
            return true;
    }
    return false;
}

void SpoolAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileBeingDragged = false;
    if (files.isEmpty()) return;

    juce::File f (files[0]);
    if (processorRef.loadFile (f))
    {
        playButton.setButtonText ("PLAY");
    }
    repaint();
}

void SpoolAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileBeingDragged = true; repaint();
}

void SpoolAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    fileBeingDragged = false; repaint();
}

//==============================================================================
void SpoolAudioProcessorEditor::timerCallback()
{
    // Live position update + transport button text sync.
    repaint (waveformArea.expanded (2));

    const auto playing = processorRef.isPlaying();

    // Reel rotation: ~24 RPM (0.4 rotations/sec) while playing.
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float  dt    = (float) ((nowMs - lastTickMs) * 0.001);  // seconds
    lastTickMs = nowMs;

    if ((playing || processorRef.isRecording()) && ! reel.isScratching())
    {
        // Vinyl spins at ~33⅓ RPM-feel base rate, multiplied by SPEED knob
        // so the disc visibly slows down / speeds up as you varispeed.
        // While the user is scratching, the disc follows the mouse instead.
        constexpr float kBaseRotPerSec = 0.55f;
        const float spinMul = processorRef.isRecording() ? 1.0f
                                                         : processorRef.getPlaybackSpeed();
        reelAngle += dt * kBaseRotPerSec * spinMul * juce::MathConstants<float>::twoPi;
        if (reelAngle > juce::MathConstants<float>::twoPi)
            reelAngle -= juce::MathConstants<float>::twoPi;
        reel.setAngle (reelAngle);
    }

    // LOOP LED + knob LED fades.
    if (processorRef.isLoopWrappedThisBlock())
        filterKnob.flashLed();
    filterKnob    .tickLed (dt);
    ghostKnob .tickLed (dt);
    hazeKnob    .tickLed (dt);

    // INPUT knob LED = continuous level meter (so user can verify audio is arriving).
    inputKnob.setLedLevel (juce::jmin (1.0f, processorRef.getInputPeakLevel() * 2.0f));

    // Keep JUCE's auto-feedback-mute disabled. JUCE may re-mute when the
    // device changes, so we re-assert every tick. Cheap.
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        if ((bool) holder->getMuteInputValue().getValue())
            holder->getMuteInputValue().setValue (false);
    }

    // Slot button visual state: filled slots glow orange + bright number,
    // empty slots are dim. Only re-set when state changes (avoid extra repaints).
    for (int i = 0; i < SpoolAudioProcessor::kNumSlots; ++i)
    {
        auto& b = slotButtons[(size_t) i];
        const bool filled = processorRef.isSlotFilled (i);
        const auto wantTextCol = filled ? juce::Colours::white
                                        : juce::Colour::fromRGB (0x90, 0x90, 0x94);
        const auto wantBtnCol  = filled ? kAccent.withAlpha (0.55f) : kPanelDark;
        if (b.findColour (juce::TextButton::textColourOffId) != wantTextCol)
        {
            b.setColour (juce::TextButton::textColourOffId, wantTextCol);
            b.setColour (juce::TextButton::buttonColourId,  wantBtnCol);
            b.repaint();
        }
    }

    // Show/hide NUDGE based on LOOP knob state, and sync slider on re-capture.
    // NUDGE is relevant whenever a loop region exists — either the knob-based
    // anchor OR a user-highlighted custom region.
    const bool loopOn = processorRef.getLoopLength() > 0 || processorRef.hasCustomLoop();
    if (nudgeSlider.isVisible() != loopOn)
        nudgeSlider.setVisible (loopOn);
    if (processorRef.consumeNudgeResetFlag())
        nudgeSlider.setValue (0.0, juce::dontSendNotification);

    // Play/Rec are glyph buttons now — visual state communicated via toggle/colour
    // rather than text changes. No-op here.

    // OLED in the top-right (drawn in paint()) shows filename, position, REC
    // state, and NO INPUT warnings — no separate status label needed.

    // Recover from a record auto-stop (hit capacity).
    if (recordButton.getToggleState() && ! processorRef.isRecording())
    {
        recordButton.setToggleState (false, juce::dontSendNotification);
        processorRef.stopRecording(); // finalize buffer into a Sample
        repaint();
    }
}

void SpoolAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // New sample loaded → zoom out to fit it.
    waveformZoom   = 1.0f;
    waveformOffset = 0.0f;
    repaint (waveformArea);
}

//==============================================================================
void SpoolAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load a sample",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file != juce::File{})
        {
            processorRef.loadFile (file);
            playButton.setButtonText ("PLAY");
            repaint();
        }
    });
}

void SpoolAudioProcessorEditor::openExportChooser()
{
    if (processorRef.getLoadedSampleName().isEmpty())
        return; // nothing to export

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export to WAV",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (processorRef.getLoadedSampleName() + "_spool.wav"),
        "*.wav");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file != juce::File{})
            processorRef.exportToWav (file);
    });
}

void SpoolAudioProcessorEditor::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    // While recording: peak-meter style draw straight from the live record
    // buffer so the user sees the waveform fill in as they go.
    if (processorRef.isRecording())
    {
        const auto& rb       = processorRef.getRecordBuffer();
        const int    written = processorRef.getRecordedSampleCount();
        if (written <= 0 || rb.getNumSamples() == 0)
        {
            g.setColour (kAccent.withAlpha (0.7f));
            g.setFont   (juce::Font (juce::FontOptions { 13.0f, juce::Font::bold }));
            g.drawText  ("REC...", area, juce::Justification::centred, false);
            return;
        }

        // Show a rolling ~6-second window so the wave scrolls along while
        // recording rather than getting infinitely squashed.
        const double sr      = juce::jmax (1.0, processorRef.getHostSampleRate());
        const int    winSamp = (int) (6.0 * sr);
        const int    fromS   = juce::jmax (0, written - winSamp);
        const int    visible = juce::jmin (written - fromS, winSamp);
        if (visible <= 0) return;

        const auto a = area.reduced (6, 8).toFloat();
        const int  pxCount = juce::jmax (1, (int) a.getWidth());
        const int  samplesPerPx = juce::jmax (1, visible / pxCount);
        const int  channels = juce::jmin (2, rb.getNumChannels());
        const float midY = a.getCentreY();
        const float halfH = a.getHeight() * 0.5f;

        g.setColour (kAccent.withAlpha (0.85f));
        for (int px = 0; px < pxCount; ++px)
        {
            const int s0 = fromS + (px * visible) / pxCount;
            const int s1 = juce::jmin (written, s0 + samplesPerPx);
            float peak = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* d = rb.getReadPointer (ch);
                for (int i = s0; i < s1; ++i)
                    peak = juce::jmax (peak, std::abs (d[i]));
            }
            const float h = juce::jmin (halfH, peak * halfH);
            const float x = a.getX() + (float) px;
            g.drawLine (x, midY - h, x, midY + h, 1.0f);
        }

        // REC marker — small red dot in the top-left corner.
        g.setColour (juce::Colour::fromRGB (0xe5, 0x3a, 0x3a));
        g.fillEllipse (a.getX() + 2.0f, a.getY() + 2.0f, 7.0f, 7.0f);
        return;
    }

    auto& thumb = processorRef.getThumbnail();
    if (thumb.getNumChannels() == 0 || thumb.getTotalLength() <= 0.0)
    {
        g.setColour (kDim);
        g.setFont   (juce::Font (juce::FontOptions { 13.0f }));
        g.drawText  (fileBeingDragged ? "drop to load" : "no sample loaded",
                     area, juce::Justification::centred, false);
        return;
    }

    const double total  = thumb.getTotalLength();
    const double start  = (double) waveformOffset * total;
    const double window = total / (double) waveformZoom;
    g.setColour (kAccent.withAlpha (0.85f));
    thumb.drawChannels (g, area.reduced (6, 8), start, start + window, 1.0f);
}

void SpoolAudioProcessorEditor::maybeShowWelcomeOverlay()
{
    juce::PropertiesFile prefs (welcomePrefsOptions());
    if (prefs.getBoolValue (kWelcomeShownKey, false))
        return;     // user has seen it already
    showWelcomeOverlay();
}

void SpoolAudioProcessorEditor::showWelcomeOverlay()
{
    // Force-show — used by the standalone Options menu's "About / Credits"
    // entry to reopen the welcome card any time after dismissal.
    if (welcomeOverlay != nullptr) return;    // already up
    welcomeOverlay = std::make_unique<WelcomeOverlay> (*this);
    welcomeOverlay->setBounds (getLocalBounds());
    welcomeOverlay->resized();
    addAndMakeVisible (welcomeOverlay.get());
    welcomeOverlay->toFront (true);
}

void SpoolAudioProcessorEditor::dismissWelcomeOverlay()
{
    juce::PropertiesFile prefs (welcomePrefsOptions());
    prefs.setValue (kWelcomeShownKey, true);
    prefs.saveIfNeeded();
    welcomeOverlay.reset();
}

void SpoolAudioProcessorEditor::loadBackgroundImage()
{
    // Optional custom background. Users can drop a `bg.jpg` / `bg.png` (or
    // `background.*`) into either of these locations and SPOOL will pick
    // it up at launch:
    //   1. The current working directory + /assets   (dev: <repo>/assets/)
    //   2. %APPDATA%/SPOOL/assets/                   (per-user theming dir)
    //   3. The plugin install directory + /assets    (sysadmin-provided)
    // If nothing is found, the silver panel renders without an overlay.
    juce::Array<juce::File> dirs;
    dirs.add (juce::File::getCurrentWorkingDirectory().getChildFile ("assets"));
    dirs.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("SPOOL").getChildFile ("assets"));
    dirs.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                  .getParentDirectory().getChildFile ("assets"));

    const juce::StringArray names { "bg.jpg", "bg.png", "background.jpg", "background.png" };
    for (const auto& dir : dirs)
    {
        if (! dir.isDirectory()) continue;
        for (const auto& name : names)
        {
            auto f = dir.getChildFile (name);
            if (! f.existsAsFile()) continue;
            auto img = juce::ImageFileFormat::loadFrom (f);
            if (img.isValid())
            {
                backgroundImage = img;
                return;
            }
        }
    }

    // No disk override found — fall back to the bg.png embedded into the
    // binary at build time via juce_add_binary_data (CMakeLists.txt). This
    // is what makes the background work in shipped releases, since the
    // installed .exe/.vst3 has no on-disk assets/ folder beside it.
    auto embedded = juce::ImageFileFormat::loadFrom (BinaryData::bg_png,
                                                     BinaryData::bg_pngSize);
    if (embedded.isValid())
        backgroundImage = embedded;
}

void SpoolAudioProcessorEditor::syncControlsFromProcessor()
{
    // Re-pull every value with dontSendNotification so we don't trip onValueChange.
    {
        const auto v = processorRef.getInputMix();
        inputKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        inputKnob.setValueText (juce::String ((int) (v * 100.0f)));
    }
    {
        const auto v = processorRef.getPlaybackSpeed();
        speedKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        speedKnob.setValueText (juce::String (v, 2) + "x");
    }
    {
        const auto v = processorRef.getSampleGain();
        sampleGainKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        sampleGainKnob.setValueText (juce::String (juce::Decibels::gainToDecibels (v, -60.0f), 1));
    }
    {
        const auto v = processorRef.getFilterPos();
        filterKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        // value text auto-recomputed via onValueChange would be cleaner, but
        // we set it directly to avoid a notification.
        if (std::abs (v - 0.5f) < 0.02f)         filterKnob.setValueText ("OFF");
        else if (v < 0.5f)                       filterKnob.setValueText (juce::String::formatted ("LP %.1fk",
                                                     (200.0f + (v * 2.0f) * (20000.0f - 200.0f)) / 1000.0f));
        else                                     filterKnob.setValueText (juce::String::formatted ("HP %.0f",
                                                     20.0f + ((v - 0.5f) * 2.0f) * (4000.0f - 20.0f)));
    }
    {
        nudgeSlider.setValue ((double) processorRef.getLoopAnchorOffset(), juce::dontSendNotification);
    }
    filterQButton.setButtonText (SpoolAudioProcessor::getFilterQLabel (processorRef.getFilterQMode()));
    filterLfoRateButton .setButtonText (SpoolAudioProcessor::getFilterLfoRateLabel  (processorRef.getFilterLfoRate()));
    filterLfoRangeButton.setButtonText (SpoolAudioProcessor::getFilterLfoRangeLabel (processorRef.getFilterLfoRange()));
    inputCompButton.setButtonText (SpoolAudioProcessor::getInputCompLabel (processorRef.getInputCompMode()));
    applyInputCompColour();

    // SQUEEZE / HAZE / FILTER / FREEZE may also be part of future snapshots —
    // pull current values so the UI stays consistent.
    {
        const auto v = processorRef.getGhostAmount();
        ghostKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        ghostKnob.setValueText (juce::String ((int) (v * 100.0f)));
    }
    {
        const auto v = processorRef.getHazeAmount();
        hazeKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        hazeKnob.setValueText (juce::String ((int) (v * 100.0f)));
    }
    ghostTimeButton.setButtonText (SpoolAudioProcessor::getGhostTimeLabel (processorRef.getGhostTimeMode()));
    freezeButton.setToggleState (processorRef.isHazeFrozen(), juce::dontSendNotification);
    hazePresetButton.setButtonText (SpoolAudioProcessor::getHazePresetLabel (processorRef.getHazePreset()));

    // TAPE state.
    {
        const auto v = processorRef.getTapeMix();
        tapeKnob.getSlider().setValue ((double) v, juce::dontSendNotification);
        tapeKnob.setValueText (juce::String ((int) (v * 100.0f)));
    }
    applyTapeMachineColour();   // colour-only pill, no text

    // LOOP CUTOFF cycle label.
    loopCutoffButton.setButtonText (SpoolAudioProcessor::getLoopCutoffLabel (processorRef.getLoopCutoffMode()));

    // BPM readout — slot may have restored a different tempo.
    bpmControl.repaint();
}

void SpoolAudioProcessorEditor::registerTap()
{
    // TAP TEMPO: each call counts a tap; we average inter-tap intervals.
    // Reset the session if it's been more than 2 seconds since the last tap.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTapMs > 0.0 && (now - lastTapMs) < 2000.0)
    {
        tapIntervals.push_back (now - lastTapMs);
        if (tapIntervals.size() > 4) tapIntervals.erase (tapIntervals.begin());

        double avg = 0.0;
        for (auto t : tapIntervals) avg += t;
        avg /= (double) tapIntervals.size();
        if (avg > 0.0)
        {
            const double newBpm = juce::jlimit (40.0, 240.0, 60000.0 / avg);
            processorRef.setBpm (newBpm);
            bpmControl.repaint();
        }
    }
    else
    {
        tapIntervals.clear();
    }
    lastTapMs = now;
}

void SpoolAudioProcessorEditor::triggerSlot (int slot, bool allowSave)
{
    if (slot < 0 || slot >= SpoolAudioProcessor::kNumSlots) return;
    juce::Logger::writeToLog (juce::String::formatted ("triggerSlot %d (save=%d)",
                                                       slot, (int) allowSave));
    if (processorRef.isSlotFilled (slot))
    {
        processorRef.loadSlot (slot);
        syncControlsFromProcessor();
        resized();
    }
    else if (allowSave)
    {
        processorRef.saveCurrentLoopToSlot (slot);
    }
    // empty + !allowSave (keyboard) = no-op, never accidentally overwrite.
}

bool SpoolAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();

    // SPACEBAR — toggle play/stop. Universal transport convention.
    if (code == juce::KeyPress::spaceKey)
    {
        processorRef.togglePlay();
        return true;
    }

    // 1-8 on the number row OR numpad → LOAD slot 1-8. Never saves —
    // saving is mouse-only to prevent accidental overwrites.
    for (int i = 0; i < SpoolAudioProcessor::kNumSlots; ++i)
    {
        if (code == (int) ('1' + i)
            || code == (juce::KeyPress::numberPad1 + i))
        {
            triggerSlot (i, /* allowSave = */ false);
            return true;
        }
    }
    return false;
}

void SpoolAudioProcessorEditor::handleKnobReorder (int knobEffectIdx, int dropX)
{
    // Figure out which slot the drop X is over by inspecting the three knobs'
    // bounds (they're already arranged by current signal order).
    JuiceKnob* knobs[3] = { &filterKnob, &ghostKnob, &hazeKnob };

    int targetSlot = -1;
    for (int slot = 0; slot < 3; ++slot)
    {
        const int effectIdx = processorRef.getSignalOrder (slot);
        const auto bb = knobs[effectIdx]->getBounds();
        if (dropX >= bb.getX() && dropX <= bb.getRight())
        {
            targetSlot = slot;
            break;
        }
    }
    if (targetSlot < 0) return;

    // Find the source slot (where knobEffectIdx currently is).
    int sourceSlot = -1;
    for (int slot = 0; slot < 3; ++slot)
        if (processorRef.getSignalOrder (slot) == knobEffectIdx) { sourceSlot = slot; break; }

    if (sourceSlot < 0 || sourceSlot == targetSlot) return;

    // Swap.
    int order[3] = { processorRef.getSignalOrder (0),
                     processorRef.getSignalOrder (1),
                     processorRef.getSignalOrder (2) };
    std::swap (order[sourceSlot], order[targetSlot]);
    processorRef.setSignalOrder (order[0], order[1], order[2]);
    juce::Logger::writeToLog (juce::String::formatted ("reorder: %d %d %d",
                                                       order[0], order[1], order[2]));
    resized();
}

void SpoolAudioProcessorEditor::applyRandomTheme()
{
    // Random hue, fixed saturation/value so the vibe stays consistent —
    // accent always reads as a vivid mid-bright tone against the silver body.
    auto& rnd = juce::Random::getSystemRandom();
    const float h = rnd.nextFloat();
    const auto newAccent  = juce::Colour::fromHSV (h, 0.78f, 0.95f, 1.0f);
    const auto newOledTxt = juce::Colour::fromHSV (h, 0.70f, 0.95f, 1.0f);
    const auto newOledDim = juce::Colour::fromHSV (h, 0.65f, 0.55f, 1.0f);
    const auto newOledRim = juce::Colour::fromHSV (h, 0.85f, 0.25f, 1.0f);

    kAccent   = newAccent;
    kOledText = newOledTxt;
    kOledDim  = newOledDim;
    kOledRim  = newOledRim;

    // Re-apply colours on every widget that captured kAccent at construction.
    // (JUCE caches setColour values per-component, so changing the global
    // kAccent variable alone wouldn't propagate to existing widgets.)
    inputKnob     .setAccent (kAccent);
    filterKnob    .setAccent (kAccent);
    speedKnob     .setAccent (kAccent);
    sampleGainKnob.setAccent (kAccent);
    ghostKnob     .setAccent (kAccent);
    hazeKnob      .setAccent (kAccent);
    tapeKnob      .setAccent (kAccent);
    // Comp + tape voice pills still carry their identity colours (LED).
    applyInputCompColour();
    applyTapeMachineColour();

    nudgeSlider.setColour (juce::Slider::trackColourId, kAccent);
    nudgeSlider.setColour (juce::Slider::thumbColourId, kAccent.brighter (0.2f));

    // Cycle / toggle buttons: re-apply their on-colour so the active state
    // (and any accent-filled buttons like loopCutoff) match the new theme.
    auto retintBtn = [] (juce::TextButton& b, juce::Colour bg, juce::Colour bgOn)
    {
        b.setColour (juce::TextButton::buttonColourId,   bg);
        b.setColour (juce::TextButton::buttonOnColourId, bgOn);
        b.repaint();
    };
    retintBtn (filterQButton,        kPanelDark, kAccent);
    retintBtn (filterLfoRateButton,  kPanelDark, kAccent);
    retintBtn (filterLfoRangeButton, kPanelDark, kAccent);
    retintBtn (ghostTimeButton,      kPanelDark, kAccent);
    retintBtn (freezeButton,         kPanelDark, kAccent);
    retintBtn (hazePresetButton,     kPanelDark, kAccent);
    // Utility-row toggle buttons (LOOP, OVERDUB) follow the theme too.
    retintBtn (loopButton,       kPanelDark, kAccent);
    retintBtn (overdubButton,    kPanelDark, kAccent);
    // LOOP CUTOFF is the accent-FILLED mode selector — both colours track accent.
    retintBtn (loopCutoffButton, kAccent,    kAccent.brighter (0.15f));
    for (auto& b : loopSizeButtons) retintBtn (b, kPanelDark, kAccent);
    // Slot buttons (1..8) — onColour drives the "filled" highlight.
    for (auto& sb : slotButtons)    retintBtn (sb, kPanelDark, kAccent);

    juce::Logger::writeToLog (juce::String::formatted (
        "theme: H=%.2f → accent=%08X", h, (uint32_t) kAccent.getARGB()));
    repaint();
}

void SpoolAudioProcessorEditor::applyInputCompColour()
{
    // VINTAGE = amber, FET = red, OPTO = cyan. Recolour the INPUT knob AND
    // the comp cycle button so the button itself reads as the comp voice.
    juce::Colour c;
    switch (processorRef.getInputCompMode())
    {
        case SpoolAudioProcessor::CompFet:    c = juce::Colour::fromRGB (0xe5, 0x4a, 0x3a); break;
        case SpoolAudioProcessor::CompOpto:   c = juce::Colour::fromRGB (0x4a, 0xb4, 0xee); break;
        case SpoolAudioProcessor::CompVintage:
        default:                              c = juce::Colour::fromRGB (0xff, 0xa6, 0x4a); break;
    }
    // The INPUT knob itself follows the theme accent (set by applyRandomTheme).
    // Only the LED pill below carries the comp-voice identity colour.
    inputCompButton.setColour (juce::TextButton::buttonColourId, c);
    inputCompButton.setColour (juce::TextButton::buttonOnColourId, c.brighter (0.2f));
    inputCompButton.repaint();
}

void SpoolAudioProcessorEditor::applyTapeMachineColour()
{
    // SAT = warm orange (saturation), WOW = sea-green (modulation),
    // LO-FI = magenta (digital crunch). Recolour both knob and button.
    juce::Colour c;
    switch (processorRef.getTapeMachine())
    {
        case 1:  c = juce::Colour::fromRGB (0x4a, 0xc4, 0x96); break; // WOW
        case 2:  c = juce::Colour::fromRGB (0xd5, 0x5a, 0xb4); break; // LO-FI
        case 0:
        default: c = juce::Colour::fromRGB (0xe8, 0x9a, 0x3a); break; // SAT
    }
    auto& s = tapeKnob.getSlider();
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.setColour (juce::Slider::thumbColourId,            c.brighter (0.2f));
    tapeKnob.repaint();

    tapeMachineButton.setColour (juce::TextButton::buttonColourId,   c);
    tapeMachineButton.setColour (juce::TextButton::buttonOnColourId, c.brighter (0.2f));
    tapeMachineButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
    tapeMachineButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    tapeMachineButton.repaint();
}

juce::String SpoolAudioProcessorEditor::formatTime (double seconds)
{
    if (seconds <= 0.0) return "0:00";
    const int total = (int) seconds;
    const int m = total / 60;
    const int s = total % 60;
    return juce::String::formatted ("%d:%02d", m, s);
}
