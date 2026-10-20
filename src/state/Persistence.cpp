#include "state/Persistence.h"

namespace ids
{
    const juce::Identifier root ("MicVST"), inDev ("inputDevice"), outDev ("outputDevice"),
        sr ("sampleRate"), userBuf ("userBufferSize"), safety ("safetyBufferMs"), folders ("pluginFolders"),
        priority ("processPriority"),
        window ("windowState"),
        plugins ("plugins"), plugin ("plugin"), fileId ("fileOrId"), byp ("bypassed"), blob ("state"),
        pluginId ("pluginId"), pluginName ("pluginName"), pluginUid ("pluginUid"),
        preset ("presetName"), presetModified ("presetModified"), theme ("theme");
}

juce::ValueTree toValueTree (const MicVSTState& s)
{
    juce::ValueTree t (ids::root);
    t.setProperty (ids::inDev, s.inputDevice, nullptr);
    t.setProperty (ids::outDev, s.outputDevice, nullptr);
    t.setProperty (ids::sr, s.sampleRate, nullptr);
    t.setProperty (ids::userBuf, s.bufferSize, nullptr);
    t.setProperty (ids::safety, s.safetyBufferMs, nullptr);
    t.setProperty (ids::priority, s.processPriority, nullptr);
    t.setProperty (ids::folders, s.pluginFolders.joinIntoString ("\n"), nullptr);
    t.setProperty (ids::window, s.windowState, nullptr);

    if (s.presetName.isNotEmpty())
    {
        t.setProperty (ids::preset, s.presetName, nullptr);
        t.setProperty (ids::presetModified, s.presetModified, nullptr);
    }
    if (s.theme.isNotEmpty())
        t.setProperty (ids::theme, s.theme, nullptr);
    t.appendChild (pluginsToValueTree (s.plugins), nullptr);
    return t;
}

juce::ValueTree pluginsToValueTree (const juce::Array<PluginEntryState>& plugins)
{
    juce::ValueTree list (ids::plugins);
    for (auto& p : plugins)
    {
        juce::ValueTree pt (ids::plugin);
        pt.setProperty (ids::fileId, p.fileOrId, nullptr);
        pt.setProperty (ids::byp, p.bypassed, nullptr);
        pt.setProperty (ids::blob, p.state.toBase64Encoding(), nullptr);
        if (p.pluginId.isNotEmpty())
        {
            pt.setProperty (ids::pluginId, p.pluginId, nullptr);
            pt.setProperty (ids::pluginName, p.pluginName, nullptr);
            pt.setProperty (ids::pluginUid, p.pluginUid, nullptr);
        }
        list.appendChild (pt, nullptr);
    }
    return list;
}

juce::Array<PluginEntryState> pluginsFromValueTree (const juce::ValueTree& list)
{
    juce::Array<PluginEntryState> out;
    for (auto pt : list)
    {
        if (! pt.hasType (ids::plugin)) continue;
        PluginEntryState p;
        p.fileOrId = pt.getProperty (ids::fileId);
        p.bypassed = pt.getProperty (ids::byp, false);
        p.state.fromBase64Encoding (pt.getProperty (ids::blob).toString());
        p.pluginId   = pt.getProperty (ids::pluginId).toString();
        p.pluginName = pt.getProperty (ids::pluginName).toString();
        p.pluginUid  = (int) pt.getProperty (ids::pluginUid, 0);
        if (p.fileOrId.isNotEmpty()) out.add (p);
    }
    return out;
}

MicVSTState fromValueTree (const juce::ValueTree& t)
{
    MicVSTState s;
    s.inputDevice  = t.getProperty (ids::inDev);
    s.outputDevice = t.getProperty (ids::outDev);
    s.sampleRate   = t.getProperty (ids::sr, 48000.0);
    // Migration v1.0.x: der alte Key "bufferSize" (immer 128) wird bewusst ignoriert --
    // im Shared-Modus war er nie wirksam. Bestandsnutzer starten mit Auto.
    s.bufferSize   = t.getProperty (ids::userBuf, 0);
    s.safetyBufferMs = juce::jlimit (0, 200, (int) t.getProperty (ids::safety, 0));   // fehlt bei <= 1.1.1 -> Auto
    s.processPriority = juce::jlimit (0, 2, (int) t.getProperty (ids::priority, 0));   // fehlt in älteren config.xml -> Normal
    {
        const auto f = t.getProperty (ids::folders).toString();
        if (f.isNotEmpty()) { s.pluginFolders.addLines (f); s.pluginFolders.removeEmptyStrings(); }
    }
    s.windowState = t.getProperty (ids::window).toString();

    s.plugins = pluginsFromValueTree (t.getChildWithName (ids::plugins));
    s.presetName = t.getProperty (ids::preset).toString();
    s.presetModified = (bool) t.getProperty (ids::presetModified, false);
    s.theme = t.getProperty (ids::theme).toString();
    return s;
}

juce::File configFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("config.xml");
}

bool saveState (const MicVSTState& s)
{
    auto f = configFile();
    f.getParentDirectory().createDirectory();
    auto xml = toValueTree (s).createXml();
    if (xml == nullptr)
        return false;
    // Unchanged content (e.g. repeated device notifications) is not written again: spares the disk.
    // Message thread only.
    static juce::String lastWritten;
    const auto text = xml->toString();
    if (text == lastWritten && f.existsAsFile())
        return true;
    if (! xml->writeTo (f))   // via a temporary file, replaced at once
        return false;
    lastWritten = text;
    return true;
}

MicVSTState loadState()
{
    auto f = configFile();
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::XmlDocument::parse (f))
        return fromValueTree (juce::ValueTree::fromXml (*xml));
    return {};
}
