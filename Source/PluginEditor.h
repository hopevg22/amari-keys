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

class Knob : public juce::Component, public Bindable
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, int size = 30);
    void bind (const juce::String& id) override;
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
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
    juce::Slider slider;
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
    std::function<void()> onDoubleClick;
    bool big = false;
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
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
} // namespace ak

class AmariKeysEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmariKeysEditor (AmariKeysProcessor&);
    ~AmariKeysEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseW = 1720, baseH = 640;

private:
    void timerCallback() override;
    void selectLayer (int layer);
    void paintContent (juce::Graphics&);
    void chooseSamples (bool sfz, int layer);

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
    juce::TextButton bankButtons[4], slotButtons[4];
    juce::TextButton storeButton { "STORE" }, panicButton { "PANIC" }, learnButton { "MIDI LEARN" };
    juce::TextButton keysLoadFolder { "LOAD FOLDER" }, keysLoadSfz { "LOAD SFZ" }, synLoadFolder { "LOAD FOLDER" }, synLoadSfz { "LOAD SFZ" };
    juce::Slider splitNote, transpose;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> splitAtt, transAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    ak::Lcd* progLcd = nullptr; ak::Lcd* keysLcd = nullptr; ak::Lcd* synLcd = nullptr;

    struct Section { juce::Rectangle<int> r; juce::String title; bool dynamicTitle = false; int kind = 0; };
    juce::Array<Section> sections;
    juce::Array<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::Array<juce::Rectangle<int>> rules;
    juce::Rectangle<int> zoneBar;
    int lastSplit = -1; bool lastSplitOn = false; int lastProgram = -1, lastLearnHits = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmariKeysEditor)
};
