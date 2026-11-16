#include "ui/Header.h"

#include "plugin/PluginProcessor.h"

namespace halite::ui
{

namespace
{
    constexpr int pad = 14, gap = 6, buttonHeight = 26, presetHeight = 28;
    constexpr int brandWidth = 92;          // the cube, "Halite"
    const juce::String version (HALITE_VERSION_STRING);
}

Header::PresetName::PresetName()
    : HelpedButton ("Preset")
{
    setHelp (helpFor ("preset"));
}

void Header::PresetName::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    auto area = getLocalBounds().toFloat();

    if (down || highlighted)
    {
        g.setColour (t.hover);
        g.fillRect (area.reduced (0.0f, 1.0f));
    }

    const auto prefixFont = fonts->sans (type::preset);
    const auto nameFont = fonts->sans (type::preset);
    const auto prefix = category.isEmpty() ? juce::String() : category + " / ";
    const auto dotSpace = modified ? 11.0f : 0.0f;
    const auto room = area.getWidth() - 16.0f - dotSpace;
    const auto prefixWidth = std::min (textWidth (prefixFont, prefix), room * 0.45f);
    const auto shownName = fitted (name, nameFont, room - prefixWidth);
    const auto nameWidth = textWidth (nameFont, shownName);
    const auto total = prefixWidth + nameWidth + dotSpace;
    auto x = area.getCentreX() - total * 0.5f;

    g.setFont (prefixFont);
    g.setColour (t.secondary);
    g.drawText (fitted (prefix, prefixFont, prefixWidth + 0.5f), juce::Rectangle<float> (x, 0.0f, prefixWidth + 1.0f, area.getHeight()),
                juce::Justification::centredLeft, false);
    x += prefixWidth;

    g.setFont (nameFont);
    g.setColour (t.text);
    g.drawText (shownName, juce::Rectangle<float> (x, 0.0f, nameWidth + 1.0f, area.getHeight()), juce::Justification::centredLeft, false);
    x += nameWidth;

    if (modified)
    {
        g.setColour (t.essText);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x + 7.5f, area.getCentreY() }));
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area.reduced (1.0f), 4.0f);
}

Header::Header (HaliteController& c)
    : controller (c)
{
    setTitle ("Header");
    setFocusContainerType (FocusContainerType::focusContainer);

    auto order = 1;
    for (auto* b : std::initializer_list<juce::Component*> { &previous, &presetName, &next, &ab, &undo, &redo, &latency, &bypass, &settings })
    {
        addAndMakeVisible (b);
        b->setExplicitFocusOrder (order++);
    }

    previous.setBorderless (true);
    next.setBorderless (true);
    previous.setHelp (helpFor ("presetPrev"));
    next.setHelp (helpFor ("presetNext"));
    previous.onClick = [this] { controller.getPresets().step (-1); };
    next.onClick = [this] { controller.getPresets().step (1); };
    presetName.onClick = [this] { if (onPresetMenu != nullptr) onPresetMenu(); };

    ab.setHelp (helpFor ("ab"));
    ab.setCompact (true);
    ab.getIndex = [this] { return controller.getAb().getActiveSlot(); };
    ab.setIndex = [this] (int slot) { controller.getAb().selectSlot (slot); };
    ab.onPopupMenu = [this] { if (onAbMenu != nullptr) onAbMenu(); };

    undo.setHelp (helpFor ("undo"));
    redo.setHelp (helpFor ("redo"));
    undo.onClick = [this] { controller.getUndoManager().undo(); };
    redo.onClick = [this] { controller.getUndoManager().redo(); };

    latency.setBorderless (true);
    latency.setMonoText (true);
    latency.setHelp (helpFor ("latency"));
    latency.onClick = [this] { if (onLatencyMenu != nullptr) onLatencyMenu(); };

    bypass.setCaution (true);
    bypass.setHelp (helpFor (params::bypass));
    bypass.onClick = [this] { controller.toggle (params::bypass); };

    settings.setHelp (helpFor ("settings"));
    settings.onClick = [this] { if (onSettingsMenu != nullptr) onSettingsMenu(); };
}

