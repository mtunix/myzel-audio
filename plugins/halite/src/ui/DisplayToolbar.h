#pragma once

#include "ui/HaliteController.h"
#include "ui/Widgets.h"

namespace halite::ui
{

/**
    The display's toolbar (PLAN.md, Parts): the view tabs Timeline | Cloud, the legend, zoom, freeze,
    and the events-sidebar toggle at the right end. The legend gives way first when the toolbar is narrow.
*/
class DisplayToolbar final : public juce::Component
{
public:
    explicit DisplayToolbar (HaliteController&);

    std::function<void()> onZoomMenu, onToggleSidebar, onFreeze;

    /** Freeze is the display's (DisplayModel), not the session's: it starts off in every editor. The button shows it. */
    bool isFrozen() const noexcept { return freeze.isOn(); }
    void setFrozen (bool f) { freeze.setOn (f); }

    /** Whether the sidebar is showing (docked or as an overlay), for the toggle's state. */
    void setSidebarShowing (bool showing) { events.setOn (showing); }

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

    Segmented& getTabs() noexcept { return tabs; }
    MenuButton& getZoom() noexcept { return zoom; }
    ToolButton& getFreeze() noexcept { return freeze; }
    ToolButton& getEvents() noexcept { return events; }

    /** Where the legend is drawn (empty when it gave way). */
    juce::Rectangle<int> legendArea() const noexcept { return legend; }

private:
    float legendWidth() const;

    HaliteController& controller;
    juce::SharedResourcePointer<Fonts> fonts;

    Segmented tabs { "View", { "Timeline", "Cloud" } };
    MenuButton zoom { "Zoom" };
    ToolButton freeze { "Freeze", Icon::freeze }, events { "Events sidebar", Icon::sidebar };
    juce::Rectangle<int> legend;
    DisplayView shownView = DisplayView::timeline;
};

} // namespace halite::ui
