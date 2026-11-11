#include "ui/StudioPanel.h"

#include "plugin/PluginProcessor.h"
#include "ui/Texts.h"

namespace umber::ui
{

namespace
{
    constexpr int circuitRadioGroup = 1001;
    constexpr int numCircuits = 5;

    const char* const listenNotes[] = {
        "Keeps the loudness steady as you drive, so you judge the colour, not the level.",
        "Plays only what the circuit adds: the processed signal minus the dry.",
    };
}

StudioPanel::StudioPanel (EditorController& c, UmberProcessor& p)
    : controller (c),
      processor (p),
      header (p, c.getPresets(), c.getAb(), dialog, headerConfig (p)),
      meters ({ { "IN",    [&m = p.getMeters()] { return m.inputRms.load (std::memory_order_relaxed); },
                           [&m = p.getMeters()] { return m.inputPeak.exchange (0.0f, std::memory_order_relaxed); } },
                { "DRIVE", [&m = p.getMeters()] { return m.driveRms.load (std::memory_order_relaxed); }, nullptr },
                { "OUT",   [&m = p.getMeters()] { return m.outputRms.load (std::memory_order_relaxed); },
                           [&m = p.getMeters()] { return m.outputPeak.exchange (0.0f, std::memory_order_relaxed); } } },
              std::nullopt,
              "Input level, the level hitting the circuit (after Drive and Slam) and the output level "
              "(bar: RMS, line: peak hold, number: peak). Click to reset the peaks."),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setOpaque (true);
    setSize (metrics::baseWidth, metrics::baseHeight);
    setWantsKeyboardFocus (false);

    buildControls();
    addAndMakeVisible (meters);
    addAndMakeVisible (header);
    addChildComponent (dialog);

    wireHeader (header);

    // Mix lock: a padlock after the MIX label while it is on.
    {
        const auto labelFont = drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking);
        const auto row = studio::outputRow (0);
        const auto x = row.getX() + juce::GlyphArrangement::getStringWidth (labelFont, "MIX") + 10.0f;
        mixLockBadge.setBounds (juce::Rectangle<int> (LockBadge::width, LockBadge::height)
                                    .withCentre ({ juce::roundToInt (x), juce::roundToInt (row.getY() + 8.5f) }));
    }
    addChildComponent (mixLockBadge);

    controller.getPresets().onChange = [this]
    {
        header.refresh();
        mixLockBadge.setVisible (controller.getPresets().isMixLocked());
    };
    mixLockBadge.setVisible (controller.getPresets().isMixLocked());
    processor.getState().state.addListener (this);
    header.refresh();
}

StudioPanel::~StudioPanel()
{
    controller.getPresets().onChange = nullptr;
    processor.getState().state.removeListener (this);
}

//==============================================================================
StudioSlider& StudioPanel::addSlider (const char* id, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip)
{
    auto slider = std::make_unique<StudioSlider> (controller.parameter (id), &controller.getUndoManager(), label);
    slider->setBounds (bounds.toNearestInt());
    slider->setTooltip (tip);
    addAndMakeVisible (*slider);
    sliders.push_back (std::move (slider));
    return *sliders.back();
}

StudioToggle& StudioPanel::addToggle (StudioToggle& toggle, const char* id, juce::Rectangle<float> bounds, const juce::String& tip)
{
    toggle.setBounds (bounds.toNearestInt());
    toggle.setClickingTogglesState (true);
    toggle.setTooltip (tip);
    addAndMakeVisible (toggle);
    buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (controller.parameter (id), toggle, &controller.getUndoManager()));
    toggles.push_back ({ id, &toggle });
    return toggle;
}

