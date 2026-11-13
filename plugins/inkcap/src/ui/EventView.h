#pragma once

#include "ui/InkcapController.h"
#include "ui/Theme.h"

#include <vector>

namespace inkcap::ui
{

/**
    Glitch's next 4 bars: one lane per kind of failure, every event where and how long it will
    play. The events are not sent by the audio thread: the view calls the same pure function
    the audio thread plays (glitch::eventAt) for the seed in use and the song position, so what
    it shows is what will play (src/dsp/chain/NOTES.md). The window is the bar the song is in
    and the three after it; the playhead runs through it.
*/
class EventView final : public juce::Component
{
public:
    explicit EventView (InkcapController&);

    void update();

    struct Shown
    {
        int64_t slot = 0;
        double startBeat = 0.0, lengthBeats = 0.0;
        GlitchType type = GlitchType::skip;
    };

    const std::vector<Shown>& getEvents() const noexcept { return events; }
    double getWindowStart() const noexcept { return windowStart; }
    double getWindowBeats() const noexcept { return windowBeats; }

    void paint (juce::Graphics&) override;

    static constexpr int bars = 4;
    static constexpr float laneX = 52.0f, lanesY = 22.0f, laneHeight = 16.0f, laneGap = 4.0f;

private:
    void recompute();
    float xFor (double beat) const;

    InkcapController& controller;
    juce::SharedResourcePointer<GlassFonts> fonts;

    std::vector<Shown> events;
    double windowStart = 0.0, windowBeats = 16.0, beatsPerBar = 4.0, ppq = 0.0, bpm = 120.0;
    int beatsInBar = 4;
    uint32_t seed = 0;
    int64_t playingSlot = -1;
    float playheadX = -1.0f;
    juce::String status, gridName;

    struct Key
    {
        double windowStart = -1.0, bpm = 0.0, beatsPerBar = 0.0;
        uint32_t seed = 0;
        bool on = false;
        float amount = -1.0f, damage = -1.0f, chaos = -1.0f, length = -1.0f, groove = -1.0f;
        int type = -1, rate = -1;
        bool operator== (const Key&) const = default;
    } key;
};

} // namespace inkcap::ui
