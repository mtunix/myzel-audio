#pragma once

#include "ui/Bindings.h"

namespace halite::ui
{

/** More: opens the Advanced row above the bar. Tall and lettered upright in one row, a word when the bar wraps. */
class MoreButton final : public HelpedButton
{
public:
    MoreButton();

    void setOpen (bool o) { if (open != o) { open = o; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool open = false;
};

/**
    The control bar (PLAN.md, Parts): the band selector, the selected band's controls, Shape and
    Detection, Learn and Listen, More. Three arrangements by width (DESIGN.md, Breakpoints): wide
    (labels beside the segmented controls), medium (labels above, tighter gaps) and wrapped (two rows,
    below about 900 px).
*/
class ControlBar final : public juce::Component
{
public:
    explicit ControlBar (HaliteController&);

    enum class Mode
    {
        wide,
        medium,
        wrapped
    };

    void setCompact (bool c);
    Mode modeFor (int width) const;
    int preferredHeight (int width) const;
    Mode getMode() const noexcept { return mode; }

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

    /** The Listen menu, and what choosing an item does (the self-test calls both). */
    juce::PopupMenu listenMenu() const;
    void chooseListen (int itemId);
    static constexpr int listenItemBase = 1;

    BandPill& getEssPill() noexcept { return essPill; }
    BandPill& getHarshPill() noexcept { return harshPill; }
    RingKnob& getThreshold (Band b) noexcept { return b == Band::ess ? essThreshold : harshThreshold; }
    RingKnob& getRange (Band b) noexcept { return b == Band::ess ? essRange : harshRange; }
    RingKnob& getFocus() noexcept { return focus; }
    RingKnob& getHarshSpeed() noexcept { return harshSpeed; }
    ValueField& getDetectLow() noexcept { return low; }
    ValueField& getDetectHigh() noexcept { return high; }
    ValueField& getHarshFreq() noexcept { return freq; }
    ValueField& getHarshWidth() noexcept { return width; }
    Segmented& getShape() noexcept { return shape; }
    Segmented& getDetection() noexcept { return detection; }
    FilledButton& getLearn() noexcept { return learn; }
    MenuButton& getListen() noexcept { return listen; }
    MoreButton& getMore() noexcept { return more; }

    /** Every value control of both bands, shown or not. */
    std::vector<ValueControl*> valueControls();

private:
    struct Sizes
    {
        float large, small;
        int largeCell, smallCell;
    };

    Sizes sizes() const;
    int widthFor (Mode) const;
    void showBand (Band);

    HaliteController& controller;

    ParameterBinding essThresholdValue, essRangeValue, focusValue, lowValue, highValue;
    ParameterBinding harshThresholdValue, harshRangeValue, freqValue, widthValue;
    SpeedBinding harshSpeedValue;

    BandPill essPill { "Ess", false }, harshPill { "Harsh", true };
    RingKnob essThreshold, essRange, focus;
    RingKnob harshThreshold, harshRange, harshSpeed;
    ValueField low, high, freq, width;
    Segmented shape, detection;
    FilledButton learn { "Learn" };
    MenuButton listen { "Listen" };
    MoreButton more;

    Mode mode = Mode::wide;
    bool compact = false;
    Band shownBand = Band::ess;
    std::vector<juce::Rectangle<int>> dividers;

    static constexpr int pillsWidth = 92, fieldsWidth = 120, fieldLabelWidth = 40, fieldBoxWidth = 72;
};

/**
    The Advanced row (PLAN.md, Parts; More): Ess Speed, Look-ahead, Stereo, Key with Key HPF, Output.
    Its items flow left to right and wrap into a second row when the window is narrow.
*/
class AdvancedRow final : public juce::Component
{
public:
    explicit AdvancedRow (HaliteController&);

    void setCompact (bool c) { compact = c; }
    int preferredHeight (int width) const;

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

    ValueField& getSpeed() noexcept { return speed; }
    ValueField& getLookahead() noexcept { return lookahead; }
    ValueField& getKeyHpf() noexcept { return keyHpf; }
    ValueField& getOutput() noexcept { return output; }
    Segmented& getStereo() noexcept { return stereo; }
    Segmented& getKey() noexcept { return key; }

    std::vector<ValueControl*> valueControls() { return { &speed, &lookahead, &keyHpf, &output }; }

private:
    std::vector<juce::Component*> items();
    int itemWidth (juce::Component*) const;
    int rowHeight() const noexcept { return compact ? 30 : 34; }
    int rowsFor (int width) const;

    HaliteController& controller;
    SpeedBinding speedValue;
    ParameterBinding lookaheadValue, keyHpfValue, outputValue;

    ValueField speed, lookahead, keyHpf, output;
    Segmented stereo, key;
    bool compact = false;

    static constexpr int pad = 16, gap = 20, boxWidth = 72;
};

} // namespace halite::ui
