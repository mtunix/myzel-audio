#include "plugin/SeedState.h"

#include "dsp/common/SeedCode.h"

#include <random>

namespace inkcap
{

namespace
{
    juce::String codeOf (uint32_t s)
    {
        return dsp::seedcode::format (s);
    }
}

SeedState::SeedState (myzel::plugin::PresetHost& h)
    : host (h),
      seed (dsp::seedcode::defaultSeed)
{
    auto state = root();
    if (! parseProperty (state.getProperty (seedstate::seed)).has_value())
    {
        state.setProperty (seedstate::seed, codeOf (initial().seed), nullptr);
        state.setProperty (seedstate::seedLabel, initial().label, nullptr);
    }

    // Held by the APVTS tree's own handle: a restored session redirects it, and this follows.
    host.getState().state.addListener (this);
    sync();
}

SeedState::~SeedState()
{
    host.getState().state.removeListener (this);
}

juce::ValueTree SeedState::root() const
{
    return host.getState().state;
}

SeedState::Entry SeedState::initial()
{
    return { dsp::seedcode::defaultSeed, juce::String (std::string (dsp::seedcode::defaultWord)) };
}

std::optional<uint32_t> SeedState::parseProperty (const juce::var& property)
{
    if (! property.isString())
        return std::nullopt;

    return dsp::seedcode::parseCode (property.toString().toStdString());
}

void SeedState::repair (juce::ValueTree& savedState)
{
    if (parseProperty (savedState.getProperty (seedstate::seed)).has_value())
        return;

    savedState.setProperty (seedstate::seed, codeOf (initial().seed), nullptr);
    savedState.setProperty (seedstate::seedLabel, initial().label, nullptr);
}

void SeedState::repairAfterPreset()
{
    auto state = root();
    if (parseProperty (state.getProperty (seedstate::seed)).has_value())
        return;

    state.setProperty (seedstate::seed, codeOf (initial().seed), &host.getUndoManager());
    state.setProperty (seedstate::seedLabel, juce::String(), &host.getUndoManager());
}

std::vector<myzel::plugin::PresetStateProperty> SeedState::presetProperties()
{
    // A preset without a seed (none of ours) loads the fresh instance's, without its word.
    return { { seedstate::seed, codeOf (initial().seed), seedstate::seedLock },
             { seedstate::seedLabel, juce::String(), seedstate::seedLock } };
}

juce::String SeedState::code() const
{
    return codeOf (current());
}

juce::String SeedState::label() const
{
    return root().getProperty (seedstate::seedLabel).toString();
}

bool SeedState::set (uint32_t newSeed, const juce::String& newLabel, const juce::String& undoName)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());

    const Entry previous { current(), label() };
    if (previous.seed == newSeed && previous.label == newLabel)
        return false;

    auto state = root();
    auto& undo = host.getUndoManager();
    undo.beginNewTransaction (undoName);
    state.setProperty (seedstate::seed, codeOf (newSeed), &undo);
    state.setProperty (seedstate::seedLabel, newLabel, &undo);

    remember (previous);
    remember ({ newSeed, newLabel });
    return true;
}

SeedState::Paste SeedState::paste (const juce::String& text)
{
    const auto parsed = dsp::seedcode::parse (text.toStdString());
    if (! parsed.has_value())
        return Paste::nothing;

    const auto label = juce::String::fromUTF8 (parsed->label.data(), (int) parsed->label.size());
    const auto name = label.isEmpty() ? "Seed " + codeOf (parsed->seed) : "Seed \"" + label + "\"";
    set (parsed->seed, label, name);
    return label.isEmpty() ? Paste::code : Paste::word;
}

void SeedState::reroll()
{
    std::random_device entropy;
    auto next = current();

    while (next == current())
        next = (uint32_t) entropy();

    set (next, {}, "Reroll seed");
}

std::vector<SeedState::Entry> SeedState::history() const
{
    std::vector<Entry> entries;
    const auto list = host.getUiState().getChildWithName (seedstate::history);

    for (const auto& e : list)
        if (const auto s = parseProperty (e.getProperty (seedstate::code)))
            entries.push_back ({ *s, e.getProperty (seedstate::label).toString() });

    return entries;
}

bool SeedState::pickFromHistory (int index)
{
    const auto entries = history();
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return false;

    const auto& e = entries[(size_t) index];
    return set (e.seed, e.label, "Seed " + codeOf (e.seed));
}

void SeedState::remember (const Entry& e)
{
    auto ui = host.getUiState();
    auto list = ui.getOrCreateChildWithName (seedstate::history, nullptr);
    const auto code = codeOf (e.seed);

    for (int i = list.getNumChildren(); --i >= 0;)
        if (list.getChild (i).getProperty (seedstate::code).toString() == code)
            list.removeChild (i, nullptr);

    list.addChild (juce::ValueTree (seedstate::entry, { { seedstate::code, code }, { seedstate::label, e.label } }), 0, nullptr);

    while (list.getNumChildren() > historySize)
        list.removeChild (list.getNumChildren() - 1, nullptr);
}

void SeedState::adopt (uint32_t newSeed, const juce::String& newLabel)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    auto state = root();
    state.setProperty (seedstate::seed, codeOf (newSeed), nullptr);
    state.setProperty (seedstate::seedLabel, newLabel, nullptr);
}

void SeedState::sync()
{
    seed.store (parseProperty (root().getProperty (seedstate::seed)).value_or (dsp::seedcode::defaultSeed), std::memory_order_relaxed);
    locked.store ((bool) host.getUiState().getProperty (seedstate::seedLock, false), std::memory_order_relaxed);
}

void SeedState::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if ((property == seedstate::seed && tree == root()) || property == seedstate::seedLock)
        sync();
}

void SeedState::valueTreeRedirected (juce::ValueTree&)
{
    sync();
}

} // namespace inkcap
