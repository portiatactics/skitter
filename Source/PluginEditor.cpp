#include "PluginEditor.h"

namespace skitterui
{
    namespace C
    {
        const juce::Colour bg      { 0xff0d0e11 };
        const juce::Colour panel   { 0xff16181d };
        const juce::Colour line    { 0xff262931 };
        const juce::Colour text    { 0xffe8e9ec };
        const juce::Colour dim     { 0xff7d828f };
    }

    static juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // ---------------------------------------------------------------- LookAndFeel
    LookAndFeel::LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, C::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, C::text);
        setColour (juce::TextEditor::highlightColourId, C::line);
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float start, float end, juce::Slider& s)
    {
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float radius = size * 0.5f, thick = juce::jmax (3.0f, size * 0.07f);
        const float angle = start + pos * (end - start);

        juce::Path track; track.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, start, end, true);
        g.setColour (C::line);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (pos > 0.001f)
        {
            juce::Path val; val.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, start, angle, true);
            g.setColour (accent.withAlpha (0.25f));
            g.strokePath (val, juce::PathStrokeType (thick * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float inner = radius - thick * 2.6f;
        g.setColour (C::panel.brighter (0.08f));
        g.fillEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (c));
        g.setColour (C::line.brighter (0.2f));
        g.drawEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (c), 1.0f);

        const auto tip = c.getPointOnCircumference (inner * 0.72f, angle);
        g.setColour (C::text);
        g.fillEllipse (juce::Rectangle<float> (thick * 1.1f, thick * 1.1f).withCentre (tip));
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (12.0f));
        l->setColour (juce::Label::textColourId, C::dim);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }

    // ---------------------------------------------------------------- Knob
    Knob::Knob (juce::AudioProcessorValueTreeState& vts, const juce::String& id, const juce::String& n, juce::Colour col)
        : name (n)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, col);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, C::dim);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (slider);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (vts, id, slider);
        slider.setDoubleClickReturnValue (true, vts.getParameter (id)->convertFrom0to1 (vts.getParameter (id)->getDefaultValue()));
    }

    void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (18)); }

    void Knob::paint (juce::Graphics& g)
    {
        g.setColour (C::text);
        g.setFont (font (11.5f, true));
        g.drawText (name.toUpperCase(), getLocalBounds().removeFromTop (16), juce::Justification::centred);
    }

    // ---------------------------------------------------------------- SliceSelector
    SliceSelector::SliceSelector (juce::AudioProcessorValueTreeState& vts)
        : attach (*vts.getParameter ("slice"), [this] (float v) { current = juce::roundToInt (v); repaint(); })
    {
        attach.sendInitialUpdate();
    }

    int SliceSelector::indexAt (juce::Point<int> p) const
    {
        const int n = SkitterProcessor::sliceNames().size();
        return juce::jlimit (0, n - 1, p.x * n / juce::jmax (1, getWidth()));
    }

    void SliceSelector::mouseDown (const juce::MouseEvent& e)
    {
        attach.setValueAsCompleteGesture ((float) indexAt (e.getPosition()));
    }

    void SliceSelector::paint (juce::Graphics& g)
    {
        const auto& names = SkitterProcessor::sliceNames();
        auto b = getLocalBounds().toFloat();
        g.setColour (C::panel);
        g.fillRoundedRectangle (b, 6.0f);
        const float w = b.getWidth() / (float) names.size();
        for (int i = 0; i < names.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (2.0f);
            if (i == current)
            {
                g.setColour (C::text);
                g.fillRoundedRectangle (cell, 4.0f);
            }
            g.setColour (i == current ? C::bg : C::dim);
            g.setFont (font (12.0f, i == current));
            g.drawText (names[i], cell, juce::Justification::centred);
        }
    }

    // ---------------------------------------------------------------- HoldButton
    HoldButton::HoldButton (juce::AudioProcessorValueTreeState& vts)
        : attach (*vts.getParameter ("force"), [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attach.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void HoldButton::mouseDown (const juce::MouseEvent&) { attach.beginGesture(); attach.setValueAsPartOfGesture (1.0f); }
    void HoldButton::mouseUp (const juce::MouseEvent&)   { attach.setValueAsPartOfGesture (0.0f); attach.endGesture(); }

    void HoldButton::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const auto col = fxColour (1);
        if (on)
        {
            g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (b.expanded (2.0f), 12.0f);
            g.setColour (col); g.fillRoundedRectangle (b, 10.0f);
        }
        else
        {
            g.setColour (C::panel); g.fillRoundedRectangle (b, 10.0f);
            g.setColour (col.withAlpha (0.7f)); g.drawRoundedRectangle (b, 10.0f, 1.5f);
        }
        auto top = b.withTrimmedBottom (b.getHeight() * 0.5f);
        auto sub = b.withTrimmedTop (b.getHeight() * 0.5f);
        g.setColour (on ? C::bg : C::text);
        g.setFont (font (18.0f, true));
        g.drawText ("HOLD", top.withTrimmedBottom (2.0f), juce::Justification::centredBottom);
        g.setFont (font (10.5f));
        g.setColour (on ? C::bg.withAlpha (0.8f) : C::dim);
        g.drawText ("press to stutter", sub.withTrimmedTop (4.0f), juce::Justification::centredTop);
    }

    // ---------------------------------------------------------------- EventLane
    EventLane::EventLane (skitter::GlitchEngine& e) : engine (e) { startTimerHz (30); }

    void EventLane::timerCallback()
    {
        const int head = engine.historyHead();
        if (head != lastHead) { lastHead = head; flash = 1.0f; }
        else flash *= 0.85f;
        repaint();
    }

    void EventLane::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (C::panel);
        g.fillRoundedRectangle (b, 8.0f);

        // legend
        auto legend = b.removeFromTop (22.0f).reduced (10.0f, 4.0f);
        const char* labels[] = { "STUTTER", "REVERSE", "TAPE", "GATE", "CRUSH" };
        g.setFont (font (10.5f, true));
        for (int k = 0; k < 5; ++k)
        {
            auto item = legend.removeFromLeft (78.0f);
            g.setColour (fxColour (k + 1));
            g.fillEllipse (item.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f));
            g.setColour (C::dim);
            g.drawText (labels[k], item.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
        }
        g.setColour (C::dim);
        g.drawText ("RECENT SLICES  >", legend, juce::Justification::centredRight);

        // cells: oldest left, newest right
        auto area = b.reduced (10.0f, 8.0f);
        constexpr int cells = 32;
        const float gap = 3.0f, w = (area.getWidth() - gap * (cells - 1)) / (float) cells;
        const int head = engine.historyHead();
        const int live = engine.currentFx();
        for (int i = 0; i < cells; ++i)
        {
            const int idx = (head - (cells - 1 - i) + skitter::GlitchEngine::historySize * 4) % skitter::GlitchEngine::historySize;
            const int fx = engine.historyAt (idx);
            auto cell = juce::Rectangle<float> (area.getX() + (float) i * (w + gap), area.getY(), w, area.getHeight());
            const bool newest = (i == cells - 1);
            if (fx == 0)
            {
                g.setColour (C::line);
                g.fillRoundedRectangle (cell.withSizeKeepingCentre (w, 4.0f), 2.0f);
            }
            else
            {
                const float alpha = 0.35f + 0.65f * ((float) i / (float) (cells - 1));
                g.setColour (fxColour (fx).withAlpha (alpha));
                g.fillRoundedRectangle (cell, 3.0f);
            }
            if (newest)
            {
                g.setColour (C::text.withAlpha (0.25f + 0.6f * flash));
                g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 1.2f);
                if (live != 0) { g.setColour (fxColour (live).withAlpha (0.35f * flash)); g.fillRoundedRectangle (cell.expanded (3.0f), 5.0f); }
            }
        }
    }
}

