#include "PluginEditor.h"

using namespace juce;
using Params::lid;
using Params::Kind;

namespace ak
{
static void setParamProp (Component& c, const String& id) { c.getProperties().set ("paramId", id); }

void drawLed (Graphics& g, Rectangle<float> r, bool on)
{
    if (on) { g.setColour (col::acc.withAlpha (0.35f)); g.fillEllipse (r.expanded (2.5f)); }
    g.setColour (on ? col::acc : col::ledOff); g.fillEllipse (r);
}

//==============================================================================
LAF::LAF()
{
    setColour (Slider::textBoxTextColourId, col::acc);
    setColour (Slider::textBoxBackgroundColourId, col::well);
    setColour (Slider::textBoxOutlineColourId, col::btnBorder);
    setColour (TextButton::buttonColourId, col::btn);
    setColour (TextButton::buttonOnColourId, col::btn);
    setColour (TextButton::textColourOffId, col::muted);
    setColour (TextButton::textColourOnId, col::text);
    setColour (Label::textColourId, col::text);
    setColour (BubbleComponent::backgroundColourId, col::well);
    setColour (BubbleComponent::outlineColourId, col::border);
    setColour (TooltipWindow::textColourId, col::acc);
    setColour (AlertWindow::backgroundColourId, col::panel);
    setColour (AlertWindow::textColourId, col::text);
    setColour (TextEditor::backgroundColourId, col::well);
    setColour (TextEditor::textColourId, col::acc);
    setColour (TextEditor::outlineColourId, col::btnBorder);
}

void LAF::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f);
    const float size = jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);
    const auto c = b.getCentre();
    const float r = size * 0.5f;
    const float angle = start + pos * (end - start);
    Path track; track.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, start, end, true);
    g.setColour (col::track); g.strokePath (track, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    Path val; val.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, start, angle, true);
    g.setColour (col::acc); g.strokePath (val, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    auto body = b.reduced (4.0f);
    g.setGradientFill (ColourGradient (Colour (0xff3a3a3d), body.getX() + body.getWidth() * 0.4f, body.getY() + body.getHeight() * 0.3f,
                                       Colour (0xff161617), body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (Colours::black.withAlpha (0.7f)); g.drawEllipse (body, 1.0f);
    Path p; const float len = body.getWidth() * 0.5f - 2.0f;
    p.addRoundedRectangle (-1.0f, -len, 2.0f, len * 0.55f, 1.0f);
    g.setColour (Colour (0xffe8e8ea));
    g.fillPath (p, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LAF::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s)
{
    const bool ladder = (bool) s.getProperties().getWithDefault ("ladder", false);
    const float cx = ladder ? x + w * 0.62f : x + w * 0.5f;
    auto track = Rectangle<float> (cx - 3.0f, (float) y, 6.0f, (float) h);
    g.setColour (col::well); g.fillRoundedRectangle (track, 3.0f);
    g.setColour (Colour (0xff262628)); g.drawRoundedRectangle (track, 3.0f, 1.0f);
    if (ladder)
    {
        const int n = 12;
        const float lit = (float) ((s.getValue() - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * n;
        const bool on = (bool) s.getProperties().getWithDefault ("active", true);
        for (int i = 0; i < n; ++i)
        {
            const float ly = (float) y + 4.0f + (h - 8.0f) * (1.0f - (i + 0.5f) / n);
            drawLed (g, { x + w * 0.22f - 2.0f, ly - 2.0f, 4.0f, 4.0f }, on && i < roundToInt (lit));
        }
    }
    auto cap = Rectangle<float> (cx - 11.0f, pos - 7.0f, 22.0f, 14.0f);
    g.setGradientFill (ColourGradient (Colour (0xff4a4a4e), cap.getX(), cap.getY(), Colour (0xff1e1e20), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (Colours::black); g.drawRoundedRectangle (cap, 2.0f, 1.0f);
    g.setColour (Colour (0xffe8e8ea)); g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 6.0f, 1.0f);
}

void LAF::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (highlighted ? col::btn.brighter (0.06f) : col::btn); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (col::btnBorder); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const bool on = b.getToggleState();
    const auto text = b.getButtonText();
    if (text.isEmpty()) { drawLed (g, r.withSizeKeepingCentre (6.0f, 6.0f), on); return; }
    drawLed (g, { r.getX() + 7.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f }, on);
    g.setColour (on ? col::text : col::muted);
    g.setFont (FontOptions (9.5f, Font::bold));
    g.drawText (text, r.withTrimmedLeft (17.0f).withTrimmedRight (2.0f), Justification::centredLeft, false);
}

void LAF::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? col::btn.brighter (0.15f) : highlighted ? col::btn.brighter (0.06f) : col::btn); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (col::btnBorder); g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void LAF::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool hasLed = (bool) b.getProperties().getWithDefault ("led", false);
    g.setFont (FontOptions (9.5f, Font::bold));
    g.setColour (on ? col::text : col::muted);
    if (hasLed)
    {
        auto tw = (float) GlyphArrangement::getStringWidth (g.getCurrentFont(), b.getButtonText());
        const float total = tw + 11.0f, x0 = r.getCentreX() - total * 0.5f;
        drawLed (g, { x0, r.getCentreY() - 2.5f, 5.0f, 5.0f }, on);
        g.drawText (b.getButtonText(), Rectangle<float> (x0 + 11.0f, r.getY(), tw + 4.0f, r.getHeight()), Justification::centredLeft, false);
    }
    else g.drawText (b.getButtonText(), r, Justification::centred, false);
}

Label* LAF::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (11.0f, Font::bold));
    l->setJustificationType (Justification::centred);
    return l;
}

void LAF::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    g.setColour (col::well); g.fillRoundedRectangle (body, 3.0f);
    g.setColour (col::border); g.drawRoundedRectangle (body, 3.0f, 1.0f);
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const String& id, const String& cap, int size)
    : state (s), caption (cap), knobSize (size)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    addAndMakeVisible (slider);
    bind (id);
}
void Knob::bind (const String& id)
{
    att.reset();
    paramId = id; setParamProp (*this, id);
    if (auto* p = state.getParameter (id)) slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}
void Knob::resized() { slider.setBounds (getLocalBounds().removeFromTop (knobSize).withSizeKeepingCentre (knobSize, knobSize)); }
void Knob::paint (Graphics& g)
{
    g.setColour (col::muted); g.setFont (FontOptions (8.5f, Font::bold));
    g.drawText (caption, getLocalBounds().withTop (knobSize + 3), Justification::centredTop, false);
}

//==============================================================================
VSlider::VSlider (AudioProcessorValueTreeState& s, const String& id, const String& cap) : state (s), caption (cap)
{
    slider.setSliderStyle (Slider::LinearVertical);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    addAndMakeVisible (slider);
    bind (id);
}
void VSlider::bind (const String& id)
{
    att.reset(); paramId = id; setParamProp (*this, id);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}
void VSlider::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (14).reduced (0, 6)); }
void VSlider::paint (Graphics& g)
{
    g.setColour (col::muted); g.setFont (FontOptions (8.5f, Font::bold));
    g.drawText (caption, getLocalBounds().removeFromBottom (12), Justification::centred, false);
}

//==============================================================================
LedToggle::LedToggle (AudioProcessorValueTreeState& s, const String& id, const String& label) : state (s)
{
    setButtonText (label);
    bind (id);
}
void LedToggle::bind (const String& id)
{
    att.reset(); paramId = id; setParamProp (*this, id);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, id, *this);
}

