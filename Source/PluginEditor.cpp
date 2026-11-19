#include "PluginEditor.h"

using namespace juce;
namespace ak
{
//==============================================================================
LAF::LAF()
{
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxBackgroundColourId, col::well);
    setColour (Slider::textBoxOutlineColourId, col::btnBorder);
    setColour (TextButton::buttonColourId, col::btn);
    setColour (TextButton::textColourOffId, col::text);
    setColour (Label::textColourId, col::text);
}

void LAF::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f);
    const float size = jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);
    const auto c = b.getCentre();
    const float r = size * 0.5f;
    const float angle = start + pos * (end - start);

    Path track; track.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, start, end, true);
    g.setColour (Colour (0xff2e323c)); g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    Path val; val.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, start, angle, true);
    g.setColour (accent); g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    auto body = b.reduced (4.5f);
    g.setGradientFill (ColourGradient (Colour (0xff4a505c), body.getX() + body.getWidth() * 0.35f, body.getY() + body.getHeight() * 0.3f,
                                       Colour (0xff22252d), body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (Colours::black.withAlpha (0.5f)); g.drawEllipse (body, 1.0f);

    Path p; const float len = body.getWidth() * 0.5f - 2.0f;
    p.addRoundedRectangle (-1.0f, -len, 2.0f, len * 0.6f, 1.0f);
    g.setColour (col::text);
    g.fillPath (p, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LAF::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    const auto accent = s.findColour (Slider::trackColourId);
    if (style == Slider::LinearVertical)
    {
        const float cx = x + w * 0.5f;
        auto track = Rectangle<float> (cx - 4.0f, (float) y, 8.0f, (float) h);
        g.setColour (col::well); g.fillRoundedRectangle (track, 4.0f);
        g.setColour (Colour (0xff2e323c)); g.drawRoundedRectangle (track, 4.0f, 1.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.fillRoundedRectangle (track.withTop (pos), 4.0f);
        auto cap = Rectangle<float> (cx - 12.0f, pos - 8.0f, 24.0f, 16.0f);
        g.setGradientFill (ColourGradient (Colour (0xff6a707c), cap.getX(), cap.getY(), Colour (0xff2a2e37), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::black); g.drawRoundedRectangle (cap, 3.0f, 1.0f);
        g.setColour (accent); g.fillRect (cap.reduced (4.0f, 7.0f));
    }
    else
    {
        const float cy = y + h * 0.5f;
        auto track = Rectangle<float> ((float) x, cy - 4.0f, (float) w, 8.0f);
        g.setGradientFill (ColourGradient (col::keys.withAlpha (0.75f), track.getX(), cy, col::syn.withAlpha (0.75f), track.getRight(), cy, false));
        g.fillRoundedRectangle (track, 4.0f);
        auto cap = Rectangle<float> (pos - 9.0f, cy - 8.0f, 18.0f, 16.0f);
        g.setGradientFill (ColourGradient (Colour (0xff6a707c), cap.getX(), cap.getY(), Colour (0xff2a2e37), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Colours::black); g.drawRoundedRectangle (cap, 3.0f, 1.0f);
    }
}

void LAF::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (highlighted ? col::btn.brighter (0.08f) : col::btn); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::btnBorder); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto accent = b.findColour (ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    auto led = Rectangle<float> (r.getX() + 7.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    if (on) { g.setColour (accent.withAlpha (0.35f)); g.fillEllipse (led.expanded (2.5f)); }
    g.setColour (on ? accent : col::ledOff); g.fillEllipse (led);
    g.setColour (on ? col::text : col::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (17.0f).withTrimmedRight (3.0f), Justification::centredLeft, false);
}

void LAF::drawButtonBackground (Graphics& g, Button& b, const Colour& c, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? c.brighter (0.2f) : highlighted ? c.brighter (0.08f) : c); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::btnBorder); g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

Label* LAF::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (11.0f, Font::bold));
    l->setJustificationType (Justification::centred);
    return l;
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const String& id, const String& cap, Colour accent, int size)
    : caption (cap), knobSize (size)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, (double) s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}
void Knob::resized() { slider.setBounds (getLocalBounds().removeFromTop (knobSize).withSizeKeepingCentre (knobSize, knobSize)); }
void Knob::paint (Graphics& g)
{
    g.setColour (col::muted); g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText (caption, getLocalBounds().withTop (knobSize + 2), Justification::centredTop, false);
}

//==============================================================================
Fader::Fader (AudioProcessorValueTreeState& s, const String& id, const String& cap, Colour accent, bool)
    : caption (cap)
{
    slider.setSliderStyle (Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setColour (Slider::trackColourId, accent);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    addAndMakeVisible (slider);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}
void Fader::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (16).reduced (0, 6)); }
void Fader::paint (Graphics& g)
{
    g.setColour (col::text); g.setFont (FontOptions (9.5f, Font::bold));
    g.drawText (caption, getLocalBounds().removeFromBottom (14), Justification::centred, false);
}

