#pragma once

#include "common/ui/HeaderBar.h"
#include "ui/Theme.h"

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/** Cerulean's header: its name, texts and its own settings. */
myzel::ui::HeaderConfig headerConfig (CeruleanProcessor& processor);

} // namespace cerulean::ui
