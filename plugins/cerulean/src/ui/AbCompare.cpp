#include "ui/AbCompare.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace cerulean::ui
{

AbCompare::AbCompare (CeruleanProcessor& p, PresetManager& pm)
    : processor (p),
      presets (pm)
{
}

int AbCompare::getActiveSlot() const
{
    return juce::jlimit (0, 1, (int) processor.getUiState().getProperty (uistate::abActive, 0));
}

juce::ValueTree AbCompare::slotTree (int slot) const
{
    return processor.getUiState().getChildWithName (slot == 0 ? uistate::abSlot0 : uistate::abSlot1);
}

void AbCompare::storeInto (int slot)
{
    auto ui = processor.getUiState();
    const auto& id = slot == 0 ? uistate::abSlot0 : uistate::abSlot1;

    auto snapshot = presets.capture();
    snapshot.setProperty (uistate::presetName, presets.getCurrentName(), nullptr);
    snapshot.setProperty (uistate::presetUser, presets.isCurrentUserPreset(), nullptr);

    if (auto existing = ui.getChildWithName (id); existing.isValid())
        ui.removeChild (existing, nullptr);

    juce::ValueTree child (id);
    child.copyPropertiesFrom (snapshot, nullptr);
    ui.appendChild (child, nullptr);
}

void AbCompare::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    const auto active = getActiveSlot();

    if (slot == active)
        return;

    storeInto (active);

    const auto target = slotTree (slot);

    if (target.isValid() && target.getNumProperties() > 0)
    {
        presets.apply (target, slot == 0 ? "Recall A" : "Recall B");
        presets.setCurrent (target.getProperty (uistate::presetName).toString(),
                            (bool) target.getProperty (uistate::presetUser, false));
    }
    else
    {
        storeInto (slot);
    }

    processor.getUiState().setProperty (uistate::abActive, slot, nullptr);

    if (onChange != nullptr)
        onChange();
}

void AbCompare::copyActiveToOther()
{
    storeInto (1 - getActiveSlot());

    if (onChange != nullptr)
        onChange();
}

} // namespace cerulean::ui