//==============================================================================
Toggle::Toggle (AudioProcessorValueTreeState& s, const String& id, const String& label, Colour accent)
{
    setButtonText (label);
    setColour (ToggleButton::tickColourId, accent);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, id, *this);
}

//==============================================================================
Segmented::Segmented (AudioProcessorValueTreeState& s, const String& id, StringArray opts, Colour a, int cols, int valueOffset)
    : options (std::move (opts)), accent (a), columns (cols <= 0 ? options.size() : cols), offset (valueOffset)
{
    att = std::make_unique<ParameterAttachment> (*s.getParameter (id), [this] (float v)
    {
        selected = roundToInt (v) - offset; repaint();
    });
    att->sendInitialUpdate();
}
void Segmented::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colour (0xff2e323c)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    const int rows = (options.size() + columns - 1) / columns;
    const float cw = (r.getWidth() - 4.0f) / columns, ch = (r.getHeight() - 4.0f) / rows;
    g.setFont (FontOptions (jmin (10.0f, ch * 0.6f), Font::bold));
    for (int i = 0; i < options.size(); ++i)
    {
        auto cell = Rectangle<float> (2.0f + (i % columns) * cw, 2.0f + (i / columns) * ch, cw, ch).reduced (1.0f);
        const bool on = i == selected;
        if (on) { g.setColour (Colours::white.withAlpha (0.09f)); g.fillRoundedRectangle (cell, 3.0f); }
        g.setColour (on ? accent : col::muted);
        g.drawFittedText (options[i], cell.toNearestInt(), Justification::centred, 1, 0.7f);
    }
}
void Segmented::mouseDown (const MouseEvent& e)
{
    const int rows = (options.size() + columns - 1) / columns;
    const float cw = (getWidth() - 4.0f) / columns, ch = (getHeight() - 4.0f) / rows;
    const int cx = jlimit (0, columns - 1, (int) ((e.position.x - 2.0f) / cw));
    const int cy = jlimit (0, rows - 1, (int) ((e.position.y - 2.0f) / ch));
    const int idx = cy * columns + cx;
    if (idx < options.size()) att->setValueAsCompleteGesture ((float) (idx + offset));
}

//==============================================================================
Drawbar::Drawbar (AudioProcessorValueTreeState& s, const String& id, const String& cap, Colour c)
    : caption (cap), capColour (c)
{
    att = std::make_unique<ParameterAttachment> (*s.getParameter (id), [this] (float v) { value = roundToInt (v); repaint(); });
    att->sendInitialUpdate();
}
void Drawbar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto slot = r.withTrimmedBottom (14.0f);
    g.setColour (col::well); g.fillRoundedRectangle (slot, 4.0f);
    g.setColour (Colour (0xff2e323c)); g.drawRoundedRectangle (slot.reduced (0.5f), 4.0f, 1.0f);
    const float capH = 22.0f, travel = slot.getHeight() - capH - 2.0f;
    const float capY = slot.getY() + 1.0f + travel * value / 8.0f;
    // shaft with markings
    auto shaft = Rectangle<float> (slot.getCentreX() - 5.0f, slot.getY() + 1.0f, 10.0f, capY - slot.getY());
    g.setColour (Colour (0xff2a2e37)); g.fillRect (shaft);
    g.setColour (Colour (0xff3a3f49));
    for (int i = 1; i <= value; ++i) g.fillRect (shaft.getX(), capY - i * travel / 8.0f, shaft.getWidth(), 1.0f);
    auto cap = Rectangle<float> (slot.getX() + 1.0f, capY, slot.getWidth() - 2.0f, capH);
    g.setColour (capColour); g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (capColour.getPerceivedBrightness() > 0.5f ? col::bg : col::text);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (String (value), cap, Justification::centred, false);
    g.setColour (col::muted); g.setFont (FontOptions (8.5f, Font::bold));
    g.drawText (caption, r.removeFromBottom (12.0f), Justification::centred, false);
}
void Drawbar::setFromY (float y)
{
    const float travel = getHeight() - 14.0f - 24.0f;
    const int v = jlimit (0, 8, roundToInt ((y - 12.0f) / travel * 8.0f));
    if (v != value) att->setValueAsPartOfGesture ((float) v);
}
void Drawbar::mouseDown (const MouseEvent& e) { att->beginGesture(); setFromY (e.position.y); }
void Drawbar::mouseDrag (const MouseEvent& e) { setFromY (e.position.y); }
void Drawbar::mouseUp (const MouseEvent&) { att->endGesture(); }

