#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ak
{
namespace col
{
    const juce::Colour bg { 0xff0d0d0e }, top { 0xff0a0a0b }, panel { 0xff18181a }, border { 0xff2a2a2d }, well { 0xff080809 },
                       btn { 0xff222224 }, btnBorder { 0xff323235 }, text { 0xffd6d6d8 }, muted { 0xff8e8e93 }, dim { 0xff6e6e73 },
                       ledOff { 0xff3a3a3d }, acc { 0xffd9541e }, accDim { 0xff8a5a44 }, track { 0xff2c2c2f };
}

class LAF : public juce::LookAndFeel_V4
{
public:
    LAF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return juce::FontOptions (12.0f, juce::Font::bold); }
};

void drawLed (juce::Graphics&, juce::Rectangle<float> r, bool on);

// Base for controls that can be re-pointed at another parameter (editor panels follow the selected layer)
struct Bindable { virtual ~Bindable() = default; virtual void bind (const juce::String& paramId) = 0; juce::String paramId; };

// Left button only: the right button opens the MIDI Learn menu instead of moving a control.
class LSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
};
class Btn : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseUp (e); }
};

class Knob : public juce::Component, public Bindable
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, int size = 30);
    void bind (const juce::String& id) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    LSlider slider;
private:
    juce::AudioProcessorValueTreeState& state;
    juce::String caption; int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class VSlider : public juce::Component, public Bindable
{
public:
    VSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void bind (const juce::String& id) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    LSlider slider;
private:
    juce::AudioProcessorValueTreeState& state;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class LedToggle : public juce::ToggleButton, public Bindable
{
public:
    LedToggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    void bind (const juce::String& id) override;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseUp (e); }
private:
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// grid of LED + label items for choice / int parameters
class LedList : public juce::Component, public Bindable
{
public:
    LedList (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray options, int columns = 1, int valueOffset = 0);
    void bind (const juce::String& id) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::AudioProcessorValueTreeState& state;
    juce::StringArray options; int columns, offset, selected = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

class Drawbar : public juce::Component, public Bindable
{
public:
    Drawbar (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, bool lightCap);
    void bind (const juce::String& id) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setFromY (float y);
    juce::AudioProcessorValueTreeState& state;
    juce::String caption; bool light; int value = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

class Lcd : public juce::Component
{
public:
    std::function<std::pair<juce::String, juce::String>()> text;
    std::function<void()> onDoubleClick, onClick;
    bool big = false;
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && ! e.mods.isPopupMenu() && e.getNumberOfClicks() == 1 && ! e.mouseWasDraggedSinceMouseDown()) onClick(); }
};

class PadButton : public juce::Component
{
public:
    PadButton (AmariKeysProcessor& p, int idx) : proc (p), index (idx) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    float flash = 0.0f;
private:
    AmariKeysProcessor& proc; int index;
};

class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (AmariKeysProcessor&, int layer);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
    bool selected = false;
    LedToggle onButton;
    juce::Slider fader;
private:
    AmariKeysProcessor& proc; int layer;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAtt;
    juce::Rectangle<int> zoneArea;
};
// Endless data dial (MODX style): drag or scroll to step, double-click = ENTER.
class DataDial : public juce::Component
{
public:
    std::function<void (int)> onStep;
    std::function<void()> onEnter;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragAcc = 0.0f; lastY = 0; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const int y = e.getDistanceFromDragStartY();
        dragAcc += (float) (lastY - y); lastY = y;
        while (dragAcc >= 7.0f) { dragAcc -= 7.0f; step (1); }
        while (dragAcc <= -7.0f) { dragAcc += 7.0f; step (-1); }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 8.0f;
        while (wheelAcc >= 1.0f) { wheelAcc -= 1.0f; step (1); }
        while (wheelAcc <= -1.0f) { wheelAcc += 1.0f; step (-1); }
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onEnter) onEnter(); }
private:
    void step (int d) { angle += 0.26f * (float) d; repaint(); if (onStep) onStep (d); }
    float angle = 0.0f, dragAcc = 0.0f, wheelAcc = 0.0f; int lastY = 0;
};

