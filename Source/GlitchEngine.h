#pragma once
/*
    Skitter — glitch / stutter engine
    ---------------------------------
    Plain C++ (no JUCE) so it can be tested on its own.

    How it works
      * Every input sample is written into a circular history buffer.
      * Time is split into tempo-synced slices (1/32 note up to 1 bar).
      * At each slice boundary a dice roll ("Chance") decides whether that slice
        glitches. If it does, one effect is picked using the per-effect weights:
            Stutter  – loops a tiny chunk of the most recent audio (sometimes a
                       "roll" that speeds up across the slice)
            Reverse  – plays the previous slice backwards
            Tape     – tape-stop: playback slows to a halt
            Gate     – chops the slice with a random on/off rhythm
            Crush    – bit-depth and sample-rate reduction
      * Short crossfades at every edge keep it clicky-on-purpose, not clicky-by-accident.
*/

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <array>

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

namespace skitter
{

enum class Fx : int { None = 0, Stutter, Reverse, Tape, Gate, Crush, Count };

struct Params
{
    double sliceBeats   = 0.25;  // length of one slice, in quarter-note beats
    float  chance       = 0.5f;  // 0..1 probability a slice glitches
    float  wStutter     = 1.0f;  // relative weights of each effect
    float  wReverse     = 0.5f;
    float  wTape        = 0.3f;
    float  wGate        = 0.5f;
    float  wCrush       = 0.3f;
    float  intensity    = 0.5f;  // 0..1 how extreme each effect gets
    float  smoothMs     = 3.0f;  // edge crossfade
    float  mix          = 1.0f;  // dry/wet
    bool   force        = false; // momentary: stutter every slice
};

class GlitchEngine
{
public:
    static constexpr int historySize = 64; // events kept for the display

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::max (1, numChannels);
        bufLen = (int) (sr * 12.0) + 1;               // room for a full bar at very slow tempos
        buffer.assign ((size_t) channels, std::vector<float> ((size_t) bufLen, 0.0f));
        holdVal.assign ((size_t) channels, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& ch : buffer) std::fill (ch.begin(), ch.end(), 0.0f);
        writePos = 0;
        ev = {};
        lastSlice = -1;
        internalPpq = 0.0;
        mixSmoothed = -1.0f;
    }

    void setSeed (unsigned s) { rng.seed (s); }

    /*  Processes a block in place.
        ppqAtStart: host musical position (quarter notes) at the first sample, or a
                    negative number if the host gives no position (internal clock is used).
        bpm:        host tempo (fallback 120). */
    void process (float* const* data, int numChannels, int numSamples,
                  const Params& p, double bpm, double ppqAtStart)
    {
        if (bufLen <= 0) return;
        numChannels = std::min (numChannels, channels);
        bpm = (bpm > 1.0 && bpm < 1000.0) ? bpm : 120.0;

        const double beatsPerSample = bpm / (60.0 * sr);
        const double samplesPerBeat = 60.0 * sr / bpm;
        const double sliceBeats = std::max (1.0 / 64.0, p.sliceBeats);
        const int    fadeLen = std::max (1, (int) (p.smoothMs * 0.001 * sr));

        double ppq = ppqAtStart >= 0.0 ? ppqAtStart : internalPpq;
        if (mixSmoothed < 0.0f) mixSmoothed = p.mix;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- 1. record the input --------------------------------------------
            for (int c = 0; c < numChannels; ++c)
                buffer[(size_t) c][(size_t) writePos] = data[c][i];

            // ---- 2. slice boundary? ---------------------------------------------
            const long long slice = (long long) std::floor (ppq / sliceBeats + 1e-9);
            if (slice != lastSlice)
            {
                // samples left until the next grid line (handles starting mid-slice)
                const double beatsLeft = (double) (slice + 1) * sliceBeats - ppq;
                const int len = std::max (16, (int) std::lround (beatsLeft * samplesPerBeat));
                startSlice (p, len, (int) std::lround (sliceBeats * samplesPerBeat), fadeLen);
                lastSlice = slice;
            }

            // ---- 3. render -------------------------------------------------------
            mixSmoothed += (p.mix - mixSmoothed) * 0.002f;
            const float g = edgeGain (fadeLen);

            for (int c = 0; c < numChannels; ++c)
            {
                const float dry = data[c][i];
                float wet = dry;
                if (ev.type != Fx::None)
                {
                    const float fx = renderFx (c, dry);
                    wet = dry + (fx - dry) * g;
                }
                const float out = dry + (wet - dry) * mixSmoothed;
                data[c][i] = std::isfinite (out) ? out : 0.0f;
            }

            advanceFx();
            ppq += beatsPerSample;
            if (++writePos >= bufLen) writePos = 0;
        }

