#include "ui/InkcapController.h"

#include "dsp/DamageCurves.h"
#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace inkcap::ui
{

namespace
{
    /** "Dark 40" -> "dark 40", "Straight" -> "straight": the display sets words in lower case, units as they are. */
    juce::String displayCase (const juce::String& s)
    {
        if (s.length() >= 2 && juce::CharacterFunctions::isUpperCase (s[0]) && juce::CharacterFunctions::isLowerCase (s[1]))
            return s.substring (0, 1).toLowerCase() + s.substring (1);
        return s;
    }
}

InkcapController::InkcapController (InkcapProcessor& p)
    : EditorController (p, p.getPresets()),
      processor (p)
{
}

SeedState& InkcapController::getSeeds() noexcept
{
    return processor.getSeeds();
}

const dsp::Telemetry& InkcapController::getTelemetry() const noexcept
{
    return processor.getTelemetry();
}

float InkcapController::plain (const char* id) const
{
    auto& p = parameter (id);
    return p.convertFrom0to1 (p.getValue());
}

float InkcapController::normalised (const char* id) const
{
    return parameter (id).getValue();
}

juce::String InkcapController::text (const char* id) const
{
    auto& p = parameter (id);
    return displayCase (p.getText (p.getValue(), 64));
}

bool InkcapController::isOn (Module m) const
{
    return plain (params::onIds[(size_t) m]) >= 0.5f;
}

float InkcapController::amount (Module m) const
{
    return plain (params::amountIds[(size_t) m]) / 100.0f;
}

int InkcapController::type (Module m) const
{
    const auto count = (int) moduleInfo (m).types.size();
    return juce::jlimit (0, count - 1, juce::roundToInt (plain (params::typeIds[(size_t) m])));
}

juce::String InkcapController::typeName (Module m) const
{
    return typeName (m, type (m));
}

juce::String InkcapController::typeName (Module m, int t)
{
    // The display's column is 70 px: Bits' samplers go by their word length.
    if (m == Module::bits)
    {
        switch ((BitsType) t)
        {
            case BitsType::sampler12: return "12-bit";
            case BitsType::sampler8:  return "8-bit";
            case BitsType::phone:     return "phone";
        }
    }

    const auto& types = moduleInfo (m).types;
    return juce::isPositiveAndBelow (t, (int) types.size()) ? juce::String (types[(size_t) t]).toLowerCase() : juce::String();
}

juce::String InkcapController::moduleName (Module m)
{
    return juce::String (moduleInfo (m).name).toLowerCase();
}

float InkcapController::intensity (Module m) const
{
    return isOn (m) ? dsp::moduleIntensity (m, amount (m), damage()) : 0.0f;
}

bool InkcapController::anyModuleOn() const
{
    for (int i = 0; i < numModules; ++i)
        if (isOn ((Module) i))
            return true;
    return false;
}

float InkcapController::wander() const
{
    return dsp::chaosAmount (chaos()) * dsp::padWear (damage());
}

void InkcapController::setNormalised (const char* id, float value01)
{
    auto& p = parameter (id);
    value01 = juce::jlimit (0.0f, 1.0f, value01);

    getUndoManager().beginNewTransaction();
    p.beginChangeGesture();
    p.setValueNotifyingHost (value01);
    p.endChangeGesture();
}

void InkcapController::setPlain (const char* id, float value)
{
    auto& p = parameter (id);
    setNormalised (id, p.convertTo0to1 (value));
}

void InkcapController::toggle (const char* id)
{
    setNormalised (id, normalised (id) >= 0.5f ? 0.0f : 1.0f);
}

void InkcapController::resetToDefault (const char* id)
{
    setNormalised (id, parameter (id).getDefaultValue());
}

void InkcapController::setFromText (const char* id, const juce::String& typed)
{
    auto& p = parameter (id);
    const auto value = p.getValueForText (typed.trim());

    if (! juce::approximatelyEqual (value, p.getValue()))
        setNormalised (id, value);
}

void InkcapController::beginEdit (const char* id)
{
    getUndoManager().beginNewTransaction();
    parameter (id).beginChangeGesture();
}

void InkcapController::endEdit (const char* id)
{
    parameter (id).endChangeGesture();
}

juce::String InkcapController::seedCode() const
{
    return processor.getSeeds().code();
}

juce::String InkcapController::seedLabel() const
{
    return processor.getSeeds().label();
}

void InkcapController::copySeed()
{
    juce::SystemClipboard::copyTextToClipboard (seedCode());
    flash ("seed " + seedCode() + " copied");
}

SeedState::Paste InkcapController::pasteSeed (const juce::String& typed)
{
    const auto result = processor.getSeeds().paste (typed);

    switch (result)
    {
        case SeedState::Paste::nothing: flash ("nothing to paste: the seed stays"); break;
        case SeedState::Paste::code:    flash ("seed " + seedCode()); break;
        case SeedState::Paste::word:    flash ("\"" + seedLabel() + "\" is seed " + seedCode()); break;
    }

    return result;
}

void InkcapController::pasteSeedFromClipboard()
{
    pasteSeed (juce::SystemClipboard::getTextFromClipboard());
}

void InkcapController::rerollSeed()
{
    processor.getSeeds().reroll();
    flash ("new seed " + seedCode());
}

bool InkcapController::pickSeed (int historyIndex)
{
    const auto picked = processor.getSeeds().pickFromHistory (historyIndex);
    if (picked)
        flash ("seed " + seedCode());
    return picked;
}

bool InkcapController::isSeedLocked() const
{
    return processor.isSeedLocked();
}

void InkcapController::setSeedLocked (bool shouldLock)
{
    processor.setSeedLocked (shouldLock);
    flash (shouldLock ? "seed locked: presets keep it" : "seed unlocked: presets bring their own");
}

juce::String InkcapController::presetNumber() const
{
    const auto index = processor.getPresets().getCurrentIndex();
    return index < 0 ? juce::String() : juce::String (index + 1).paddedLeft ('0', 3);
}

void InkcapController::flash (const juce::String& message)
{
    flashText = message;
    flashLeft = flashSeconds;
}

void InkcapController::tick (float dt)
{
    if (flashLeft > 0.0f)
    {
        flashLeft -= dt;
        if (flashLeft <= 0.0f)
            flashText.clear();
    }
}

int InkcapController::getModulePage() const
{
    const auto stored = (int) processor.getUiState().getProperty (uistate::modulePage, -1);
    return juce::isPositiveAndBelow (stored, numModules) ? stored : -1;
}

void InkcapController::setModulePage (int moduleIndex)
{
    processor.getUiState().setProperty (uistate::modulePage, juce::isPositiveAndBelow (moduleIndex, numModules) ? moduleIndex : -1,
                                        nullptr);
}

} // namespace inkcap::ui
