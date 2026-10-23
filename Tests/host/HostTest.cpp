// Loads the built Skitter.vst3 like a DAW would, renders a loop through it,
// checks parameters/state, and saves a screenshot of the editor.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../../Source/PluginProcessor.h"
#include "../../Source/PluginEditor.h"

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::puts ("usage: HostTest <plugin.vst3> <in.wav> <outdir>"); return 2; }
    const juce::File pluginFile (argv[1]), inFile (argv[2]), outDir (argv[3]);

    // ---- 1. load the real VST3 binary through JUCE's hosting code ---------------
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile (descs, pluginFile.getFullPathName());
    if (descs.isEmpty()) { std::puts ("FAIL: no plugin found in file"); return 1; }
    std::printf ("found: %s by %s (%s)\n", descs[0]->name.toRawUTF8(), descs[0]->manufacturerName.toRawUTF8(), descs[0]->pluginFormatName.toRawUTF8());

    juce::String err;
    auto inst = fm.createPluginInstance (*descs[0], 44100.0, 512, err);
    if (! inst) { std::printf ("FAIL: %s\n", err.toRawUTF8()); return 1; }

    std::printf ("params:");
    for (auto* p : inst->getParameters()) std::printf (" [%s=%s]", p->getName (20).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
    std::printf ("\n");

    // push Chance and Intensity up a bit
    for (auto* p : inst->getParameters())
    {
        if (p->getName (20) == "Chance")    p->setValueNotifyingHost (0.6f);
        if (p->getName (20) == "Intensity") p->setValueNotifyingHost (0.65f);
    }

    // ---- 2. render audio ---------------------------------------------------------
    juce::AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (inFile));
    if (! reader) { std::puts ("FAIL: can't read input"); return 1; }
    const int total = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (2, total);
    reader->read (&audio, 0, total, 0, true, true);

    FakePlayHead ph;
    inst->setPlayHead (&ph);
    inst->setPlayConfigDetails (2, 2, 44100.0, 512);
    inst->prepareToPlay (44100.0, 512);
    juce::MidiBuffer midi;
    const double spb = 60.0 / ph.bpm * 44100.0;
    for (int s = 0; s < total; s += 512)
    {
        const int n = juce::jmin (512, total - s);
        juce::AudioBuffer<float> blk (audio.getArrayOfWritePointers(), 2, s, n);
        ph.ppq = s / spb;
        inst->processBlock (blk, midi);
    }
    inst->releaseResources();

    bool finite = true; float peak = 0.0f;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i)
    { const float v = audio.getSample (c, i); finite &= std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }
    std::printf ("rendered %d samples via VST3, finite=%d peak=%.3f\n", total, (int) finite, peak);

    juce::WavAudioFormat wav;
    auto outFile = outDir.getChildFile ("glitched_via_vst3.wav"); outFile.deleteFile();
    if (auto os = std::unique_ptr<juce::OutputStream> (outFile.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), 44100.0, 2, 24, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (audio, 0, total); }

    // ---- 3. state save / restore ----------------------------------------------
    juce::MemoryBlock state; inst->getStateInformation (state);
    inst->getParameters()[1]->setValueNotifyingHost (0.0f);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("state restore: chance=%s (expect 60%%)\n", inst->getParameters()[1]->getCurrentValueAsText().toRawUTF8());

    // ---- 4. editor screenshot (built from the same sources) -------------------
    {
        SkitterProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        // feed it some audio so the event lane has history to show
        FakePlayHead ph2; proc.setPlayHead (&ph2);
        *proc.apvts.getRawParameterValue ("chance") = 0.7f;
        reader->read (&audio, 0, total, 0, true, true);
        for (int s = 0; s + 512 <= total / 2; s += 512)
        { juce::AudioBuffer<float> blk (audio.getArrayOfWritePointers(), 2, s, 512); ph2.ppq = s / spb; proc.processBlock (blk, midi); }

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto png = outDir.getChildFile ("editor.png"); png.deleteFile();
        juce::FileOutputStream fos (png);
        juce::PNGImageFormat().writeImageToStream (img, fos);
        std::printf ("editor %dx%d saved\n", ed->getWidth(), ed->getHeight());
        ed.reset();
    }

    inst.reset();
    return finite ? 0 : 1;
}
