#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // slice lengths in quarter-note beats, matching sliceNames()
    constexpr double sliceBeats[] = { 1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0, 1.0 / 2.0, 1.0, 2.0, 4.0 };
}

const juce::StringArray& SkitterProcessor::sliceNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2", "1 bar" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout SkitterProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto pct = AudioParameterFloatAttributes().withLabel ("%")
                   .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });

    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "slice", 1 }, "Slice", sliceNames(), 2));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "chance", 1 }, "Chance", NormalisableRange<float> (0.0f, 1.0f), 0.45f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "intensity", 1 }, "Intensity", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "stutter", 1 }, "Stutter", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "reverse", 1 }, "Reverse", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tape", 1 }, "Tape Stop", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, "Gate", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "crush", 1 }, "Crush", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smooth",
                      NormalisableRange<float> (0.2f, 20.0f, 0.0f, 0.5f), 3.0f,
                      AudioParameterFloatAttributes().withLabel ("ms")
                          .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "force", 1 }, "Hold Stutter", false));
    return { ps.begin(), ps.end() };
}

SkitterProcessor::SkitterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SkitterState", createLayout())
{
    pSlice     = apvts.getRawParameterValue ("slice");
    pChance    = apvts.getRawParameterValue ("chance");
    pIntensity = apvts.getRawParameterValue ("intensity");
    pStutter   = apvts.getRawParameterValue ("stutter");
    pReverse   = apvts.getRawParameterValue ("reverse");
    pTape      = apvts.getRawParameterValue ("tape");
    pGate      = apvts.getRawParameterValue ("gate");
    pCrush     = apvts.getRawParameterValue ("crush");
    pSmooth    = apvts.getRawParameterValue ("smooth");
    pMix       = apvts.getRawParameterValue ("mix");
    pForce     = apvts.getRawParameterValue ("force");
}

bool SkitterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void SkitterProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, getTotalNumOutputChannels());
    engine.setSeed ((unsigned) juce::Time::getMillisecondCounter());
}

void SkitterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    double bpm = 120.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) ppq = *q;
        }

    skitter::Params p;
    const int sliceIdx = juce::jlimit (0, 7, (int) pSlice->load());
    p.sliceBeats = sliceBeats[sliceIdx];
    p.chance     = pChance->load();
    p.intensity  = pIntensity->load();
    p.wStutter   = pStutter->load();
    p.wReverse   = pReverse->load();
    p.wTape      = pTape->load();
    p.wGate      = pGate->load();
    p.wCrush     = pCrush->load();
    p.smoothMs   = pSmooth->load();
    p.mix        = pMix->load();
    p.force      = pForce->load() > 0.5f;

    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                    buffer.getNumSamples(), p, bpm, ppq);
}

juce::AudioProcessorEditor* SkitterProcessor::createEditor() { return new SkitterEditor (*this); }

void SkitterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SkitterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkitterProcessor(); }
