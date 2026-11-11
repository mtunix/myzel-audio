#include "common/ui/Theme.h"

#include <MyzelAssets.h>

namespace myzel::ui
{

Fonts::Fonts()
    : medium   (juce::Typeface::createSystemTypefaceFor (MyzelAssets::InterMedium_otf,
                                                         (size_t) MyzelAssets::InterMedium_otfSize)),
      semiBold (juce::Typeface::createSystemTypefaceFor (MyzelAssets::InterSemiBold_otf,
                                                         (size_t) MyzelAssets::InterSemiBold_otfSize)),
      black    (juce::Typeface::createSystemTypefaceFor (MyzelAssets::InterDisplayBlack_otf,
                                                         (size_t) MyzelAssets::InterDisplayBlack_otfSize))
{
}

juce::Font Fonts::legend (float height) const
{
    return juce::Font (juce::FontOptions (semiBold).withHeight (height));
}

juce::Font Fonts::label (float height) const
{
    return juce::Font (juce::FontOptions (medium).withHeight (height));
}

juce::Font Fonts::display (float height) const
{
    return juce::Font (juce::FontOptions (black).withHeight (height));
}

} // namespace myzel::ui
