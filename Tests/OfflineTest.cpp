// Offline tests for Prisma 0.2
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Presets.h"
#include <set>
using namespace prisma;

static constexpr double SR = 48000.0;
static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr = SR)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

// six different timbres of the note C4, with some silence in front
static juce::AudioBuffer<float> makeSound (int kind, double sr, double silence)
{
    const double f = 261.63, dur = 0.45 + 0.1 * kind;
    const int pre = (int) (silence * sr), n = pre + (int) (dur * sr);
    juce::AudioBuffer<float> b (1, n);
    b.clear();
    juce::Random r (kind + 1);
    double ph = 0.0;
    for (int i = 0; i < n - pre; ++i)
    {
        const double t = i / sr;
        ph += f / sr;
        const double tw = juce::MathConstants<double>::twoPi;
        double s = 0.0;
        switch (kind)
        {
            case 0: s = std::sin (tw * ph) * std::exp (-t * 3.0); break;                                     // sine pluck
            case 1: s = (2.0 * std::fmod (ph, 1.0) - 1.0) * 0.6 * std::exp (-t * 2.0); break;               // saw
            case 2: s = (std::fmod (ph, 1.0) < 0.5 ? 0.5 : -0.5) * std::exp (-t * 4.0); break;              // square
            case 3: s = std::sin (tw * ph + 3.0 * std::exp (-t * 6.0) * std::sin (tw * ph * 3.5)) * std::exp (-t * 2.5); break; // FM bell
            case 4: s = (std::sin (tw * ph) * 0.5 + (r.nextFloat() - 0.5) * 0.6 * std::exp (-t * 30.0)) * std::exp (-t * 5.0); break;
            default:
            {
                double v = 0.0;
                for (int h = 1; h < 10; ++h) v += std::sin (tw * ph * h) * std::exp (-std::pow ((f * h - 800.0) / 400.0, 2)) / h;
                s = v * std::min (1.0, t * 20.0) * std::exp (-t * 1.2);
            }
        }
        b.setSample (0, pre + i, (float) s);
    }
    return b;
}

struct FakePlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

using Events = std::vector<std::tuple<double, int, bool>>;
struct Stats { float peak = 0, rms = 0; bool finite = true; int hits = 0; double cpu = 0; };

static Stats render (PrismaProcessor& p, const Events& ev, double seconds, juce::AudioBuffer<float>& out,
                     bool randomBlocks = false, FakePlayHead* head = nullptr)
{
    p.setPlayHead (head);
    p.setPlayConfigDetails (0, 2, SR, 1024);
    p.prepareToPlay (SR, 1024);
    if (head != nullptr) head->ppq = 0.0;
    const int total = (int) (seconds * SR);
    out.setSize (2, total);
    out.clear();
    juce::Random r (3);
    const int hits0 = p.shared.logWrite.load();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int pos = 0;
    while (pos < total)
    {
        const int bs = juce::jmin (randomBlocks ? 1 + r.nextInt (1024) : 512, total - pos);
        juce::AudioBuffer<float> block (2, bs);
        juce::MidiBuffer midi;
        for (auto& [t, note, on] : ev)
        {
            const int s = (int) std::llround (t * SR);
            if (s >= pos && s < pos + bs)
                midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), s - pos);
        }
        p.processBlock (block, midi);
        if (head != nullptr) head->ppq += bs * head->bpm / (60.0 * SR);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, bs);
        pos += bs;
    }
    Stats st;
    st.cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / seconds;
    st.hits = p.shared.logWrite.load() - hits0;
    double sum = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (c, i);
            if (! std::isfinite (v)) st.finite = false;
            st.peak = std::max (st.peak, std::abs (v));
            sum += v * v;
        }
    st.rms = (float) std::sqrt (sum / (2.0 * total));
    p.setPlayHead (nullptr);
    return st;
}

static double maxSecondDiff (const juce::AudioBuffer<float>& b, int from, int to)
{
    double m = 0;
    for (int i = from + 1; i < juce::jmin (to, b.getNumSamples()) - 1; ++i)
        m = std::max (m, (double) std::abs (b.getSample (0, i + 1) - 2 * b.getSample (0, i) + b.getSample (0, i - 1)));
    return m;
}

static bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (std::abs (a.getSample (c, i) - b.getSample (c, i)) > 1.0e-6f) return false;
    return true;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    auto dir = outDir.getChildFile ("sounds");
    dir.createDirectory();

    juce::Array<juce::File> six;
    juce::AudioBuffer<float> joined (1, 0);
    for (int k = 0; k < 6; ++k)
    {
        auto f = dir.getChildFile ("c_sound_" + juce::String (k + 1) + ".wav");
        writeWav (f, makeSound (k, k == 2 ? 44100.0 : SR, 0.05 + 0.04 * k), k == 2 ? 44100.0 : SR);
        six.add (f);
        auto s = makeSound (k, SR, 0.0);
        const int old = joined.getNumSamples();
        joined.setSize (1, old + 24000, true, true);   // exactly 0.5 s per sound
        for (int i = 0; i < 24000; ++i) joined.setSample (0, old + i, i < s.getNumSamples() ? s.getSample (0, i) : 0.0f);
    }
    auto single = dir.getChildFile ("joined.wav");
    writeWav (single, joined);
    juce::AudioBuffer<float> sine (1, (int) (2.0 * SR));
    for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, (float) (0.8 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / SR)));
    auto sineFile = dir.getChildFile ("sine.wav");
    writeWav (sineFile, sine);

    if (std::getenv ("PRISMA_SNAPSHOT") != nullptr)
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.applyPreset (1);
        p.setParam ("locked", 1.0f);
        if (std::getenv ("PRISMA_SNAPSHOT")[0] == '2') { p.clearSounds(); }
        FakePlayHead head;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* pe = dynamic_cast<PrismaEditor*> (ed.get());
        if (std::getenv ("PRISMA_TAB") != nullptr) pe->showTab (1);
        juce::AudioBuffer<float> out;
        // play a little and let the display animate between blocks
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, SR, 512);
        p.prepareToPlay (SR, 512);
        for (int k = 0; k < 60; ++k)
        {
            juce::AudioBuffer<float> blk (2, 512);
            juce::MidiBuffer m;
            if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            p.processBlock (blk, m);
            head.ppq += 512 * 120.0 / (60.0 * SR);
            pe->animate();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile (std::getenv ("PRISMA_TAB") != nullptr ? "ui_order.png" : "ui.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::cout << "snapshot written" << std::endl;
        p.setPlayHead (nullptr);
        return 0;
    }

    // ---- loading
    {
        PrismaProcessor p;
        const auto msg = p.loadFiles (six);
        auto pool = p.getPool();
        std::cout << "Load message: " << msg << std::endl;
        check (pool->parts.size() == 6 && pool->multiFile, "six files become six parts");
        bool trimmed = true;
        for (auto& s : pool->sources)
        {
            const auto* L = s->audio.getReadPointer (0);
            int first = 0;
            while (first < s->audio.getNumSamples() && std::abs (L[first]) < 0.05f) ++first;
            if (first > (int) (0.004 * s->sampleRate)) trimmed = false;
        }
        check (trimmed, "silence in front of each sound is trimmed (hit starts within 4 ms)");

        PrismaProcessor q;
        q.setParam ("parts", 3); // index 3 = 6 parts
        q.loadFiles ({ single });
        check (q.getPool()->parts.size() == 6 && ! q.getPool()->multiFile, "one file sliced into 6 equal parts");
        bool equal = true;
        for (auto& part : q.getPool()->parts) equal &= std::abs (part.length() - 24000) <= 1;
        check (equal, "equal parts have equal length");
        q.setParam ("split", 1);
        q.rebuildPool();
        std::cout << "Transient split found " << q.getPool()->parts.size() << " parts (expect ~6)" << std::endl;
        check (q.getPool()->parts.size() >= 4 && q.getPool()->parts.size() <= 10, "transient split");
    }

    // ---- silence when empty
    {
        PrismaProcessor p;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, 60, true } }, 0.5, out);
        check (s.finite && s.peak < 1.0e-5f && s.hits == 0, "no sounds loaded: silent, no crash");
    }

    // ---- rate off, one-shot: a single hit that ends by itself
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.setParam ("rate", 0);
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, 60, true }, { 0.05, 60, false } }, 2.0, out);
        float tail = 0.0f;
        for (int i = (int) (1.6 * SR); i < out.getNumSamples(); ++i) tail = std::max (tail, std::abs (out.getSample (0, i)));
        check (s.hits == 1 && s.rms > 0.003f, "rate OFF one-shot plays exactly one hit (" + juce::String (s.hits) + ")");
        check (tail < 1.0e-3f, "one-shot ends by itself");
    }

    // ---- rate 1/16 held for 2 s at 120 bpm: 16 hits, random parts
    {
        PrismaProcessor p;
        p.loadFiles (six);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, 60, true }, { 1.999, 60, false } }, 2.5, out, false, &head);
        check (s.hits == 16, "1/16 for 2 s at 120 BPM = 16 hits (got " + juce::String (s.hits) + ")");
        std::set<int> used;
        const int w = p.shared.histWrite.load();
        for (int k = juce::jmax (0, w - 16); k < w; ++k) used.insert (p.shared.histPart[(size_t) (k % 32)].load());
        check (used.size() >= 4, "random order uses many different sounds (" + juce::String ((int) used.size()) + ")");
        writeWav (outDir.getChildFile ("01_random_1-16.wav"), out);
    }

    // ---- key sync and swing
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.setParam ("gridSync", 0.0f);
        p.setParam ("swing", 0.3f);
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.013, 60, true }, { 1.013, 60, false } }, 1.3, out);
        check (s.hits >= 7 && s.hits <= 9, "key-synced 1/16 with swing for 1 s = ~8 hits (" + juce::String (s.hits) + ")");
    }

    // ---- locked pattern is identical on every play
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.dicePattern();
        p.setParam ("reverse", 0.0f);
        FakePlayHead head;
        Events ev { { 0.0, 60, true }, { 3.9, 60, false } };
        juce::AudioBuffer<float> a, b;
        auto pr = [&p] (const char* tag)
        {
            std::cout << "    " << tag << ":";
            const int w = p.shared.histWrite.load();
            for (int k = juce::jmax (0, w - 32); k < w; ++k) std::cout << " " << p.shared.histStep[(size_t) (k % 32)].load() << ":" << p.shared.histPart[(size_t) (k % 32)].load();
            std::cout << std::endl;
        };
        const auto sa = render (p, ev, 4.0, a, false, &head);
        if (std::getenv ("PRISMA_DEBUG")) pr ("fixed ");
        const auto sb = render (p, ev, 4.0, b, true, &head);
        if (std::getenv ("PRISMA_DEBUG")) { writeWav (outDir.getChildFile ("dbg_a.wav"), a); writeWav (outDir.getChildFile ("dbg_b.wav"), b); pr ("random"); std::cout << "    hits " << sa.hits << " vs " << sb.hits << std::endl; }
        if (! identical (a, b))
        {
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (std::abs (a.getSample (0, i) - b.getSample (0, i)) > 1.0e-6f)
                { std::cout << "    first difference at sample " << i << " (" << i / SR << " s): " << a.getSample (0, i) << " vs " << b.getSample (0, i) << std::endl; break; }
            juce::AudioBuffer<float> c2;
            render (p, ev, 4.0, c2, false, &head);
            std::cout << "    same block size again identical: " << (identical (a, c2) ? "yes" : "no") << std::endl;
        }
        check (identical (a, b), "locked pattern renders identically twice (different block sizes)");
        bool follows = true;
        const int w = p.shared.histWrite.load();
        for (int k = juce::jmax (0, w - 16); k < w; ++k)
        {
            const auto i = (size_t) (k % 32);
            if (p.shared.histPart[i].load() != p.getPatternCell (p.shared.histStep[i].load() % 8) % 6) follows = false;
        }
        check (follows, "locked hits follow the pattern cells");
        writeWav (outDir.getChildFile ("02_locked_pattern.wav"), a);
    }

    // ---- capture freezes what was heard
    {
        PrismaProcessor p;
        p.loadFiles (six);
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        render (p, { { 0.0, 60, true }, { 1.0, 60, false } }, 1.1, out, false, &head);
        std::array<int, 8> heard {};
        const int w = p.shared.histWrite.load();
        for (int k = w - 8; k < w; ++k)
            heard[(size_t) (p.shared.histStep[(size_t) (k % 32)].load() % 8)] = p.shared.histPart[(size_t) (k % 32)].load();
        const int got = p.capture();
        bool same = got == 8 && p.getParam ("locked") > 0.5f;
        for (int s = 0; s < 8; ++s) same &= p.getPatternCell (s) == heard[(size_t) s];
        check (same, "capture copies the last 8 hits into the pattern and locks it");
    }

    // ---- bar reset restarts the cycle
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.setParam ("order", 2);   // cycle
        p.setParam ("reset", 1);   // every bar
        FakePlayHead head;
        juce::AudioBuffer<float> out;
        render (p, { { 0.0, 60, true }, { 3.99, 60, false } }, 4.0, out, false, &head);
        // at 120 BPM a bar is 2 s = 16 hits; the hit at the start of bar 2 must be part 0 again
        bool ok = false;
        const int w = p.shared.histWrite.load();
        for (int k = juce::jmax (0, w - 32); k < w; ++k)
        {
            const auto i = (size_t) (k % 32);
            if (k == w - 16) ok = p.shared.histStep[i].load() == 0 && p.shared.histPart[i].load() == 0;
        }
        check (ok, "reset every bar restarts the cycle at sound 1");
    }

    // ---- keys and random keys
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.setParam ("rate", 0);
        p.setParam ("order", 5);
        p.setParam ("choke", 0.0f);
        Events ev;
        for (int k = 0; k < 6; ++k) { ev.push_back ({ k * 0.3, 36 + k, true }); ev.push_back ({ k * 0.3 + 0.2, 36 + k, false }); }
        juce::AudioBuffer<float> out;
        render (p, ev, 2.2, out);
        bool ok = true;
        const int w = p.shared.logWrite.load();
        for (int k = 0; k < 6; ++k) ok &= p.shared.logPart[(size_t) ((w - 6 + k) % SharedState::logSize)].load() == k;
        check (ok, "Keys: C1..F1 play sounds 1..6");

        p.setParam ("order", 6);
        p.shuffleKeys();
        render (p, ev, 2.2, out);
        std::set<int> parts;
        const int w2 = p.shared.logWrite.load();
        for (int k = 0; k < 6; ++k) parts.insert (p.shared.logPart[(size_t) ((w2 - 6 + k) % SharedState::logSize)].load());
        check (parts.size() == 6, "Rand Keys: six keys still reach all six sounds");
        writeWav (outDir.getChildFile ("03_keys.wav"), out);
    }

    // ---- every mode and order, clicks on a sine
    {
        for (int mode = 0; mode < 4; ++mode)
        {
            PrismaProcessor p;
            p.loadFiles ({ sineFile });
            p.setParam ("parts", 4); // 8 parts
            p.rebuildPool();
            p.setParam ("mode", (float) mode);
            p.setParam ("rate", 10);
            p.setParam ("length", 1.0f);
            FakePlayHead head;
            juce::AudioBuffer<float> out;
            const auto s = render (p, { { 0.0, 60, true }, { 2.0, 60, false } }, 2.4, out, false, &head);
            const double d2 = maxSecondDiff (out, 200, (int) (1.95 * SR));
            check (s.finite && d2 < 0.02, modeNames()[mode] + ": no clicks on a chopped sine (max 2nd diff " + juce::String (d2, 4) + ")");
        }
        PrismaProcessor p;
        p.loadFiles ({ sineFile });
        p.setParam ("rate", 0);
        p.setParam ("mode", 2);   // loop, held
        p.setParam ("parts", 0);  // 2 parts
        p.rebuildPool();
        juce::AudioBuffer<float> out;
        const auto s = render (p, { { 0.0, 60, true }, { 3.0, 60, false } }, 3.4, out);
        const double d2 = maxSecondDiff (out, 200, (int) (2.9 * SR));
        check (s.finite && s.rms > 0.05f && d2 < 0.02, "held Loop keeps sounding without clicks (" + juce::String (d2, 4) + ")");
        p.setParam ("mode", 3);
        const auto s2 = render (p, { { 0.0, 60, true }, { 3.0, 60, false } }, 3.4, out);
        check (s2.finite && s2.rms > 0.05f && maxSecondDiff (out, 200, (int) (2.9 * SR)) < 0.02, "held Ping-Pong keeps sounding without clicks");
    }

    // ---- presets, dice, random blocks, polyphony
    {
        const auto names = prisma::presetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            PrismaProcessor p;
            p.loadFiles (six);
            p.applyPreset (i);
            p.setParam ("reverse", i % 2 ? 0.3f : 0.0f);
            FakePlayHead head;
            Events ev { { 0.0, 60, true }, { 1.5, 60, false }, { 1.6, 40, true }, { 1.6, 43, true }, { 3.0, 40, false }, { 3.0, 43, false } };
            juce::AudioBuffer<float> out;
            const auto s = render (p, ev, 4.0, out, i % 2 == 1, &head);
            check (s.finite && s.peak <= 1.0f && s.rms > 0.003f,
                   "preset " + names[i] + " (peak " + juce::String (s.peak, 2) + ", rms " + juce::String (s.rms, 3)
                       + ", hits " + juce::String (s.hits) + ", CPU " + juce::String (s.cpu, 1) + " ms/s)");
            writeWav (outDir.getChildFile ("p" + juce::String (i).paddedLeft ('0', 2) + "_" + names[i].replaceCharacters (" /", "__") + ".wav"), out);
        }

        PrismaProcessor p;
        p.loadFiles (six);
        juce::AudioBuffer<float> out;
        bool ok = true;
        for (int k = 0; k < 10; ++k)
        {
            p.dice();
            p.setParam ("crunch", (float) (k % 3) / 2.0f);
            p.setParam ("wash", (float) ((k + 1) % 3) / 2.0f);
            p.setParam ("pump", (float) ((k + 2) % 3) / 2.0f);
            p.setParam ("lowCut", 20.0f + 100.0f * (float) k);
            p.setParam ("highCut", 20000.0f - 1800.0f * (float) k);
            p.setParam ("reso", 0.7f + 0.4f * (float) k);
            p.setParam ("tune", (float) (k - 5) * 3.0f);
            Events ev;
            for (int c = 0; c < 8; ++c) { ev.push_back ({ 0.1 * c, 48 + c * 2, true }); ev.push_back ({ 1.5 + 0.05 * c, 48 + c * 2, false }); }
            const auto s = render (p, ev, 3.0, out, true);
            ok &= s.finite && s.peak <= 1.0f;
            if (! ok) std::cout << "    dice run " << k << " failed: peak " << s.peak << std::endl;
        }
        check (ok, "10 random settings with 8-note chords, random block sizes");
    }

    // ---- state, settings text
    {
        PrismaProcessor a, b;
        a.loadFiles (six);
        a.applyPreset (4);
        a.dicePattern();
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (rp->getValue() - b.apvts.getParameter (rp->getParameterID())->getValue()) < 1e-5f;
        for (int i = 0; i < 32; ++i) same &= a.getPatternCell (i) == b.getPatternCell (i);
        check (same, "parameters and pattern restored");
        check (b.getPool()->parts.size() == 6, "sounds reloaded from saved file paths");

        PrismaProcessor c;
        const int k = c.settingsFromText (a.settingsToText());
        bool same2 = true;
        for (auto* prm : a.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same2 &= std::abs (rp->getValue() - c.apvts.getParameter (rp->getParameterID())->getValue()) < 1e-3f;
        for (int i = 0; i < 32; ++i) same2 &= a.getPatternCell (i) == c.getPatternCell (i);
        check (k > 25 && same2, "copy / paste settings round trip (" + juce::String (k) + " values)");
    }

    // ---- swapping sounds while playing
    {
        PrismaProcessor p;
        p.loadFiles (six);
        p.setPlayConfigDetails (0, 2, SR, 256);
        p.prepareToPlay (SR, 256);
        bool ok = true;
        for (int k = 0; k < 600; ++k)
        {
            juce::AudioBuffer<float> blk (2, 256);
            juce::MidiBuffer m;
            if (k % 100 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60 + k / 100, (juce::uint8) 90), 0);
            p.processBlock (blk, m);
            if (k % 60 == 30) p.loadFiles (k % 120 == 30 ? juce::Array<juce::File> { single } : six);
            if (k % 60 == 50) p.removeSound (0);
            if (k % 200 == 199) p.clearSounds();
            if (k % 7 == 0) { p.setParam ("parts", (float) (k % 9)); p.rebuildPool(); }
            p.releaseRetired();
            for (int i = 0; i < 256; ++i) ok &= std::isfinite (blk.getSample (0, i));
        }
        check (ok, "loading, removing and re-slicing sounds while playing");
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