//==============================================================================
LedList::LedList (AudioProcessorValueTreeState& s, const String& id, StringArray opts, int cols, int valueOffset)
    : state (s), options (std::move (opts)), columns (jmax (1, cols)), offset (valueOffset)
{
    bind (id);
}
void LedList::bind (const String& id)
{
    att.reset(); paramId = id; setParamProp (*this, id);
    att = std::make_unique<ParameterAttachment> (*state.getParameter (id), [this] (float v) { selected = roundToInt (v) - offset; repaint(); });
    att->sendInitialUpdate();
}
void LedList::paint (Graphics& g)
{
    const int rows = (options.size() + columns - 1) / columns;
    const float cw = (float) getWidth() / columns, ch = (float) getHeight() / rows;
    g.setFont (FontOptions (9.0f, Font::bold));
    for (int i = 0; i < options.size(); ++i)
    {
        auto cell = Rectangle<float> ((i % columns) * cw, (i / columns) * ch, cw, ch);
        const bool on = i == selected;
        drawLed (g, { cell.getX() + 1.0f, cell.getCentreY() - 2.5f, 5.0f, 5.0f }, on);
        g.setColour (on ? col::text : col::muted);
        g.drawText (options[i], cell.withTrimmedLeft (11.0f), Justification::centredLeft, false);
    }
}
void LedList::mouseDown (const MouseEvent& e)
{
    const int rows = (options.size() + columns - 1) / columns;
    const float cw = (float) getWidth() / columns, ch = (float) getHeight() / rows;
    const int idx = jlimit (0, rows - 1, (int) (e.position.y / ch)) * columns + jlimit (0, columns - 1, (int) (e.position.x / cw));
    if (idx < options.size()) att->setValueAsCompleteGesture ((float) (idx + offset));
}

//==============================================================================
Drawbar::Drawbar (AudioProcessorValueTreeState& s, const String& id, const String& cap, bool lightCap)
    : state (s), caption (cap), light (lightCap) { bind (id); }
void Drawbar::bind (const String& id)
{
    att.reset(); paramId = id; setParamProp (*this, id);
    att = std::make_unique<ParameterAttachment> (*state.getParameter (id), [this] (float v) { value = roundToInt (v); repaint(); });
    att->sendInitialUpdate();
}
void Drawbar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto slot = r.withTrimmedBottom (14.0f);
    g.setColour (col::well); g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (Colour (0xff262628)); g.drawRoundedRectangle (slot.reduced (0.5f), 2.0f, 1.0f);
    const float capH = 18.0f, travel = slot.getHeight() - capH - 2.0f;
    const float capY = slot.getY() + 1.0f + travel * value / 8.0f;
    auto shaft = Rectangle<float> (slot.getCentreX() - 4.0f, slot.getY() + 1.0f, 8.0f, capY - slot.getY());
    g.setColour (Colour (0xff232325)); g.fillRect (shaft);
    g.setColour (Colour (0xff2f2f32));
    for (int i = 1; i <= value; ++i) g.fillRect (shaft.getX(), capY - i * travel / 8.0f, shaft.getWidth(), 1.0f);
    auto cap = Rectangle<float> (slot.getX() + 1.0f, capY, slot.getWidth() - 2.0f, capH);
    g.setColour (light ? Colour (0xffbdbdc0) : Colour (0xff2e2e31)); g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (light ? Colour (0xff111111) : col::text);
    g.setFont (FontOptions (9.5f, Font::bold));
    g.drawText (String (value), cap, Justification::centred, false);
    g.setColour (col::dim); g.setFont (FontOptions (8.0f, Font::bold));
    g.drawText (caption, r.removeFromBottom (12.0f), Justification::centred, false);
}
void Drawbar::setFromY (float y)
{
    const float travel = getHeight() - 14.0f - 20.0f;
    const int v = jlimit (0, 8, roundToInt ((y - 10.0f) / travel * 8.0f));
    if (v != value) att->setValueAsPartOfGesture ((float) v);
}
void Drawbar::mouseDown (const MouseEvent& e) { att->beginGesture(); setFromY (e.position.y); }
void Drawbar::mouseDrag (const MouseEvent& e) { setFromY (e.position.y); }
void Drawbar::mouseUp (const MouseEvent&) { att->endGesture(); }

//==============================================================================
void Lcd::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::well); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Colour (0xff262628)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    if (! text) return;
    auto t = text();
    auto in = r.reduced (big ? 12.0f : 9.0f, big ? 8.0f : 5.0f);
    g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), big ? 17.0f : 12.0f, Font::bold));
    g.setColour (col::acc);
    g.drawText (t.first, in.removeFromTop (in.getHeight() * 0.55f), Justification::centredLeft, true);
    g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), big ? 11.0f : 9.5f, Font::plain));
    g.setColour (col::accDim);
    g.drawText (t.second, in, Justification::centredLeft, true);
}

//==============================================================================
void PadButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (flash > 0.02f)
    {
        g.setGradientFill (ColourGradient (col::acc.withAlpha (0.55f * flash), r.getCentreX(), r.getCentreY() + 4.0f,
                                           Colour (0xff202022), r.getX(), r.getY(), true));
    }
    else g.setGradientFill (ColourGradient (Colour (0xff232325), 0, r.getY(), Colour (0xff1a1a1c), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (flash > 0.02f ? col::acc.withAlpha (0.4f + 0.6f * flash) : Colour (0xff2c2c2f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (Colours::black.withAlpha (0.5f)); g.fillRect (r.getX() + 3.0f, r.getBottom() - 2.0f, r.getWidth() - 6.0f, 1.5f);
    const String label = String (index / 4 + 1) + String::charToString ((juce_wchar) ('A' + index % 4));
    g.setColour (flash > 0.02f ? col::text : col::dim); g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText (label, r.reduced (5.0f, 4.0f), Justification::topLeft, false);
}
void PadButton::mouseDown (const MouseEvent& e)
{
    const float vel = jlimit (0.35f, 1.0f, 1.0f - e.position.getDistanceFrom (getLocalBounds().getCentre().toFloat()) / (float) getWidth());
    proc.padPressed (index, vel);
}

//==============================================================================
ChannelStrip::ChannelStrip (AmariKeysProcessor& p, int l) : onButton (p.apvts, lid (l, "on"), {}), proc (p), layer (l)
{
    addAndMakeVisible (onButton);
    fader.setSliderStyle (Slider::LinearVertical);
    fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    fader.setPopupDisplayEnabled (true, true, nullptr);
    fader.getProperties().set ("ladder", true);
    fader.setDoubleClickReturnValue (true, 0.7);
    setParamProp (fader, lid (l, "level"));
    addAndMakeVisible (fader);
    faderAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, lid (l, "level"), fader);
}
void ChannelStrip::resized()
{
    onButton.setBounds (getLocalBounds().removeFromTop (18).withSizeKeepingCentre (30, 18));
    fader.setBounds (2, 24, getWidth() - 4, getHeight() - 24 - 34);
    zoneArea = { 0, getHeight() - 14, getWidth(), 14 };
}
void ChannelStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (selected)
    {
        g.setColour (Colour (0xff202022)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colour (0xff3a3a3d)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
    const bool on = proc.apvts.getRawParameterValue (lid (layer, "on"))->load() > 0.5f;
    fader.getProperties().set ("active", on);
    g.setFont (FontOptions (8.5f, Font::bold));
    g.setColour (on ? col::text : col::muted);
    g.drawText (Params::layerShort (layer), Rectangle<float> (0, r.getBottom() - 30.0f, r.getWidth(), 14.0f), Justification::centred, false);
    const int zone = (int) proc.apvts.getRawParameterValue (lid (layer, "zone"))->load();
    g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 9.0f, Font::bold));
    g.setColour (on ? col::acc : col::dim);
    g.drawText (Params::zones()[zone], zoneArea, Justification::centred, false);
}
void ChannelStrip::mouseDown (const MouseEvent& e)
{
    if (zoneArea.contains (e.getPosition()))
    {
        auto* prm = proc.apvts.getParameter (lid (layer, "zone"));
        const int z = ((int) proc.apvts.getRawParameterValue (lid (layer, "zone"))->load() + 1) % 3;
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) z));
        repaint();
    }
    if (onSelect) onSelect (layer);
}
// ---------------------------------------------------------------------------
void DataDial::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float d = jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (d, d);
    g.setColour (col::well); g.fillEllipse (c);
    g.setColour (col::border); g.drawEllipse (c, 1.2f);
    auto knob = c.reduced (d * 0.1f);
    ColourGradient grad (Colour (0xff3a3a3e), knob.getCentreX(), knob.getY(), Colour (0xff17171a), knob.getCentreX(), knob.getBottom(), false);
    g.setGradientFill (grad); g.fillEllipse (knob);
    // knurled edge that turns with the dial
    g.setColour (Colours::black.withAlpha (0.5f));
    for (int i = 0; i < 24; ++i)
    {
        const float a = angle + MathConstants<float>::twoPi * (float) i / 24.0f;
        const auto p1 = knob.getCentre().getPointOnCircumference (knob.getWidth() * 0.5f, a);
        const auto p2 = knob.getCentre().getPointOnCircumference (knob.getWidth() * 0.42f, a);
        g.drawLine ({ p1, p2 }, 1.2f);
    }
    // finger dimple
    const auto dp = knob.getCentre().getPointOnCircumference (knob.getWidth() * 0.27f, angle);
    g.setColour (col::acc); g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (dp));
}

