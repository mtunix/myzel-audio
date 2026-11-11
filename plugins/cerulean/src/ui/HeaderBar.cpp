#include "ui/HeaderBar.h"

#include "plugin/PluginProcessor.h"

namespace cerulean::ui
{

namespace
{
    constexpr float keyHeight = 28.0f;
    constexpr float scales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    enum MenuIds
    {
        presetBase = 1,          // + entry index
        saveAsId = 10000,
        overwriteId,
        renameId,
        deleteId,
        revealId,
        rescanId,
        mixLockId,
        scaleBase = 20000       // + index into scales
    };

    juce::String versionText()
    {
       #ifdef CERULEAN_VERSION_STRING
        return juce::String ("v") + CERULEAN_VERSION_STRING;
       #else
        return {};
       #endif
    }

}

HeaderBar::HeaderBar (CeruleanProcessor& p, PresetManager& pm, AbCompare& abCompare, InlineDialog& d)
    : processor (p),
      presets (pm),
      ab (abCompare),
      dialog (d)
{
    for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &presetDisplay, &nextButton, &bypassButton, &slotA, &slotB,
                                                             &copyButton, &undoButton, &redoButton, &panelViewButton, &studioViewButton,
                                                             &settingsButton })
        addAndMakeVisible (b);

    addChildComponent (saveButton);   // over the right end of the preset field, only with unsaved changes

    bypassButton.setTooltip ("Bypass: the dry signal passes untouched. With Trails on (Advanced), the reverb keeps ringing out.");
    bypassButton.setTitle ("Bypass");
    bypassButton.setClickingTogglesState (false);

    // Not undoable on purpose: bypass is for listening, not part of the sound.
    bypassAttachment = std::make_unique<juce::ParameterAttachment> (
        *processor.getState().getParameter (params::bypass),
        [this] (float v)
        {
            bypassButton.setLit (v >= 0.5f);
            bypassButton.setToggleState (v >= 0.5f, juce::dontSendNotification);
            bypassButton.setDescription (v >= 0.5f ? "Bypassed: the reverb is off" : "Reverb on");
        });
    bypassButton.setCaution (true);
    bypassAttachment->sendInitialUpdate();
    bypassButton.onClick = [this] { bypassAttachment->setValueAsCompleteGesture (bypassButton.isLit() ? 0.0f : 1.0f); };

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    slotA.setTooltip ("Compare: setting A");
    slotB.setTooltip ("Compare: setting B");
    copyButton.setTooltip ("Copy the current setting to the other compare slot");
    undoButton.setTooltip ("Undo");
    redoButton.setTooltip ("Redo");
    settingsButton.setTooltip ("Settings: mix lock, window size");
    settingsButton.setTitle ("Settings");
    saveButton.setTooltip ("Save the changes: overwrites a user preset, or asks for a name for a factory preset.");
    saveButton.setTitle ("Save preset");
    saveButton.setLit (true);
    panelViewButton.setSegment (HeaderButton::Segment::left);
    studioViewButton.setSegment (HeaderButton::Segment::right);
    panelViewButton.setTooltip ("Panel view: the instrument face, knobs and keys. New instances open in the view you chose last.");
    studioViewButton.setTooltip ("Studio view: every control on one flat screen, with exact values, meters and a per-band tail graph. "
                                 "New instances open in the view you chose last.");
    panelViewButton.setTitle ("Panel view");
    studioViewButton.setTitle ("Studio view");
    slotA.setTitle ("Compare slot A");
    slotB.setTitle ("Compare slot B");
    copyButton.setTitle ("Copy to other compare slot");

    prevButton.onClick = [this] { presets.step (-1); refresh(); };
    nextButton.onClick = [this] { presets.step (1); refresh(); };
    presetDisplay.onClick = [this] { showPresetMenu(); };
    slotA.onClick = [this] { ab.selectSlot (0); refresh(); };
    slotB.onClick = [this] { ab.selectSlot (1); refresh(); };
    copyButton.onClick = [this] { ab.copyActiveToOther(); refresh(); };
    undoButton.onClick = [this] { processor.getUndoManager().undo(); refresh(); };
    redoButton.onClick = [this] { processor.getUndoManager().redo(); refresh(); };
    saveButton.onClick = [this] { saveCurrent(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    auto chooseView = [this] (ViewMode m)
    {
        if (onViewChosen != nullptr)
            onViewChosen (m);
    };
    panelViewButton.onClick = [chooseView] { chooseView (ViewMode::panel); };
    studioViewButton.onClick = [chooseView] { chooseView (ViewMode::studio); };

    refresh();
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::setView (ViewMode mode)
{
    view = mode;
    panelViewButton.setLit (mode == ViewMode::panel);
    studioViewButton.setLit (mode == ViewMode::studio);
}

void HeaderBar::refresh()
{
    presetDisplay.setPreset (presets.getCurrentName(), presets.getCurrentCategory(), presets.isModified());

    if (saveButton.isVisible() != presets.isModified())
    {
        saveButton.setVisible (presets.isModified());
        presetDisplay.setReservedRight (presets.isModified() ? (float) saveButton.getWidth() : 0.0f);
    }

    auto& um = processor.getUndoManager();
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());

    const auto active = ab.getActiveSlot();
    slotA.setLit (active == 0);
    slotB.setLit (active == 1);
    copyButton.setLabel (active == 0 ? juce::String::fromUTF8 ("A\xe2\x86\x92" "B") : juce::String::fromUTF8 ("B\xe2\x86\x92" "A"));
}

void HeaderBar::resized()
{
    background.invalidate();

    const auto y = (getHeight() - keyHeight) * 0.5f;
    auto key = [y] (float x, float w) { return juce::Rectangle<float> (x, y, w, keyHeight).toNearestInt(); };

    // Right to left. The same in both views: nothing moves when the view changes.
    auto x = (float) getWidth() - 16.0f;
    x -= 30.0f; settingsButton.setBounds (key (x, 30.0f));
    x -= 12.0f + 60.0f; studioViewButton.setBounds (key (x, 60.0f));
    x -= 54.0f; panelViewButton.setBounds (key (x, 54.0f));

    x -= 22.0f + 28.0f; redoButton.setBounds (key (x, 28.0f));
    x -= 2.0f + 28.0f; undoButton.setBounds (key (x, 28.0f));
    x -= 22.0f + 44.0f; copyButton.setBounds (key (x, 44.0f));
    x -= 4.0f + 28.0f; slotB.setBounds (key (x, 28.0f));
    x -= 2.0f + 28.0f; slotA.setBounds (key (x, 28.0f));
    x -= 22.0f + 64.0f; bypassButton.setBounds (key (x, 64.0f));

    // Preset browser fills the middle.
    const auto right = x - 24.0f;
    const auto left = 168.0f;
    prevButton.setBounds (key (left, 26.0f));
    nextButton.setBounds (key (right - 26.0f, 26.0f));
    presetDisplay.setBounds (juce::Rectangle<float> (left + 30.0f, y - 2.0f, right - left - 60.0f, keyHeight + 4.0f).toNearestInt());

    // SAVE sits inside the preset field, just left of its caret.
    constexpr float saveWidth = 46.0f;
    presetDisplay.setReservedRight (saveWidth);
    saveButton.setBounds (presetDisplay.reservedArea().translated ((float) presetDisplay.getX(), (float) presetDisplay.getY()).toNearestInt());
    presetDisplay.setReservedRight (saveButton.isVisible() ? saveWidth : 0.0f);
}

void HeaderBar::paint (juce::Graphics& g)
{
    background.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        const auto& pal = Theme::palette();

        // Flat dark rail.
        lg.setGradientFill (juce::ColourGradient (pal.headerTop, 0.0f, a.getY(), pal.headerBottom, 0.0f, a.getBottom(), false));
        lg.fillRect (a);
        lg.setColour (juce::Colour (0x1affffff));
        lg.drawHorizontalLine (0, a.getX(), a.getRight());

        // Bottom edge: the rail overhangs the panel and casts a short shadow onto it (drawn by the panel).
        lg.setColour (juce::Colour (0xff000000));
        lg.drawHorizontalLine ((int) a.getBottom() - 1, a.getX(), a.getRight());

        // The name, set plainly: no emblem, no effects. The version follows it, dimmed, so
        // screenshots and bug reports always say which build they are from.
        const auto nameFont = drawing::tracked (fonts->legend (12.5f), 0.26f);
        const juce::String name ("CERULEAN");
        lg.setColour (pal.headerLegend);
        lg.setFont (nameFont);
        lg.drawText (name, juce::Rectangle<float> (24.0f, 0.0f, 240.0f, a.getHeight()),
                     juce::Justification::centredLeft, false);

        const auto nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);
        lg.setColour (pal.headerLegendDim);
        lg.setFont (drawing::tracked (fonts->label (10.0f), 0.06f));
        lg.drawText (versionText(), juce::Rectangle<float> (24.0f + nameWidth + 8.0f, 1.0f, 80.0f, a.getHeight()),
                     juce::Justification::centredLeft, false);

        // Grooves between groups.
        auto groove = [&lg, &a] (float x)
        {
            lg.setColour (juce::Colour (0xc0000000));
            lg.drawVerticalLine ((int) x, a.getY() + 12.0f, a.getBottom() - 12.0f);
            lg.setColour (juce::Colour (0x14ffffff));
            lg.drawVerticalLine ((int) x + 1, a.getY() + 12.0f, a.getBottom() - 12.0f);
        };

        groove ((float) slotA.getX() - 12.0f);
        groove ((float) bypassButton.getX() - 11.0f);
        groove ((float) undoButton.getX() - 11.0f);
        groove ((float) panelViewButton.getX() - 11.0f);
    });
}

