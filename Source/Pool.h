#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace prisma
{

// One loaded audio file, already trimmed and normalised. Always 2 channels.
struct Source
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String path, name;
};

// A playable piece of a source: [start, end) in source samples.
struct Part
{
    int source = 0;
    int start = 0, end = 0;
    juce::String name;
    std::vector<float> overview;   // peaks for drawing, 0..1
    float brightness = 0.5f;       // rough high-frequency share 0..1
    int length() const { return end - start; }
};

enum SplitMode { SplitEqual = 0, SplitTransients };

struct Pool
{
    std::vector<std::shared_ptr<const Source>> sources;
    std::vector<Part> parts;
    bool multiFile = false;
    juce::StringArray paths() const
    {
        juce::StringArray p;
        for (auto& s : sources) p.add (s->path);
        return p;
    }
};

using PoolPtr = std::shared_ptr<const Pool>;
using SourcePtr = std::shared_ptr<const Source>;

constexpr int maxParts = 32;

// Reads, trims leading/trailing silence, normalises. Message thread.
SourcePtr loadSource (const juce::File& file, bool trimSilence, double maxSeconds, juce::String& error);

// Builds the part list. One source: split it. Several: one part per source.
PoolPtr buildPool (std::vector<SourcePtr> sources, int splitMode, int splitCount);

} // namespace prisma
