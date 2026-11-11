#pragma once

#include "common/ui/HeaderBar.h"
#include "ui/Theme.h"

namespace umber
{
class UmberProcessor;
}

namespace umber::ui
{

/** Umber's header: its name, texts and its own settings. */
myzel::ui::HeaderConfig headerConfig (UmberProcessor& processor);

} // namespace umber::ui
