#pragma once

#include "ui/HaliteController.h"
#include "ui/Widgets.h"

namespace halite::ui
{

/**
    Halite's own header (PLAN.md, Parts; Settled 63): the wordmark and version; the preset browser
    centred (category / name, a dot when modified, ‹ › to step); A/B; undo and redo; the latency
    read-out; Bypass in the caution colour; ⚙. The editor builds the preset and settings menus.
*/
class Header final : public juce::Component
{
public:
    explicit Header (HaliteController&);

    std::function<void()> onPresetMenu, onSettingsMenu, onLatencyMenu, onAbMenu;

    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Button& getPresetName() noexcept { return presetName; }
    juce::Button& getPrevious() noexcept { return previous; }
    juce::Button& getNext() noexcept { return next; }
    Segmented& getAb() noexcept { return ab; }
    ToolButton& getUndo() noexcept { return undo; }
    ToolButton& getRedo() noexcept { return redo; }
    ToolButton& getLatency() noexcept { return latency; }
    ToolButton& getBypass() noexcept { return bypass; }
    ToolButton& getSettings() noexcept { return settings; }

    /** The preset field (‹ name ›), for the layout checks. */
    juce::Rectangle<int> presetArea() const noexcept { return presetBox; }

private:
    class PresetName final : public HelpedButton
    {
    public:
        PresetName();
        juce::String category, name;
        bool modified = false;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    HaliteController& controller;
    juce::SharedResourcePointer<Fonts> fonts;

    PresetName presetName;
    ToolButton previous { "Previous preset", Icon::chevronLeft }, next { "Next preset", Icon::chevronRight };
    Segmented ab { "A/B", { "A", "B" } };
    ToolButton undo { "Undo", Icon::undo }, redo { "Redo", Icon::redo };
    ToolButton latency { "Latency", Icon::none, "10.7 ms" };
    ToolButton bypass { "Bypass", Icon::none, "Bypass" };
    ToolButton settings { "Settings", Icon::gear };

    juce::Rectangle<int> presetBox;
    bool showVersion = true;
};

} // namespace halite::ui
