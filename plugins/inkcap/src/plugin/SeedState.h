#pragma once

#include "common/plugin/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace inkcap
{

namespace seedstate
{
    // Saved in sessions and preset files: never rename.
    inline const juce::Identifier seed      { "seed" };          // state tree: the code, "3F2A-91C7"
    inline const juce::Identifier seedLabel { "seedLabel" };     // state tree: the word it came from, or empty
    inline const juce::Identifier seedLock  { "seedLock" };      // UI state: bool, presets and A/B keep the seed
    inline const juce::Identifier history   { "SeedHistory" };   // UI state child: the last seeds, newest first
    inline const juce::Identifier entry     { "Seed" };          // a child of SeedHistory
    inline const juce::Identifier code      { "code" };          // an entry's code
    inline const juce::Identifier label     { "label" };         // an entry's word, or empty
}

/**
    The seed of the chaos (PLAN.md, Seeds). Not a host parameter: it lives in the APVTS state tree
    (seedstate::seed, seedstate::seedLabel), so sessions, presets and A/B carry it, and every seed
    action (paste, a typed word, reroll, a history pick) is one step in the shared undo history.
    Lock and history are session settings in the UI state: the preset manager reads the lock
    (PresetStateProperty), and the history keeps every seed a seed action replaced, so a reroll
    is never lost. Preset loads, A/B and undo change the seed but never the history.

    The audio thread reads the seed through current() only. Everything else: message thread.
*/
class SeedState final : private juce::ValueTree::Listener
{
public:
    explicit SeedState (myzel::plugin::PresetHost& host);
    ~SeedState() override;

    /** The seed in effect. Any thread. */
    uint32_t current() const noexcept { return seed.load (std::memory_order_relaxed); }

    /** The seed Lock as the session holds it. Any thread (a host's program change reads it off the message thread). */
    bool lockedNow() const noexcept { return locked.load (std::memory_order_relaxed); }

    /** A seed for the audio thread now, from any thread; the state tree takes it when adopt() follows on the message thread. */
    void preview (uint32_t newSeed) noexcept { seed.store (newSeed, std::memory_order_relaxed); }

    /** Takes a seed and its word into the state tree as a preset load does: no undo step, no history. Message thread. */
    void adopt (uint32_t newSeed, const juce::String& newLabel);

    juce::String code() const;
    juce::String label() const;

    /** A seed action: one undo step, and the history keeps both the old and the new seed. False if nothing changed. */
    bool set (uint32_t newSeed, const juce::String& newLabel, const juce::String& undoName);

    enum class Paste
    {
        nothing,    // empty or white space only: the seed stays
        code,       // "3F2A-91C7", "3f2a91c7" or "0x3F2A91C7": that seed
        word        // anything else: its hash, the word as the label
    };

    /** Paste and "Type a word...": a code is that seed, any other text hashes to one (SeedCode). */
    Paste paste (const juce::String& text);

    /** A new seed from system entropy, never the current one. */
    void reroll();

    struct Entry
    {
        uint32_t seed = 0;
        juce::String label;
    };

    static constexpr int historySize = 8;

    std::vector<Entry> history() const;
    bool pickFromHistory (int index);

    /** The preset properties that carry the seed, kept while the seed Lock is on. */
    static std::vector<myzel::plugin::PresetStateProperty> presetProperties();

    /** A fresh instance's seed: the word "inkcap". */
    static Entry initial();

    /** Gives a saved state without a valid seed (older or damaged) the initial one. Any thread: touches only that tree. */
    static void repair (juce::ValueTree& savedState);

    /**
        After a preset or A/B: a seed that is not a code (a hand-edited or damaged preset) becomes
        the fresh instance's seed without a word, as a preset without one, so the line never shows
        a word beside a code it does not hash to (PLAN.md, Settled 16). Part of the load's undo step.
    */
    void repairAfterPreset();

    /** The seed a property holds: a code, else nothing. */
    static std::optional<uint32_t> parseProperty (const juce::var& property);

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    juce::ValueTree root() const;
    void sync();
    void remember (const Entry&);

    myzel::plugin::PresetHost& host;
    std::atomic<uint32_t> seed;
    std::atomic<bool> locked { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedState)
};

} // namespace inkcap
