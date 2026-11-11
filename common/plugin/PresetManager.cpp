#include "common/plugin/PresetManager.h"

#include "common/plugin/UiState.h"

namespace myzel::plugin
{

namespace
{
    const juce::Identifier nameId { "name" };
    const juce::Identifier categoryId { "category" };
    const juce::Identifier versionId { "presetVersion" };

    juce::File& directoryOverride()
    {
        static juce::File dir;
        return dir;
    }

    /** Normalised value a snapshot asks for. Parameters a (older) snapshot does not mention go
        to their defaults, so loading it never keeps a stray value from the current sound. */
    float targetFor (const juce::ValueTree& snapshot, const juce::Identifier& key, juce::RangedAudioParameter& p)
    {
        return snapshot.hasProperty (key) ? p.convertTo0to1 ((float) snapshot.getProperty (key))
                                          : p.getDefaultValue();
    }
}

PresetManager::PresetManager (PresetHost& h, PresetFormat f)
    : host (h),
      format (std::move (f))
{
    scanUserPresets();
}

juce::ValueTree PresetManager::snapshotFromFactory (const FactorySnapshot& preset) const
{
    juce::ValueTree t (format.presetType);
    t.setProperty (nameId, preset.name, nullptr);
    t.setProperty (categoryId, preset.category, nullptr);
    t.setProperty (versionId, format.version, nullptr);

    for (const auto& v : preset.values)
        t.setProperty (v.name, v.value, nullptr);

    return t;
}

juce::File PresetManager::getUserPresetDirectory() const
{
    if (directoryOverride() != juce::File())
        return directoryOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Myzel Audio")
        .getChildFile (format.pluginName)
        .getChildFile ("Presets");
}

void PresetManager::setUserPresetDirectoryOverride (const juce::File& directory)
{
    directoryOverride() = directory;
}

juce::ValueTree PresetManager::readPresetFile (const juce::File& file) const
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType (format.presetType))
            return tree;
    }

    return {};
}

void PresetManager::scanUserPresets()
{
    entries.clear();

    for (const auto& f : format.factory)
        entries.push_back ({ f.name, f.category, true, {}, snapshotFromFactory (f) });

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
    auto& apvts = host.getState();
    juce::ValueTree t (format.presetType);
    t.setProperty (versionId, format.version, nullptr);

    for (const auto& id : parameterIds())
        if (auto* p = apvts.getParameter (id))
            t.setProperty (juce::Identifier (id), p->convertFrom0to1 (p->getValue()), nullptr);

    return t;
}

void PresetManager::apply (const juce::ValueTree& snapshot, const juce::String& undoName)
{
    auto& apvts = host.getState();
    host.getUndoManager().beginNewTransaction (undoName);

    for (const auto& id : parameterIds())
    {
        const juce::Identifier key (id);

        if (isLockedParameter (id))
            continue;

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

    host.presetApplied();
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
    return host.getUiState().getProperty (uistate::presetName, entries.empty() ? juce::String() : entries.front().name);
}

juce::String PresetManager::getCurrentCategory() const
{
    const auto index = getCurrentIndex();
    return index >= 0 ? entries[(size_t) index].category : juce::String();
}

bool PresetManager::isCurrentUserPreset() const
{
    return host.getUiState().getProperty (uistate::presetUser, false);
}

void PresetManager::setCurrent (const juce::String& name, bool isUser)
{
    auto ui = host.getUiState();
    ui.setProperty (uistate::presetName, name, nullptr);
    ui.setProperty (uistate::presetUser, isUser, nullptr);

    host.currentPresetChanged (name, isUser);
    notify();
}

bool PresetManager::isMixLocked() const
{
    return canLockMix() && (bool) host.getUiState().getProperty (uistate::mixLock, false);
}

void PresetManager::setMixLocked (bool shouldLock)
{
    host.getUiState().setProperty (uistate::mixLock, shouldLock, nullptr);
    host.mixLockChanged (shouldLock);
    notify();
}

bool PresetManager::isLockedParameter (const juce::String& parameterId) const
{
    return format.lockableIds.contains (parameterId) && isMixLocked();
}

bool PresetManager::isModified() const
{
    const auto index = getCurrentIndex();

    if (index < 0)
        return true;

    const auto& values = entries[(size_t) index].values;
    auto& apvts = host.getState();

    for (const auto& id : parameterIds())
    {
        const juce::Identifier key (id);

        if (isLockedParameter (id))
            continue;

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

} // namespace myzel::plugin
