#pragma once

#include "common/ui/InlineDialog.h"
#include "ui/GlassWidgets.h"
#include "ui/InkcapController.h"

namespace inkcap::ui
{

/**
    The preset browser: the whole display below the header, from the presets soft key. Categories
    on the left, a search field and the list (number, name, category, seed) on the right, the
    file actions under it. A click loads a preset and keeps the page open, so sounds can be
    compared; the arrow keys step through the list the same way; Return or a double-click loads
    and closes, Escape closes. Save, rename and delete ask in the shared inline dialog.
*/
class PresetsPage final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    PresetsPage (InkcapController&, myzel::ui::InlineDialog&);
    ~PresetsPage() override;

    /** Re-reads the preset list (after a save, rename, delete or rescan) and shows the current preset. */
    void refresh();
    void update();

    std::function<void()> onClose;

    void setCategory (const juce::String& category);   // "" = all
    juce::String getCategory() const { return category; }
    void setSearch (const juce::String& text);
    const std::vector<int>& getVisible() const noexcept { return visible; }

    juce::ListBox& getList() noexcept { return list; }
    juce::TextEditor& getSearchField() noexcept { return search; }
    SoftKey& getSaveKey() noexcept { return saveKey; }
    SoftKey& getSaveAsKey() noexcept { return saveAsKey; }
    SoftKey& getRenameKey() noexcept { return renameKey; }
    SoftKey& getDeleteKey() noexcept { return deleteKey; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    static constexpr float categoryWidth = 170.0f, listX = 190.0f, rowHeight = 24.0f;

private:
    class CategoryKey;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void returnKeyPressed (int lastRowSelected) override;
    juce::String getNameForRow (int row) override;

    void rebuildCategories();
    void filter();
    void showCurrent();
    void load (int row);
    void saveCurrent();
    void promptSaveAs();
    void promptRename();
    void promptDelete();

    InkcapController& controller;
    myzel::ui::InlineDialog& dialog;
    juce::SharedResourcePointer<GlassFonts> fonts;

    std::vector<std::unique_ptr<CategoryKey>> categories;
    juce::String category;
    juce::TextEditor search;
    juce::ListBox list { "Presets", this };
    std::vector<int> visible;          // indices into PresetManager::getEntries()

    SoftKey saveKey { "save", "Save the changes to this user preset" };
    SoftKey saveAsKey { "save as", "Save as a new user preset" };
    SoftKey renameKey { "rename", "Rename this user preset" };
    SoftKey deleteKey { "delete", "Delete this user preset" };
    SoftKey folderKey { "folder", "Show the user preset folder" };
    SoftKey closeKey { "close", "Close the preset browser" };

    int shownCurrent = -2;
    bool shownModified = false;
    size_t shownCount = 0;
    bool syncingSelection = false;
};

} // namespace inkcap::ui