//==============================================================================
juce::PopupMenu::Options HeaderBar::menuOptionsFor (juce::Component& target) const
{
    auto options = juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (target.getWidth());

    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        options = options.withParentComponent (editor);

    return options;
}

void HeaderBar::showPresetMenu()
{
    const auto& entries = presets.getEntries();
    const auto current = presets.getCurrentIndex();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory");

    juce::StringArray categories;

    for (const auto& e : entries)
        if (e.isFactory)
            categories.addIfNotAlreadyThere (e.category);

    for (const auto& category : categories)
    {
        juce::PopupMenu sub;
        auto containsCurrent = false;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].isFactory && entries[i].category == category)
            {
                sub.addItem ((int) i + presetBase, entries[i].name, true, (int) i == current);
                containsCurrent = containsCurrent || (int) i == current;
            }
        }

        menu.addSubMenu (category, sub, true, nullptr, containsCurrent);
    }

    menu.addSectionHeader ("User");
    auto anyUser = false;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (! entries[i].isFactory)
        {
            menu.addItem ((int) i + presetBase, entries[i].name, true, (int) i == current);
            anyUser = true;
        }
    }

    if (! anyUser)
        menu.addItem (-1, "No user presets yet", false, false);

    menu.addSeparator();
    menu.addItem (saveAsId, "Save As...");

    const auto isUser = current >= 0 && ! entries[(size_t) current].isFactory;
    menu.addItem (overwriteId, "Save \"" + presets.getCurrentName() + "\"", isUser && presets.isModified());
    menu.addItem (renameId, "Rename...", isUser);
    menu.addItem (deleteId, "Delete...", isUser);
    menu.addSeparator();
    menu.addItem (revealId, "Show Preset Folder");
    menu.addItem (rescanId, "Rescan Presets");

    juce::Component::SafePointer<HeaderBar> safe (this);

    menu.showMenuAsync (menuOptionsFor (presetDisplay), [safe, current] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;

        if (result >= presetBase && result < saveAsId)
            self.presets.load (result - presetBase);
        else if (result == saveAsId)
            self.promptSaveAs();
        else if (result == overwriteId)
            self.presets.saveUserPreset (self.presets.getCurrentName());
        else if (result == renameId)
            self.promptRename (current);
        else if (result == deleteId)
            self.promptDelete (current);
        else if (result == revealId)
        {
            const auto dir = PresetManager::getUserPresetDirectory();
            dir.createDirectory();
            dir.startAsProcess();
        }
        else if (result == rescanId)
            self.presets.scanUserPresets();

        self.refresh();
    });
}

