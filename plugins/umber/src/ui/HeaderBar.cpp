#include "ui/HeaderBar.h"

#include "plugin/PluginProcessor.h"

namespace umber::ui
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
        rescanId
    };

    juce::String versionText()
    {
       #ifdef UMBER_VERSION_STRING
        return juce::String ("v") + UMBER_VERSION_STRING;
       #else
        return {};
       #endif
    }

    juce::String timesSign()
    {
        return juce::String::fromUTF8 ("\xc3\x97");
    }
}

HeaderBar::HeaderBar (UmberProcessor& p, PresetManager& pm, AbCompare& abCompare, InlineDialog& d)
    : processor (p),
      presets (pm),
      ab (abCompare),
      dialog (d)
{
    for (auto* b : std::initializer_list<juce::Component*> { &prevButton, &presetDisplay, &nextButton, &bypassButton, &slotA, &slotB,
                                                             &copyButton, &undoButton, &redoButton, &qualityButton, &scaleButton })
        addAndMakeVisible (b);

    bypassButton.setTooltip ("Bypass: the dry signal, latency-aligned, with a short crossfade");
    bypassButton.setTitle ("Bypass");
    bypassButton.setClickingTogglesState (false);

    // Not undoable on purpose: bypass is for listening, not part of the sound.
    bypassAttachment = std::make_unique<juce::ParameterAttachment> (
        *processor.getState().getParameter (params::bypass),
        [this] (float v) { bypassButton.setLit (v >= 0.5f); bypassButton.setToggleState (v >= 0.5f, juce::dontSendNotification); });
    bypassAttachment->sendInitialUpdate();
    bypassButton.onClick = [this] { bypassAttachment->setValueAsCompleteGesture (bypassButton.isLit() ? 0.0f : 1.0f); };

    prevButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    slotA.setTooltip ("Compare: setting A");
    slotB.setTooltip ("Compare: setting B");
    copyButton.setTooltip ("Copy the current setting to the other compare slot");
    undoButton.setTooltip ("Undo");
    redoButton.setTooltip ("Redo");
    scaleButton.setTooltip ("Window size");
    qualityButton.setShowsCaret (true);
    scaleButton.setShowsCaret (true);
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
    qualityButton.onClick = [this] { showQualityMenu(); };
    scaleButton.onClick = [this] { showScaleMenu(); };

    refresh();
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::refresh()
{
    presetDisplay.setPreset (presets.getCurrentName(), presets.getCurrentCategory(), presets.isModified());

    auto& um = processor.getUndoManager();
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());

    const auto active = ab.getActiveSlot();
    slotA.setLit (active == 0);
    slotB.setLit (active == 1);
    copyButton.setLabel (active == 0 ? juce::String::fromUTF8 ("A\xe2\x86\x92" "B") : juce::String::fromUTF8 ("B\xe2\x86\x92" "A"));

    const auto factor = processor.getMeters().oversamplingFactor.load (std::memory_order_relaxed);
    qualityButton.setLabel (juce::String (factor) + timesSign());

    auto& state = processor.getState();
    const auto qualityText = state.getParameter (params::quality)->getCurrentValueAsText();
    const auto renderText = state.getParameter (params::renderQuality)->getCurrentValueAsText();
    qualityButton.setTooltip ("Oversampling. Realtime: " + qualityText + ", offline render: " + renderText
                              + ". Currently running at " + juce::String (factor) + timesSign() + ".");

    if (getScale != nullptr)
        scaleButton.setLabel (juce::String (juce::roundToInt (getScale() * 100.0f)) + "%");
}

void HeaderBar::resized()
{
    background.invalidate();

    const auto y = (getHeight() - keyHeight) * 0.5f;
    auto key = [y] (float x, float w) { return juce::Rectangle<float> (x, y, w, keyHeight).toNearestInt(); };

    // Right to left.
    auto x = (float) getWidth() - 16.0f;
    x -= 56.0f; scaleButton.setBounds (key (x, 56.0f));
    x -= 4.0f + 46.0f; qualityButton.setBounds (key (x, 46.0f));
    x -= 22.0f + 28.0f; redoButton.setBounds (key (x, 28.0f));
    x -= 2.0f + 28.0f; undoButton.setBounds (key (x, 28.0f));
    x -= 22.0f + 44.0f; copyButton.setBounds (key (x, 44.0f));
    x -= 4.0f + 28.0f; slotB.setBounds (key (x, 28.0f));
    x -= 2.0f + 28.0f; slotA.setBounds (key (x, 28.0f));
    x -= 22.0f + 64.0f; bypassButton.setBounds (key (x, 64.0f));

    // Preset browser fills the middle.
    const auto right = x - 24.0f;
    const auto left = 300.0f;
    prevButton.setBounds (key (left, 26.0f));
    nextButton.setBounds (key (right - 26.0f, 26.0f));
    presetDisplay.setBounds (juce::Rectangle<float> (left + 30.0f, y - 2.0f, right - left - 60.0f, keyHeight + 4.0f).toNearestInt());
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
        const juce::String name ("UMBER");
        lg.setColour (pal.legend);
        lg.setFont (nameFont);
        lg.drawText (name, juce::Rectangle<float> (24.0f, 0.0f, 240.0f, a.getHeight()),
                     juce::Justification::centredLeft, false);

        const auto nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);
        lg.setColour (pal.legendDim);
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
        groove ((float) qualityButton.getX() - 11.0f);
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

void HeaderBar::setChoice (const char* parameterId, int index)
{
    if (auto* p = processor.getState().getParameter (parameterId))
    {
        processor.getUndoManager().beginNewTransaction();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void HeaderBar::showQualityMenu()
{
    auto& state = processor.getState();
    auto* quality = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (params::quality));
    auto* render = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (params::renderQuality));

    if (quality == nullptr || render == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Realtime quality");

    for (int i = 0; i < quality->choices.size(); ++i)
        menu.addItem (100 + i, quality->choices[i], true, quality->getIndex() == i);

    menu.addSectionHeader ("Offline render");

    for (int i = 0; i < render->choices.size(); ++i)
        menu.addItem (200 + i, render->choices[i], true, render->getIndex() == i);

    menu.addSeparator();
    const auto factor = processor.getMeters().oversamplingFactor.load (std::memory_order_relaxed);
    menu.addItem (-1, "Running at " + juce::String (factor) + timesSign() + " oversampling", false, false);

    juce::Component::SafePointer<HeaderBar> safe (this);
    menu.showMenuAsync (menuOptionsFor (qualityButton), [safe] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        if (result >= 200)
            safe->setChoice (params::renderQuality, result - 200);
        else if (result >= 100)
            safe->setChoice (params::quality, result - 100);

        safe->refresh();
    });
}

void HeaderBar::showScaleMenu()
{
    const auto current = getScale != nullptr ? getScale() : 1.0f;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Window size");

    for (int i = 0; i < (int) std::size (scales); ++i)
        menu.addItem (i + 1, juce::String (juce::roundToInt (scales[i] * 100.0f)) + " %", true,
                      std::abs (scales[i] - current) < 0.01f);

    juce::Component::SafePointer<HeaderBar> safe (this);
    menu.showMenuAsync (menuOptionsFor (scaleButton), [safe] (int result)
    {
        if (safe == nullptr || result <= 0 || safe->onScaleChosen == nullptr)
            return;

        safe->onScaleChosen (scales[result - 1]);
    });
}

} // namespace umber::ui
