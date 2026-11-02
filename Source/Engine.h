#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Pool.h"
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace prisma
{

constexpr int numVoices = 8;
constexpr int maxPattern = 32;

enum HitMode { HitOneShot = 0, HitGate, HitLoop, HitPingPong };
enum Order   { OrderRandom = 0, OrderNoRepeat, OrderCycle, OrderBackward, OrderPingPong, OrderKeys, OrderRandKeys };

inline juce::StringArray rateNames()  { return { "Off", "1/1", "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32", "1/64" }; }
inline double rateBeats (int r)
{
    static const double b[] = { 0.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return b[juce::jlimit (0, 13, r)];
}
inline juce::StringArray modeNames()   { return { "One-Shot", "Gate", "Loop", "Ping-Pong" }; }
inline juce::StringArray orderNames()  { return { "Random", "No Repeat", "Cycle", "Backward", "Ping-Pong", "Keys", "Rand Keys" }; }
inline juce::StringArray stepsNames()  { return { "4", "8", "16", "32" }; }
inline int stepsCount (int i)          { static const int s[] = { 4, 8, 16, 32 }; return s[juce::jlimit (0, 3, i)]; }
inline juce::StringArray resetNames()  { return { "Free", "1 Bar", "2 Bars", "4 Bars" }; }
inline int resetBars (int i)           { static const int b[] = { 0, 1, 2, 4 }; return b[juce::jlimit (0, 3, i)]; }
inline juce::StringArray splitNames()  { return { "Equal", "Transients" }; }
inline juce::StringArray partsNames()  { return { "2", "3", "4", "6", "8", "12", "16", "24", "32" }; }
inline int partsCount (int i)          { static const int p[] = { 2, 3, 4, 6, 8, 12, 16, 24, 32 }; return p[juce::jlimit (0, 8, i)]; }

struct Params
{
    int rate = 10; float length = 0.85f, swing = 0.0f, decay = 0.0f; bool gridSync = true;
    int mode = HitOneShot; bool choke = true;
    int order = OrderRandom; int steps = 8; bool locked = false; int resetBars = 0; float reverse = 0.0f;
    float attack = 0.002f, decayTime = 0.3f, sustain = 1.0f, release = 0.15f;
    float tune = 0.0f;
};

// State shared between the audio thread and the UI.
struct SharedState
{
    std::array<std::atomic<int>, maxPattern> pattern {};
    std::array<std::atomic<int>, maxPattern> keyPerm {};

    // history of hits for Capture: (step, part)
    std::array<std::atomic<int>, maxPattern> histStep {}, histPart {};
    std::atomic<int> histWrite { 0 };

    // hit log for the display
    static constexpr int logSize = 64;
    std::array<std::atomic<int>, logSize> logPart {};
    std::array<std::atomic<int>, logSize> logFlags {};
    std::atomic<int> logWrite { 0 };

    // playheads for the display
    std::array<std::atomic<int>, numVoices> headPart {};
    std::array<std::atomic<float>, numVoices> headPos {};
    std::array<std::atomic<float>, numVoices> headLevel {};

    std::atomic<int> currentStep { -1 };

    SharedState()
    {
        for (int i = 0; i < maxPattern; ++i) { pattern[(size_t) i] = i % 4; keyPerm[(size_t) i] = i; }
        for (auto& h : headPart) h = -1;
    }
};

//==============================================================================
class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
        counter = 0;
        lastTick = noTick;
        lastResetBlock = noTick;
        wasPlaying = false;
    }

    void setShared (SharedState* s) { shared = s; }

    // ---- transport handling, once per block
    void beginBlock (bool isPlaying, double ppq, const Params& p)
    {
        if (isPlaying && ! wasPlaying)
        {
            counter = 0;
            lastResetBlock = noTick;
            lastTick = noTick;
        }
        if (std::abs (ppq - expectedPpq) > 0.5) lastTick = noTick;
        if (rateBeats (p.rate) != lastBeats) { lastTick = noTick; lastBeats = rateBeats (p.rate); }
        wasPlaying = isPlaying;
    }

    void endBlock (double ppqAfter) { expectedPpq = ppqAfter; publishHeads(); }

    // Sample offsets of (swung) grid ticks in [0, n). Positions are solved
    // directly from the tick times so tiny transport rounding errors never
    // move a tick by a sample.
    int findTicks (double ppq, double ppqPerSample, int n, const Params& p, int* out, int maxOut)
    {
        const double beats = rateBeats (p.rate);
        if (beats <= 0.0 || ! p.gridSync || ppqPerSample <= 0.0) return 0;
        auto offset = [&p] (juce::int64 g) { return (g & 1) ? (double) p.swing : 0.0; };

        if (lastTick == noTick)
        {
            const double x0 = ppq / beats;
            const auto g0 = (juce::int64) std::floor (x0 + 1.0e-9);
            lastTick = (x0 - (double) g0 + 1.0e-9 >= offset (g0)) ? g0 : g0 - 1; // never fire a tick we are already past
            if (lastTick == g0 && std::abs (x0 - (double) g0 - offset (g0)) < 1.0e-9)
                lastTick = g0 - 1;                                            // ...unless it is exactly now
        }
        int count = 0;
        for (juce::int64 g = lastTick + 1; count < maxOut; ++g)
        {
            const double tickPpq = ((double) g + offset (g)) * beats;
            const int s = (int) std::ceil ((tickPpq - ppq) / ppqPerSample - 1.0e-6);
            if (s >= n) break;
            out[count++] = juce::jmax (0, s);
            lastTick = g;
        }
        return count;
    }

    void gridTick (const Params& p, const Pool& pool, double interval, double ppqNow, double barBeats)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.envStage < 3 && v.sinceHit >= interval * 0.5)
                fireHit (v, p, pool, interval, ppqNow, barBeats);
    }

    void noteOn (int note, float vel, const Params& p, const Pool& pool, double interval, double ppqNow, double barBeats)
    {
        if (pool.parts.empty()) return;
        if (p.choke)
            for (auto& v : voices)
                if (v.active) { v.envStage = 3; v.fastRelease = true; v.held = false; }

        Voice* t = nullptr;
        for (auto& v : voices) if (! v.active) { t = &v; break; }
        if (t == nullptr) { t = &voices[0]; for (auto& v : voices) if (v.age > t->age) t = &v; }
        auto& v = *t;
        v = Voice();
        v.active = true; v.held = true; v.note = note;
        v.velocity = 0.3f + 0.7f * vel;
        v.rng.setSeed ((juce::int64) note * 92821 + (juce::int64) (++seedCounter) * 7919);
        fireHit (v, p, pool, interval, ppqNow, barBeats);
    }

    void noteOff (int note, const Params& p)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note)
            {
                v.held = false;
                const bool oneShotFree = rateBeats (p.rate) <= 0.0 && p.mode == HitOneShot;
                if (! oneShotFree) v.envStage = 3;
            }
    }

    void allNotesOff()
    {
        for (auto& v : voices) if (v.active) { v.held = false; v.envStage = 3; }
    }

    void render (float* L, float* R, int n, const Params& p, const Pool& pool, double interval,
                 double ppqStart, double ppqPerSample, double barBeats)
    {
        if (pool.parts.empty())
        {
            for (auto& v : voices) v.active = false;
            return;
        }
        const double attackStep = 1.0 / juce::jmax (1.0, (double) p.attack * sr);
        const double decayCoef = std::exp (-6.9 / juce::jmax (1.0, (double) p.decayTime * sr));
        const double releaseCoef = std::exp (-6.9 / juce::jmax (1.0, (double) p.release * sr));
        const double fastCoef = std::exp (-6.9 / (0.008 * sr));
        const bool retrig = rateBeats (p.rate) > 0.0;

        for (auto& v : voices)
        {
            if (! v.active) continue;

            for (int i = 0; i < n; ++i)
            {
                switch (v.envStage)
                {
                    case 0: v.env += (float) attackStep; if (v.env >= 1.0f) { v.env = 1.0f; v.envStage = 1; } break;
                    case 1: v.env = (float) (p.sustain + (v.env - p.sustain) * decayCoef);
                            if (std::abs (v.env - p.sustain) < 1.0e-4f) v.envStage = 2;
                            break;
                    case 2: v.env = p.sustain; break;
                    default: v.env *= (float) (v.fastRelease ? fastCoef : releaseCoef); break;
                }
                if (v.envStage == 3 && v.env < 1.0e-4f) { v.active = false; break; }

                // key-synced retrigger
                if (retrig && ! p.gridSync && v.held && v.envStage < 3)
                {
                    v.untilNext -= 1.0;
                    if (v.untilNext <= 0.0)
                        fireHit (v, p, pool, interval, ppqStart + i * ppqPerSample, barBeats);
                }

                float l = 0.0f, r = 0.0f;
                bool anyHit = false;
                for (auto& h : v.hits)
                {
                    if (! h.active || (size_t) h.source >= pool.sources.size()) { h.active = false; continue; }
                    anyHit = true;
                    renderHit (h, pool, l, r);
                }
                if (! anyHit && v.envStage < 3 && (! v.held || (! retrig && p.mode == HitOneShot)))
                {
                    v.envStage = 3;
                    v.fastRelease = true;
                }

                const float a = v.env * v.velocity * 0.6f;
                L[i] += l * a;
                R[i] += r * a;
                v.sinceHit += 1.0;
                ++v.age;
            }
        }
    }

