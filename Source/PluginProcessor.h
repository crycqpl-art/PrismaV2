#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

class PrismaProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    PrismaProcessor();
    ~PrismaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prisma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- sounds (message thread)
    juce::String loadFiles (const juce::Array<juce::File>& files);   // returns a status line
    void removeSound (int index);
    void clearSounds();
    void rebuildPool();
    prisma::PoolPtr getPool() const { return std::atomic_load (&pool); }
    juce::StringArray missingFiles;

    // ---- order / pattern (message thread)
    int capture();                 // returns how many steps were captured
    void dicePattern();
    void shuffleKeys();
    void setPatternCell (int index, int value);
    int getPatternCell (int index) const { return shared.pattern[(size_t) index].load(); }

    // ---- misc UI helpers
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const;
    void dice();
    void applyPreset (int index);
    juce::String settingsToText() const;
    int settingsFromText (const juce::String& text);
    void releaseRetired();

    juce::AudioProcessorValueTreeState apvts;
    prisma::SharedState shared;
    std::atomic<float> outputLevel { 0.0f };

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override { rebuildPool(); }
    prisma::Params readParams() const;
    void setPool (prisma::PoolPtr p);
    void storeLists();

    prisma::PoolPtr pool;
    std::vector<prisma::SourcePtr> sources;          // message thread only
    std::vector<prisma::PoolPtr> retired;
    juce::CriticalSection retiredLock;

    prisma::Engine engine;
    prisma::FX fx;
    double sr = 44100.0, internalPpq = 0.0;
    juce::Random uiRandom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismaProcessor)
};
