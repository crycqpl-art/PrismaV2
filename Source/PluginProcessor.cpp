#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace prisma;

static const prisma::Pool& emptyPool()
{
    static const prisma::Pool p;
    return p;
}

juce::AudioProcessorValueTreeState::ParameterLayout PrismaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v < 10.0f ? v : (float) roundToInt (v), v < 10.0f ? 1 : 0) + " ms"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto st = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " st"; });
    auto ct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " ct"; });
    auto plain = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });

    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, a)); };
    auto c = [&] (const char* id, const char* name, StringArray items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto b = [&] (const char* id, const char* name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    // play
    c ("rate", "Rate", rateNames(), 10);
    f ("length", "Length", { 0.05f, 1.0f }, 0.85f, pct);
    f ("swing", "Swing", { 0.0f, 0.5f }, 0.0f, pct);
    f ("decay", "Repeat Decay", { 0.0f, 1.0f }, 0.0f, pct);
    b ("gridSync", "Grid Sync", true);
    c ("mode", "Hit Mode", modeNames(), 0);
    b ("choke", "Choke", true);
    // order
    c ("order", "Order", orderNames(), 0);
    c ("steps", "Pattern Steps", stepsNames(), 1);
    b ("locked", "Pattern Lock", false);
    c ("reset", "Pattern Reset", resetNames(), 0);
    f ("reverse", "Reverse", { 0.0f, 1.0f }, 0.0f, pct);
    // slicing (single file)
    c ("split", "Split", splitNames(), 0);
    c ("parts", "Parts", partsNames(), 4);
    // amp
    f ("attack", "Attack", { 0.5f, 2000.0f, 0.0f, 0.3f }, 2.0f, ms);
    f ("decayTime", "Decay", { 5.0f, 4000.0f, 0.0f, 0.3f }, 300.0f, ms);
    f ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f, pct);
    f ("release", "Release", { 5.0f, 5000.0f, 0.0f, 0.3f }, 150.0f, ms);
    // tone
    f ("lowCut", "Low Cut", { 20.0f, 2000.0f, 0.0f, 0.3f }, 20.0f, hz);
    f ("highCut", "High Cut", { 200.0f, 20000.0f, 0.0f, 0.3f }, 20000.0f, hz);
    f ("reso", "Resonance", { 0.5f, 5.0f, 0.0f, 0.5f }, 0.7f, plain);
    // color
    f ("crunch", "Crunch", { 0.0f, 1.0f }, 0.0f, pct);
    f ("wash", "Wash", { 0.0f, 1.0f }, 0.0f, pct);
    f ("pump", "Pump", { 0.0f, 1.0f }, 0.0f, pct);
    // master
    f ("tune", "Tune", { -24.0f, 24.0f, 1.0f }, 0.0f, st);
    f ("fine", "Fine", { -100.0f, 100.0f, 1.0f }, 0.0f, ct);
    f ("volume", "Volume", { -30.0f, 12.0f }, 0.0f, db);
    return l;
}

PrismaProcessor::PrismaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PRISMA", createLayout())
{
    engine.setShared (&shared);
    std::atomic_store (&pool, buildPool ({}, 0, 8));
    apvts.addParameterListener ("split", this);
    apvts.addParameterListener ("parts", this);
}

PrismaProcessor::~PrismaProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("split", this);
    apvts.removeParameterListener ("parts", this);
}

bool PrismaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void PrismaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sr);
    fx.prepare (sr, samplesPerBlock);
}

Params PrismaProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    Params p;
    p.rate = (int) v ("rate");
    p.length = v ("length");
    p.swing = v ("swing");
    p.decay = v ("decay");
    p.gridSync = v ("gridSync") > 0.5f;
    p.mode = (int) v ("mode");
    p.choke = v ("choke") > 0.5f;
    p.order = (int) v ("order");
    p.steps = stepsCount ((int) v ("steps"));
    p.locked = v ("locked") > 0.5f;
    p.resetBars = (int) v ("reset");
    p.reverse = v ("reverse");
    p.attack = v ("attack") * 0.001f;
    p.decayTime = v ("decayTime") * 0.001f;
    p.sustain = v ("sustain");
    p.release = v ("release") * 0.001f;
    p.tune = v ("tune") + v ("fine") * 0.01f;
    return p;
}

void PrismaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    const auto p = readParams();
    const auto poolRef = std::atomic_load (&pool);
    const Pool& pl = poolRef != nullptr ? *poolRef : emptyPool();

    double bpm = 120.0, ppq = internalPpq, barBeats = 4.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0) barBeats = ts->numerator * 4.0 / ts->denominator;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) { ppq = *q; playing = true; }
        }
    const double ppqPerSample = bpm / (60.0 * sr);
    const double interval = rateBeats (p.rate) * 60.0 / bpm * sr;

    engine.beginBlock (playing, ppq, p);

    std::array<int, 128> ticks {};
    const int numTicks = engine.findTicks (ppq, ppqPerSample, n, p, ticks.data(), (int) ticks.size());

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    int cursor = 0, tickIdx = 0;
    auto it = midi.begin();

    auto renderTo = [&] (int end)
    {
        if (end > cursor)
            engine.render (L + cursor, R + cursor, end - cursor, p, pl, interval, ppq + cursor * ppqPerSample, ppqPerSample, barBeats);
        cursor = juce::jmax (cursor, end);
    };

    while (true)
    {
        const int nextTick = tickIdx < numTicks ? ticks[(size_t) tickIdx] : n;
        const int nextMidi = it != midi.end() ? juce::jlimit (0, n - 1, (*it).samplePosition) : n;
        const int next = juce::jmin (nextTick, nextMidi);
        if (next >= n) break;
        renderTo (next);
        const double ppqNow = ppq + next * ppqPerSample;
        if (nextTick <= nextMidi)
        {
            engine.gridTick (p, pl, interval, ppqNow, barBeats);
            ++tickIdx;
        }
        else
        {
            const auto msg = (*it).getMessage();
            if (msg.isNoteOn()) engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), p, pl, interval, ppqNow, barBeats);
            else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber(), p);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allNotesOff();
            ++it;
        }
    }
    renderTo (n);

    FX::Settings s;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    s.lowCut = v ("lowCut"); s.highCut = v ("highCut"); s.reso = v ("reso");
    s.crunch = v ("crunch"); s.wash = v ("wash"); s.pump = v ("pump"); s.volumeDb = v ("volume");
    fx.process (L, R, n, s, ppq, ppqPerSample);

    engine.endBlock (ppq + n * ppqPerSample);
    outputLevel.store (buffer.getMagnitude (0, n));
    internalPpq = ppq + n * ppqPerSample;
}

//==============================================================================
void PrismaProcessor::setPool (PoolPtr p)
{
    auto old = std::atomic_exchange (&pool, p);
    const juce::ScopedLock sl (retiredLock);
    if (old != nullptr) retired.push_back (old);
}

void PrismaProcessor::releaseRetired()
{
    const juce::ScopedLock sl (retiredLock);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const PoolPtr& p) { return p.use_count() == 1; }),
                   retired.end());
}

void PrismaProcessor::rebuildPool()
{
    setPool (buildPool (sources, (int) getParam ("split"), partsCount ((int) getParam ("parts"))));
}

void PrismaProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