void Wheel::paint (Graphics& g)
{
    auto r = wheelArea();
    g.setColour (col::well); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::border); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    auto w = r.reduced (5.0f, 6.0f);
    // the wheel: shaded drum with ribs that roll with the value
    ColourGradient grad (Colour (0xff141416), w.getX(), w.getY(), Colour (0xff141416), w.getX(), w.getBottom(), false);
    grad.addColour (0.5, Colour (0xff3a3a3e));
    g.setGradientFill (grad); g.fillRoundedRectangle (w, 3.0f);
    const float travel = pitch ? value * 0.5f + 0.5f : value;  // 0..1 (bottom..top)
    g.saveState(); g.reduceClipRegion (w.toNearestInt());
    const float spacing = 9.0f, offset = std::fmod (travel * w.getHeight() * 1.2f, spacing);
    for (float y = w.getY() - spacing + offset; y < w.getBottom() + spacing; y += spacing)
    {
        const float d = std::abs (y - w.getCentreY()) / (w.getHeight() * 0.5f);
        g.setColour (Colours::black.withAlpha (0.35f + 0.4f * d)); g.fillRect (w.getX(), y, w.getWidth(), 1.6f);
    }
    g.restoreState();
    // position marker (orange) and centre notch for the pitch wheel
    const float my = w.getBottom() - travel * w.getHeight();
    if (pitch) { g.setColour (col::dim); g.fillRect (w.getX() - 4.0f, w.getCentreY() - 0.5f, 3.0f, 1.0f); g.fillRect (w.getRight() + 1.0f, w.getCentreY() - 0.5f, 3.0f, 1.0f); }
    g.setColour (col::acc); g.fillRoundedRectangle (w.getX() + 1.0f, jlimit (w.getY(), w.getBottom() - 4.0f, my - 2.0f), w.getWidth() - 2.0f, 4.0f, 1.5f);
    g.setColour (col::muted); g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText (caption, getLocalBounds().removeFromBottom (14), Justification::centred, false);
}