private:
    static constexpr juce::int64 noTick = std::numeric_limits<juce::int64>::min();

    struct Hit
    {
        bool active = false, looped = false;
        int part = 0, source = 0, mode = HitOneShot, dir = 1;
        double start = 0.0, end = 0.0, pos = 0.0, step = 1.0;
        double age = 0.0, length = -1.0, stopAt = -1.0, fade = 1.0;
        float gain = 1.0f;
    };

    struct Voice
    {
        bool active = false, held = false, fastRelease = false;
        int note = 60, envStage = 0, repeat = 0;
        float velocity = 1.0f, env = 0.0f;
        double sinceHit = 1.0e9, untilNext = 0.0;
        juce::int64 age = 0;
        std::array<Hit, 2> hits {};
        juce::Random rng;
    };

    void renderHit (Hit& h, const Pool& pool, float& l, float& r)
    {
        const auto& src = *pool.sources[(size_t) h.source];
        const auto* SL = src.audio.getReadPointer (0);
        const auto* SR = src.audio.getReadPointer (1);
        const double len = h.end - h.start;
        const double seam = juce::jmax (2.0, 0.0015 * src.sampleRate);

        // wrap, bounce or stop at the part edges
        if (h.dir > 0 && h.pos >= h.end - 1.0)
        {
            if (h.mode == HitLoop && len > 8.0) { h.pos -= (len - 1.0); h.looped = true; }
            else if (h.mode == HitPingPong && len > 8.0) { h.pos = 2.0 * (h.end - 1.0) - h.pos; h.dir = -1; }
            else { h.active = false; return; }
        }
        else if (h.dir < 0 && h.pos <= h.start)
        {
            if (h.mode == HitLoop && len > 8.0) { h.pos += (len - 1.0); h.looped = true; }
            else if (h.mode == HitPingPong && len > 8.0) { h.pos = 2.0 * h.start - h.pos; h.dir = 1; }
            else { h.active = false; return; }
        }
        h.pos = juce::jlimit (h.start, h.end - 1.0001, h.pos);

        const int i0 = (int) h.pos;
        const float f = (float) (h.pos - i0);
        const int i1 = juce::jmin (i0 + 1, (int) h.end - 1);
        const float sl = SL[i0] + f * (SL[i1] - SL[i0]);
        const float sr2 = SR[i0] + f * (SR[i1] - SR[i0]);

        double g = juce::jmin (1.0, h.age / h.fade);
        const double toEnd = h.end - 1.0 - h.pos, toStart = h.pos - h.start;
        if (h.mode == HitOneShot || h.mode == HitGate)
        {
            const double remain = (h.dir > 0 ? toEnd : toStart) / h.step;
            g = juce::jmin (g, remain / (0.004 * sr));
        }
        else if (h.mode == HitLoop && len > seam * 4.0)
        {
            // dip at the loop seam so the jump never clicks
            g = juce::jmin (g, (h.dir > 0 ? toEnd : toStart) / seam);
            if (h.looped) g = juce::jmin (g, (h.dir > 0 ? toStart : toEnd) / seam);
        }
        if (h.length >= 0.0)
        {
            const double remain = h.length - h.age;
            if (remain <= 0.0) { h.active = false; return; }
            g = juce::jmin (g, remain / (0.004 * sr));
        }
        if (h.stopAt >= 0.0)
        {
            const double remain = h.stopAt - h.age;
            if (remain <= 0.0) { h.active = false; return; }
            g = juce::jmin (g, remain / (0.005 * sr));
        }
        const float gg = (float) juce::jlimit (0.0, 1.0, g) * h.gain;
        l += sl * gg;
        r += sr2 * gg;

        h.pos += h.step * h.dir;
        h.age += 1.0;
    }

    static int keyPart (int note, int n)
    {
        int k = (note - 36) % n;
        if (k < 0) k += n;
        return k;
    }

    int choosePart (Voice& v, const Params& p, int n, double ppqNow, double barBeats)
    {
        if (p.order == OrderKeys) return keyPart (v.note, n);
        if (p.order == OrderRandKeys)
        {
            const int k = keyPart (v.note, n);
            int seen = 0;
            for (int i = 0; i < maxPattern; ++i)
            {
                const int val = shared != nullptr ? shared->keyPerm[(size_t) i].load() : i;
                if (val >= 0 && val < n) { if (seen == k) return val; ++seen; }
            }
            return k;
        }

        const int bars = resetBars (p.resetBars);
        if (bars > 0 && barBeats > 0.0)
        {
            const auto block = (juce::int64) std::floor ((ppqNow + 1.0e-6) / (barBeats * bars));
            if (block != lastResetBlock) { lastResetBlock = block; counter = 0; }
        }

        const int step = counter++;
        int part = 0;
        if (p.locked && shared != nullptr)
            part = juce::jmax (0, shared->pattern[(size_t) (step % p.steps)].load()) % n;
        else
        {
            switch (p.order)
            {
                case OrderNoRepeat:
                    part = v.rng.nextInt (n);
                    if (n > 1 && part == lastGlobalPart) part = (part + 1 + v.rng.nextInt (n - 1)) % n;
                    break;
                case OrderCycle:    part = step % n; break;
                case OrderBackward: part = (n - 1) - step % n; break;
                case OrderPingPong:
                {
                    if (n < 2) { part = 0; break; }
                    const int period = 2 * n - 2;
                    const int t = step % period;
                    part = t < n ? t : period - t;
                    break;
                }
                default: part = v.rng.nextInt (n); break;
            }
        }
        if (shared != nullptr)
        {
            const int w = shared->histWrite.load (std::memory_order_relaxed);
            shared->histStep[(size_t) (w % maxPattern)].store (step);
            shared->histPart[(size_t) (w % maxPattern)].store (part);
            shared->histWrite.store (w + 1, std::memory_order_release);
            shared->currentStep.store (step);
        }
        lastGlobalPart = part;
        return part;
    }

    void fireHit (Voice& v, const Params& p, const Pool& pool, double interval, double ppqNow, double barBeats)
    {
        const int n = (int) pool.parts.size();
        if (n == 0) return;
        const bool retrig = rateBeats (p.rate) > 0.0;

        const int partIndex = choosePart (v, p, n, ppqNow, barBeats);
        const auto& part = pool.parts[(size_t) partIndex];
        const auto& src = *pool.sources[(size_t) part.source];

        // fade out whatever this voice is still playing
        for (auto& h : v.hits)
            if (h.active && (h.stopAt < 0.0 || h.stopAt > h.age + 0.005 * sr))
                h.stopAt = h.age + 0.005 * sr;

        Hit* slot = nullptr;
        for (auto& h : v.hits) if (! h.active) { slot = &h; break; }
        if (slot == nullptr)
        {
            slot = &v.hits[0];
            for (auto& h : v.hits) if (h.stopAt - h.age < slot->stopAt - slot->age) slot = &h;
        }
        auto& h = *slot;

        const bool rev = p.reverse > 0.0f && v.rng.nextFloat() < p.reverse;
        h = Hit();
        h.active = true;
        h.part = partIndex;
        h.source = part.source;
        h.mode = p.mode;
        h.start = part.start;
        h.end = part.end;
        h.dir = rev ? -1 : 1;
        h.pos = rev ? h.end - 1.001 : h.start;
        h.step = std::pow (2.0, (double) p.tune / 12.0) * src.sampleRate / sr;
        h.fade = juce::jmax (1.0, 0.0015 * sr);
        h.gain = v.repeat == 0 ? 1.0f : std::pow (juce::jmax (0.0f, 1.0f - p.decay), (float) v.repeat);
        if (retrig)
            h.length = juce::jmax (0.004 * sr, interval * p.length);
        else if (p.mode == HitOneShot || p.mode == HitGate)
            h.length = juce::jmax (0.004 * sr, (part.length() / h.step) * juce::jmax (0.05f, p.length));
        else
            h.length = -1.0;

        if (retrig && ! p.gridSync)
        {
            const double shift = (v.repeat % 2 == 0) ? p.swing : -p.swing;
            v.untilNext = interval * (1.0 + shift);
        }

        ++v.repeat;
        v.sinceHit = 0.0;

        if (shared != nullptr)
        {
            const int w = shared->logWrite.load (std::memory_order_relaxed);
            shared->logPart[(size_t) (w % SharedState::logSize)].store (partIndex);
            shared->logFlags[(size_t) (w % SharedState::logSize)].store (rev ? 1 : 0);
            shared->logWrite.store (w + 1, std::memory_order_release);
        }
    }

    void publishHeads()
    {
        if (shared == nullptr) return;
        for (int i = 0; i < numVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            int part = -1; float pos = 0.0f;
            if (v.active)
                for (auto& h : v.hits)
                    if (h.active && h.stopAt < 0.0)
                    {
                        part = h.part;
                        pos = (float) ((h.pos - h.start) / juce::jmax (1.0, h.end - h.start));
                    }
            shared->headPart[(size_t) i].store (part);
            shared->headPos[(size_t) i].store (pos);
            shared->headLevel[(size_t) i].store (v.active ? v.env : 0.0f);
        }
    }

    std::array<Voice, numVoices> voices {};
    SharedState* shared = nullptr;
    double sr = 44100.0, expectedPpq = -1000.0, lastBeats = -1.0;
    int counter = 0, lastGlobalPart = -1;
    juce::int64 seedCounter = 0;
    juce::int64 lastTick = noTick, lastResetBlock = noTick;
    bool wasPlaying = false;
};

