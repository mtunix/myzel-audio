#include "ui/PresetsPage.h"

#include "plugin/SeedState.h"

namespace inkcap::ui
{

namespace
{
    const juce::String userCategory { "User" };   // PresetManager files user presets under it
    constexpr float categoryPitch = 26.0f, actionsY = 298.0f, listY = 34.0f;
}

/** A category in the left column: lower case, amber while it is the one shown. */
class PresetsPage::CategoryKey final : public juce::Button
{
public:
    CategoryKey (PresetsPage& p, const juce::String& categoryName)
        : juce::Button (categoryName), name (categoryName), page (p)
    {
        setTitle (categoryName.isEmpty() ? juce::String ("All presets") : categoryName);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        setClickingTogglesState (false);
        onClick = [this] { page.setCategory (name); };
    }

    const juce::String name;

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& c = Theme::glass();
        const auto area = getLocalBounds().toFloat();
        const auto selected = page.category == name;

        if (selected)
            g.setColour (c.rowSelected), g.fillRoundedRectangle (area, metrics::rowCorner);
        else if (highlighted)
            g.setColour (c.rowHover), g.fillRoundedRectangle (area, metrics::rowCorner);

        g.setFont (page.fonts->mono (metrics::textSize));
        g.setColour (selected ? c.amber : c.text);
        g.drawText (name.isEmpty() ? juce::String ("all") : name.toLowerCase(), area.withTrimmedLeft (8.0f), juce::Justification::centredLeft, false);

        if (hasKeyboardFocus (false))
            glassdraw::focusRing (g, area, metrics::rowCorner);
    }

private:
    PresetsPage& page;
};

//==============================================================================
PresetsPage::PresetsPage (InkcapController& c, myzel::ui::InlineDialog& d)
    : controller (c), dialog (d)
{
    setTitle ("Presets");
    const auto& g = Theme::glass();

    search.setTitle ("Search presets");
    search.setTextToShowWhenEmpty ("search", g.textDim);
    search.setFont (fonts->mono (metrics::textSize));
    search.setColour (juce::TextEditor::backgroundColourId, g.glass.darker (0.4f));
    search.setColour (juce::TextEditor::textColourId, g.text);
    search.setColour (juce::TextEditor::highlightColourId, g.amber.withAlpha (0.3f));
    search.setColour (juce::TextEditor::highlightedTextColourId, g.text);
    search.setColour (juce::TextEditor::outlineColourId, g.line);
    search.setColour (juce::TextEditor::focusedOutlineColourId, g.amber);
    search.setColour (juce::CaretComponent::caretColourId, g.amber);
    search.setIndents (8, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onTextChange = [this] { filter(); };
    search.onEscapeKey = [this]
    {
        if (search.isEmpty())
        {
            if (onClose != nullptr)
                onClose();
        }
        else
        {
            search.clear();
            filter();
        }
    };
    search.onReturnKey = [this] { list.grabKeyboardFocus(); };
    addAndMakeVisible (search);

    list.setRowHeight ((int) rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, g.textFaint);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    list.setTitle ("Preset list");
    addAndMakeVisible (list);

    for (auto* k : { &saveKey, &saveAsKey, &renameKey, &deleteKey, &folderKey, &closeKey })
        addAndMakeVisible (k);

    saveKey.onClick = [this] { saveCurrent(); };
    saveAsKey.onClick = [this] { promptSaveAs(); };
    renameKey.onClick = [this] { promptRename(); };
    deleteKey.onClick = [this] { promptDelete(); };
    folderKey.onClick = [this]
    {
        const auto dir = controller.getPresets().getUserPresetDirectory();
        dir.createDirectory();
        dir.startAsProcess();
    };
    closeKey.onClick = [this] { if (onClose != nullptr) onClose(); };

    refresh();
}

PresetsPage::~PresetsPage()
{
    list.setModel (nullptr);
}

void PresetsPage::rebuildCategories()
{
    for (auto& k : categories)
        removeChildComponent (k.get());
    categories.clear();

    juce::StringArray names { "" };
    for (const auto& e : controller.getPresets().getEntries())
        if (e.isFactory)
            names.addIfNotAlreadyThere (e.category);
    names.add (userCategory);

    for (const auto& n : names)
    {
        categories.push_back (std::make_unique<CategoryKey> (*this, n));
        addAndMakeVisible (*categories.back());
    }

    if (! names.contains (category))
        category = {};

    resized();
}

void PresetsPage::refresh()
{
    shownCount = controller.getPresets().getEntries().size();
    rebuildCategories();
    filter();
}

void PresetsPage::setCategory (const juce::String& newCategory)
{
    category = newCategory;
    for (auto& k : categories)
        k->repaint();
    filter();
}

void PresetsPage::setSearch (const juce::String& text)
{
    search.setText (text, true);
    filter();
}

void PresetsPage::filter()
{
    const auto& entries = controller.getPresets().getEntries();
    const auto query = search.getText().trim();
    visible.clear();

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const auto inCategory = category.isEmpty() || (category == userCategory ? ! e.isFactory : (e.isFactory && e.category == category));
        const auto seed = e.values.getProperty (seedstate::seed).toString();
        const auto matches = query.isEmpty() || e.name.containsIgnoreCase (query) || e.category.containsIgnoreCase (query)
                          || seed.containsIgnoreCase (query);

        if (inCategory && matches)
            visible.push_back ((int) i);
    }

    list.updateContent();
    showCurrent();
    list.repaint();
}