void Wheel::mouseDown (const MouseEvent&) { stopTimer(); dragging = true; startValue = value; }
void Wheel::mouseDrag (const MouseEvent& e)
{
    const float h = wheelArea().getHeight();
    const float dv = -(float) e.getDistanceFromDragStartY() / h * (pitch ? 2.0f : 1.0f);
    set (jlimit (pitch ? -1.0f : 0.0f, 1.0f, startValue + dv));
}
void Wheel::mouseUp (const MouseEvent&) { dragging = false; if (pitch) startTimerHz (60); }
void Wheel::timerCallback() // spring back to the centre
{
    const float v = value * 0.6f;
    if (std::abs (v) < 0.01f) { stopTimer(); set (0.0f); } else set (v);
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
    auto sec = [this] (int x, int y, int w, int h, const String& t, int kind = 0) { sections.add ({ { x, y, w, h }, t, kind != 0, kind }); };
    auto cap = [this] (int x, int y, int w, const char* t) { captions.add ({ { x, y, w, 12 }, String::fromUTF8 (t) }); };
    auto place = [] (Component* c, int x, int y, int w, int h) { c->setBounds (x, y, w, h); return c; };
    auto styleBtn = [this] (TextButton& b, bool led) { b.getProperties().set ("led", led); content.addAndMakeVisible (b); };

    // ---------------- top bar ----------------
    cap (1354, 14, 40, "SPLIT");
    place (make<LedToggle> (s, "split_on", "ON"), 1394, 9, 44, 22);
    splitNote.setSliderStyle (Slider::IncDecButtons);
    splitNote.setTextBoxStyle (Slider::TextBoxLeft, false, 40, 22);
    content.addAndMakeVisible (splitNote); splitNote.setBounds (1444, 9, 88, 22);
    splitAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "split_note", splitNote);
    cap (1554, 14, 60, "VELOCITY");
    place (make<LedList> (s, "vel_curve", Params::velCurves(), 3), 1614, 12, 170, 16);

    const int top = 48, H = 344;
    // ---------------- MASTER ----------------
    {
        const int x = 8, w = 104; sec (x, top, w, H, "MASTER");
        place (make<Knob> (s, "master", "LEVEL", 46), x + 12, top + 44, 80, 66);
        cap (x + 12, top + 124, 80, "TRANSPOSE");
        transpose.setSliderStyle (Slider::IncDecButtons);
        transpose.setTextBoxStyle (Slider::TextBoxLeft, false, 30, 22);
        content.addAndMakeVisible (transpose); transpose.setBounds (x + 12, top + 138, 80, 22);
        transAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "transpose", transpose);
        rules.add ({ x + 12, top + 176, 80, 1 });
        styleBtn (learnButton, true); learnButton.setBounds (x + 12, top + 190, 80, 22);
        learnButton.setClickingTogglesState (true);
        learnButton.onClick = [this] { proc.setLearnMode (learnButton.getToggleState()); };
        styleBtn (panicButton, false); panicButton.setBounds (x + 12, top + 218, 80, 22);
        panicButton.onClick = [this] { if (! proc.isLearnMode()) proc.panic(); };
        panicButton.getProperties().set ("learnAction", AmariKeysProcessor::actPanic);
        styleBtn (mapButton, false); mapButton.setBounds (x + 12, top + 246, 80, 22);
        mapButton.onClick = [this] { showMidiMap(); };
        place (make<Knob> (s, "pedal_tail", "PAD TAIL", 30), x + 12, top + 280, 80, 56);
    }
    // ---------------- ORGAN ----------------
    {
        const int x = 118, w = 450; sec (x, top, w, H, "ORGAN");
        const char* caps[9] = { "16'", "5 1/3", "8'", "4'", "2 2/3", "2'", "1 3/5", "1 1/3", "1'" };
        const bool light[9] = { false, false, true, true, false, true, false, false, true };
        for (int i = 0; i < 9; ++i) place (make<Drawbar> (s, "db" + String (i + 1), caps[i], light[i]), x + 16 + i * 30, top + 42, 22, 200);
        const int rx = x + 320;
        cap (rx, top + 42, 80, "MODEL");
        place (make<LedList> (s, "org_model", Params::organModels(), 2), rx, top + 56, 82, 34);
        cap (rx, top + 100, 80, "PERCUSSION");
        place (make<LedToggle> (s, "perc_on", "ON"), rx, top + 114, 40, 20);
        place (make<LedToggle> (s, "perc_3rd", "3RD"), rx + 42, top + 114, 40, 20);
        place (make<LedToggle> (s, "perc_fast", "FST"), rx, top + 138, 40, 20);
        place (make<LedToggle> (s, "perc_soft", "SFT"), rx + 42, top + 138, 40, 20);
        cap (rx, top + 170, 80, "VIB / CHORUS");
        place (make<LedList> (s, "vib_mode", Params::vibModes(), 3), rx, top + 184, 84, 54);
        place (make<Knob> (s, "org_click", "CLICK", 26), rx - 4, top + 250, 42, 44);
        place (make<Knob> (s, "org_drive", "DRIVE", 26), rx + 40, top + 250, 42, 44);
        cap (x + 16, top + 262, 60, "ROTARY");
        place (make<LedToggle> (s, "rot_on", "ON"), x + 68, top + 257, 42, 20);
        place (make<LedList> (s, "rot_speed", Params::rotarySpeeds(), 3), x + 120, top + 259, 150, 16);
        cap (x + 16, top + 282, 260, "MOD WHEEL UP = ROTARY FAST");
        cap (x + 16, top + 306, 60, "OCTAVE");
        place (make<LedList> (s, lid (2, "oct"), Params::octaves(), 5, -2), x + 68, top + 304, 202, 16);
    }
    // ---------------- KEYS (follows PIANO / E.PIANO / KEYS) ----------------
    {
        const int x = 574, w = 236; sec (x, top, w, H, "KEYS", 1);
        keysLcd = make<Lcd>(); keysLcd->setBounds (x + 12, top + 40, w - 24, 40);
        keysLcd->text = [this]
        {
            const int l = keysLayer;
            const int model = (int) proc.apvts.getRawParameterValue (lid (l, "model"))->load();
            String l1 = String (Params::layerLong (l)) + "  " + Params::keysModels()[model];
            String l2 = model == 4 ? (proc.hasSamples (l) ? proc.getSampleName (l) + "  " + proc.getSampleStatus (l).fromFirstOccurrenceOf ("|", false, false).trim()
                                                          : proc.getSampleStatus (l) + " (plays TINE EP)")
                                   : String ("BUILT-IN ENGINE");
            return std::make_pair (l1, l2);
        };
        cap (x + 12, top + 90, 80, "TYPE");
        auto* model = make<LedList> (s, lid (0, "model"), Params::keysModels(), 2); model->setBounds (x + 12, top + 104, 140, 52);
        cap (x + 164, top + 90, 60, "TIMBRE");
        auto* timbre = make<LedList> (s, lid (0, "timbre"), Params::timbres(), 1); timbre->setBounds (x + 164, top + 104, 64, 52);
        auto* dyn = make<Knob> (s, lid (0, "dyn"), "DYNAMICS", 32); dyn->setBounds (x + 12, top + 168, 64, 54);
        auto* dec = make<Knob> (s, lid (0, "decay"), "DECAY", 32); dec->setBounds (x + 86, top + 168, 64, 54);
        auto* trem = make<Knob> (s, lid (0, "trem"), "TREMOLO", 32); trem->setBounds (x + 160, top + 168, 64, 54);
        cap (x + 12, top + 230, 100, "MY SAMPLES");
        styleBtn (keysSounds, false); keysSounds.setBounds (x + 12, top + 244, 104, 22);
        styleBtn (keysSaveSound, false); keysSaveSound.setBounds (x + 120, top + 244, 104, 22);
        styleBtn (keysLoadFolder, false); keysLoadFolder.setBounds (x + 12, top + 270, 104, 22);
        styleBtn (keysLoadSfz, false); keysLoadSfz.setBounds (x + 120, top + 270, 104, 22);
        keysSounds.onClick = [this] { showSoundsMenu (keysLayer, keysSounds); };
        keysSaveSound.onClick = [this] { saveSound (keysLayer); };
        keysLoadFolder.onClick = [this] { chooseSamples (false, keysLayer); };
        keysLoadSfz.onClick = [this] { chooseSamples (true, keysLayer); };
        cap (x + 12, top + 308, 60, "OCTAVE");
        auto* oct = make<LedList> (s, lid (0, "oct"), Params::octaves(), 5, -2); oct->setBounds (x + 64, top + 306, 160, 16);
        keysControls = { { model, "model" }, { timbre, "timbre" }, { dyn, "dyn" }, { dec, "decay" }, { trem, "trem" }, { oct, "oct" } };
    }
    // ---------------- PROGRAM + LAYER MIXER ----------------
    {
        const int x = 816, w = 444; sec (x, top, w, H, "PATCH");
        styleBtn (storeButton, false); storeButton.setBounds (x + w - 78, top + 6, 66, 22);
        storeButton.onClick = [this]
        {
            const int pi = proc.getCurrentPatch();
            if (pi >= 0) { proc.storePatch (pi); content.repaint(); }
            else askName ("Save patch", "Name for the new patch", "NEW PATCH", [this] (String n) { proc.savePatchAs (n); content.repaint(); });
        };
        progLcd = make<Lcd>(); progLcd->big = true; progLcd->setBounds (x + 12, top + 40, w - 24 - 124, 56);
        progLcd->getProperties().set ("learnAction", AmariKeysProcessor::actBrowsePatches);
        dataDial = make<DataDial>(); dataDial->setBounds (x + w - 12 - 116, top + 34, 66, 66);
        dataDial->getProperties().set ("learnAction", AmariKeysProcessor::actDial);
        dataDial->onStep = [this] (int d) { if (! proc.isLearnMode()) { proc.dialStep (d); progLcd->repaint(); } };
        dataDial->onEnter = [this] { if (! proc.isLearnMode()) { proc.dialEnter(); content.repaint(); } };
        styleBtn (enterButton, false); enterButton.setBounds (x + w - 12 - 46, top + 56, 46, 22);
        enterButton.getProperties().set ("learnAction", AmariKeysProcessor::actEnter);
        enterButton.onClick = [this] { if (! proc.isLearnMode()) { proc.dialEnter(); content.repaint(); } };
        cap (x + w - 12 - 50, top + 40, 54, "DATA");
        progLcd->text = [this]
        {
            if (proc.isBrowsePending() && ! proc.isLearnMode()) return proc.getBrowseText();
            const int pi = proc.getCurrentPatch(), count = proc.getPatchCount();
            String l1 = pi >= 0 ? String (pi + 1).paddedLeft ('0', 3) + "  " + proc.getPatchName (pi).toUpperCase() : String ("---  NO PATCH");
            String l2;
            if (proc.isLearnMode())
            {
                const auto t = proc.getLearnTargetName();
                l2 = t.isEmpty() ? "LEARN: click a control" : "LEARN: move a hardware control for " + t;
            }
            else if (pi < 0) l2 = count == 0 ? "SAVE AS to keep this sound as your first patch" : String (count) + " PATCHES  |  BROWSE or < > to pick one";
            else
            {
                StringArray on;
                for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
                    if (proc.apvts.getRawParameterValue (lid (l, "on"))->load() > 0.5f) on.add (Params::layerShort (l));
                l2 = (on.isEmpty() ? String ("NO LAYERS ON") : on.joinIntoString (" + ")) + "   |   " + String (pi + 1) + " OF " + String (count);
            }
            if (! proc.isLearnMode())
                l2 << "   |   DIAL: " << (proc.getBrowseTarget() < 0 ? String ("PATCHES") : String (Params::layerShort (proc.getBrowseTarget())) + " SOUNDS");
            return std::make_pair (l1, l2);
        };
        progLcd->onDoubleClick = [this] { showPatchMenu(); };
        progLcd->onClick = [this] { if (! proc.isLearnMode()) { proc.setBrowseTarget (-1); for (auto* st : strips) { st->selected = false; st->repaint(); } progLcd->repaint(); } };
        auto pb = [this, &styleBtn] (TextButton& b, int bx, int bw) { styleBtn (b, false); b.setBounds (bx, 0, bw, 22); };
        const int by = top + 104;
        pb (prevPatch, x + 12, 26); pb (nextPatch, x + 42, 26); pb (browsePatch, x + 74, 70); pb (saveAsPatch, x + 150, 66);
        pb (renamePatchBtn, x + 222, 62); pb (deletePatchBtn, x + 290, 58);
        for (auto* b : { &prevPatch, &nextPatch, &browsePatch, &saveAsPatch, &renamePatchBtn, &deletePatchBtn }) b->setTopLeftPosition (b->getX(), by);
        prevPatch.onClick = [this] { if (! proc.isLearnMode()) stepPatch (-1); };
        nextPatch.onClick = [this] { if (! proc.isLearnMode()) stepPatch (1); };
        prevPatch.getProperties().set ("learnAction", AmariKeysProcessor::actPatchPrev);
        nextPatch.getProperties().set ("learnAction", AmariKeysProcessor::actPatchNext);
        browsePatch.onClick = [this] { showPatchMenu(); };
        saveAsPatch.onClick = [this]
        {
            StringArray on;
            for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
                if (proc.apvts.getRawParameterValue (lid (l, "on"))->load() > 0.5f)
                    on.add (proc.hasSamples (l) && proc.getSampleName (l).isNotEmpty() ? proc.getSampleName (l) : String (Params::layerShort (l)));
            askName ("Save as new patch", "Name for the new patch", on.joinIntoString (" + "), [this] (String n) { proc.savePatchAs (n); content.repaint(); });
        };
        renamePatchBtn.onClick = [this]
        {
            const int pi = proc.getCurrentPatch(); if (pi < 0) return;
            askName ("Rename patch", "New name for patch " + String (pi + 1), proc.getPatchName (pi), [this, pi] (String n) { proc.renamePatch (pi, n); content.repaint(); });
        };
        deletePatchBtn.onClick = [this]
        {
            const int pi = proc.getCurrentPatch(); if (pi < 0) return;
            auto opts = MessageBoxOptions::makeOptionsOkCancel (MessageBoxIconType::QuestionIcon, "Delete patch",
                "Delete patch " + String (pi + 1) + " \"" + proc.getPatchName (pi) + "\"? The sounds it uses are not deleted.", "Delete", "Keep", this);
            AlertWindow::showAsync (opts, [this, pi] (int r) { if (r == 1) { proc.deletePatch (pi); content.repaint(); } });
        };
        rules.add ({ x + 12, top + 136, w - 24, 1 });
        cap (x + 12, top + 142, 120, "LAYER MIXER");
        for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
        {
            strips[l] = make<ChannelStrip> (proc, l);
            strips[l]->setBounds (x + 12 + l * 42, top + 158, 40, 180);
            strips[l]->onSelect = [this] (int layer) { if (! proc.isLearnMode()) { selectLayer (layer); proc.setBrowseTarget (layer); } };
            strips[l]->getProperties().set ("learnAction", AmariKeysProcessor::actSelectLayer + l);
        }
    }
    // ---------------- SYNTH (follows STRINGS / BRASS / PADS / SYNTH) ----------------
    {
        const int x = 1266, w = 384; sec (x, top, w, H, "SYNTH", 2);
        auto* voice = make<LedList> (s, lid (4, "voice"), Params::voiceModes(), 3); voice->setBounds (x + 210, top + 11, 162, 16);
        synLcd = make<Lcd>(); synLcd->setBounds (x + 12, top + 38, w - 24, 36);
        synLcd->text = [this]
        {
            const int l = synthLayer;
            const bool smp = (int) proc.apvts.getRawParameterValue (lid (l, "src"))->load() == 1;
            String l1 = String (Params::layerLong (l)) + "  " + (smp ? "SAMPLES" : "SYNTH");
            String l2 = smp ? (proc.hasSamples (l) ? proc.getSampleName (l) + "  " + proc.getSampleStatus (l).fromFirstOccurrenceOf ("|", false, false).trim()
                                                   : proc.getSampleStatus (l) + " (plays synth)")
                            : String ("Built-in synth engine");
            return std::make_pair (l1, l2);
        };
        cap (x + 12, top + 84, 80, "SOURCE");
        auto* src = make<LedList> (s, lid (4, "src"), Params::sources(), 2); src->setBounds (x + 12, top + 98, 112, 16);
        cap (x + 12, top + 124, 60, "OSC 1");
        auto* w1 = make<LedList> (s, lid (4, "wave1"), Params::waves(), 2); w1->setBounds (x + 12, top + 138, 112, 34);
        cap (x + 12, top + 180, 60, "OSC 2");
        auto* w2 = make<LedList> (s, lid (4, "wave2"), Params::waves(), 2); w2->setBounds (x + 12, top + 194, 112, 34);
        const int fx = x + 136;
        cap (fx, top + 84, 60, "FILTER");
        auto* ft = make<LedList> (s, lid (4, "ftype"), Params::filterTypes(), 2); ft->setBounds (fx, top + 98, 112, 34);
        auto* cut = make<Knob> (s, lid (4, "cutoff"), "CUTOFF", 40); cut->setBounds (fx - 2, top + 140, 58, 56);
        auto* res = make<Knob> (s, lid (4, "reso"), "RESO", 40); res->setBounds (fx + 58, top + 140, 58, 56);
        auto* fe = make<Knob> (s, lid (4, "fenv"), "ENV", 24); fe->setBounds (fx - 4, top + 204, 40, 42);
        auto* fk = make<Knob> (s, lid (4, "fkey"), "KEY", 24); fk->setBounds (fx + 36, top + 204, 40, 42);
        auto* lfo = make<Knob> (s, lid (4, "lfo"), "LFO", 24); lfo->setBounds (fx + 76, top + 204, 40, 42);
        const int ex = x + 266;
        cap (ex, top + 84, 100, "FILTER ENV");
        const char* fids[4] = { "fa", "fd", "fs", "fr" }; const char* aids[4] = { "aa", "ad", "as", "ar" }; const char* adsr[4] = { "A", "D", "S", "R" };
        std::vector<std::pair<Bindable*, String>> envs;
        for (int i = 0; i < 4; ++i) { auto* v = make<VSlider> (s, lid (4, fids[i]), adsr[i]); v->setBounds (ex + i * 27, top + 96, 26, 80); envs.push_back ({ v, fids[i] }); }
        cap (ex, top + 182, 100, "AMP ENV");
        for (int i = 0; i < 4; ++i) { auto* v = make<VSlider> (s, lid (4, aids[i]), adsr[i]); v->setBounds (ex + i * 27, top + 194, 26, 80); envs.push_back ({ v, aids[i] }); }
        auto* mix = make<Knob> (s, lid (4, "oscmix"), "MIX", 24); mix->setBounds (x + 8, top + 240, 40, 42);
        auto* det = make<Knob> (s, lid (4, "detune"), "DETUNE", 24); det->setBounds (x + 48, top + 240, 40, 42);
        auto* gl = make<Knob> (s, lid (4, "glide"), "GLIDE", 24); gl->setBounds (x + 88, top + 240, 40, 42);
        auto* rate = make<Knob> (s, lid (4, "lforate"), "LFO RATE", 24); rate->setBounds (x + 8, top + 290, 44, 42);
        cap (x + 60, top + 300, 76, "MY SAMPLES");
        styleBtn (synSounds, false); synSounds.setBounds (x + 136, top + 294, 58, 22);
        styleBtn (synSaveSound, false); synSaveSound.setBounds (x + 198, top + 294, 58, 22);
        styleBtn (synLoadFolder, false); synLoadFolder.setBounds (x + 136, top + 318, 58, 22);
        styleBtn (synLoadSfz, false); synLoadSfz.setBounds (x + 198, top + 318, 58, 22);
        synSounds.onClick = [this] { showSoundsMenu (synthLayer, synSounds); };
        synSaveSound.onClick = [this] { saveSound (synthLayer); };
        synLoadFolder.onClick = [this] { chooseSamples (false, synthLayer); };
        synLoadSfz.onClick = [this] { chooseSamples (true, synthLayer); };
        cap (ex, top + 290, 60, "OCTAVE");
        auto* oct = make<LedList> (s, lid (4, "oct"), Params::octaves(), 5, -2); oct->setBounds (ex - 6, top + 306, 118, 16);
        synthControls = { { voice, "voice" }, { src, "src" }, { w1, "wave1" }, { w2, "wave2" }, { ft, "ftype" }, { cut, "cutoff" }, { res, "reso" },
                          { fe, "fenv" }, { fk, "fkey" }, { lfo, "lfo" }, { mix, "oscmix" }, { det, "detune" }, { gl, "glide" }, { rate, "lforate" }, { oct, "oct" } };
        for (auto& e : envs) synthControls.push_back (e);
    }
    // ---------------- EFFECTS ----------------
    {
        const int x = 1656, w = 260; sec (x, top, w, H, "EFFECTS");
        place (make<LedList> (s, "fx_target", Params::fxTargets(), 4), x + 74, top + 11, 180, 16);
        struct B { const char* on; const char* name; const char* k[3]; const char* c[3]; const char* type; };
        const B blocks[6] = {
            { "mod_on", "MOD", { "mod_rate", "mod_depth", "mod_mix" }, { "RATE", "DEPTH", "MIX" }, nullptr },
            { "dly_on", "DELAY", { "dly_time", "dly_fb", "dly_mix" }, { "TIME", "FDBK", "MIX" }, "PING-PONG" },
            { "rev_on", "REVERB", { "rev_size", "rev_damp", "rev_mix" }, { "SIZE", "DAMP", "MIX" }, "HALL" },
            { "amp_on", "AMP", { "amp_drive", "amp_tone", "amp_level" }, { "DRIVE", "TONE", "LEVEL" }, "DRIVE" },
            { "comp_on", "COMP", { "comp_amt", "comp_atk", "comp_gain" }, { "AMT", "ATK", "GAIN" }, "GLUE" },
            { "eq_on", "EQ", { "eq_low", "eq_mid", "eq_high" }, { "LOW", "MID", "HIGH" }, "3-BAND" } };
        for (int i = 0; i < 6; ++i)
        {
            const int by = top + 40 + i * 50;
            place (make<LedToggle> (s, blocks[i].on, blocks[i].name), x + 12, by + 12, 66, 20);
            if (blocks[i].type == nullptr) place (make<LedList> (s, "mod_type", StringArray { "CHO", "PHA" }, 1), x + 84, by + 6, 44, 32);
            else captions.add ({ { x + 84, by + 16, 50, 12 }, blocks[i].type });
            for (int k = 0; k < 3; ++k) place (make<Knob> (s, blocks[i].k[k], blocks[i].c[k], 24), x + 132 + k * 40, by + 2, 40, 42);
            if (i < 5) rules.add ({ x + 12, by + 48, w - 24, 1 });
        }
    }
    // ---------------- DRUM PADS ----------------
    {
        const int x = 8, y = 400, w = 248, h = 232; sec (x, y, w, h, "DRUM PADS");
        place (make<LedList> (s, "pad_mode", Params::padModes(), 3), x + 110, y + 11, 128, 16);
        for (int i = 0; i < AmariKeysProcessor::numPads; ++i)
        {
            pads[i] = make<PadButton> (proc, i);
            pads[i]->setBounds (x + 12 + (i % 4) * 48, y + 38 + (i / 4) * 48, 42, 42);
        }
        place (make<VSlider> (s, "pads_level", "LVL"), x + 210, y + 34, 28, 192);
    }
    // ---------------- WHEELS ----------------
    {
        const int x = 262, y = 400, w = 112, h = 232; sec (x, y, w, h, "WHEELS");
        pitchWheel = make<Wheel> (true, "PITCH"); pitchWheel->setBounds (x + 10, y + 34, 44, 150);
        modWheel = make<Wheel> (false, "MOD"); modWheel->setBounds (x + 58, y + 34, 44, 150);
        pitchWheel->onChange = [this] (float v) { proc.setPitchWheel (v); };
        modWheel->onChange = [this] (float v) { proc.setModWheel (v); };
        cap (x + 12, y + 190, 88, "BEND RANGE (ST)");
        bendRange.setSliderStyle (Slider::IncDecButtons);
        bendRange.setTextBoxStyle (Slider::TextBoxLeft, false, 30, 22);
        content.addAndMakeVisible (bendRange); bendRange.setBounds (x + 12, y + 202, 88, 22);
        bendAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, "bend_range", bendRange);
    }
    // ---------------- keyboard ----------------
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (21);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffeeede9));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff111112));
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xffbfbdb7));
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, col::acc.withAlpha (0.5f));
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::acc.withAlpha (0.15f));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::black.withAlpha (0.35f));
    keyboard.setOctaveForMiddleC (4);
    content.addAndMakeVisible (keyboard);
    const int kx = 380, kw = baseW - 8 - kx;
    keyboard.setKeyWidth ((float) kw / 52.0f);
    keyboard.setBounds (kx, 420, kw, 212);
    zoneBar = { kx, 400, kw, 16 };

    content.addMouseListener (this, true);
    selectLayer (0);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (1000, 1000 * baseH / baseW, 2800, 2800 * baseH / baseW);
    int startW = baseW;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) startW = jmin (baseW, (int) (d->userArea.getWidth() * 0.92));
    setSize (startW, startW * baseH / baseW);
    startTimerHz (30);
}

