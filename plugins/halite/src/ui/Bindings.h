#pragma once

#include "ui/HaliteController.h"
#include "ui/Widgets.h"

namespace halite::ui
{

/**
    One parameter as a knob's or a field's value. The parameter may follow the mode (a band's
    Threshold edits the relative one in Voice, the absolute one in Allround); a gesture stays on the
    parameter it began on.
*/
class ParameterBinding final : public ValueBinding
{
public:
    using IdSource = std::function<const char*()>;

    ParameterBinding (HaliteController&, IdSource);
    ParameterBinding (HaliteController&, const char* id);

    const char* currentId() const { return idSource(); }

    HelpText help() const override;
    float position() const override;
    float defaultPosition() const override;
    void begin() override;
    void setPosition (float) override;
    void end() override;
    juce::String text() const override;
    juce::String shortText() const override;
    void setFromText (const juce::String&) override;
    float step() const override;
    juce::StringArray parameterIds() const override;

private:
    HaliteController& controller;
    IdSource idSource;
    const char* editing = nullptr;
};

/**
    A band's Speed: Auto or a fixed release, two parameters (speedAuto and release, or Harsh's) as one
    control. The first eighth of the travel is Auto; the rest is the release, logarithmic as its
    parameter. Turning up from Auto switches Auto off; the default is Auto (DESIGN.md, Speed).
*/
class SpeedBinding final : public ValueBinding
{
public:
    SpeedBinding (HaliteController&, const char* autoId, const char* releaseId);

    HelpText help() const override;
    float position() const override;
    float defaultPosition() const override { return 0.0f; }
    void begin() override;
    void setPosition (float) override;
    void end() override;
    juce::String text() const override;
    void setFromText (const juce::String&) override;
    float step() const override { return 1.0f / 40.0f; }
    float stepFrom (float position, float travel) const override;
    juce::StringArray parameterIds() const override { return { autoId, releaseId }; }

    static constexpr float autoSpan = 0.125f;

private:
    bool isAuto() const;

    HaliteController& controller;
    const char* autoId;
    const char* releaseId;
    bool editing = false;
};

} // namespace halite::ui
