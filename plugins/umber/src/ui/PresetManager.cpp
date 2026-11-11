#include "ui/PresetManager.h"

#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"
#include "ui/UiState.h"

namespace umber::ui
{

namespace
{
    const juce::Identifier presetType { "UmberPreset" };
    const juce::Identifier nameId { "name" };
    const juce::Identifier categoryId { "category" };
    const juce::Identifier versionId { "presetVersion" };
    constexpr int presetVersion = 2;   // 2: adds stereoMode, emphasis, bias

    /** Normalised value a snapshot asks for. Parameters a (older) snapshot does not mention go
        to their defaults, so loading it never keeps a stray value from the current sound. */
    float targetFor (const juce::ValueTree& snapshot, const juce::Identifier& key, juce::RangedAudioParameter& p)
    {
        return snapshot.hasProperty (key) ? p.convertTo0to1 ((float) snapshot.getProperty (key))
                                          : p.getDefaultValue();
    }

    juce::ValueTree snapshotFromFactory (const FactoryPreset& p)
    {
        juce::ValueTree t (presetType);
        t.setProperty (nameId, p.name, nullptr);
        t.setProperty (categoryId, p.category, nullptr);
        t.setProperty (params::drive, p.drive, nullptr);
        t.setProperty (params::model, p.model, nullptr);
        t.setProperty (params::slam, p.slam ? 1.0f : 0.0f, nullptr);
        t.setProperty (params::tone, p.tone, nullptr);
        t.setProperty (params::lowCut, p.lowCutHz, nullptr);
        t.setProperty (params::weight, p.weight ? 1.0f : 0.0f, nullptr);
        t.setProperty (params::highCut, p.highCutHz, nullptr);
        t.setProperty (params::autoGain, p.autoGain ? 1.0f : 0.0f, nullptr);
        t.setProperty (params::mix, p.mix, nullptr);
        t.setProperty (params::output, p.outputDb, nullptr);
        t.setProperty (params::stereoMode, p.stereoMode, nullptr);
        t.setProperty (params::emphasis, p.emphasis, nullptr);
        t.setProperty (params::bias, p.bias, nullptr);
        return t;
    }
}

PresetManager::PresetManager (UmberProcessor& p)
    : processor (p)
{
    scanUserPresets();
}

const juce::StringArray& PresetManager::parameterIds()
{
    static const juce::StringArray ids { params::drive, params::model, params::slam, params::tone, params::lowCut,
                                         params::weight, params::highCut, params::autoGain, params::mix, params::output,
                                         params::stereoMode, params::emphasis, params::bias };
    return ids;
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Myzel Audio")
        .getChildFile ("Umber")
        .getChildFile ("Presets");
}

juce::ValueTree PresetManager::readPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType (presetType))
            return tree;
    }

    return {};
}

void PresetManager::scanUserPresets()
{
    entries.clear();

    for (const auto& f : factoryPresets())
        entries.push_back ({ f.name, f.category, true, {}, snapshotFromFactory (f) });

    numFactory = (int) entries.size();

    std::vector<Entry> user;
    const auto dir = getUserPresetDirectory();

    if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*" + fileExtension))
        {
            const auto file = entry.getFile();
            auto values = readPresetFile (file);

            if (! values.isValid())
                continue;

            auto name = values.getProperty (nameId, file.getFileNameWithoutExtension()).toString();
            user.push_back ({ name, "User", false, file, values });
        }
    }

    std::sort (user.begin(), user.end(), [] (const Entry& a, const Entry& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    for (auto& e : user)
        entries.push_back (std::move (e));
}

juce::ValueTree PresetManager::capture() const
{
    auto& apvts = processor.getState();
    juce::ValueTree t (presetType);
    t.setProperty (versionId, presetVersion, nullptr);

    for (const auto& id : parameterIds())
        if (auto* p = apvts.getParameter (id))
            t.setProperty (juce::Identifier (id), p->convertFrom0to1 (p->getValue()), nullptr);

    return t;
}

void PresetManager::apply (const juce::ValueTree& snapshot, const juce::String& undoName)
{
    auto& apvts = processor.getState();
    processor.getUndoManager().beginNewTransaction (undoName);

    for (const auto& id : parameterIds())
    {
        const juce::Identifier key (id);

        if (auto* p = apvts.getParameter (id))
        {
            const auto target = targetFor (snapshot, key, *p);

            if (std::abs (target - p->getValue()) > 1.0e-6f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (target);
                p->endChangeGesture();
            }
        }
    }
}

void PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return;

    const auto& e = entries[(size_t) index];
    apply (e.values, "Load preset " + e.name);
    setCurrent (e.name, ! e.isFactory);
}