AmariKeysEditor::~AmariKeysEditor()
{
    stopTimer();
    content.removeMouseListener (this);
    chooser.reset();
    splitAtt.reset(); transAtt.reset(); bendAtt.reset();
    owned.clear();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AmariKeysEditor::selectLayer (int layer)
{
    selectedLayer = layer;
    const auto kind = Params::kindOf (layer);
    if (kind == Kind::Keys) { keysLayer = layer; for (auto& c : keysControls) c.first->bind (lid (layer, c.second.toRawUTF8())); }
    if (kind == Kind::Synth) { synthLayer = layer; for (auto& c : synthControls) c.first->bind (lid (layer, c.second.toRawUTF8())); }
    for (int l = 0; l < AmariKeysProcessor::numLayers; ++l) { strips[l]->selected = (l == layer); strips[l]->repaint(); }
    content.repaint();
}

void AmariKeysEditor::chooseSamples (bool sfz, int layer)
{
    if (sfz)
    {
        chooser = std::make_unique<FileChooser> ("Choose an .sfz instrument for " + String (Params::layerLong (layer)), File(), "*.sfz");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this, layer] (const FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) proc.loadSamples (layer, f); });
    }
    else
    {
        chooser = std::make_unique<FileChooser> ("Choose a sample folder for " + String (Params::layerLong (layer)), File(), "*");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this, layer] (const FileChooser& fc) { auto f = fc.getResult(); if (f.isDirectory()) proc.loadSamples (layer, f); });
    }
}