//==============================================================================
void PadButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::btn.interpolatedWith (col::pad, flash * 0.45f)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (flash > 0.05f ? col::pad : col::btnBorder); g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (col::pad.withAlpha (0.8f)); g.fillRect (r.withTop (r.getBottom() - 3.0f).reduced (4.0f, 0.0f));
    g.setColour (col::text); g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (DrumEngine::name (index), r.reduced (6.0f, 4.0f), Justification::topLeft, false);
    g.setColour (col::dim); g.setFont (FontOptions (8.5f, Font::bold));
    g.drawText (MidiMessage::getMidiNoteName (36 + index, true, true, 4), r.reduced (6.0f, 5.0f), Justification::bottomRight, false);
}
void PadButton::mouseDown (const MouseEvent& e)
{
    const float vel = jlimit (0.3f, 1.0f, 1.0f - e.position.y / (float) getHeight() * 0.6f);
    proc.triggerPad (index, vel);
}
} // namespace ak

//==============================================================================
AmariKeysEditor::AmariKeysEditor (AmariKeysProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
{
    using namespace ak;
    setLookAndFeel (&laf);
    content.owner = this;
    content.setLookAndFeel (&laf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, baseW, baseH);

    auto& s = proc.apvts;
    auto sec = [this] (int x, int y, int w, int h, const String& t, Colour c) { sections.add ({ { x, y, w, h }, t, c }); };
    auto cap = [this] (int x, int y, int w, const char* t) { captions.add ({ { x, y, w, 12 }, String::fromUTF8 (t) }); };
    auto place = [] (Component* c, int x, int y, int w, int h) { c->setBounds (x, y, w, h); return c; };

    // ---------------- top bar ----------------
    cap (420, 16, 40, "SPLIT");
    place (make<Toggle> (s, "split_on", "ON", col::org), 460, 11, 46, 22);
    splitNote.setSliderStyle (Slider::IncDecButtons);
    splitNote.setTextBoxStyle (Slider::TextBoxLeft, false, 44, 22);
    content.addAndMakeVisible (splitNote); splitNote.setBounds (512, 11, 92, 22);
    splitAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "split_note", splitNote);
    cap (626, 16, 70, "TRANSPOSE");
    transpose.setSliderStyle (Slider::IncDecButtons);
    transpose.setTextBoxStyle (Slider::TextBoxLeft, false, 36, 22);
    content.addAndMakeVisible (transpose); transpose.setBounds (694, 11, 84, 22);
    transAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "transpose", transpose);
    cap (800, 16, 60, "VELOCITY");
    place (make<Segmented> (s, "vel_curve", Params::velCurves(), col::mst), 858, 11, 150, 22);
    panicButton.setColour (TextButton::textColourOffId, col::red);
    panicButton.onClick = [this] { proc.panic(); };
    content.addAndMakeVisible (panicButton); panicButton.setBounds (1030, 11, 70, 22);

    const int top = 52, H = 330;
    // ---------------- MIX ----------------
    {
        const int x = 10, w = 170; sec (x, top, w, H, "MIX", col::mst);
        const char* ids[5] = { "mix_org", "mix_keys", "mix_syn", "mix_pads", "master" };
        const char* caps[5] = { "ORG", "KEYS", "SYN", "PADS", "MAIN" };
        const Colour cs[5] = { col::org, col::keys, col::syn, col::pad, col::mst };
        for (int i = 0; i < 5; ++i) place (make<Fader> (s, ids[i], caps[i], cs[i]), x + 8 + i * 31, top + 34, 30, 214);
        cap (x + 12, top + 266, 50, "KEYS");
        cap (x + 66, top + 266, 50, "BLEND");
        cap (x + 128, top + 266, 40, "SYNTH");
        auto* blend = make<Slider>();
        blend->setSliderStyle (Slider::LinearHorizontal);
        blend->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        blend->setDoubleClickReturnValue (true, 0.5);
        place (blend, x + 12, top + 282, w - 24, 26);
        blendAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "blend", *blend);
    }
    // ---------------- ORGAN ----------------
    {
        const int x = 188, w = 400; sec (x, top, w, H, "ORGAN", col::org);
        place (make<Segmented> (s, "org_model", Params::organModels(), col::org), x + 96, top + 8, 200, 22);
        place (make<Toggle> (s, "org_on", "ON", col::org), x + w - 54, top + 8, 44, 22);
        const char* ids[9] = { "db1", "db2", "db3", "db4", "db5", "db6", "db7", "db8", "db9" };
        const char* caps[9] = { "16'", "5 1/3", "8'", "4'", "2 2/3", "2'", "1 3/5", "1 1/3", "1'" };
        const Colour brown (0xff7a4a2a), white (0xffece8dd), black (0xff1a1a1a);
        const Colour cc[9] = { brown, brown, white, white, black, white, black, black, white };
        for (int i = 0; i < 9; ++i) place (make<Drawbar> (s, ids[i], caps[i], cc[i]), x + 12 + i * 28, top + 42, 24, 196);
        const int rx = x + 274, rw = 114;
        cap (rx, top + 42, rw, "PERCUSSION");
        place (make<Toggle> (s, "perc_on", "ON", col::org), rx, top + 56, 55, 22);
        place (make<Toggle> (s, "perc_3rd", "3RD", col::org), rx + 59, top + 56, 55, 22);
        place (make<Toggle> (s, "perc_fast", "FAST", col::org), rx, top + 82, 55, 22);
        place (make<Toggle> (s, "perc_soft", "SOFT", col::org), rx + 59, top + 82, 55, 22);
        cap (rx, top + 114, rw, "VIBRATO / CHORUS");
        place (make<Segmented> (s, "vib_mode", Params::vibModes(), col::org, 4), rx, top + 128, rw, 40);
        place (make<Knob> (s, "org_click", "CLICK", col::org, 30), rx - 4, top + 180, 40, 46);
        place (make<Knob> (s, "org_drive", "DRIVE", col::org, 30), rx + 37, top + 180, 40, 46);
        place (make<Knob> (s, "org_vol", "VOL", col::org, 30), rx + 78, top + 180, 40, 46);
        cap (x + 12, top + 258, 60, "ROTARY");
        place (make<Toggle> (s, "rot_on", "ON", col::org), x + 62, top + 253, 46, 22);
        place (make<Segmented> (s, "rot_speed", Params::rotarySpeeds(), col::org), x + 114, top + 253, 150, 22);
        cap (x + 12, top + 296, 40, "OCTAVE");
        place (make<Segmented> (s, "org_oct", StringArray { "-2", "-1", "0", "+1", "+2" }, col::org, 0, -2), x + 62, top + 291, 150, 22);
    }
    // ---------------- KEYS ----------------
    {
        const int x = 596, w = 260; sec (x, top, w, H, "KEYS", col::keys);
        place (make<Toggle> (s, "keys_on", "ON", col::keys), x + w - 54, top + 8, 44, 22);
        cap (x + 12, top + 42, w - 24, "MODEL  ·  SAMPLE-FREE (SAMPLED PIANO COMES NEXT)");
        place (make<Segmented> (s, "keys_model", Params::keysModels(), col::keys, 2), x + 12, top + 58, w - 24, 50);
        cap (x + 12, top + 124, 60, "TIMBRE");
        place (make<Segmented> (s, "keys_timbre", Params::timbres(), col::keys), x + 70, top + 119, w - 82, 22);
        place (make<Knob> (s, "keys_dyn", "DYNAMICS", col::keys, 34), x + 8, top + 158, 58, 52);
        place (make<Knob> (s, "keys_decay", "DECAY", col::keys, 34), x + 68, top + 158, 58, 52);
        place (make<Knob> (s, "keys_trem", "TREMOLO", col::keys, 34), x + 128, top + 158, 58, 52);
        place (make<Knob> (s, "keys_vol", "VOL", col::keys, 34), x + 188, top + 158, 58, 52);
        cap (x + 12, top + 296, 50, "OCTAVE");
        place (make<Segmented> (s, "keys_oct", StringArray { "-2", "-1", "0", "+1", "+2" }, col::keys, 0, -2), x + 70, top + 291, 150, 22);
        cap (x + 12, top + 232, w - 24, "SUSTAIN PEDAL (CC64) HOLDS KEYS + SYNTH");
    }
    // ---------------- SYNTH ----------------
    {
        const int x = 864, w = 420; sec (x, top, w, H, "SYNTH", col::syn);
        place (make<Segmented> (s, "syn_voice", Params::voiceModes(), col::syn), x + 96, top + 8, 170, 22);
        place (make<Toggle> (s, "syn_on", "ON", col::syn), x + w - 54, top + 8, 44, 22);
        // oscillators
        cap (x + 12, top + 42, 40, "OSC 1");
        place (make<Segmented> (s, "syn_wave1", Params::waves(), col::syn), x + 12, top + 56, 128, 22);
        cap (x + 12, top + 84, 40, "OSC 2");
        place (make<Segmented> (s, "syn_wave2", Params::waves(), col::syn), x + 12, top + 98, 128, 22);
        place (make<Knob> (s, "syn_oscmix", "OSC MIX", col::syn, 30), x + 8, top + 132, 66, 46);
        place (make<Knob> (s, "syn_detune", "DETUNE", col::syn, 30), x + 76, top + 132, 66, 46);
        place (make<Knob> (s, "syn_sub", "SUB", col::syn, 30), x + 8, top + 186, 66, 46);
        place (make<Knob> (s, "syn_glide", "GLIDE", col::syn, 30), x + 76, top + 186, 66, 46);
        // filter
        const int fx = x + 152;
        cap (fx, top + 42, 120, "FILTER");
        place (make<Segmented> (s, "flt_type", Params::filterTypes(), col::syn), fx, top + 56, 124, 22);
        place (make<Knob> (s, "flt_cutoff", "CUTOFF", col::syn, 44), fx - 2, top + 88, 64, 60);
        place (make<Knob> (s, "flt_reso", "RESO", col::syn, 44), fx + 62, top + 88, 64, 60);
        place (make<Knob> (s, "flt_env", "ENV", col::syn, 28), fx - 4, top + 160, 44, 44);
        place (make<Knob> (s, "flt_key", "KEY", col::syn, 28), fx + 40, top + 160, 44, 44);
        place (make<Knob> (s, "flt_drive", "DRIVE", col::syn, 28), fx + 84, top + 160, 44, 44);
        // envelopes
        const int ex = x + 290;
        cap (ex, top + 42, 120, "FILTER ENV");
        const char* fe[4] = { "fenv_a", "fenv_d", "fenv_s", "fenv_r" };
        const char* ae[4] = { "aenv_a", "aenv_d", "aenv_s", "aenv_r" };
        const char* adsr[4] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i) place (make<Fader> (s, fe[i], adsr[i], col::syn), ex + i * 30, top + 54, 28, 86);
        cap (ex, top + 146, 120, "AMP ENV");
        for (int i = 0; i < 4; ++i) place (make<Fader> (s, ae[i], adsr[i], col::syn), ex + i * 30, top + 158, 28, 86);
        // bottom row
        cap (x + 12, top + 258, 40, "LFO");
        place (make<Knob> (s, "lfo_rate", "RATE", col::syn, 28), x + 44, top + 248, 44, 44);
        place (make<Knob> (s, "lfo_amt", "AMOUNT", col::syn, 28), x + 90, top + 248, 44, 44);
        place (make<Knob> (s, "syn_vol", "VOL", col::syn, 28), x + 146, top + 248, 44, 44);
        cap (x + 214, top + 258, 50, "OCTAVE");
        place (make<Segmented> (s, "syn_oct", StringArray { "-2", "-1", "0", "+1", "+2" }, col::syn, 0, -2), x + 262, top + 253, 146, 22);
        cap (x + 12, top + 306, w - 24, "PITCH BEND ±2 ST  ·  ARP COMES IN A LATER BUILD");
    }
    // ---------------- EFFECTS ----------------
    {
        const int x = 1292, w = 298; sec (x, top, w, H, "EFFECTS", col::fx);
        cap (x + 104, top + 13, 20, "TO");
        place (make<Segmented> (s, "fx_target", Params::fxTargets(), col::fx), x + 124, top + 8, 164, 22);
        struct B { const char* on; const char* name; const char* k[3]; const char* c[3]; };
        const B blocks[6] = {
            { "mod_on", "MOD", { "mod_rate", "mod_depth", "mod_mix" }, { "RATE", "DEPTH", "MIX" } },
            { "dly_on", "DELAY", { "dly_time", "dly_fb", "dly_mix" }, { "TIME", "FDBK", "MIX" } },
            { "rev_on", "REVERB", { "rev_size", "rev_damp", "rev_mix" }, { "SIZE", "DAMP", "MIX" } },
            { "amp_on", "AMP", { "amp_drive", "amp_tone", "amp_level" }, { "DRIVE", "TONE", "LEVEL" } },
            { "comp_on", "COMP", { "comp_amt", "comp_atk", "comp_gain" }, { "AMOUNT", "ATTACK", "GAIN" } },
            { "eq_on", "EQ", { "eq_low", "eq_mid", "eq_high" }, { "LOW", "MID", "HIGH" } } };
        const int bw = (w - 24 - 8) / 2, bh = 94;
        for (int i = 0; i < 6; ++i)
        {
            const int bx = x + 12 + (i % 2) * (bw + 8), by = top + 40 + (i / 2) * (bh + 6);
            sections.add ({ { bx, by, bw, bh }, {}, Colour (0xff181b21) });
            place (make<Toggle> (s, blocks[i].on, blocks[i].name, col::fx), bx + 6, by + 6, i == 0 ? 60 : 80, 20);
            if (i == 0) place (make<Segmented> (s, "mod_type", Params::modTypes(), col::fx), bx + 68, by + 6, bw - 74, 20);
            for (int k = 0; k < 3; ++k)
                place (make<Knob> (s, blocks[i].k[k], blocks[i].c[k], col::fx, 30), bx + 4 + k * (bw - 8) / 3, by + 34, (bw - 8) / 3, 48);
        }
    }

    // ---------------- pads ----------------
    {
        const int x = 10, y = 392, w = 270, h = 198; sec (x, y, w, h, "PADS", col::pad);
        place (make<Toggle> (s, "pads_on", "ON", col::pad), x + w - 54, y + 8, 44, 22);
        const int pw = (w - 24 - 9) / 4, ph = 32;
        for (int i = 0; i < DrumEngine::numPads; ++i)
        {
            const int row = 3 - i / 4, c = i % 4; // pad 1 bottom-left like an MPC
            pads[i] = make<PadButton> (proc, i);
            pads[i]->setBounds (x + 12 + c * (pw + 3), y + 38 + row * (ph + 3), pw, ph);
        }
        cap (x + 12, y + 184, w - 24, "SYNTH KIT  ·  MIDI CH 10, NOTES C2–D#3");
    }
    // ---------------- keyboard ----------------
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (21);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xfff4f1ea));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff15171b));
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xffc9c3b6));
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, col::syn.withAlpha (0.55f));
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::syn.withAlpha (0.18f));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::black.withAlpha (0.3f));
    keyboard.setOctaveForMiddleC (4);
    content.addAndMakeVisible (keyboard);
    const int kx = 292, kw = baseW - 10 - kx;
    keyboard.setKeyWidth ((float) kw / 52.0f);
    keyboard.setBounds (kx, 414, kw, 176);
    zoneBar = { kx, 394, kw, 16 };

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (1100, 1100 * baseH / baseW, 2600, 2600 * baseH / baseW);
    setSize (baseW, baseH);
    startTimerHz (30);
}