void PresetsPage::showCurrent()
{
    const auto current = controller.getPresets().getCurrentIndex();
    const auto it = std::find (visible.begin(), visible.end(), current);
    const juce::ScopedValueSetter<bool> guard (syncingSelection, true);

    if (it != visible.end())
    {
        const auto row = (int) std::distance (visible.begin(), it);
        list.selectRow (row, false, true);
    }
    else
    {
        list.deselectAllRows();
    }

    shownCurrent = current;
    shownModified = controller.getPresets().isModified();

    auto& pm = controller.getPresets();
    const auto isUser = pm.isCurrentUserPreset();
    saveKey.setEnabled (isUser && pm.isModified());
    renameKey.setEnabled (isUser);
    deleteKey.setEnabled (isUser);
}

void PresetsPage::update()
{
    auto& pm = controller.getPresets();

    if (pm.getEntries().size() != shownCount)
        refresh();
    else if (pm.getCurrentIndex() != shownCurrent || pm.isModified() != shownModified)
    {
        showCurrent();
        list.repaint();
    }
}

void PresetsPage::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        if (isShowing())
            list.grabKeyboardFocus();
    }
    else
    {
        dialog.close();
    }
}

int PresetsPage::getNumRows()
{
    return (int) visible.size();
}

juce::String PresetsPage::getNameForRow (int row)
{
    const auto& entries = controller.getPresets().getEntries();
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return {};
    const auto& e = entries[(size_t) visible[(size_t) row]];
    return e.name + ", " + e.category;
}

void PresetsPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;

    const auto& c = Theme::glass();
    const auto& pm = controller.getPresets();
    const auto index = visible[(size_t) row];
    const auto& e = pm.getEntries()[(size_t) index];
    const auto current = index == pm.getCurrentIndex();
    const auto area = juce::Rectangle<float> ((float) width, (float) height).withTrimmedRight (10.0f);

    if (selected)
        g.setColour (c.rowSelected), g.fillRoundedRectangle (area.reduced (0.0f, 1.0f), metrics::rowCorner);

    const auto font = fonts->mono (metrics::textSize);
    g.setFont (font);
    g.setColour (c.textDim);
    g.drawText (juce::String (index + 1).paddedLeft ('0', 3), juce::Rectangle<float> (8.0f, 0.0f, 32.0f, area.getHeight()),
                juce::Justification::centredLeft, false);

    const auto seedWidth = 84.0f, categoryWidthInList = 150.0f;
    const auto nameArea = juce::Rectangle<float> (52.0f, 0.0f, area.getWidth() - 52.0f - seedWidth - categoryWidthInList - 16.0f, area.getHeight());
    auto name = e.name;
    if (current && pm.isModified())
        name += " *";
    glassdraw::fittedText (g, name, font, nameArea, juce::Justification::centredLeft, current ? c.amber : c.text);

    glassdraw::fittedText (g, e.isFactory ? e.category.toLowerCase() : juce::String ("user"), font,
                           { nameArea.getRight() + 8.0f, 0.0f, categoryWidthInList, area.getHeight() }, juce::Justification::centredLeft, c.textDim);

    const auto seed = e.values.getProperty (seedstate::seed).toString();
    g.setColour (c.textDim);
    g.drawText (seed, juce::Rectangle<float> (area.getRight() - seedWidth - 8.0f, 0.0f, seedWidth, area.getHeight()),
                juce::Justification::centredRight, false);
}

void PresetsPage::load (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;

    const auto index = visible[(size_t) row];
    auto& pm = controller.getPresets();

    if (index != pm.getCurrentIndex() || pm.isModified())
        pm.load (index);

    showCurrent();
    list.repaint();
}

void PresetsPage::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    load (row);
}

void PresetsPage::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    load (row);
    if (onClose != nullptr)
        onClose();
}

void PresetsPage::selectedRowsChanged (int lastRowSelected)
{
    // The arrow keys audition presets as they move; a selection the page makes itself does not.
    if (! syncingSelection && lastRowSelected >= 0 && list.hasKeyboardFocus (true))
        load (lastRowSelected);
}

void PresetsPage::returnKeyPressed (int lastRowSelected)
{
    load (lastRowSelected);
    if (onClose != nullptr)
        onClose();
}