void AmariKeysEditor::askName (const String& title, const String& message, const String& initial, std::function<void (String)> done)
{
    auto* aw = new AlertWindow (title, message, MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", initial);
    aw->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    aw->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    aw->setLookAndFeel (&laf);
    aw->enterModalState (true, ModalCallbackFunction::create ([aw, done] (int r)
    {
        const auto n = aw->getTextEditorContents ("name").trim();
        if (r == 1 && n.isNotEmpty() && done) done (n);
    }), true);
}

void AmariKeysEditor::stepPatch (int delta)
{
    proc.reloadPatches();
    const int count = proc.getPatchCount();
    if (count == 0) return;
    const int cur = proc.getCurrentPatch();
    const int next = cur < 0 ? (delta > 0 ? 0 : count - 1) : (cur + delta + count) % count;
    proc.recallPatch (next);
    content.repaint();
}

void AmariKeysEditor::showPatchMenu()
{
    proc.reloadPatches();
    PopupMenu m;
    const int count = proc.getPatchCount(), cur = proc.getCurrentPatch();
    if (count == 0) m.addItem (-1, "No patches yet - use SAVE AS", false);
    for (int i = 0; i < count; ++i)
        m.addItem (i + 1, String (i + 1).paddedLeft ('0', 3) + "   " + proc.getPatchName (i), true, i == cur);
    m.addSeparator();
    m.addItem (-2, "Library folder: " + AmariLibrary::root().getFullPathName(), false);
    m.addItem (-3, "Change library folder...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (browsePatch).withMaximumNumColumns (3).withStandardItemHeight (20),
                     [this] (int r)
    {
        if (r > 0) { proc.recallPatch (r - 1); content.repaint(); }
        else if (r == -3)
        {
            chooser = std::make_unique<FileChooser> ("Choose the Amari Keys library folder (patches and sounds are kept in its Library subfolder)", AmariLibrary::root());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
            {
                const auto d = fc.getResult();
                if (d.isDirectory()) { AmariLibrary::setRoot (d); proc.reloadPatches(); content.repaint(); }
            });
        }
    });
}