AmariKeysEditor::~AmariKeysEditor()
{
    stopTimer();
    splitAtt.reset(); transAtt.reset(); blendAtt.reset();
    owned.clear();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AmariKeysEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    content.setTransform (AffineTransform::scale (scale));
}

void AmariKeysEditor::paint (Graphics& g) { g.fillAll (ak::col::bg); }

void AmariKeysEditor::paintContent (Graphics& g)
{
    using namespace ak;
    g.fillAll (col::bg);
    // top bar
    g.setColour (Colour (0xff101216)); g.fillRect (0, 0, baseW, 44);
    g.setColour (col::border); g.fillRect (0, 44, baseW, 1);
    g.setColour (col::text); g.setFont (FontOptions (19.0f, Font::bold));
    g.drawText ("A M A R I   K E Y S", 18, 0, 230, 44, Justification::centredLeft, false);
    g.setColour (col::dim); g.setFont (FontOptions (9.5f, Font::bold));
    g.drawText (String::fromUTF8 ("AMARI LABS  ·  TEST BUILD 0.1"), 232, 0, 180, 44, Justification::centredLeft, false);
    g.drawText (String::fromUTF8 ("ORGAN · ELECTRIC KEYS · SYNTH · DRUM PADS  —  ALL SAMPLE-FREE"), 1120, 0, 470, 44, Justification::centredRight, false);

    for (auto& s : sections)
    {
        auto r = s.r.toFloat();
        if (s.title.isEmpty())
        {
            g.setColour (s.accent); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (col::border); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            continue;
        }
        g.setColour (col::panel); g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::border); g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (s.accent); g.fillRoundedRectangle (r.withHeight (3.0f).reduced (4.0f, 0.0f), 1.5f);
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 14.0f, 9.0f, 9.0f, 2.0f);
        g.setColour (col::text); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (s.title, (int) r.getX() + 28, (int) r.getY() + 8, 90, 22, Justification::centredLeft, false);
    }
    g.setFont (FontOptions (9.0f, Font::bold));
    g.setColour (col::dim);
    for (auto& c : captions) g.drawText (c.second, c.first, Justification::centredLeft, false);

    // zone bar from split point
    const bool split = proc.apvts.getRawParameterValue ("split_on")->load() > 0.5f;
    const int splitN = (int) proc.apvts.getRawParameterValue ("split_note")->load();
    auto zb = zoneBar.toFloat();
    float sx = zb.getX();
    {
        // x position of the split note's left edge on the keyboard
        auto kr = keyboard.getRectangleForKey (splitN);
        sx = (float) keyboard.getX() + kr.getX();
    }
    g.setFont (FontOptions (9.0f, Font::bold));
    if (split)
    {
        auto lower = zb.withRight (sx - 1.0f), upper = zb.withLeft (sx + 1.0f);
        g.setColour (col::org.withAlpha (0.16f)); g.fillRoundedRectangle (lower, 3.0f);
        g.setColour (col::org); g.drawRoundedRectangle (lower.reduced (0.5f), 3.0f, 1.0f);
        g.drawText (String::fromUTF8 ("LOWER  ·  ORGAN  ·  A0–") + MidiMessage::getMidiNoteName (splitN - 1, true, true, 4), lower.reduced (8.0f, 0.0f), Justification::centredLeft, false);
        g.setColour (col::syn.withAlpha (0.12f)); g.fillRoundedRectangle (upper, 3.0f);
        g.setColour (col::syn); g.drawRoundedRectangle (upper.reduced (0.5f), 3.0f, 1.0f);
        g.drawText (String::fromUTF8 ("UPPER  ·  KEYS + SYNTH  ·  ") + MidiMessage::getMidiNoteName (splitN, true, true, 4) + String::fromUTF8 ("–C8"), upper.reduced (8.0f, 0.0f), Justification::centredLeft, false);
    }
    else
    {
        g.setColour (col::mst.withAlpha (0.08f)); g.fillRoundedRectangle (zb, 3.0f);
        g.setColour (col::muted); g.drawRoundedRectangle (zb.reduced (0.5f), 3.0f, 1.0f);
        g.drawText (String::fromUTF8 ("NO SPLIT  ·  ALL SECTIONS THAT ARE ON PLAY ACROSS THE WHOLE KEYBOARD"), zb.reduced (8.0f, 0.0f), Justification::centredLeft, false);
    }
}

void AmariKeysEditor::timerCallback()
{
    for (int i = 0; i < DrumEngine::numPads; ++i)
    {
        const int h = proc.padHits[i].load();
        if (h != lastPadHits[i]) { lastPadHits[i] = h; pads[i]->flash = 1.0f; pads[i]->repaint(); }
        else if (pads[i]->flash > 0.0f) { pads[i]->flash = jmax (0.0f, pads[i]->flash - 0.12f); pads[i]->repaint(); }
    }
    const bool splitOn = proc.apvts.getRawParameterValue ("split_on")->load() > 0.5f;
    const int split = (int) proc.apvts.getRawParameterValue ("split_note")->load();
    if (split != lastSplit || splitOn != lastSplitOn)
    {
        lastSplit = split; lastSplitOn = splitOn;
        content.repaint (zoneBar.expanded (2));
    }
}