//==============================================================================
// Low cut / high cut, Crunch, Pump, Wash and output.
class FX
{
public:
    struct Settings { float lowCut = 20.0f, highCut = 20000.0f, reso = 0.7f, crunch = 0.0f, wash = 0.0f, pump = 0.0f, volumeDb = 0.0f; };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, blockSize), 2 };
        for (auto* f : { &hp, &lp, &washLp }) { f->prepare (spec); f->reset(); }
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        washLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        washLp.setResonance (0.6f);
        hp.setResonance (0.7f);
        reverb.setSampleRate (sr);
        reverb.reset();
        for (auto* s : { &crunchSm, &washSm, &pumpSm, &gainSm, &hpSm, &lpSm }) s->reset (sr, 0.05);
        hpSm.setCurrentAndTargetValue (20.0f);
        lpSm.setCurrentAndTargetValue (20000.0f);
        holdCount = 0; follower = 0.0f;
        snapNext = true;
    }

    void process (float* L, float* R, int n, const Settings& s, double ppqStart, double ppqPerSample)
    {
        if (snapNext)
        {
            // start exactly at the current settings instead of ramping from zero
            snapNext = false;
            hpSm.setCurrentAndTargetValue (s.lowCut);
            lpSm.setCurrentAndTargetValue (juce::jmin (s.highCut, (float) (sr * 0.45)));
            crunchSm.setCurrentAndTargetValue (s.crunch);
            washSm.setCurrentAndTargetValue (s.wash);
            pumpSm.setCurrentAndTargetValue (s.pump);
            gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.volumeDb));
            hp.setCutoffFrequency (s.lowCut);
            lp.setCutoffFrequency (juce::jmin (s.highCut, (float) (sr * 0.45)));
        }
        hpSm.setTargetValue (s.lowCut);
        lpSm.setTargetValue (juce::jmin (s.highCut, (float) (sr * 0.45)));
        crunchSm.setTargetValue (s.crunch);
        washSm.setTargetValue (s.wash);
        pumpSm.setTargetValue (s.pump);
        gainSm.setTargetValue (juce::Decibels::decibelsToGain (s.volumeDb));
        lp.setResonance (juce::jmax (0.5f, s.reso));
        const float relCoef = (float) std::exp (-1.0 / (0.4 * sr));

        for (int i = 0; i < n; ++i)
        {
            const float hpf = hpSm.getNextValue(), lpf = lpSm.getNextValue();
            const float w = washSm.getNextValue();
            if ((i & 15) == 0)
            {
                hp.setCutoffFrequency (hpf);
                lp.setCutoffFrequency (lpf);
                washLp.setCutoffFrequency (juce::jmin (20000.0f * std::pow (1.0f - w * 0.93f, 2.2f) + 400.0f, (float) (sr * 0.45)));
            }
            float l = L[i], r = R[i];
            const float lvl = juce::jmax (std::abs (l), std::abs (r));
            follower = lvl > follower ? lvl : follower * relCoef;

            l = lp.processSample (0, hp.processSample (0, l));
            r = lp.processSample (1, hp.processSample (1, r));

            const float c = crunchSm.getNextValue();
            if (c > 0.001f)
            {
                const float drive = 1.0f + c * 14.0f;
                const float norm = 1.0f / std::tanh (drive);
                const float mix = juce::jmin (1.0f, c * 1.6f);
                l += (std::tanh (l * drive) * norm * 0.8f - l) * mix;
                r += (std::tanh (r * drive) * norm * 0.8f - r) * mix;
                const int hold = 1 + (int) (juce::jmax (0.0f, c - 0.5f) * 10.0f);
                if (holdCount <= 0) { holdL = l; holdR = r; holdCount = hold; }
                --holdCount;
                if (hold > 1) { l = holdL; r = holdR; }
            }

            const float pm = pumpSm.getNextValue();
            if (pm > 0.001f)
            {
                double ph = ppqStart + i * ppqPerSample;
                ph -= std::floor (ph);
                const float duck = (float) std::exp (-ph * 5.0);
                const float g = 1.0f - pm * 0.92f * duck * juce::jmin (1.0f, (float) (ph * 60.0) + 0.2f);
                l *= g; r *= g;
            }

            if (w > 0.001f)
            {
                l = washLp.processSample (0, l);
                r = washLp.processSample (1, r);
                const float gate = juce::jmin (1.0f, follower * 6.0f);
                if (rng.nextFloat() < w * 0.0012f * gate)
                {
                    const float pop = (rng.nextFloat() - 0.5f) * 0.25f * w;
                    l += pop; r += pop * 0.8f;
                }
                const float hiss = (rng.nextFloat() - 0.5f) * 0.004f * w * gate;
                l += hiss; r += hiss;
            }
            L[i] = l; R[i] = r;
        }
        hp.snapToZero(); lp.snapToZero(); washLp.snapToZero();

        const float wv = washSm.getCurrentValue();
        if (wv > 0.001f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.6f + 0.38f * wv;
            rp.damping = 0.55f;
            rp.wetLevel = 0.32f * wv;
            rp.dryLevel = 0.5f * (1.0f - 0.35f * wv);
            rp.width = 1.0f;
            reverb.setParameters (rp);
            reverb.processStereo (L, R, n);
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = gainSm.getNextValue();
            L[i] = safety (L[i] * g);
            R[i] = safety (R[i] * g);
        }
    }

private:
    static float safety (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        const float a = std::abs (x);
        if (a < 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }

    double sr = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> hp, lp, washLp;
    juce::Reverb reverb;
    juce::SmoothedValue<float> crunchSm, washSm, pumpSm, gainSm, hpSm, lpSm;
    float holdL = 0.0f, holdR = 0.0f, follower = 0.0f;
    int holdCount = 0;
    bool snapNext = true;
    juce::Random rng { 99 };
};

} // namespace prisma
