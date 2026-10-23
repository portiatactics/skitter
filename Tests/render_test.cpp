// Offline test: synthesizes a drum-and-chord loop, runs it through the engine,
// writes before/after WAVs and checks the output is sane.
#include "../Source/GlitchEngine.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : { L[i], R[i] })
        {
            const int16_t v = (int16_t) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f);
            w16 ((uint16_t) v);
        }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const int sr = 44100;
    const double bpm = 120.0;
    const int bars = 8;
    const int total = (int) (bars * 4 * 60.0 / bpm * sr);
    const double spb = 60.0 / bpm * sr;

    std::vector<float> L (total), R (total);
    std::mt19937 noise (7);
    std::uniform_real_distribution<float> nd (-1.0f, 1.0f);
    const double chordHz[] = { 220.0, 261.63, 329.63, 392.0 };

    for (int i = 0; i < total; ++i)
    {
        const double beat = i / spb;
        const double inBeat = beat - std::floor (beat);
        const double in8th = std::fmod (beat * 2.0, 1.0);
        const int beatNum = (int) std::floor (beat) % 4;
        float s = 0.0f;
        // kick on every beat
        { const double t = inBeat * spb / sr; s += 0.8f * (float) (std::sin (2 * M_PI * (50 + 120 * std::exp (-t * 30)) * t) * std::exp (-t * 7)); }
        // snare on 2 and 4
        if (beatNum == 1 || beatNum == 3) { const double t = inBeat * spb / sr; s += 0.35f * nd (noise) * (float) std::exp (-t * 18); }
        // hats on 8ths
        { const double t = in8th * spb * 0.5 / sr; s += 0.12f * nd (noise) * (float) std::exp (-t * 60); }
        // chord pad
        float pad = 0.0f; const double t = (double) i / sr;
        for (double hz : chordHz) pad += (float) std::sin (2 * M_PI * hz * t);
        s += 0.06f * pad;
        L[i] = s * 0.5f; R[i] = s * 0.5f;
    }
    writeWav (outDir + "/dry_loop.wav", L, R, sr);

    skitter::GlitchEngine eng;
    eng.prepare (sr, 2);
    skitter::Params p;
    p.sliceBeats = 0.25; p.chance = 0.6f; p.intensity = 0.65f;
    p.wStutter = 1.0f; p.wReverse = 0.6f; p.wTape = 0.4f; p.wGate = 0.6f; p.wCrush = 0.4f;

    std::vector<float> oL = L, oR = R;
    const int block = 512;
    int counts[6] = {};
    int lastHead = eng.historyHead();
    for (int start = 0; start < total; start += block)
    {
        const int n = std::min (block, total - start);
        float* ptrs[2] = { oL.data() + start, oR.data() + start };
        // simulate host: playhead in quarter notes
        eng.process (ptrs, 2, n, p, bpm, start / spb);
        while (lastHead != eng.historyHead()) { lastHead = (lastHead + 1) % skitter::GlitchEngine::historySize; counts[eng.historyAt (lastHead)]++; }
    }
    writeWav (outDir + "/glitched_loop.wav", oL, oR, sr);

    // sanity checks
    float peak = 0.0f, maxJump = 0.0f; double diff = 0.0; bool finite = true;
    for (int i = 0; i < total; ++i)
    {
        finite &= std::isfinite (oL[i]) && std::isfinite (oR[i]);
        peak = std::max (peak, std::abs (oL[i]));
        if (i) maxJump = std::max (maxJump, std::abs (oL[i] - oL[i - 1]));
        diff += std::abs (oL[i] - L[i]);
    }
    float dryJump = 0.0f; for (int i = 1; i < total; ++i) dryJump = std::max (dryJump, std::abs (L[i] - L[i - 1]));
    const char* names[] = { "none", "stutter", "reverse", "tape", "gate", "crush" };
    std::printf ("slices: "); for (int k = 0; k < 6; ++k) std::printf ("%s=%d ", names[k], counts[k]); std::printf ("\n");
    std::printf ("finite=%d peak=%.3f maxStep(out)=%.3f maxStep(dry)=%.3f meanAbsDiff=%.4f\n", finite, peak, maxJump, dryJump, diff / total);

    // mix = 0 must be transparent
    skitter::GlitchEngine e2; e2.prepare (sr, 2); p.mix = 0.0f;
    std::vector<float> zL = L, zR = R; float* zp[2] = { zL.data(), zR.data() };
    e2.process (zp, 2, total, p, bpm, 0.0);
    double d0 = 0; for (int i = 0; i < total; ++i) d0 += std::abs (zL[i] - L[i]);
    std::printf ("mix=0 deviation=%.6f\n", d0);

    // no host position (internal clock) and odd tempo / tiny blocks
    skitter::GlitchEngine e3; e3.prepare (48000, 2); p.mix = 1.0f; p.sliceBeats = 1.0 / 32.0; p.chance = 1.0f; p.intensity = 1.0f;
    std::vector<float> tL = L, tR = R; bool ok3 = true;
    for (int s = 0; s < total; s += 7) { int n = std::min (7, total - s); float* tp[2] = { tL.data() + s, tR.data() + s }; e3.process (tp, 2, n, p, 173.0, -1.0); }
    for (int i = 0; i < total; ++i) ok3 &= std::isfinite (tL[i]) && std::abs (tL[i]) < 4.0f;
    std::printf ("internal-clock/max-intensity ok=%d\n", ok3);
    return (finite && ok3 && d0 < 1e-3) ? 0 : 1;
}