void HeaderBar::promptSaveAs()
{
    InlineDialog::Options o;
    o.title = "Save preset";
    o.message = "Name your preset. It is stored as an XML file in your user preset folder.";
    o.withTextField = true;
    o.initialText = presets.isCurrentUserPreset() ? presets.getCurrentName() : juce::String();
    o.confirmLabel = "Save";

    juce::Component::SafePointer<HeaderBar> safe (this);
    o.onConfirm = [safe] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};

        if (safe->presets.userPresetExists (text.trim()))
            return "A preset called \"" + text.trim() + "\" already exists. Choose another name.";

        const auto result = safe->presets.saveUserPreset (text);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

void HeaderBar::promptRename (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.getEntries().size()))
        return;

    InlineDialog::Options o;
    o.title = "Rename preset";
    o.message = "Enter a new name for \"" + presets.getEntries()[(size_t) index].name + "\".";
    o.withTextField = true;
    o.initialText = presets.getEntries()[(size_t) index].name;
    o.confirmLabel = "Rename";

    juce::Component::SafePointer<HeaderBar> safe (this);
    o.onConfirm = [safe, index] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};

        const auto result = safe->presets.renameUserPreset (index, text);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

void HeaderBar::promptDelete (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.getEntries().size()))
        return;

    InlineDialog::Options o;
    o.title = "Delete preset";
    o.message = "Delete \"" + presets.getEntries()[(size_t) index].name + "\"? The file is removed from disk.";
    o.confirmLabel = "Delete";
    o.destructive = true;

    juce::Component::SafePointer<HeaderBar> safe (this);
    o.onConfirm = [safe, index] (const juce::String&) -> juce::String
    {
        if (safe == nullptr)
            return {};

        const auto result = safe->presets.deleteUserPreset (index);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

void HeaderBar::saveCurrent()
{
    if (presets.isCurrentUserPreset())
    {
        const auto result = presets.saveUserPreset (presets.getCurrentName());

        if (result.failed())
        {
            InlineDialog::Options o;
            o.title = "Could not save";
            o.message = result.getErrorMessage();
            o.confirmLabel = "OK";
            dialog.open (std::move (o));
        }

        refresh();
        return;
    }

    // Factory presets are read-only: save the changes under a new name.
    promptSaveAs();
}

void HeaderBar::showSettingsMenu()
{
    const auto current = getScale != nullptr ? getScale() : 1.0f;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Presets");
    menu.addItem (mixLockId, "Lock Mix and Wet", true, presets.isMixLocked());

    menu.addSectionHeader ("Window size");

    for (int i = 0; i < (int) std::size (scales); ++i)
        menu.addItem (scaleBase + i, juce::String (juce::roundToInt (scales[i] * 100.0f)) + " %", true,
                      std::abs (scales[i] - current) < 0.01f);

    menu.addSeparator();
    menu.addItem (-1, "Cerulean " + versionText() + " by Myzel Audio", false, false);

    juce::Component::SafePointer<HeaderBar> safe (this);
    menu.showMenuAsync (menuOptionsFor (settingsButton).withMinimumWidth (170), [safe] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == mixLockId)
        {
            safe->presets.setMixLocked (! safe->presets.isMixLocked());
            return;
        }

        if (result < scaleBase || result >= scaleBase + (int) std::size (scales) || safe->onScaleChosen == nullptr)
            return;

        safe->onScaleChosen (scales[result - scaleBase]);
    });
}

} // namespace cerulean::ui