bool PresetsPage::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose != nullptr)
    {
        onClose();
        return true;
    }
    return false;
}

void PresetsPage::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    g.setColour (c.line);
    g.fillRect (juce::Rectangle<float> (categoryWidth + 9.5f, 0.0f, 1.0f, actionsY - 10.0f));
    g.fillRect (juce::Rectangle<float> (listX, actionsY - 6.0f, (float) getWidth() - listX, 1.0f));

    if (visible.empty())
    {
        g.setFont (fonts->mono (metrics::textSize));
        g.setColour (c.textDim);
        const auto text = category == userCategory && search.isEmpty() ? juce::String ("no user presets yet: save one with \"save as\"")
                                                                       : juce::String ("no preset matches");
        g.drawText (text, juce::Rectangle<float> (listX, listY, (float) getWidth() - listX, 60.0f), juce::Justification::centred, false);
    }
}

void PresetsPage::resized()
{
    for (size_t i = 0; i < categories.size(); ++i)
        categories[i]->setBounds (juce::Rectangle<float> (0.0f, (float) i * categoryPitch, categoryWidth, rowHeight).toNearestInt());

    const auto w = (float) getWidth() - listX;
    search.setBounds (juce::Rectangle<float> (listX, 0.0f, w, 26.0f).toNearestInt());
    list.setBounds (juce::Rectangle<float> (listX, listY, w, actionsY - 10.0f - listY).toNearestInt());

    auto x = listX;
    for (auto* k : { &saveKey, &saveAsKey, &renameKey, &deleteKey, &folderKey })
    {
        const auto kw = k->idealWidth();
        k->setBounds (juce::Rectangle<float> (x, actionsY, kw, 24.0f).toNearestInt());
        x += kw + 16.0f;
    }
    const auto cw = closeKey.idealWidth();
    closeKey.setBounds (juce::Rectangle<float> ((float) getWidth() - cw, actionsY, cw, 24.0f).toNearestInt());
}

//==============================================================================
void PresetsPage::saveCurrent()
{
    auto& pm = controller.getPresets();

    if (! pm.isCurrentUserPreset())
    {
        promptSaveAs();
        return;
    }

    const auto result = pm.saveUserPreset (pm.getCurrentName());
    if (result.failed())
    {
        myzel::ui::InlineDialog::Options o;
        o.title = "Could not save";
        o.message = result.getErrorMessage();
        dialog.open (std::move (o));
    }
    refresh();
}

void PresetsPage::promptSaveAs()
{
    auto& pm = controller.getPresets();
    myzel::ui::InlineDialog::Options o;
    o.title = "Save preset";
    o.message = "Name your preset. It keeps the seed, and is stored as an XML file in your user preset folder.";
    o.withTextField = true;
    o.initialText = pm.isCurrentUserPreset() ? pm.getCurrentName() : juce::String();
    o.confirmLabel = "Save";

    juce::Component::SafePointer<PresetsPage> safe (this);
    o.onConfirm = [safe] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};

        auto& presets = safe->controller.getPresets();
        if (presets.userPresetExists (text.trim()))
            return "A preset called \"" + text.trim() + "\" already exists. Choose another name.";

        const auto result = presets.saveUserPreset (text);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

void PresetsPage::promptRename()
{
    auto& pm = controller.getPresets();
    const auto index = pm.getCurrentIndex();
    if (! pm.isCurrentUserPreset() || index < 0)
        return;

    myzel::ui::InlineDialog::Options o;
    o.title = "Rename preset";
    o.message = "Enter a new name for \"" + pm.getEntries()[(size_t) index].name + "\".";
    o.withTextField = true;
    o.initialText = pm.getEntries()[(size_t) index].name;
    o.confirmLabel = "Rename";

    juce::Component::SafePointer<PresetsPage> safe (this);
    o.onConfirm = [safe, index] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};

        const auto result = safe->controller.getPresets().renameUserPreset (index, text);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

void PresetsPage::promptDelete()
{
    auto& pm = controller.getPresets();
    const auto index = pm.getCurrentIndex();
    if (! pm.isCurrentUserPreset() || index < 0)
        return;

    myzel::ui::InlineDialog::Options o;
    o.title = "Delete preset";
    o.message = "Delete \"" + pm.getEntries()[(size_t) index].name + "\"? The file is removed from disk.";
    o.confirmLabel = "Delete";
    o.destructive = true;

    juce::Component::SafePointer<PresetsPage> safe (this);
    o.onConfirm = [safe, index] (const juce::String&) -> juce::String
    {
        if (safe == nullptr)
            return {};

        const auto result = safe->controller.getPresets().deleteUserPreset (index);
        safe->refresh();
        return result.wasOk() ? juce::String() : result.getErrorMessage();
    };

    dialog.open (std::move (o));
}

} // namespace inkcap::ui
