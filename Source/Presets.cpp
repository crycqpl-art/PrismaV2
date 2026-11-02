#include "PluginProcessor.h"
#include "Presets.h"
#include <utility>

namespace
{
using Setting = std::pair<const char*, float>;
struct Preset { const char* name; std::vector<Setting> values; };

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Init", {} },
        { "Roulette 1/16", { { "rate", 10 }, { "order", 0 }, { "length", 0.8f } } },
        { "Stutter 1/32", { { "rate", 12 }, { "order", 1 }, { "length", 0.6f }, { "decay", 0.05f } } },
        { "Triplet Ping", { { "rate", 11 }, { "order", 4 }, { "length", 0.9f }, { "wash", 0.25f } } },
        { "Swing Chops", { { "rate", 10 }, { "swing", 0.3f }, { "order", 1 }, { "length", 0.55f },
                           { "crunch", 0.2f }, { "pump", 0.4f } } },
        { "Slow Loops", { { "rate", 7 }, { "mode", 2 }, { "length", 1.0f }, { "order", 2 }, { "attack", 10 }, { "wash", 0.3f } } },
        { "Ping-Pong Haze", { { "rate", 4 }, { "mode", 3 }, { "length", 1.0f }, { "wash", 0.6f },
                              { "highCut", 6000 }, { "release", 800 }, { "volume", 2 } } },
        { "Lo-Fi Crunch", { { "rate", 10 }, { "crunch", 0.5f }, { "highCut", 5000 }, { "lowCut", 120 }, { "wash", 0.2f } } },
        { "Keys Kit", { { "rate", 0 }, { "mode", 0 }, { "order", 5 }, { "choke", 0 } } },
        { "Gate Keys", { { "rate", 0 }, { "mode", 1 }, { "order", 6 }, { "release", 80 } } },
    };
    return p;
}
} // namespace

juce::StringArray prisma::presetNames()
{
    juce::StringArray n;
    for (auto& p : presets()) n.add (p.name);
    return n;
}

void PrismaProcessor::applyPreset (int index)
{
    const auto& all = presets();
    if (index < 0 || index >= (int) all.size()) return;

    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (id == "split" || id == "parts") continue; // keep how the sound is sliced
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    for (auto& [id, v] : all[(size_t) index].values)
        setParam (id, v);
}
