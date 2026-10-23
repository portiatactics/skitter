#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlitchEngine.h"

class SkitterProcessor : public juce::AudioProcessor
{
public:
    SkitterProcessor();
    ~SkitterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Skitter"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& sliceNames();

    juce::AudioProcessorValueTreeState apvts;
    skitter::GlitchEngine engine;

private:
    std::atomic<float>* pSlice; std::atomic<float>* pChance; std::atomic<float>* pIntensity;
    std::atomic<float>* pStutter; std::atomic<float>* pReverse; std::atomic<float>* pTape;
    std::atomic<float>* pGate; std::atomic<float>* pCrush; std::atomic<float>* pSmooth;
    std::atomic<float>* pMix; std::atomic<float>* pForce;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkitterProcessor)
};
