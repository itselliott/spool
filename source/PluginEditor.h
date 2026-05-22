#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include "PluginProcessor.h"

//==============================================================================
/**
    Numbered slot button. Left-click → onClick (save or load).
    Right-click → onRightClick (clear).
*/
struct SlotButton : juce::TextButton
{
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
        {
            if (onRightClick) onRightClick();
            return;
        }
        juce::TextButton::mouseDown (e);
    }
};

//==============================================================================
/**
    TextButton that doubles as an external drag source. Single click still
    fires onClick (file-save dialog); a click-and-drag past a small threshold
    renders a WAV via onDragOut() and hands it to the OS as a drag-and-drop
    payload so the user can drop it onto a DAW track / Finder / Explorer.
*/
struct DragOutButton : juce::TextButton
{
    using juce::TextButton::TextButton;
    std::function<juce::File()> onDragOut;

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Suppress until we have a real drag, otherwise tiny mouse jitter
        // during a click would steal the click event.
        if (dragInProgress || ! onDragOut) { juce::TextButton::mouseDrag (e); return; }
        if (e.getDistanceFromDragStart() < 5) return;

        const auto file = onDragOut();
        if (file == juce::File{}) return;

        dragInProgress = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles (
            { file.getFullPathName() }, /* canMoveFiles = */ false, this,
            [this] { dragInProgress = false; });
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragInProgress) return;     // drop in progress, swallow the click
        juce::TextButton::mouseUp (e);
    }
private:
    bool dragInProgress = false;
};

//==============================================================================
/**
    Icon button that paints a vector shape instead of relying on font glyphs.
    Removes the "PLAY text shows when ▶ glyph won't render" class of bug.
*/
struct IconShapeButton : juce::TextButton
{
    enum class Shape { Triangle, Circle, Square };

    IconShapeButton (Shape s, juce::Colour off, juce::Colour on)
        : shape (s), shapeOff (off), shapeOn (on) {}

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto& lnf = getLookAndFeel();
        lnf.drawButtonBackground (g, *this,
                                  findColour (getToggleState() ? buttonOnColourId : buttonColourId),
                                  isMouseOver, isButtonDown);

        // Centred SQUARE bounding box — keeps the shape proportional regardless
        // of how wide-and-short the button itself is.
        const float side = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.50f;
        const float cx   = (float) getWidth()  * 0.5f;
        const float cy   = (float) getHeight() * 0.5f;
        const auto  b    = juce::Rectangle<float> (cx - side * 0.5f, cy - side * 0.5f, side, side);

        g.setColour (getToggleState() ? shapeOn : shapeOff);
        juce::Path p;
        switch (shape)
        {
            case Shape::Triangle:
                p.addTriangle (b.getX(),     b.getY(),
                               b.getX(),     b.getBottom(),
                               b.getRight(), b.getCentreY());
                break;
            case Shape::Circle:
                p.addEllipse (b);
                break;
            case Shape::Square:
                p.addRectangle (b);
                break;
        }
        g.fillPath (p);
    }

private:
    Shape        shape;
    juce::Colour shapeOff;
    juce::Colour shapeOn;
};

//==============================================================================
/**
    Spinning tape reel. Rotation is driven from the outside via setAngle()
    in radians; the editor's timer ticks the angle forward while playing.
*/
class TapeReelComponent : public juce::Component
{
public:
    TapeReelComponent() = default;

    void setAngle (float radians) noexcept { if (! scratching) { angle = radians; repaint(); } }
    void paint    (juce::Graphics&) override;

    // DJ-scratch interaction.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    bool isScratching() const noexcept { return scratching; }

    // Callbacks (set by the editor):
    std::function<void(float)> onScratchUpdate;  // signed playback rate
    std::function<void()>      onScratchEnd;

private:
    float  angleFromMouse (const juce::MouseEvent&) const noexcept;
    static float wrapPi (float a) noexcept;

    float  angle             = 0.0f;
    bool   scratching        = false;
    float  lastMouseAngle    = 0.0f;
    double lastScratchTimeMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeReelComponent)
};