void AmariKeysEditor::showSoundsMenu (int layer, Component& anchor)
{
    const auto group = AmariLibrary::groupFolder (layer);
    auto sounds = AmariLibrary::soundsFor (group);
    const auto loaded = proc.getSamplePath (layer);
    PopupMenu m;
    m.addSectionHeader (String (Params::layerLong (layer)) + " SOUNDS");
    int id = 1;
    std::vector<AmariLibrary::Sound> table;
    for (auto& snd : sounds)
    {
        m.addItem (id++, (snd.saved ? "" : "   ") + snd.name, true, File (snd.path) == File (loaded));
        table.push_back (snd);
    }
    if (sounds.isEmpty()) m.addItem (-9, "No sounds yet. Load an SFZ, then SAVE SOUND", false);
    // other groups
    PopupMenu others;
    for (int g = 0; g < AmariKeysProcessor::numLayers; ++g)
    {
        if (g == 2 || g == layer) continue; // organ has no samples
        PopupMenu sub; bool any = false;
        for (auto& snd : AmariLibrary::soundsFor (AmariLibrary::groupFolder (g))) { sub.addItem (id++, snd.name); table.push_back (snd); any = true; }
        if (any) others.addSubMenu (AmariLibrary::groupFolder (g), sub);
    }
    m.addSeparator();
    if (others.getNumItems() > 0) m.addSubMenu ("From other groups", others);
    if (loaded.isNotEmpty()) m.addItem (-1, "Unload samples (use the built-in sound)");
    m.addItem (-2, "Remove a saved sound...", true);
    m.addItem (-4, "Library: " + AmariLibrary::root().getFullPathName(), false);
    m.addItem (-3, "Change library folder...");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (anchor).withStandardItemHeight (20), [this, layer, table] (int r)
    {
        if (r > 0 && r <= (int) table.size())
        {
            const File f (table[(size_t) r - 1].path);
            if (f.exists()) proc.loadSamples (layer, f);
            else AlertWindow::showAsync (MessageBoxOptions::makeOptionsOk (MessageBoxIconType::WarningIcon, "Sound not found", f.getFullPathName() + " no longer exists."), nullptr);
        }
        else if (r == -1) proc.clearSamples (layer);
        else if (r == -3)
        {
            chooser = std::make_unique<FileChooser> ("Choose the Amari Keys library folder", AmariLibrary::root());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [] (const FileChooser& fc)
            { if (fc.getResult().isDirectory()) AmariLibrary::setRoot (fc.getResult()); });
        }
        else if (r == -2)
        {
            PopupMenu rm; int i = 1;
            auto saved = AmariLibrary::savedSounds();
            for (auto& snd : saved) rm.addItem (i++, snd.group + " / " + snd.name);
            if (saved.isEmpty()) rm.addItem (-9, "No saved sounds", false);
            rm.showMenuAsync (PopupMenu::Options(), [saved] (int k)
            { if (k > 0 && k <= saved.size()) AmariLibrary::removeSound (saved[k - 1].group, saved[k - 1].path); });
        }
    });
}

