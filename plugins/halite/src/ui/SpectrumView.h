#pragma once

#include "ui/DisplayModel.h"
#include "ui/DisplayScales.h"
#include "ui/DrawCache.h"
#include "ui/HaliteController.h"

namespace halite::ui
{

/**
    The spectrum (PLAN.md, Parts): 200 Hz–20 kHz under the main view, the input filled and the output
    as a line, each band's live cut as a curve under the 0 dB line, the bands as shaded regions with
    their handles, Spectral's target as a thin line. Ess: a handle on each edge (Detect Low, High);
    Harsh: a centre handle for Freq and edges for Width. Dragging is one gesture and one undo step
    (Shift fine); clicking a band selects it. Redrawn at the model's 30 fps, only where the curves moved,
    and whole on any other change.
*/
class SpectrumView final : public juce::Component
{
public:
    SpectrumView (HaliteController&, DisplayModel&);
    ~SpectrumView() override;

    scales::SpectrumGeometry geometry() const;
    scales::BandMarks marks() const;

    /** x of a frequency inside the plot area. */
    float xOf (double hz) const { return geometry().xOf (hz); }
    juce::Rectangle<float> plotArea() const { return geometry().plot; }

    scales::SpectrumTarget hovered() const noexcept { return hover; }

    /** Repaints the frame clock asked for (the snapshot tool's timing paints only those). */
    std::uint32_t frameRepaints() const noexcept { return repaintsAsked; }

    /** What the frame clock asked to repaint since the last call: the whole view, or the rows the curves moved in. */
    juce::RectangleList<int> takeAskedArea() { return std::exchange (askedArea, {}); }

    void update();
    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    /** What moves with the audio, in the view's px: where two frames' traces differ is all a new frame repaints. */
    struct Trace
    {
        int count = 0;
        bool target = false, harsh = false;
        std::array<float, DisplayModel::spectrumPoints> x {}, input {}, output {}, targetY {}, essY {}, harshY {};
        juce::String essTitle, harshTitle;
    };

    /** The static layer's key: a change repaints the whole view. */
    struct BackgroundKey
    {
        int width = 0, height = 0;
        float scale = 0.0f;
        ThemeName theme = ThemeName::dark;
        dsp::Detector::Bands bands;
        bool harsh = false;
        Band band = Band::ess;
        bool harshEdgeLit = false;
        bool operator== (const BackgroundKey&) const = default;
    };

    /** The band titles' row (y 8 to 20) with a pixel to spare: a changed title repaints it. */
    static constexpr int titleTop = 6, titleBottom = 22;

    Trace trace (const scales::SpectrumGeometry&, const scales::BandMarks&) const;
    BackgroundKey keyFor (float scale, const scales::BandMarks&) const;
    juce::RectangleList<int> changedArea (const Trace& before, const Trace& now) const;
    void ask (const juce::RectangleList<int>&);
    void paintBackground (juce::Graphics&, const scales::SpectrumGeometry&, const scales::BandMarks&);
    const juce::Path& ribbon (const juce::Point<float>*, int count, float thickness, bool dashed = false);
    const char* parameterFor (scales::SpectrumTarget) const;
    HelpText helpFor (scales::SpectrumTarget) const;
    void setHover (scales::SpectrumTarget);

    HaliteController& controller;
    DisplayModel& model;
    juce::SharedResourcePointer<Fonts> fonts;

    juce::Image background;
    BackgroundKey backgroundKey;
    Trace painted;
    std::array<float, 2> titleCuts {};      // the cut the titles show, taken with the frame so a trace changes only in update()
    bool canCompare = false;                // the last paint kept its static layer: the window shows `painted`
    juce::RectangleList<int> askedArea;
    std::vector<juce::Point<float>> points = std::vector<juce::Point<float>> ((size_t) DisplayModel::spectrumPoints);
    juce::Path spectrumFill, cutArea, line;
    ChipCache labels;

    std::uint32_t shownFrame = 0, repaintsAsked = 0;
    dsp::Detector::Bands shownBands;
    Band shownBand = Band::ess;
    bool shownHarsh = false;
    ThemeName shownTheme = ThemeName::dark;

    scales::SpectrumTarget hover = scales::SpectrumTarget::none, pressed = scales::SpectrumTarget::none;
    const char* dragId = nullptr;
    const char* lowId = nullptr;        // Detect's lower handle edits whichever of Low and High is lower
    bool dragging = false, fine = false, helpShown = false;
    float anchorPointer = 0.0f, anchorHandle = 0.0f;
};

} // namespace halite::ui
