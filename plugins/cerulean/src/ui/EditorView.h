#pragma once

#include "ui/HeaderBar.h"
#include "ui/InlineDialog.h"
#include "ui/ViewMode.h"

namespace cerulean::ui
{

/**
    One face of the editor (the panel or the studio view), laid out once at base size
    (metrics::baseWidth x baseHeight) and scaled by the editor. Each view owns its header,
    dialog and frame clock; the model they share (presets, A/B, meanings) is the
    PanelController, which outlives them.
*/
class EditorView : public juce::Component
{
public:
    ~EditorView() override = default;

    virtual ViewMode getMode() const noexcept = 0;

    /** Height at 100 % (the panel grows while its Advanced deck is open). */
    virtual int getBaseHeight() const noexcept { return metrics::baseHeight; }

    /** Wired by the editor: called when getBaseHeight() changes, so the window follows. */
    std::function<void()> onBaseSizeChanged;

    /** One animation frame (called on vblank; the snapshot tool calls it directly). */
    virtual void animateFrame (double timeSeconds) = 0;

    virtual HeaderBar& getHeader() noexcept = 0;
    virtual InlineDialog& getDialog() noexcept = 0;

    /** Wired by the editor: the scale menu and the view switch in the header. */
    std::function<void (float)> onScaleChosen;
    std::function<float()> getScale;
    std::function<void (ViewMode)> onViewChosen;

protected:
    /** Connects the header's scale menu and view switch to the callbacks above. */
    void wireHeader (HeaderBar& header)
    {
        header.getScale = [this] { return getScale != nullptr ? getScale() : 1.0f; };
        header.onScaleChosen = [this] (float s)
        {
            if (onScaleChosen != nullptr)
                onScaleChosen (s);
        };
        header.onViewChosen = [this] (ViewMode m)
        {
            if (onViewChosen != nullptr && m != getMode())
                onViewChosen (m);
        };
        header.setView (getMode());
    }
};

} // namespace cerulean::ui
