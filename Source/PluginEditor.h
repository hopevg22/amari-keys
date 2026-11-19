#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ak
{
namespace col
{
    const juce::Colour bg { 0xff14161b }, panel { 0xff1c1f26 }, border { 0xff2a2e37 }, well { 0xff0b0d10 },
                       btn { 0xff23262e }, btnBorder { 0xff353a45 }, text { 0xfff2f0ea }, muted { 0xffa3a9b4 },
                       dim { 0xff8c93a0 }, ledOff { 0xff3a3f49 },
                       org { 0xffe8a33d }, keys { 0xffd9c9a3 }, syn { 0xff3dc2c9 }, fx { 0xffa596f5 },
                       pad { 0xffee7bae }, mst { 0xffe8e6e1 }, red { 0xffe5604f };
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
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (11.0f, juce::Font::bold); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// rotary knob with caption
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, juce::Colour accent, int size = 30);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::Slider slider;
    juce::String caption;
    int knobSize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// vertical fader with caption
class Fader : public juce::Component
{
public:
    Fader (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, juce::Colour accent, bool big = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// LED toggle
class Toggle : public juce::ToggleButton
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label, juce::Colour accent);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// segmented selector for choice/int params (grid of cells)
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray options, juce::Colour accent,
               int columns = 0, int valueOffset = 0);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::StringArray options; juce::Colour accent; int columns, offset, selected = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// drawbar (pull down to increase)
class Drawbar : public juce::Component
{
public:
    Drawbar (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, juce::Colour cap);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    void setFromY (float y);
    juce::String caption; juce::Colour capColour; int value = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
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
} // namespace ak

class AmariKeysEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmariKeysEditor (AmariKeysProcessor&);
    ~AmariKeysEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1600, baseH = 600;

private:
    void timerCallback() override;
    void layout();

    struct Content : public juce::Component
    {
        AmariKeysEditor* owner = nullptr;
        void paint (juce::Graphics& g) override { owner->paintContent (g); }
    } content;
    void paintContent (juce::Graphics&);

    AmariKeysProcessor& proc;
    ak::LAF laf;
    juce::OwnedArray<juce::Component> owned;
    template <typename T, typename... Args> T* make (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c); content.addAndMakeVisible (c); return c;
    }

    juce::MidiKeyboardComponent keyboard;
    ak::PadButton* pads[DrumEngine::numPads] {};
    int lastPadHits[DrumEngine::numPads] {};
    juce::TextButton panicButton { "PANIC" }, loadFolderButton { "LOAD FOLDER" }, loadSfzButton { "LOAD SFZ" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> sampleStatusArea;
    juce::String lastStatus;
    juce::Slider splitNote, transpose;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> splitAtt, transAtt, blendAtt;

    struct Section { juce::Rectangle<int> r; juce::String title; juce::Colour accent; };
    juce::Array<Section> sections;
    juce::Array<std::pair<juce::Rectangle<int>, juce::String>> captions;
    juce::Rectangle<int> zoneBar;
    int lastSplit = -1; bool lastSplitOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmariKeysEditor)
};