void Header::update()
{
    auto& presets = controller.getPresets();
    const auto category = presets.isCurrentUserPreset() ? juce::String ("User") : presets.getCurrentCategory();
    const auto name = presets.getCurrentIndex() >= 0 ? presets.getCurrentName() : juce::String ("Unsaved");
    const auto modified = presets.isModified();

    if (category != presetName.category || name != presetName.name || modified != presetName.modified)
    {
        presetName.category = category;
        presetName.name = name;
        presetName.modified = modified;
        presetName.setTitle ("Preset: " + (category.isEmpty() ? name : category + " / " + name) + (modified ? ", modified" : ""));
        presetName.repaint();
    }

    ab.update();
    undo.setEnabled (controller.getUndoManager().canUndo());
    redo.setEnabled (controller.getUndoManager().canRedo());
    latency.setText (controller.latencyText());
    bypass.setOn (controller.flag (params::bypass));
}

void Header::resized()
{
    auto area = getLocalBounds().reduced (pad, 0);
    const auto cy = area.getCentreY();

    showVersion = getWidth() >= 900;

    // Right cluster, from the right edge inwards.
    auto place = [&] (juce::Component& c, int w, int h)
    {
        c.setBounds (area.removeFromRight (w).withSizeKeepingCentre (w, h));
        area.removeFromRight (gap);
    };

    place (settings, 28, buttonHeight);
    place (bypass, (int) bypass.idealWidth(), buttonHeight);
    place (latency, std::max (58, (int) latency.idealWidth()), buttonHeight);
    place (redo, 28, buttonHeight);
    place (undo, 28, buttonHeight);
    place (ab, (int) ab.wellWidth(), (int) Segmented::wellHeight);

    // Left: the wordmark and the version.
    const auto left = brandWidth + (showVersion ? 40 : 0);
    area.removeFromLeft (left);
    area.reduce (8, 0);

    // The preset field: centred in the window where it fits, otherwise in the space between.
    const auto ideal = std::clamp (getWidth() / 3, 260, 380);
    const auto width = std::min (ideal, area.getWidth());
    auto x = getWidth() / 2 - width / 2;
    x = juce::jlimit (area.getX(), area.getRight() - width, x);
    presetBox = { x, cy - presetHeight / 2, width, presetHeight };

    auto box = presetBox.reduced (1);
    previous.setBounds (box.removeFromLeft (28));
    next.setBounds (box.removeFromRight (28));
    presetName.setBounds (box);
}

void Header::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (t.surface);
    g.setColour (t.line);
    g.fillRect (bounds.withTop (bounds.getBottom() - 1.0f));

    // The wordmark: Halite's cube filled with the accent, its inner edges cut in the background; the name, the version.
    auto left = bounds.reduced ((float) pad, 0.0f);
    {
        const auto box = left.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 20.0f);
        const auto at = juce::AffineTransform::translation (box.getX(), box.getY());
        static const auto outline = juce::Drawable::parseSVGPath ("M9 1.5 16.5 5.5V14.5L9 18.5 1.5 14.5V5.5Z");
        static const auto edges = juce::Drawable::parseSVGPath ("M1.5 5.5 9 9.5 16.5 5.5M9 9.5V18.5");

        g.setColour (t.essFill);
        g.fillPath (outline, at);
        g.setColour (t.removedEdge);    // Light's fill reads 1.7:1 on the paper (N7); transparent in Dark
        g.strokePath (outline, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved), at);
        g.setColour (t.background);
        g.strokePath (edges, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), at);
    }
    left.removeFromLeft (8.0f);

    g.setColour (t.text);
    g.setFont (fonts->sansSemiBold (type::wordmark));
    const auto nameWidth = textWidth (g.getCurrentFont(), "Halite") + 1.0f;
    g.drawText ("Halite", left.removeFromLeft (nameWidth), juce::Justification::centredLeft, false);

    if (showVersion)
    {
        left.removeFromLeft (8.0f);
        g.setColour (t.secondary);
        g.setFont (fonts->mono (type::label));
        g.drawText (version, left.removeFromLeft (44.0f), juce::Justification::centredLeft, false);
    }

    // The preset field's box.
    const auto box = presetBox.toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);
}

} // namespace halite::ui
