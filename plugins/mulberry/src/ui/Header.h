#pragma once

#include "common/ui/HeaderBar.h"
#include "ui/Theme.h"

namespace mulberry
{
class MulberryProcessor;
}

namespace mulberry::ui
{

/** Mulberry's header: its name, texts and its own settings. */
myzel::ui::HeaderConfig headerConfig (MulberryProcessor& processor);

} // namespace mulberry::ui