//==============================================================================
/**
    A "JUICE" knob: rotary slider with a name label above, value label below,
    and an LED that can be flashed on trigger events (e.g. each LOOP wrap).
*/
class JuiceKnob : public juce::Component
{
public:
    explicit JuiceKnob (const juce::String& displayName);

    juce::Slider& getSlider() noexcept { return slider; }
    void setValueText (const juce::String& text);

    // Recolour everything that uses the accent (slider fill, value-label
    // text). Called after the SP-L easter-egg picks a new theme so the
    // knob doesn't keep its old orange.
    void setAccent (juce::Colour c);

    // Compact mode shrinks the name + value labels so the rotary itself
    // gets the bulk of the available area. Useful for small inline knobs
    // (e.g. the SAMPLE GAIN trim next to the waveform).
    void setCompact (bool shouldBeCompact);

    // Drag-to-reorder: when set, mouse drags on the top icon strip emit
    // onReorderDrop(pointInParent) on release. Used for the JUICE row.
    void setReorderable (bool can) { reorderable = can; }
    std::function<void (juce::Point<int>)> onReorderDrop;

    // Optional vector icon — when set, drawn centred in the name slot at
    // a uniform size regardless of which icon (replaces text). Use for
    // equal-sized JUICE icons so they don't render with varying weights.
    void setIconPath (juce::Path p);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Pulse the LED. Decays back over ~0.15s.
    void flashLed() noexcept { ledAlpha = 1.0f; repaint(); }

    // Set the LED brightness directly (for continuous level metering — bypasses fade).
    void setLedLevel (float level) noexcept { ledAlpha = juce::jlimit (0.0f, 1.0f, level); repaint(); }

    // Call from the parent's timer with a delta-time in seconds.
    void tickLed (float deltaSeconds) noexcept;

    void paint   (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    juce::Path   iconPath;
    float        ledAlpha = 0.0f;
    bool         compact  = false;
    bool         reorderable = false;
    bool         reorderDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuiceKnob)
};

