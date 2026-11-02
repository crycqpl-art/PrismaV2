#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace prisma
{
struct Theme
{
    static juce::Colour bodyTop()    { return juce::Colour (0xffefede8); }
    static juce::Colour bodyBottom() { return juce::Colour (0xffdcd9d1); }
    static juce::Colour panel()      { return juce::Colour (0xffe4e1da); }
    static juce::Colour groove()     { return juce::Colour (0xffbfbab0); }
    static juce::Colour ink()        { return juce::Colour (0xff3a3936); }
    static juce::Colour label()      { return juce::Colour (0xff6c6962); }
    static juce::Colour accent()     { return juce::Colour (0xfff0502a); }
    static juce::Colour screen()     { return juce::Colour (0xff060607); }
    static juce::Colour lcd()        { return juce::Colour (0xffc9cdd2); }
};

juce::Font labelFont (float size, bool bold = true);
juce::Font monoFont (float size);
void drawKeyBody (juce::Graphics&, juce::Rectangle<float>, bool down, bool hover);
void drawLed (juce::Graphics&, juce::Point<float> centre, bool on);

class HardwareLook : public juce::LookAndFeel_V4
{
public:
    HardwareLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, bool big = false);
    void resized() override;
    juce::Slider slider;
    juce::Label label;
    bool big;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// A row/grid of hardware keys with LEDs bound to a choice parameter.
class KeyGroup : public juce::Component
{
public:
    KeyGroup (juce::AudioProcessorValueTreeState&, const juce::String& id, int columns);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = -1; repaint(); }

private:
    juce::Rectangle<float> cell (int i) const;
    juce::RangedAudioParameter* param = nullptr;
    juce::StringArray options;
    int columns, pressed = -1, value = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// One key with an LED bound to a bool parameter.
class LedToggle : public juce::Component
{
public:
    LedToggle (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }

private:
    juce::String text;
    bool on = false, down = false;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// A key with an LED that is not a parameter (tabs).
class TabKey : public juce::Component
{
public:
    TabKey (const juce::String& t, std::function<void()> onClick) : text (t), click (std::move (onClick)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (click) click(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    bool selected = false;

private:
    juce::String text;
    bool down = false;
    std::function<void()> click;
};

//==============================================================================
class Display : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit Display (PrismaProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void tick();
    void setStatus (const juce::String& s) { status = s; statusFrames = 240; }

private:
    juce::Rectangle<float> stripArea() const;
    juce::Rectangle<float> patternArea() const;
    juce::Rectangle<float> segment (int i, int n) const;
    void drawWave (juce::Graphics&, const Part&, juce::Rectangle<float>, juce::Colour, float thickness, float yJitter) const;

    PrismaProcessor& proc;
    std::array<float, maxParts> energy {};
    std::array<juce::Colour, maxParts> colA, colB;
    std::array<bool, maxParts> reversed {};
    int readLog = 0, frame = 0, statusFrames = 0;
    juce::String status;
    bool dragOver = false;
    juce::Image overlay;
    juce::Random rng;
};

} // namespace prisma

//==============================================================================
class PrismaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismaEditor (PrismaProcessor&);
    ~PrismaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void animate() { timerCallback(); }
    void showTab (int t);

private:
    void timerCallback() override;
    void loadWithChooser();
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String&) const;
    void drawScrew (juce::Graphics&, juce::Point<float>) const;

    PrismaProcessor& proc;
    prisma::HardwareLook look;
    juce::TooltipWindow tooltips { this, 600 };

    prisma::Display display;
    juce::ComboBox presetBox;
    juce::TextButton diceButton { "DICE" }, copyButton { "COPY" }, pasteButton { "PASTE" };
    juce::TextButton loadButton { "LOAD" }, clearButton { "CLEAR" };
    juce::ComboBox splitBox, partsBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> splitAtt, partsAtt;
    prisma::TabKey playTab, orderTab;
    int tab = 0;

    // play
    prisma::Knob rate, length, swing, decay;
    prisma::KeyGroup modeKeys;
    prisma::LedToggle gridSync, choke;
    // order
    prisma::KeyGroup orderKeys, stepsKeys, resetKeys;
    juce::TextButton captureButton { "CAPTURE" }, dicePatternButton { "DICE PATTERN" }, shuffleButton { "SHUFFLE KEYS" };
    prisma::LedToggle lockToggle;
    prisma::Knob reverse;
    // always visible
    prisma::Knob attack, decayTime, sustain, release, lowCut, highCut, reso, crunch, wash, pump, tune, fine, volume;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image texture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaEditor)
};