void StudioPanel::buildControls()
{
    using namespace studio;

    // CIRCUIT
    const auto names = params::modelNames();

    for (int i = 0; i < numCircuits; ++i)
    {
        auto row = std::make_unique<SelectorRow> (i, names[i], circuitTagline (i));
        row->setBounds (circuitRow (i).toNearestInt());
        row->setTooltip (circuitDescription (i));
        row->setTitle (names[i] + " circuit");
        row->setRadioGroupId (circuitRadioGroup, juce::dontSendNotification);
        row->setClickingTogglesState (false);
        row->onClick = [this, i] { circuitAttachment->setValueAsCompleteGesture ((float) i); };
        row->onStep = [this, i] (int delta)
        {
            const auto next = juce::jlimit (0, numCircuits - 1, i + delta);
            circuitAttachment->setValueAsCompleteGesture ((float) next);
            circuitRows[(size_t) next]->grabKeyboardFocus();
        };
        addAndMakeVisible (*row);
        circuitRows[(size_t) i] = std::move (row);
    }

    circuitAttachment = std::make_unique<juce::ParameterAttachment> (controller.parameter (params::model), [this] (float v)
    {
        const auto index = juce::jlimit (0, numCircuits - 1, juce::roundToInt (v));

        for (int i = 0; i < numCircuits; ++i)
            circuitRows[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);

        if (index != shownCircuit)
        {
            shownCircuit = index;
            repaint (studio::circuitNote().toNearestInt());
        }
    }, &controller.getUndoManager());
    circuitAttachment->sendInitialUpdate();

    // DRIVE: the card's headline value, Slam next to it, then how the circuit is driven.
    addSlider (params::drive, "DRIVE", driveHero(), tips::drive).setLook (StudioSlider::Look::hero);
    addToggle (slamKey, params::slam, slamButton(), tips::slam);
    addSlider (params::bias, "BIAS", driveRow (0), tips::bias).setBipolar (true);
    addSlider (params::emphasis, "EMPHASIS", driveRow (1), tips::emphasis).setBipolar (true);

    // INPUT
    addSlider (params::stereoMode, "STEREO", cardRow (inputCard(), 0), tips::stereo).setSteps (3);
    auto& lowCut = addSlider (params::lowCut, "LOW CUT", cardRow (inputCard(), 1), tips::lowCut);
    lowCut.setTrailingSpace (chipWidth + 8.0f);
    weightKey.setTitle ("Weight");
    addToggle (weightKey, params::weight, rowChip (cardRow (inputCard(), 1)), tips::weight);

    // TONE
    addSlider (params::highCut, "HIGH CUT", cardRow (toneCard(), 0), tips::highCut).setFillFromRight (true);
    addSlider (params::tone, "TONE", cardRow (toneCard(), 1), tips::tone).setBipolar (true);

    // LISTEN
    autoKey.setTitle ("Auto gain");
    deltaKey.setTitle ("Delta");
    addToggle (autoKey, params::autoGain, listenSwitch (0), tips::autoGain);
    addToggle (deltaKey, params::delta, listenSwitch (1), tips::delta);

    // OUTPUT
    meters.setBounds (studio::meters().toNearestInt());
    addSlider (params::mix, "MIX", outputRow (0), tips::mix);
    addSlider (params::output, "OUTPUT", outputRow (1), tips::output).setBipolar (true);
}

StudioSlider* StudioPanel::findSlider (const juce::String& id) const
{
    for (const auto& s : sliders)
        if (s->getParameter().getParameterID() == id)
            return s.get();

    return nullptr;
}

StudioToggle* StudioPanel::findToggle (const juce::String& id) noexcept
{
    for (auto& [toggleId, toggle] : toggles)
        if (toggleId == id)
            return toggle;

    return nullptr;
}

void StudioPanel::valueTreeRedirected (juce::ValueTree&)
{
    header.refresh();
}