// Pitch bend / modulation wheel. Drag up and down; the pitch wheel springs back to the centre.
class Wheel : public juce::Component, private juce::Timer
{
public:
    Wheel (bool isPitch, juce::String label) : pitch (isPitch), caption (std::move (label)) {}
    std::function<void (float)> onChange;      // pitch -1..1, mod 0..1
    void setDisplayed (float v) { if (! dragging && std::abs (v - value) > 0.002f) { value = v; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) set (0.0f); }
private:
    void timerCallback() override;
    void set (float v) { value = v; repaint(); if (onChange) onChange (v); }
    bool pitch; juce::String caption;
    float value = 0.0f, startValue = 0.0f; bool dragging = false;
    juce::Rectangle<float> wheelArea() const { return getLocalBounds().toFloat().withTrimmedBottom (16.0f).reduced (4.0f, 2.0f); }
};
} // namespace ak

class AmariKeysEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmariKeysEditor (AmariKeysProcessor&);
    ~AmariKeysEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseW = 1924, baseH = 640;

private:
    void timerCallback() override;
    void selectLayer (int layer);
    void paintContent (juce::Graphics&);
    void chooseSamples (bool sfz, int layer);
    void showSoundsMenu (int layer, juce::Component& anchor);
    void saveSound (int layer);
    void showPatchMenu();
    void askName (const juce::String& title, const juce::String& message, const juce::String& initial, std::function<void (juce::String)> done);
    void stepPatch (int delta);
    void showMidiMap();
    void showLearnMenu (juce::Component* target, int code, juce::Point<int> where);
    juce::Component::SafePointer<juce::Component> learnComp;
    struct BlinkFrame : juce::Component
    {
        BlinkFrame() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xffff6a1f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.5f);
            g.setColour (juce::Colour (0x33ff6a1f)); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);
        }
    } blink;
    int learnStartHits = 0, learnStartCc = 0, learnStartNotes = 0, blinkTicks = 0;
    juce::String learnedMsg; juce::uint32 learnedMsgUntil = 0;

    struct Content : public juce::Component
    {
        AmariKeysEditor* owner = nullptr;
        void paint (juce::Graphics& g) override { owner->paintContent (g); }
    } content;

    AmariKeysProcessor& proc;
    ak::LAF laf;
    juce::OwnedArray<juce::Component> owned;
    template <typename T, typename... Args> T* make (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c); content.addAndMakeVisible (c); return c;
    }

    // layer-following controls: (control, parameter suffix)
    std::vector<std::pair<ak::Bindable*, juce::String>> keysControls, synthControls;
    int keysLayer = 0, synthLayer = 4, selectedLayer = 0;

    juce::MidiKeyboardComponent keyboard;
    ak::PadButton* pads[AmariKeysProcessor::numPads] {};
    int lastPadHits[AmariKeysProcessor::numPads] {};
    ak::ChannelStrip* strips[AmariKeysProcessor::numLayers] {};
    ak::Btn prevPatch { "<" }, nextPatch { ">" }, browsePatch { "BROWSE" }, saveAsPatch { "SAVE AS" }, renamePatchBtn { "RENAME" }, deletePatchBtn { "DELETE" };
    ak::Btn storeButton { "STORE" }, panicButton { "PANIC" }, mapButton { "MIDI MAP" }, enterButton { "ENTER" };
    ak::DataDial* dataDial = nullptr;
    ak::Btn keysLoadFolder { "LOAD FOLDER" }, keysLoadSfz { "LOAD SFZ" }, synLoadFolder { "FOLDER" }, synLoadSfz { "SFZ" };
    ak::Btn keysSounds { "SOUNDS" }, keysSaveSound { "SAVE SOUND" }, synSounds { "SOUNDS" }, synSaveSound { "SAVE" };
    ak::LSlider splitNote, transpose, bendRange;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> splitAtt, transAtt, bendAtt;
    ak::Wheel* pitchWheel = nullptr; ak::Wheel* modWheel = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    ak::Lcd* progLcd = nullptr; ak::Lcd* keysLcd = nullptr; ak::Lcd* synLcd = nullptr;

    struct Section { juce::Rectangle<int> r; juce::String title; bool dynamicTitle = false; int kind = 0; };
    juce::Array<Section> sections;
    juce::Array<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::Array<juce::Rectangle<int>> rules;
    juce::Rectangle<int> zoneBar;
    int lastSplit = -1; bool lastSplitOn = false; int lastProgram = -1, lastLearnHits = 0;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmariKeysEditor)
};
