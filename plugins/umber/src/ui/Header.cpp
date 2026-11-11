#include "ui/Header.h"

#include "plugin/PluginProcessor.h"

namespace umber::ui
{

namespace
{
    void setChoice (UmberProcessor& processor, const char* parameterId, int index)
    {
        if (auto* p = processor.getState().getParameter (parameterId))
        {
            processor.getUndoManager().beginNewTransaction();
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
            p->endChangeGesture();
        }
    }

    void addChoices (juce::PopupMenu& menu, UmberProcessor& processor, const char* parameterId, int firstId)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.getState().getParameter (parameterId));

        if (choice == nullptr)
            return;

        for (int i = 0; i < choice->choices.size(); ++i)
        {
            juce::PopupMenu::Item item (choice->choices[i]);
            item.itemID = firstId + i;
            item.isTicked = choice->getIndex() == i;
            item.action = [&processor, parameterId, i] { setChoice (processor, parameterId, i); };
            menu.addItem (std::move (item));
        }
    }
}

myzel::ui::HeaderConfig headerConfig (UmberProcessor& processor)
{
    myzel::ui::HeaderConfig c;
    c.name = "UMBER";
    c.productName = "Umber";
   #ifdef UMBER_VERSION_STRING
    c.version = UMBER_VERSION_STRING;
   #endif
    c.bypassId = params::bypass;
    c.bypassTooltip = "Bypass: the dry signal, latency-aligned, with a short crossfade";
    c.bypassedText = "Bypassed: the saturation is off";
    c.activeText = "Saturation on";
    c.settingsTooltip = "Settings: oversampling, mix lock, window size";
    c.mixLockLabel = "Lock Mix";
    c.hasViews = true;

    c.addSettings = [&processor] (juce::PopupMenu& menu)
    {
        menu.addSectionHeader ("Realtime quality");
        addChoices (menu, processor, params::quality, myzel::ui::HeaderConfig::firstPluginItemId);
        menu.addSectionHeader ("Offline render");
        addChoices (menu, processor, params::renderQuality, myzel::ui::HeaderConfig::firstPluginItemId + 100);

        const auto factor = processor.getMeters().oversamplingFactor.load (std::memory_order_relaxed);
        menu.addItem (-1, "Running at " + juce::String (factor) + juce::String::fromUTF8 ("\xc3\x97") + " oversampling", false, false);
    };

    return c;
}

} // namespace umber::ui