void StudioPanel::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f)
                                        : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    meters.tick (dt);

    if (++framesSinceRefresh >= 6)
    {
        framesSinceRefresh = 0;

        // Weight does nothing while Low Cut is off: shown greyed out (still editable), as on the panel.
        const auto lowCut = processor.getState().getRawParameterValue (params::lowCut)->load (std::memory_order_relaxed);
        weightKey.setDimmed (params::isLowCutOff (lowCut));
        header.refresh();
    }
}

//==============================================================================
void StudioPanel::resized()
{
    header.setBounds (layout::header().toNearestInt());
    dialog.setBounds (getLocalBounds());
    background.invalidate();
}

void StudioPanel::paint (juce::Graphics& g)
{
    background.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        renderBackground (lg, a);
    });

    // The selected circuit, in a sentence, under the controls that drive it.
    const auto& p = Theme::studio();
    const auto note = studio::circuitNote();
    const auto description = circuitDescription (shownCircuit);
    const auto colon = description.indexOfChar (':');
    g.setFont (drawing::tracked (fonts->legend (studio::labelSize), studio::labelTracking));
    g.setColour (p.accent);
    g.drawText (description.substring (0, colon).toUpperCase(), note.withHeight (16.0f), juce::Justification::centredLeft, false);
    g.setFont (fonts->label (studio::valueSize - 1.0f));
    g.setColour (p.textDim);
    auto sentence = description.substring (colon + 1).trim();
    sentence = sentence.substring (0, 1).toUpperCase() + sentence.substring (1);
    g.drawFittedText (sentence, note.withTrimmedTop (20.0f).toNearestInt(),
                      juce::Justification::topLeft, 2, 1.0f);
}

void StudioPanel::renderBackground (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using namespace studio;
    const auto& p = Theme::studio();

    g.setColour (p.background);
    g.fillRect (area);

    // The header rail sits on a hairline.
    g.setColour (juce::Colours::black);
    g.drawHorizontalLine ((int) metrics::headerHeight, 0.0f, area.getRight());

    drawCard (g, circuitCard(), "CIRCUIT", *fonts);
    drawCard (g, listenCard(), "LISTEN", *fonts);
    drawCard (g, driveCard(), {}, *fonts);   // Drive is the card's headline
    drawCard (g, inputCard(), "INPUT", *fonts);
    drawCard (g, toneCard(), "TONE", *fonts);
    drawCard (g, outputCard(), "OUTPUT", *fonts);

    // Drive: a rule between the head and the rows.
    {
        const auto c = driveCard();
        g.setColour (p.cardEdge);
        g.drawHorizontalLine (juce::roundToInt (c.getY() + driveHeadHeight), c.getX() + cardPad, c.getRight() - cardPad);
    }

    // Output: a rule between the meters and the controls.
    {
        const auto m = studio::meters();
        g.setColour (p.cardEdge);
        g.drawHorizontalLine (juce::roundToInt (m.getBottom() + 4.0f), m.getX(), m.getRight());
    }

    // Listen: labels left of their switches, a note under each.
    g.setFont (drawing::tracked (fonts->legend (labelSize), labelTracking));
    g.setColour (p.textDim);
    g.drawText ("AUTO", listenLabel (0), juce::Justification::centredLeft, false);
    g.drawText ("DELTA", listenLabel (1), juce::Justification::centredLeft, false);

    g.setFont (fonts->label (smallSize));
    g.setColour (p.textFaint);

    for (int i = 0; i < 2; ++i)
        g.drawFittedText (listenNotes[i], listenNote (i).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);

    // Input: what the two rows do to the signal before the circuit.
    {
        const auto r = cardRow (inputCard(), 2);
        g.drawFittedText ("Before the circuit: which part of the stereo image it saturates, and how much low end it gets.",
                          r.withTrimmedTop (-4.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }

    // Tone: after the circuit.
    {
        const auto r = cardRow (toneCard(), 2);
        g.drawFittedText ("After the circuit: tame the top it adds, then tilt the result darker or brighter.",
                          r.withTrimmedTop (-4.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
    }
}

} // namespace umber::ui