// -------------------------------------------------------------------- Editor
SkitterEditor::SkitterEditor (SkitterProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      slice (p.apvts), lane (p.engine),
      stutter   (p.apvts, "stutter",   "Stutter",   skitterui::fxColour (1)),
      reverse   (p.apvts, "reverse",   "Reverse",   skitterui::fxColour (2)),
      tape      (p.apvts, "tape",      "Tape Stop", skitterui::fxColour (3)),
      gate      (p.apvts, "gate",      "Gate",      skitterui::fxColour (4)),
      crush     (p.apvts, "crush",     "Crush",     skitterui::fxColour (5)),
      chance    (p.apvts, "chance",    "Chance",    juce::Colour (0xffe8e9ec)),
      intensity (p.apvts, "intensity", "Intensity", juce::Colour (0xffe8e9ec)),
      smooth    (p.apvts, "smooth",    "Smooth",    juce::Colour (0xff7d828f)),
      mix       (p.apvts, "mix",       "Mix",       juce::Colour (0xff7d828f)),
      hold (p.apvts)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> { &slice, &lane, &stutter, &reverse, &tape, &gate, &crush,
                                                             &chance, &intensity, &smooth, &mix, &hold })
        addAndMakeVisible (c);
    setSize (720, 470);
}

SkitterEditor::~SkitterEditor() { setLookAndFeel (nullptr); }

void SkitterEditor::paint (juce::Graphics& g)
{
    using namespace skitterui;
    g.fillAll (C::bg);

    // wordmark
    g.setColour (C::text);
    g.setFont (font (26.0f, true));
    g.drawText ("SKITTER", 20, 16, 160, 32, juce::Justification::centredLeft);
    g.setColour (C::dim);
    g.setFont (font (11.0f));
    g.drawText ("glitch / stutter", 20, 44, 160, 14, juce::Justification::centredLeft);

    g.setFont (font (10.5f, true));
    g.drawText ("SLICE", 250, 10, 120, 14, juce::Justification::centredLeft);
    g.drawText ("EFFECT MIX  (how often each one is picked)", 20, 186, 400, 14, juce::Justification::centredLeft);
    g.drawText ("BEHAVIOUR", 20, 330, 200, 14, juce::Justification::centredLeft);

    g.setColour (C::line);
    g.drawHorizontalLine (178, 20.0f, (float) getWidth() - 20.0f);
    g.drawHorizontalLine (322, 20.0f, (float) getWidth() - 20.0f);
}

void SkitterEditor::resized()
{
    slice.setBounds (250, 26, 450, 32);
    lane.setBounds (20, 74, getWidth() - 40, 92);

    auto fxRow = juce::Rectangle<int> (20, 204, getWidth() - 40, 112);
    const int fw = fxRow.getWidth() / 5;
    for (auto* k : { &stutter, &reverse, &tape, &gate, &crush })
        k->setBounds (fxRow.removeFromLeft (fw).reduced (6, 0));

    auto bottom = juce::Rectangle<int> (20, 348, getWidth() - 40, 112);
    hold.setBounds (bottom.removeFromRight (150).reduced (6, 10));
    const int bw = bottom.getWidth() / 4;
    for (auto* k : { &chance, &intensity, &smooth, &mix })
        k->setBounds (bottom.removeFromLeft (bw).reduced (6, 0));
}
