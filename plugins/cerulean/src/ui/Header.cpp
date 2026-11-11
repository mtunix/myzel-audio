#include "ui/Header.h"

#include "plugin/PluginProcessor.h"

namespace cerulean::ui
{

myzel::ui::HeaderConfig headerConfig (CeruleanProcessor&)
{
    myzel::ui::HeaderConfig c;
    c.name = "CERULEAN";
    c.productName = "Cerulean";
   #ifdef CERULEAN_VERSION_STRING
    c.version = CERULEAN_VERSION_STRING;
   #endif
    c.bypassId = params::bypass;
    c.bypassTooltip = "Bypass: the dry signal passes untouched. With Trails on (Advanced), the reverb keeps ringing out.";
    c.bypassedText = "Bypassed: the reverb is off";
    c.activeText = "Reverb on";
    c.settingsTooltip = "Settings: mix lock, window size";
    c.mixLockLabel = "Lock Mix and Wet";
    c.hasViews = true;
    return c;
}

} // namespace cerulean::ui