void AmariKeysEditor::saveSound (int layer)
{
    const auto path = proc.getSamplePath (layer);
    if (path.isEmpty())
    {
        AlertWindow::showAsync (MessageBoxOptions::makeOptionsOk (MessageBoxIconType::InfoIcon, "Save sound",
            "Load an SFZ or sample folder on " + String (Params::layerLong (layer)) + " first, then save it to your library."), nullptr);
        return;
    }
    auto initial = proc.getSampleName (layer);
    if (initial.isEmpty()) initial = File (path).getFileNameWithoutExtension();
    askName ("Save sound to library", "Name for this " + String (Params::layerLong (layer)) + " sound (always listed under SOUNDS)", initial,
             [layer, path] (String n) { AmariLibrary::saveSound (n, layer, path); });
}

void AmariKeysEditor::showMidiMap()
{
    auto maps = proc.getMappings();
    PopupMenu m;
    m.addSectionHeader ("MIDI MAP - click one to remove it");
    for (auto& mp : maps) m.addItem (mp.cc + 1, "CC " + String (mp.cc).paddedLeft ('0', 3) + "   ->   " + mp.name);
    if (maps.isEmpty()) m.addItem (-9, "Nothing learned yet. Click MIDI LEARN, then a control, then move a knob", false);
    m.addSeparator();
    m.addItem (-1, "Clear all", ! maps.isEmpty());
    m.addItem (-8, "Also learnable: PATCH < >, DATA dial, ENTER, PANIC, the PATCH screen (dial = patches) and each mixer strip (select layer)", false);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (mapButton).withStandardItemHeight (20), [this] (int r)
    {
        if (r > 0) proc.removeMapping (r - 1);
        else if (r == -1) proc.clearMappings();
    });
}

void AmariKeysEditor::mouseDown (const MouseEvent& e)
{
    if (! proc.isLearnMode()) return;
    for (auto* c = e.originalComponent; c != nullptr && c != &content; c = c->getParentComponent())
    {
        const auto id = c->getProperties()["paramId"].toString();
        if (id.isNotEmpty()) { proc.setLearnTarget (id); progLcd->repaint(); return; }
        if (c->getProperties().contains ("learnAction")) { proc.setLearnAction ((int) c->getProperties()["learnAction"]); progLcd->repaint(); return; }
    }
}

void AmariKeysEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}

void AmariKeysEditor::paint (Graphics& g) { g.fillAll (ak::col::bg); }

void AmariKeysEditor::paintContent (Graphics& g)
{
    using namespace ak;
    g.fillAll (col::bg);
    g.setColour (col::top); g.fillRect (0, 0, baseW, 40);
    g.setColour (col::border); g.fillRect (0, 40, baseW, 1);
    g.setColour (col::text); g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText ("A M A R I   K E Y S", 16, 0, 220, 40, Justification::centredLeft, false);
    drawLed (g, { 222.0f, 17.0f, 6.0f, 6.0f }, true);
    g.setColour (col::dim); g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText ("AMARI LABS  |  BUILD 0.6", 238, 0, 200, 40, Justification::centredLeft, false);

    for (auto& s : sections)
    {
        auto r = s.r.toFloat();
        g.setColour (col::panel); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::border); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.fillRect (r.getX() + 12.0f, r.getY() + 31.0f, r.getWidth() - 24.0f, 1.0f);
        String title = s.title;
        if (s.kind == 1) title = String (Params::layerShort (keysLayer)) + "  EDIT";
        if (s.kind == 2) title = String (Params::layerShort (synthLayer)) + "  EDIT";
        g.setColour (col::text); g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (title, (int) r.getX() + 12, (int) r.getY() + 6, 160, 22, Justification::centredLeft, false);
    }
    g.setColour (col::border);
    for (auto& rr : rules) g.fillRect (rr);
    g.setFont (FontOptions (9.0f, Font::bold));
    g.setColour (col::muted);
    for (auto& c : captions) g.drawText (c.second, c.first, Justification::centredLeft, false);

    // zone bar
    const bool split = proc.apvts.getRawParameterValue ("split_on")->load() > 0.5f;
    const int splitN = (int) proc.apvts.getRawParameterValue ("split_note")->load();
    auto zb = zoneBar.toFloat();
    const float sx = (float) keyboard.getX() + keyboard.getRectangleForKey (splitN).getX();
    auto layersIn = [this] (int zoneWanted)
    {
        StringArray s;
        for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
        {
            if (proc.apvts.getRawParameterValue (lid (l, "on"))->load() < 0.5f) continue;
            const int z = (int) proc.apvts.getRawParameterValue (lid (l, "zone"))->load();
            if (z == zoneWanted || z == 2) s.add (Params::layerShort (l));
        }
        return s.isEmpty() ? String ("-") : s.joinIntoString (" + ");
    };
    g.setFont (FontOptions (8.5f, Font::bold));
    if (split)
    {
        auto lower = zb.withRight (sx - 2.0f), upper = zb.withLeft (sx + 2.0f);
        g.setColour (col::acc); g.fillRect (lower.withHeight (2.0f));
        g.setColour (Colour (0xff5a5a5e)); g.fillRect (upper.withHeight (2.0f));
        g.setColour (col::muted);
        g.drawText ("LOWER  " + layersIn (0) + "  A0-" + Params::noteName (splitN - 1), lower.withTrimmedTop (3.0f), Justification::centredLeft, true);
        g.drawText ("UPPER  " + layersIn (1) + "  " + Params::noteName (splitN) + "-C8", upper.withTrimmedTop (3.0f), Justification::centredLeft, true);
    }
    else
    {
        g.setColour (Colour (0xff5a5a5e)); g.fillRect (zb.withHeight (2.0f));
        g.setColour (col::muted);
        StringArray s;
        for (int l = 0; l < AmariKeysProcessor::numLayers; ++l)
            if (proc.apvts.getRawParameterValue (lid (l, "on"))->load() > 0.5f) s.add (Params::layerShort (l));
        g.drawText ("NO SPLIT  " + s.joinIntoString (" + "), zb.withTrimmedTop (3.0f), Justification::centredLeft, true);
    }
}

void AmariKeysEditor::timerCallback()
{
    for (int i = 0; i < AmariKeysProcessor::numPads; ++i)
    {
        const int h = proc.padHits[i].load();
        if (h != lastPadHits[i]) { lastPadHits[i] = h; pads[i]->flash = 1.0f; pads[i]->repaint(); }
        else if (pads[i]->flash > 0.0f) { pads[i]->flash = jmax (0.0f, pads[i]->flash - 0.1f); pads[i]->repaint(); }
    }
    {   // a hardware control selected a layer: show it like a click on its strip
        const int sel = proc.takeSelectRequest();
        if (sel >= 0) selectLayer (sel);
    }
    pitchWheel->setDisplayed (proc.getPitchWheel());
    modWheel->setDisplayed (proc.getModWheel());
    renamePatchBtn.setEnabled (proc.getCurrentPatch() >= 0);
    deletePatchBtn.setEnabled (proc.getCurrentPatch() >= 0);
    if (learnButton.getToggleState() != proc.isLearnMode()) learnButton.setToggleState (proc.isLearnMode(), dontSendNotification);
    // cheap periodic refresh of displays, strips and zone bar
    progLcd->repaint(); keysLcd->repaint(); synLcd->repaint();
    for (auto* s : strips) s->repaint();
    content.repaint (zoneBar.expanded (2));
}