juce::String PrismaProcessor::loadFiles (const juce::Array<juce::File>& files)
{
    std::vector<SourcePtr> loaded;
    juce::StringArray failed;
    const bool several = files.size() > 1;
    for (auto& f : files)
    {
        if ((int) loaded.size() >= maxParts) break;
        juce::String err;
        auto s = loadSource (f, several, several ? 30.0 : 120.0, err);
        if (s != nullptr) loaded.push_back (s);
        else failed.add (f.getFileName() + " (" + err + ")");
    }
    if (loaded.empty())
        return failed.isEmpty() ? juce::String ("No audio files") : "Could not load " + failed.joinIntoString (", ");

    sources = loaded;
    missingFiles.clear();
    rebuildPool();
    storeLists();

    juce::String msg = loaded.size() == 1 ? "Loaded " + loaded[0]->name + ", sliced into "
                                              + juce::String ((int) getPool()->parts.size()) + " parts"
                                          : "Loaded " + juce::String ((int) loaded.size()) + " sounds";
    if (! failed.isEmpty()) msg << ", skipped " << failed.size();
    if (files.size() > maxParts) msg << " (max " << maxParts << ")";
    return msg;
}

void PrismaProcessor::removeSound (int index)
{
    if (index < 0 || index >= (int) sources.size()) return;
    sources.erase (sources.begin() + index);
    rebuildPool();
    storeLists();
}

void PrismaProcessor::clearSounds()
{
    sources.clear();
    missingFiles.clear();
    rebuildPool();
    storeLists();
}

void PrismaProcessor::storeLists()
{
    juce::StringArray paths;
    for (auto& s : sources) paths.add (s->path);
    apvts.state.setProperty ("files", paths.joinIntoString ("\n"), nullptr);
    juce::StringArray pat, keys;
    for (int i = 0; i < maxPattern; ++i)
    {
        pat.add (juce::String (shared.pattern[(size_t) i].load()));
        keys.add (juce::String (shared.keyPerm[(size_t) i].load()));
    }
    apvts.state.setProperty ("pattern", pat.joinIntoString (","), nullptr);
    apvts.state.setProperty ("keyPerm", keys.joinIntoString (","), nullptr);
}

//==============================================================================
int PrismaProcessor::capture()
{
    const int steps = stepsCount ((int) getParam ("steps"));
    const int w = shared.histWrite.load (std::memory_order_acquire);
    const int count = juce::jmin (steps, w, maxPattern);
    if (count == 0) return 0;
    for (int k = w - count; k < w; ++k)
    {
        const auto i = (size_t) (k % maxPattern);
        const int step = shared.histStep[i].load();
        shared.pattern[(size_t) (step % steps)].store (shared.histPart[i].load());
    }
    setParam ("locked", 1.0f);
    storeLists();
    return count;
}

void PrismaProcessor::dicePattern()
{
    const int n = juce::jmax (1, (int) getPool()->parts.size());
    int last = -1;
    for (int i = 0; i < maxPattern; ++i)
    {
        int v = uiRandom.nextInt (n);
        if (n > 1 && v == last) v = (v + 1 + uiRandom.nextInt (n - 1)) % n;
        shared.pattern[(size_t) i].store (v);
        last = v;
    }
    setParam ("locked", 1.0f);
    storeLists();
}

void PrismaProcessor::shuffleKeys()
{
    std::array<int, maxPattern> perm {};
    for (int i = 0; i < maxPattern; ++i) perm[(size_t) i] = i;
    for (int i = maxPattern - 1; i > 0; --i) std::swap (perm[(size_t) i], perm[(size_t) uiRandom.nextInt (i + 1)]);
    for (int i = 0; i < maxPattern; ++i) shared.keyPerm[(size_t) i].store (perm[(size_t) i]);
    storeLists();
}

void PrismaProcessor::setPatternCell (int index, int value)
{
    if (index < 0 || index >= maxPattern) return;
    shared.pattern[(size_t) index].store (juce::jmax (0, value));
    storeLists();
}

//==============================================================================
void PrismaProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
        prm->endChangeGesture();
    }
}

float PrismaProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void PrismaProcessor::dice()
{
    auto& r = uiRandom;
    const int rates[] = { 7, 8, 9, 10, 10, 10, 11, 12, 12 };
    setParam ("rate", (float) rates[r.nextInt (9)]);
    setParam ("length", 0.25f + 0.75f * r.nextFloat());
    setParam ("swing", r.nextFloat() < 0.6f ? 0.0f : 0.25f * r.nextFloat());
    setParam ("decay", r.nextFloat() < 0.7f ? 0.0f : 0.3f * r.nextFloat());
    setParam ("mode", (float) r.nextInt (4));
    setParam ("order", (float) (r.nextFloat() < 0.6f ? r.nextInt (2) : r.nextInt (5)));
    setParam ("reverse", r.nextFloat() < 0.5f ? 0.0f : 0.4f * r.nextFloat());
    setParam ("locked", 0.0f);
}

juce::String PrismaProcessor::settingsToText() const
{
    juce::String t;
    t << "PRISMA " << PRISMA_VERSION << " settings\n";
    auto pl = getPool();
    t << "# sounds: " << (int) pl->sources.size() << ", parts: " << (int) pl->parts.size() << "\n";
    for (auto& s : pl->sources) t << "#   " << s->name << "\n";
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            t << rp->getParameterID() << "=" << juce::String (rp->convertFrom0to1 (rp->getValue()), 4)
              << "  # " << rp->getCurrentValueAsText() << "\n";
    juce::StringArray pat;
    for (int i = 0; i < maxPattern; ++i) pat.add (juce::String (shared.pattern[(size_t) i].load()));
    t << "pattern=" << pat.joinIntoString (",") << "\n";
    return t;
}

int PrismaProcessor::settingsFromText (const juce::String& text)
{
    int applied = 0;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        if (! line.containsChar ('=')) continue;
        const auto id = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto val = line.fromFirstOccurrenceOf ("=", false, false).trim();
        if (id == "pattern")
        {
            auto items = juce::StringArray::fromTokens (val, ",", "");
            for (int i = 0; i < juce::jmin (maxPattern, items.size()); ++i)
                shared.pattern[(size_t) i].store (juce::jmax (0, items[i].getIntValue()));
            storeLists();
            ++applied;
        }
        else if (apvts.getParameter (id) != nullptr && val.isNotEmpty())
        {
            setParam (id, val.getFloatValue());
            ++applied;
        }
    }
    return applied;
}

//==============================================================================
void PrismaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::StringArray pat, keys;
    for (int i = 0; i < maxPattern; ++i)
    {
        pat.add (juce::String (shared.pattern[(size_t) i].load()));
        keys.add (juce::String (shared.keyPerm[(size_t) i].load()));
    }
    state.setProperty ("pattern", pat.joinIntoString (","), nullptr);
    state.setProperty ("keyPerm", keys.joinIntoString (","), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrismaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    auto readList = [] (const juce::String& s, std::array<std::atomic<int>, maxPattern>& dst)
    {
        auto items = juce::StringArray::fromTokens (s, ",", "");
        for (int i = 0; i < juce::jmin (maxPattern, items.size()); ++i) dst[(size_t) i].store (juce::jmax (0, items[i].getIntValue()));
    };
    readList (apvts.state.getProperty ("pattern").toString(), shared.pattern);
    readList (apvts.state.getProperty ("keyPerm").toString(), shared.keyPerm);

    juce::Array<juce::File> files;
    missingFiles.clear();
    for (auto& path : juce::StringArray::fromLines (apvts.state.getProperty ("files").toString()))
    {
        if (path.isEmpty()) continue;
        juce::File f (path);
        if (f.existsAsFile()) files.add (f); else missingFiles.add (f.getFileName());
    }
    const auto missing = missingFiles;
    if (! files.isEmpty())
    {
        std::vector<SourcePtr> loaded;
        const bool several = apvts.state.getProperty ("files").toString().containsChar ('\n');
        for (auto& f : files)
        {
            juce::String err;
            if (auto s = loadSource (f, several, several ? 30.0 : 120.0, err)) loaded.push_back (s);
        }
        sources = loaded;
        rebuildPool();
    }
    else
    {
        sources.clear();
        rebuildPool();
    }
    missingFiles = missing;
}

juce::AudioProcessorEditor* PrismaProcessor::createEditor()
{
    return new PrismaEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismaProcessor();
}
