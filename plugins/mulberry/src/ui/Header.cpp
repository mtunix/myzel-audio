#include "ui/Header.h"

#include "plugin/PluginProcessor.h"

namespace mulberry::ui
{

myzel::ui::HeaderConfig headerConfig (MulberryProcessor&)
{
    myzel::ui::HeaderConfig c;
    c.name = "MULBERRY";
    c.productName = "Mulberry";
   #ifdef MULBERRY_VERSION_STRING
    c.version = MULBERRY_VERSION_STRING;
   #endif
    c.bypassId = params::bypass;
    c.bypassTooltip = "Bypass: the dry signal passes untouched. With Trails on (Advanced), the echoes ring out.";
    c.bypassedText = "Bypassed: the echo is off";
    c.activeText = "Echo on";
    c.settingsTooltip = "Settings: mix lock, window size";
    c.mixLockLabel = "Lock Mix and Wet";
    c.mixLockTooltip = "Mix lock is on: presets and A/B keep Mix and Wet as they are. Turn it off in the settings menu.";
    c.hasViews = true;
    return c;
}

} // namespace mulberry::ui
