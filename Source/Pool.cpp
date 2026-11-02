#include "Pool.h"
#include <cmath>

namespace prisma
{

SourcePtr loadSource (const juce::File& file, bool trimSilence, double maxSeconds, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "unsupported or unreadable file"; return nullptr; }

    const double sr = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (sr * maxSeconds));
    if (len < 64) { error = "file is too short"; return nullptr; }

    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&tmp, 0, len, 0, true, true);

    const auto* L = tmp.getReadPointer (0);
    const auto* R = tmp.getReadPointer (tmp.getNumChannels() > 1 ? 1 : 0);
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
    if (peak < 1.0e-5f) { error = "file is silent"; return nullptr; }

    int start = 0, end = len;
    if (trimSilence)
    {
        const float startThresh = peak * 0.03f;   // about -30 dB below the peak
        const float endThresh = peak * 0.001f;    // about -60 dB
        while (start < len && std::abs (L[start]) < startThresh && std::abs (R[start]) < startThresh) ++start;
        start = juce::jmax (0, start - (int) (0.002 * sr)); // keep 2 ms before the hit
        while (end > start + 64 && std::abs (L[end - 1]) < endThresh && std::abs (R[end - 1]) < endThresh) --end;
        if (end - start < 64) { start = 0; end = len; }
    }

    auto s = std::make_shared<Source>();
    s->sampleRate = sr;
    s->path = file.getFullPathName();
    s->name = file.getFileNameWithoutExtension();
    const int n = end - start;
    s->audio.setSize (2, n);
    s->audio.copyFrom (0, 0, tmp, 0, start, n);
    s->audio.copyFrom (1, 0, tmp, tmp.getNumChannels() > 1 ? 1 : 0, start, n);
    s->audio.applyGain (0.89f / peak);

    if (trimSilence)
    {
        // short fade-in and fade-out so every sound starts and ends cleanly
        const int fin = juce::jmin (n / 4, (int) (0.001 * sr));
        const int fout = juce::jmin (n / 4, (int) (0.006 * sr));
        for (int c = 0; c < 2; ++c)
        {
            if (fin > 1) s->audio.applyGainRamp (c, 0, fin, 0.0f, 1.0f);
            if (fout > 1) s->audio.applyGainRamp (c, n - fout, fout, 1.0f, 0.0f);
        }
    }
    return s;
}

static void describe (Part& p, const Source& s)
{
    const int buckets = 96;
    p.overview.assign ((size_t) buckets, 0.0f);
    const auto* L = s.audio.getReadPointer (0);
    const auto* R = s.audio.getReadPointer (1);
    const int len = juce::jmax (1, p.length());
    long zc = 0; float prev = 0.0f;
    for (int b = 0; b < buckets; ++b)
    {
        const int a = p.start + (int) ((juce::int64) len * b / buckets);
        const int e = juce::jmax (a + 1, p.start + (int) ((juce::int64) len * (b + 1) / buckets));
        float pk = 0.0f;
        for (int i = a; i < e && i < p.end; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            pk = juce::jmax (pk, std::abs (m));
            if ((m > 0.0f) != (prev > 0.0f)) ++zc;
            prev = m;
        }
        p.overview[(size_t) b] = juce::jmin (1.0f, pk);
    }
    p.brightness = juce::jlimit (0.0f, 1.0f, (float) zc / (float) len * 8.0f);
}

static std::vector<int> transients (const Source& s)
{
    const auto* L = s.audio.getReadPointer (0);
    const auto* R = s.audio.getReadPointer (1);
    const int n = s.audio.getNumSamples();
    const int hop = juce::jmax (64, (int) (s.sampleRate * 0.01));
    const int frames = n / hop;
    std::vector<float> env ((size_t) juce::jmax (1, frames), -10.0f);
    float top = -10.0f;
    for (int f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float d = i > 0 ? m - 0.5f * (L[i - 1] + R[i - 1]) : 0.0f;
            sum += (double) m * m + 4.0 * (double) d * d;
        }
        env[(size_t) f] = (float) std::log10 (1.0e-7 + sum / hop);
        top = juce::jmax (top, env[(size_t) f]);
    }
    std::vector<int> out { 0 };
    const int minGap = juce::jmax (1, (int) (0.08 * s.sampleRate / hop));
    int last = 0;
    for (int f = 2; f < frames - 1; ++f)
    {
        const float rise = env[(size_t) f] - juce::jmin (env[(size_t) f - 1], env[(size_t) f - 2]);
        if (rise > 0.45f && env[(size_t) f] > top - 3.0f && env[(size_t) f] >= env[(size_t) f + 1] && f - last >= minGap)
        {
            out.push_back ((f - 1) * hop);
            last = f;
        }
    }
    return out;
}

PoolPtr buildPool (std::vector<SourcePtr> sources, int splitMode, int splitCount)
{
    auto pool = std::make_shared<Pool>();
    sources.erase (std::remove (sources.begin(), sources.end(), nullptr), sources.end());
    if (sources.size() > (size_t) maxParts) sources.resize ((size_t) maxParts);
    pool->sources = sources;
    pool->multiFile = sources.size() > 1;

    if (sources.empty()) return pool;

    if (pool->multiFile)
    {
        for (int i = 0; i < (int) sources.size(); ++i)
        {
            Part p;
            p.source = i;
            p.start = 0;
            p.end = sources[(size_t) i]->audio.getNumSamples();
            p.name = sources[(size_t) i]->name;
            describe (p, *sources[(size_t) i]);
            pool->parts.push_back (p);
        }
        return pool;
    }

    const auto& s = *sources[0];
    const int n = s.audio.getNumSamples();
    std::vector<int> cuts;
    if (splitMode == SplitTransients)
    {
        cuts = transients (s);
        if (cuts.size() < 2)
            splitMode = SplitEqual; // nothing detected: fall back
    }
    if (splitMode == SplitEqual)
    {
        cuts.clear();
        const int count = juce::jlimit (1, maxParts, splitCount);
        for (int i = 0; i < count; ++i) cuts.push_back ((int) ((juce::int64) n * i / count));
    }
    if (cuts.size() > (size_t) maxParts) cuts.resize ((size_t) maxParts);

    for (size_t i = 0; i < cuts.size(); ++i)
    {
        Part p;
        p.source = 0;
        p.start = cuts[i];
        p.end = i + 1 < cuts.size() ? cuts[i + 1] : n;
        if (p.length() < 32) continue;
        p.name = s.name + " " + juce::String ((int) i + 1);
        describe (p, s);
        pool->parts.push_back (p);
    }
    return pool;
}

} // namespace prisma