//==============================================================================
class SpoolAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer,
                                  private juce::ChangeListener
{
public:
    explicit SpoolAudioProcessorEditor (SpoolAudioProcessor&);
    ~SpoolAudioProcessorEditor() override;

    void paint              (juce::Graphics&) override;
    void paintOverChildren  (juce::Graphics&) override;
    void resized() override;

    // Mouse on the waveform: click = seek, drag = highlight loop region,
    // double-click = set LOOP anchor, ctrl+scroll = zoom.
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Keyboard 1-8 (number row OR numpad) triggers slot 1-8.
    bool keyPressed (const juce::KeyPress&) override;

    // FileDragAndDropTarget --------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray&, int, int) override;
    void fileDragExit           (const juce::StringArray&) override;

private:
    // Timer ------------------------------------------------------------------
    void timerCallback() override;

    // ChangeListener (AudioThumbnail) ----------------------------------------
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // Helpers ----------------------------------------------------------------
    void openFileChooser();
    void openExportChooser();
    void drawWaveform (juce::Graphics&, juce::Rectangle<int> area);

    static juce::String formatTime (double seconds);

    //------------------------------------------------------------------------
    SpoolAudioProcessor& processorRef;

    // Utility row: four matched icon buttons. Audio device settings live in
    // the JUCE standalone Options menu (top bar).
    juce::TextButton loadButton     { juce::String::fromUTF8 ("\xE2\x86\x91") }; // ↑ load
    juce::TextButton overdubButton  { "+" };
    juce::TextButton loopButton     { juce::String::fromUTF8 ("\xE2\x86\xBB") }; // ↻ loop
    DragOutButton    exportButton   { juce::String::fromUTF8 ("\xE2\x86\x93") }; // ↓ export / drag-out

    // Big transport: shape-drawn so they don't depend on font glyph support.
    // (off colour, on colour) — REC flips to white when armed (background goes red).
    IconShapeButton recordButton { IconShapeButton::Shape::Circle,
                                   juce::Colour::fromRGB (0xe5, 0x3a, 0x3a),
                                   juce::Colour::fromRGB (0xee, 0xee, 0xee) };
    IconShapeButton playButton   { IconShapeButton::Shape::Triangle,
                                   juce::Colour::fromRGB (0xee, 0xee, 0xee),
                                   juce::Colour::fromRGB (0xee, 0xee, 0xee) };
    IconShapeButton stopButton   { IconShapeButton::Shape::Square,
                                   juce::Colour::fromRGB (0xee, 0xee, 0xee),
                                   juce::Colour::fromRGB (0xee, 0xee, 0xee) };

    // Custom look for the three oversized transport buttons. Glyphs sit
    // smaller inside the big button for TP-7-style refinement.
    struct BigButtonLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::Font (juce::FontOptions { (float) buttonHeight * 0.26f, juce::Font::bold });
        }
    };
    BigButtonLookAndFeel bigButtonLnf;

    // Utility row icons (↑ + ↻ ↓): big and bold so the glyphs almost fill
    // their button. Default JUCE scaling left them tiny in the middle.
    struct UtilityIconLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::Font (juce::FontOptions { (float) buttonHeight * 0.75f, juce::Font::bold });
        }
    };
    UtilityIconLookAndFeel utilityIconLnf;

    // Brushed-metal notched-pot. 3D-ish aluminium body with a radial gradient,
    // faint concentric "brushing" rings, recessed outer well, and a single
    // notch indicator line tipped in the accent colour. Pro-audio gear vibe.
    struct BrushedMetalKnobLookAndFeel : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider& slider) override
        {
            const float cx = (float) x + (float) width  * 0.5f;
            const float cy = (float) y + (float) height * 0.5f;
            const float outerR = juce::jmin ((float) width, (float) height) * 0.5f - 2.0f;
            const float wellR  = outerR - 1.5f;            // recessed surround
            const float bodyR  = outerR - 6.0f;            // metal body
            const float angle  = rotaryStartAngle
                               + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
            const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

            // 1) Recessed well — chrome-style radial gradient so the surround
            //    catches the light instead of reading as a flat black ring.
            {
                juce::ColourGradient wellGrad (
                    juce::Colour::fromRGB (0x32, 0x32, 0x38),
                    cx - wellR * 0.55f, cy - wellR * 0.55f,
                    juce::Colour::fromRGB (0x06, 0x06, 0x09),
                    cx + wellR * 0.85f, cy + wellR * 0.85f,
                    true);
                g.setGradientFill (wellGrad);
                g.fillEllipse (cx - wellR, cy - wellR, wellR * 2.0f, wellR * 2.0f);
            }
            // Specular highlight — bright crescent on the upper-left rim.
            {
                juce::Path spec;
                const float r2 = wellR - 0.8f;
                spec.addCentredArc (cx, cy, r2, r2, 0.0f,
                                    -juce::MathConstants<float>::pi * 0.85f,
                                    -juce::MathConstants<float>::pi * 0.30f, true);
                g.setColour (juce::Colours::white.withAlpha (0.22f));
                g.strokePath (spec, juce::PathStrokeType (1.2f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            // 2) Coloured value arc just inside the well (lights up the
            //    position visibly even before you look at the notch).
            {
                juce::Path arc;
                arc.addCentredArc (cx, cy, outerR - 2.5f, outerR - 2.5f, 0.0f,
                                   rotaryStartAngle, angle, true);
                g.setColour (fill.withAlpha (0.85f));
                g.strokePath (arc, juce::PathStrokeType (2.2f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }

            // 3) Brushed aluminium body — radial gradient highlight from
            //    upper-left to lower-right for 3D shading.
            {
                juce::ColourGradient grad (
                    juce::Colour::fromRGB (0xcc, 0xcc, 0xd0),
                    cx - bodyR * 0.55f, cy - bodyR * 0.55f,
                    juce::Colour::fromRGB (0x4a, 0x4a, 0x50),
                    cx + bodyR * 0.85f, cy + bodyR * 0.85f,
                    true);
                g.setGradientFill (grad);
                g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
            }

            // 4) Faint concentric "brushing" rings — gives the matte metal
            //    its characteristic texture without being noisy.
            g.setColour (juce::Colour::fromRGB (0xa8, 0xa8, 0xae).withAlpha (0.12f));
            for (int i = 1; i <= 5; ++i)
            {
                const float r = bodyR * (0.18f + (float) i * 0.14f);
                g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 0.6f);
            }

            // 5) Edge highlight (rim) — thin bright line on top, subtle.
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawEllipse (cx - bodyR + 0.5f, cy - bodyR + 0.5f,
                           bodyR * 2.0f - 1.0f, bodyR * 2.0f - 1.0f, 0.8f);
            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.drawEllipse (cx - bodyR - 0.2f, cy - bodyR - 0.2f,
                           bodyR * 2.0f + 0.4f, bodyR * 2.0f + 0.4f, 0.6f);

            // 5b) Indentation notches in the well — like the position
            // markings on a real potentiometer. Drawn INSIDE the dark
            // well annulus so they (a) never clip past the slider bounds
            // at the cardinal points, and (b) always read clearly against
            // the dark background. 13 evenly-spaced ticks across the
            // rotary travel; the one closest to the current angle brightens
            // to accent so it reads as "you are here".
            {
                const int   numTicks   = 13;
                // Ticks live inside the well (which spans bodyR..outerR,
                // about 6px wide). Place them flush against the well's
                // outer edge so they look like notches eating into the rim.
                const float tickOuter  = outerR - 0.5f;
                const float tickInner  = outerR - 4.5f;
                const float arc        = rotaryEndAngle - rotaryStartAngle;
                const float halfTickArc= arc / (float) (numTicks - 1) * 0.45f;
                for (int i = 0; i < numTicks; ++i)
                {
                    const float t  = (float) i / (float) (numTicks - 1);
                    const float ta = rotaryStartAngle + t * arc;
                    const float sa = std::sin (ta);
                    const float ca = std::cos (ta);
                    const float x1 = cx + sa * tickInner;
                    const float y1 = cy - ca * tickInner;
                    const float x2 = cx + sa * tickOuter;
                    const float y2 = cy - ca * tickOuter;
                    const bool  active = std::abs (ta - angle) < halfTickArc;
                    if (active)
                    {
                        g.setColour (fill.brighter (0.2f));
                        g.drawLine (x1, y1, x2, y2, 2.0f);
                    }
                    else
                    {
                        g.setColour (juce::Colour::fromRGB (0xb0, 0xb0, 0xb6).withAlpha (0.70f));
                        g.drawLine (x1, y1, x2, y2, 1.2f);
                    }
                }
            }

            // 6) Notch indicator: thick dark line from near-edge inward,
            //    over-stroked thinner in accent colour. Reads from a glance.
            const float notchOuter = bodyR * 0.92f;
            const float notchInner = bodyR * 0.45f;
            const float sa = std::sin (angle);
            const float ca = std::cos (angle);
            const float nx1 = cx + sa * notchOuter;
            const float ny1 = cy - ca * notchOuter;
            const float nx2 = cx + sa * notchInner;
            const float ny2 = cy - ca * notchInner;
            g.setColour (juce::Colour::fromRGB (0x1a, 0x1a, 0x1e));
            g.drawLine (nx1, ny1, nx2, ny2, 3.5f);
            g.setColour (fill.brighter (0.10f));
            g.drawLine (nx1, ny1, nx2, ny2, 1.6f);
        }
    };
    BrushedMetalKnobLookAndFeel brushedKnobLnf;

    // Status-indicator button: a single discrete LED dot on the silver
    // panel — no dark pill behind it. The dot colour comes from
    // buttonColourId so existing colour-change wiring still works.
    struct LedPillLookAndFeel : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour&, bool isOver, bool isDown) override
        {
            const auto b = button.getLocalBounds().toFloat();
            const auto led = button.findColour (juce::TextButton::buttonColourId);
            // Size the LED to fit comfortably inside the button height with
            // a tiny padding for the halo glow.
            const float ledR = juce::jmin (b.getHeight(), b.getWidth()) * 0.38f;
            const float cx   = b.getCentreX();
            const float cy   = b.getCentreY();

            // Soft halo — multiple translucent rings for a glow.
            for (int i = 3; i > 0; --i)
            {
                const float k = (float) i;
                const float rr = ledR * (1.0f + k * 0.45f);
                g.setColour (led.withAlpha (0.09f / k));
                g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            }

            // Dark recessed ring so the LED sits in a tiny bezel and reads
            // as a discrete circle on the silver panel.
            const float bezelR = ledR + 1.5f;
            g.setColour (juce::Colour::fromRGB (0x18, 0x18, 0x1c).withAlpha (0.85f));
            g.fillEllipse (cx - bezelR, cy - bezelR, bezelR * 2.0f, bezelR * 2.0f);

            // The LED itself.
            g.setColour (led);
            g.fillEllipse (cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f);

            // Specular highlight (small bright crescent on the upper-left).
            g.setColour (juce::Colours::white.withAlpha (isDown ? 0.12f : 0.40f));
            g.fillEllipse (cx - ledR * 0.45f, cy - ledR * 0.70f,
                           ledR * 0.55f, ledR * 0.38f);

            // Crisp rim around the LED.
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f, 0.6f);

            juce::ignoreUnused (isOver);
        }
        // Don't draw any text — the colour IS the state.
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override {}
    };
    LedPillLookAndFeel ledPillLnf;

    // Snowflake-paint LookAndFeel for the FREEZE button — guarantees the
    // glyph renders regardless of system font fallback (the ❄ unicode
    // character is missing in some fallback chains and shows as a tofu box).
    struct SnowflakeLookAndFeel : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                   const juce::Colour& bg, bool isOver, bool isDown) override
        {
            juce::LookAndFeel_V4::drawButtonBackground (g, button, bg, isOver, isDown);

            const auto b      = button.getLocalBounds().toFloat();
            const float cx    = b.getCentreX();
            const float cy    = b.getCentreY();
            const float armR  = juce::jmin (b.getWidth(), b.getHeight()) * 0.36f;
            const float branchOff = armR * 0.35f;
            const float branchLen = armR * 0.30f;

            const auto col = button.findColour (button.getToggleState()
                                                ? juce::TextButton::textColourOnId
                                                : juce::TextButton::textColourOffId);
            g.setColour (col);

            // Six arms of the snowflake (every 60°) with a small "V" branch
            // near the tip of each arm.
            for (int i = 0; i < 6; ++i)
            {
                const float a   = (float) i * juce::MathConstants<float>::pi / 3.0f;
                const float sa  = std::sin (a);
                const float ca  = std::cos (a);
                const float tx  = cx + sa * armR;
                const float ty  = cy - ca * armR;
                g.drawLine (cx, cy, tx, ty, 1.5f);

                // Branches at branchOff from the centre, perpendicular-ish.
                const float bx  = cx + sa * branchOff;
                const float by  = cy - ca * branchOff;
                const float perpA1 = a + juce::MathConstants<float>::pi * 0.40f;
                const float perpA2 = a - juce::MathConstants<float>::pi * 0.40f;
                g.drawLine (bx, by,
                            bx + std::sin (perpA1) * branchLen,
                            by - std::cos (perpA1) * branchLen, 1.2f);
                g.drawLine (bx, by,
                            bx + std::sin (perpA2) * branchLen,
                            by - std::cos (perpA2) * branchLen, 1.2f);
            }
        }
        // No text — we draw the glyph ourselves.
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override {}
    };
    SnowflakeLookAndFeel snowflakeLnf;

    juce::Label statusLabel;

    // NUDGE: appears only when a loop region is active.
    juce::Slider nudgeSlider;

    juce::Rectangle<int> waveformArea;
    float dragStartNorm   = 0.0f;     // recorded on mouseDown over the waveform
    float waveformZoom    = 1.0f;     // 1.0 = full sample, >1 = zoomed in
    float waveformOffset  = 0.0f;     // 0..1, start of visible window

    // Convert between view-local norm (0..1 across visible waveform area)
    // and FULL-sample norm (0..1 across the whole sample).
    float viewToFullNorm (float v) const noexcept { return waveformOffset + v / waveformZoom; }

    TapeReelComponent reel;
    float reelAngle  = 0.0f;          // radians, accumulates while playing
    double lastTickMs = 0.0;

    juce::Image backgroundImage;      // optional: <project>/assets/bg.jpg|png

    juce::Rectangle<int> wordmarkArea;   // hit-area for SP-L double-click
    void applyRandomTheme();

    // Icon-only knobs. ⌒ = filter (LP/HP sweep), ∽ = ghost (echo), ≈ = haze (reverb).
    JuiceKnob inputKnob      { "" };
    JuiceKnob speedKnob      { juce::String::fromUTF8 ("\xC2\xBB") };               // » speed
    JuiceKnob sampleGainKnob { "" };
    JuiceKnob filterKnob     { juce::String::fromUTF8 ("\xE2\x8C\x92") };           // ⌒ filter
    JuiceKnob ghostKnob      { juce::String::fromUTF8 ("\xE2\x88\xBD") };           // ∽ ghost
    JuiceKnob hazeKnob       { juce::String::fromUTF8 ("\xE2\x89\x88") };           // ≈ haze

    juce::TextButton filterQButton;        // LOW / MID / HI Q, cycles on click
    juce::TextButton filterLfoRateButton;  // OFF / 1/2 / 1/4 / .../1/128 LFO rate
    juce::TextButton filterLfoRangeButton; // SM / MD / LG LFO depth
    juce::TextButton inputCompButton;   // VINTAGE / FET / OPTO, cycles on click
    juce::TextButton ghostTimeButton;   // 1/16 / 1/8 / 1/4, under GHOST
    juce::TextButton freezeButton;      // toggle, under HAZE
    juce::TextButton hazePresetButton;  // cycle 6 reverb presets, next to FREEZE
    juce::TextButton clearSampleButton; // ✕ — clears the loaded sample

    // Loop control row: 6 size buttons + cutoff cycle.
    std::array<juce::TextButton, 6> loopSizeButtons {};
    juce::TextButton loopCutoffButton;  // -12 / -24 / BKWL

    // BPM control: neon OLED-style window showing the current internal BPM.
    //   • drag vertically to change (up = faster, down = slower)
    //   • shift-drag for fine adjustment
    // Tap-tempo lives on a separate sibling button (tapButton).
    class BpmControl : public juce::Component
    {
    public:
        explicit BpmControl (SpoolAudioProcessor& p) : proc (p) {}
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void paint     (juce::Graphics&) override;

    private:
        SpoolAudioProcessor& proc;
        double dragStartBpm = 120.0;
        int    dragStartY   = 0;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmControl)
    };
    BpmControl       bpmControl { processorRef };
    juce::TextButton tapButton  { "TAP" };

    // Tap-tempo state lives on the editor so the bare TAP button can drive it.
    double             lastTapMs = -1.0;
    std::vector<double> tapIntervals;
    void registerTap();

    // TAPE wet/dry knob + machine cycle button.
    JuiceKnob        tapeKnob { "" };
    juce::TextButton tapeMachineButton;
    juce::TextButton folderButton;      // ◫ — pick a folder (in OLED corner)
    juce::TextButton folderPrevButton;  // − — prev sample in folder
    juce::TextButton folderNextButton;  // + — next sample in folder
    std::unique_ptr<juce::FileChooser> folderChooser;

    // 8 loop slots: left-click to save (when loop active) or load, right-click to clear.
    std::array<SlotButton, SpoolAudioProcessor::kNumSlots> slotButtons;

    // Update INPUT knob colours to match the current compressor voice.
    void applyInputCompColour();

    // Update TAPE knob+button colours to match the current tape machine.
    void applyTapeMachineColour();

    // Refresh every knob/button from the processor's current state. Used after
    // loading a slot (which restores a snapshot of all settings).
    void syncControlsFromProcessor();

    // Drag-to-reorder helper. Given the knob that was dragged (0=filter,
    // 1=ghost, 2=haze) and the parent-coord drop X, swap into the slot
    // closest to that X.
    void handleKnobReorder (int knobEffectIdx, int dropX);

    // Shared slot action. allowSave=true (mouse click): empty slot captures
    // current state. allowSave=false (keyboard): empty slot is a no-op so
    // you can't accidentally overwrite by tapping a number key.
    void triggerSlot (int slot, bool allowSave = true);

    // Try to load a background image from the assets dir. Drop a JPG/PNG named
    // bg.jpg or bg.png at <project>/assets/ to set the SPOOL backdrop.
    void loadBackgroundImage();

    bool fileBeingDragged = false;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpoolAudioProcessorEditor)
};