void PresetManager::step (int delta)
{
    const auto count = (int) entries.size();

    if (count == 0)
        return;

    auto index = getCurrentIndex();
    index = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
    load (index);
}

int PresetManager::getCurrentIndex() const
{
    const auto name = getCurrentName();
    const auto isUser = isCurrentUserPreset();

    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name == name && entries[i].isFactory != isUser)
            return (int) i;

    return -1;
}

juce::String PresetManager::getCurrentName() const
{
    return processor.getUiState().getProperty (uistate::presetName, entries.empty() ? juce::String() : entries.front().name);
}

juce::String PresetManager::getCurrentCategory() const
{
    const auto index = getCurrentIndex();
    return index >= 0 ? entries[(size_t) index].category : juce::String();
}

bool PresetManager::isCurrentUserPreset() const
{
    return processor.getUiState().getProperty (uistate::presetUser, false);
}

void PresetManager::setCurrent (const juce::String& name, bool isUser)
{
    auto ui = processor.getUiState();
    ui.setProperty (uistate::presetName, name, nullptr);
    ui.setProperty (uistate::presetUser, isUser, nullptr);
    notify();
}

bool PresetManager::isModified() const
{
    const auto index = getCurrentIndex();

    if (index < 0)
        return true;

    const auto& values = entries[(size_t) index].values;
    auto& apvts = processor.getState();

    for (const auto& id : parameterIds())
    {
        const juce::Identifier key (id);

        if (auto* p = apvts.getParameter (id))
            if (std::abs (targetFor (values, key, *p) - p->getValue()) > 0.002f)
                return true;
    }

    return false;
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return getUserPresetDirectory().getChildFile (juce::File::createLegalFileName (name.trim()) + fileExtension);
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    for (const auto& e : entries)
        if (! e.isFactory && e.name.equalsIgnoreCase (name.trim()))
            return true;

    return fileFor (name).existsAsFile();
}

juce::Result PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();

    if (name.isEmpty())
        return juce::Result::fail ("Please enter a name.");

    const auto dir = getUserPresetDirectory();

    if (! dir.isDirectory() && ! dir.createDirectory())
        return juce::Result::fail ("Could not create " + dir.getFullPathName());

    auto tree = capture();
    tree.setProperty (nameId, name, nullptr);
    tree.setProperty (categoryId, "User", nullptr);

    const auto xml = tree.createXml();

    if (xml == nullptr || ! xml->writeTo (fileFor (name)))
        return juce::Result::fail ("Could not write the preset file.");

    scanUserPresets();
    setCurrent (name, true);
    return juce::Result::ok();
}

juce::Result PresetManager::renameUserPreset (int index, const juce::String& rawName)
{
    const auto name = rawName.trim();

    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].isFactory)
        return juce::Result::fail ("Factory presets cannot be renamed.");

    if (name.isEmpty())
        return juce::Result::fail ("Please enter a name.");

    auto entry = entries[(size_t) index];
    auto tree = entry.values.createCopy();
    tree.setProperty (nameId, name, nullptr);

    const auto target = fileFor (name);

    if (target.existsAsFile() && target != entry.file)
        return juce::Result::fail ("A preset called \"" + name + "\" already exists.");

    const auto xml = tree.createXml();

    if (xml == nullptr || ! xml->writeTo (target))
        return juce::Result::fail ("Could not write the preset file.");

    if (entry.file != target)
        entry.file.deleteFile();

    const auto wasCurrent = getCurrentIndex() == index;
    scanUserPresets();

    if (wasCurrent)
        setCurrent (name, true);
    else
        notify();

    return juce::Result::ok();
}

juce::Result PresetManager::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].isFactory)
        return juce::Result::fail ("Factory presets cannot be deleted.");

    if (! entries[(size_t) index].file.deleteFile())
        return juce::Result::fail ("Could not delete the preset file.");

    const auto wasCurrent = getCurrentIndex() == index;
    scanUserPresets();

    // The sound stays as it is; it is simply no longer a saved preset.
    if (wasCurrent)
        setCurrent ("Untitled", true);
    else
        notify();

    return juce::Result::ok();
}

void PresetManager::notify()
{
    if (onChange != nullptr)
        onChange();
}

} // namespace umber::ui