        internalPpq = ppq;
    }

    // ---- info for the UI (read from the message thread) ----------------------
    int  currentFx() const               { return activeFx.load(); }
    int  historyAt (int i) const         { return history[(size_t) i].load(); }
    int  historyHead() const             { return histHead.load(); }

private:
    struct Event
    {
        Fx    type = Fx::None;
        int   n = 0;          // samples elapsed
        int   len = 0;        // event length in samples
        int   startPos = 0;   // write position when it started
        // stutter
        int   loopLen = 0;
        bool  roll = false;
        int   loopStart = 0;  // where the current repeat began (relative n)
        // tape
        double tapePos = 0.0;
        double tapeCurve = 2.0;
        // gate
        int   steps = 8;
        unsigned pattern = 0xff;
        // crush
        float bits = 8.0f;
        int   hold = 1;
        int   holdCount = 0;
    };

    float rand01() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); }
    int   randInt (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }

    int wrap (long long idx) const
    {
        long long m = idx % bufLen;
        return (int) (m < 0 ? m + bufLen : m);
    }

    float readLinear (int c, double idx) const
    {
        const double fl = std::floor (idx);
        const int a = wrap ((long long) fl), b = wrap ((long long) fl + 1);
        const float t = (float) (idx - fl);
        const auto& ch = buffer[(size_t) c];
        return ch[(size_t) a] + (ch[(size_t) b] - ch[(size_t) a]) * t;
    }

    Fx pickFx (const Params& p)
    {
        const float w[] = { p.wStutter, p.wReverse, p.wTape, p.wGate, p.wCrush };
        float total = 0.0f;
        for (float x : w) total += std::max (0.0f, x);
        if (total <= 0.0001f) return Fx::None;
        float r = rand01() * total;
        for (int k = 0; k < 5; ++k)
        {
            r -= std::max (0.0f, w[k]);
            if (r <= 0.0f) return (Fx) (k + 1);
        }
        return Fx::Crush;
    }

    void startSlice (const Params& p, int len, int fullSliceLen, int fadeLen)
    {
        ev = {};
        ev.len = len;
        ev.startPos = writePos;

        Fx type = Fx::None;
        if (p.force)                     type = Fx::Stutter;
        else if (rand01() < p.chance)    type = pickFx (p);

        // reverse/stutter need a full slice of history; skip partial first slices
        if (len < fullSliceLen / 2 && (type == Fx::Reverse)) type = Fx::None;
        ev.type = type;

        const float I = std::clamp (p.intensity, 0.0f, 1.0f);

        switch (type)
        {
            case Fx::Stutter:
            {
                // divisions of the slice: more intensity -> smaller loops
                static const int divs[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
                const int maxIdx = 1 + (int) std::lround (I * 7.0f);
                const int d = divs[randInt (0, std::min (7, maxIdx))];
                ev.loopLen = std::max (fadeLen * 4, fullSliceLen / d);
                ev.loopLen = std::max (32, ev.loopLen);
                ev.roll = rand01() < 0.15f + 0.35f * I;           // accelerating ratchet
                ev.loopStart = 0;
                break;
            }
            case Fx::Tape:
                ev.tapeCurve = 1.0 + 2.0 * rand01();               // how fast it brakes
                ev.tapePos = 0.0;
                break;
            case Fx::Gate:
            {
                ev.steps = I > 0.6f ? 16 : (I > 0.25f ? 8 : 4);
                const unsigned full = (ev.steps >= 16) ? 0xffffu : ((1u << ev.steps) - 1u);
                do { ev.pattern = (unsigned) randInt (1, (int) full); }
                while (ev.pattern == full);                         // never "all on"
                break;
            }
            case Fx::Crush:
                ev.bits = 12.0f - (9.0f * I) * (0.4f + 0.6f * rand01()); // 12 bits down to ~3
                ev.hold = 1 + (int) std::lround (I * 40.0f * rand01() * (float) (sr / 44100.0));
                ev.holdCount = 0;
                break;
            default: break;
        }

        activeFx.store ((int) ev.type);
        const int h = (histHead.load() + 1) % historySize;
        history[(size_t) h].store ((int) ev.type);
        histHead.store (h);
    }

    // crossfade in at the start, out at the end of every event
    float edgeGain (int fadeLen) const
    {
        if (ev.type == Fx::None) return 0.0f;
        const int f = std::min (fadeLen, std::max (1, ev.len / 4));
        const float a = std::min (1.0f, (float) ev.n / (float) f);
        const float b = std::min (1.0f, (float) (ev.len - ev.n) / (float) f);
        return std::max (0.0f, std::min (a, b));
    }

    float renderFx (int c, float dry)
    {
        switch (ev.type)
        {
            case Fx::Stutter:
            {
                const int pos = ev.n - ev.loopStart;               // position inside this repeat
                const float s = buffer[(size_t) c][(size_t) wrap ((long long) ev.startPos - ev.loopLen + pos)];
                // tiny window per repeat so the seams don't click
                const int f = std::max (1, std::min (64, ev.loopLen / 8));
                const float w = std::min ({ 1.0f, (float) (pos + 1) / (float) f,
                                            (float) (ev.loopLen - pos) / (float) f });
                return s * std::max (0.0f, w);
            }
            case Fx::Reverse:
                return buffer[(size_t) c][(size_t) wrap ((long long) ev.startPos - 1 - ev.n)];

            case Fx::Tape:
                return readLinear (c, (double) ev.startPos + ev.tapePos);

            case Fx::Gate:
            {
                const int stepLen = std::max (1, ev.len / ev.steps);
                const int step = std::min (ev.steps - 1, ev.n / stepLen);
                const int inStep = ev.n - step * stepLen;
                const bool on = (ev.pattern >> step) & 1u;
                const int f = std::max (1, std::min (48, stepLen / 6));
                float e = on ? 1.0f : 0.0f;
                if (on) e = std::min ({ 1.0f, (float) (inStep + 1) / (float) f,
                                        (float) (stepLen - inStep) / (float) f });
                return dry * std::max (0.0f, e);
            }
            case Fx::Crush:
            {
                if (ev.holdCount == 0) holdVal[(size_t) c] = dry;
                const float levels = std::pow (2.0f, ev.bits - 1.0f);
                return std::round (holdVal[(size_t) c] * levels) / levels;
            }
            default: return dry;
        }
    }

    void advanceFx()
    {
        if (ev.type == Fx::None) return;
        ++ev.n;

        switch (ev.type)
        {
            case Fx::Stutter:
                if (ev.n - ev.loopStart >= ev.loopLen)
                {
                    ev.loopStart = ev.n;
                    if (ev.roll) ev.loopLen = std::max (64, (int) (ev.loopLen * 0.75)); // ratchet speeds up
                }
                break;
            case Fx::Tape:
            {
                const double t = std::min (1.0, (double) ev.n / (double) std::max (1, ev.len));
                ev.tapePos += std::pow (1.0 - t, ev.tapeCurve);    // speed falls from 1 to 0
                break;
            }
            case Fx::Crush:
                if (++ev.holdCount >= ev.hold) ev.holdCount = 0;
                break;
            default: break;
        }

        if (ev.n >= ev.len)
        {
            ev.type = Fx::None;
            activeFx.store (0);
        }
    }

    double sr = 44100.0;
    int channels = 2;
    int bufLen = 0;
    int writePos = 0;
    std::vector<std::vector<float>> buffer;
    std::vector<float> holdVal;
    Event ev;
    long long lastSlice = -1;
    double internalPpq = 0.0;
    float mixSmoothed = -1.0f;
    std::mt19937 rng { 0x5317u };

    std::atomic<int> activeFx { 0 };
    std::array<std::atomic<int>, historySize> history {};
    std::atomic<int> histHead { 0 };
};

} // namespace skitter

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif
