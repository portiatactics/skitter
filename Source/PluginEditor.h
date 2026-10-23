#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace skitterui
{
    // one colour per effect, shared by the knobs and the event lane
    inline juce::Colour fxColour (int fx)
    {
        switch (fx)
        {
            case 1:  return juce::Colour (0xffb6ff3b); // stutter  – acid green
            case 2:  return juce::Colour (0xff39d6ff); // reverse  – cyan
            case 3:  return juce::Colour (0xffffb23b); // tape     – amber
            case 4:  return juce::Colour (0xffff4fa3); // gate     – hot pink
            case 5:  return juce::Colour (0xffa58bff); // crush    – violet
            default: return juce::Colour (0xff2a2d35);
        }
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };

    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, juce::Colour);
        void resized() override;
        void paint (juce::Graphics&) override;
    private:
        juce::Slider slider;
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    class SliceSelector : public juce::Component
    {
    public:
        SliceSelector (juce::AudioProcessorValueTreeState&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        int indexAt (juce::Point<int>) const;
        int current = 2;
        juce::ParameterAttachment attach;
    };

    class HoldButton : public juce::Component
    {
    public:
        HoldButton (juce::AudioProcessorValueTreeState&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        bool on = false;
        juce::ParameterAttachment attach;
    };

    class EventLane : public juce::Component, private juce::Timer
    {
    public:
        explicit EventLane (skitter::GlitchEngine&);
        void paint (juce::Graphics&) override;
    private:
        void timerCallback() override;
        skitter::GlitchEngine& engine;
        int lastHead = -1;
        float flash = 0.0f;
    };
}

class SkitterEditor : public juce::AudioProcessorEditor
{
public:
    explicit SkitterEditor (SkitterProcessor&);
    ~SkitterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SkitterProcessor& proc;
    skitterui::LookAndFeel lnf;
    skitterui::SliceSelector slice;
    skitterui::EventLane lane;
    skitterui::Knob stutter, reverse, tape, gate, crush;
    skitterui::Knob chance, intensity, smooth, mix;
    skitterui::HoldButton hold;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkitterEditor)
};
