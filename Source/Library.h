#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ============================================================================
// Amari Keys library: saved sounds (SFZ / sample folders, by group) and patches
// (all layer settings + each layer's sound), kept as two small XML files in
//   <library root>\Library\Sounds.xml   and   <library root>\Library\Patches.xml
// so every plugin instance and every project sees the same sounds and patches.
// The root defaults to F:\Music Plugins\Amari Keys when it exists, otherwise
// Documents\Amari Keys, and can be changed from the plugin.
struct AmariLibrary
{
    struct Sound { juce::String name, group, path; bool saved = false; };

    // sample groups = the layers that can play samples (ORGAN is the tonewheel engine)
    static juce::String groupFolder (int layer)
    {
        static const char* names[10] = { "Piano", "E.Piano", "Organ", "Keys", "Strings", "Brass", "Pads", "Synth", "Bass", "SFX" };
        return names[juce::jlimit (0, 9, layer)];
    }

    // ---- settings / folders ---------------------------------------------------
    static juce::PropertiesFile& settings()
    {
        static std::unique_ptr<juce::PropertiesFile> p = []
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Amari Keys"; o.filenameSuffix = ".settings"; o.folderName = "Amari Labs";
            o.osxLibrarySubFolder = "Application Support";
            return std::make_unique<juce::PropertiesFile> (o);
        }();
        return *p;
    }

    static juce::File root()
    {
        const auto saved = settings().getValue ("libraryRoot");
        if (saved.isNotEmpty() && juce::File::isAbsolutePath (saved) && juce::File (saved).isDirectory()) return juce::File (saved);
       #if JUCE_WINDOWS
        const juce::File hope ("F:\\Music Plugins\\Amari Keys");
        if (hope.isDirectory()) return hope;
       #endif
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Amari Keys");
    }
    static void setRoot (const juce::File& f) { settings().setValue ("libraryRoot", f.getFullPathName()); settings().saveIfNeeded(); }

    static juce::File libraryDir() { auto d = root().getChildFile ("Library"); d.createDirectory(); return d; }
    static juce::File samplesDir() { return root().getChildFile ("Samples"); }
    static juce::File soundsFile() { return libraryDir().getChildFile ("Sounds.xml"); }
    static juce::File patchesFile() { return libraryDir().getChildFile ("Patches.xml"); }

    static juce::ValueTree readTree (const juce::File& f, const juce::Identifier& type)
    {
        if (f.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (f))
            {
                auto t = juce::ValueTree::fromXml (*xml);
                if (t.hasType (type)) return t;
            }
        return juce::ValueTree (type);
    }
    static bool writeTree (const juce::File& f, const juce::ValueTree& t)
    {
        if (auto xml = t.createXml())
        {
            juce::TemporaryFile tmp (f);
            if (xml->writeTo (tmp.getFile())) return tmp.overwriteTargetFileWithTemporary();
        }
        return false;
    }

    // ---- sounds ---------------------------------------------------------------
    static juce::Array<Sound> savedSounds()
    {
        juce::Array<Sound> out;
        for (auto s : readTree (soundsFile(), "SOUNDS"))
            out.add ({ s.getProperty ("name").toString(), s.getProperty ("group").toString(), s.getProperty ("path").toString(), true });
        return out;
    }

    static bool saveSound (const juce::String& name, int layer, const juce::String& path)
    {
        auto t = readTree (soundsFile(), "SOUNDS");
        const auto group = groupFolder (layer);
        for (auto s : t)
            if (s.getProperty ("path").toString() == path && s.getProperty ("group").toString() == group)
            { s.setProperty ("name", name, nullptr); return writeTree (soundsFile(), t); }
        juce::ValueTree s ("SOUND");
        s.setProperty ("name", name, nullptr); s.setProperty ("group", group, nullptr); s.setProperty ("path", path, nullptr);
        t.appendChild (s, nullptr);
        return writeTree (soundsFile(), t);
    }

    static bool removeSound (const juce::String& group, const juce::String& path)
    {
        auto t = readTree (soundsFile(), "SOUNDS");
        for (int i = t.getNumChildren(); --i >= 0;)
            if (t.getChild (i).getProperty ("path").toString() == path && t.getChild (i).getProperty ("group").toString() == group) t.removeChild (i, nullptr);
        return writeTree (soundsFile(), t);
    }

    // SFZ files found under Samples\<Group>\ (up to 3 folders deep), plus every saved sound of that group
    static juce::Array<Sound> soundsFor (const juce::String& group)
    {
        juce::Array<Sound> out;
        for (auto& s : savedSounds()) if (s.group == group) out.add (s);
        const auto dir = samplesDir().getChildFile (group);
        if (dir.isDirectory())
        {
            juce::Array<juce::File> found;
            for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*.sfz", juce::File::findFiles))
                if (e.getFile().getRelativePathFrom (dir).retainCharacters ("\\/").length() <= 3) found.add (e.getFile());
            found.sort();
            for (auto& f : found)
            {
                bool known = false;
                for (auto& s : out) if (juce::File (s.path) == f) known = true;
                if (known) continue;
                // show the SFZ's own name; add its top folder only when two files share a name
                auto name = f.getFileNameWithoutExtension();
                int same = 0; for (auto& g : found) if (g.getFileNameWithoutExtension() == name) ++same;
                if (same > 1)
                {
                    const auto top = f.getRelativePathFrom (dir).upToFirstOccurrenceOf ("\\", false, false).upToFirstOccurrenceOf ("/", false, false);
                    if (top != f.getFileName()) name << "  (" << top << ")";
                }
                out.add ({ name, group, f.getFullPathName(), false });
            }
        }
        return out;
    }

    // ---- patches --------------------------------------------------------------
    // <PATCHES><PATCH name=".."><V id=".." v=".."/>...<SOUND layer="0" name=".." path=".."/></PATCH>...</PATCHES>
    static juce::ValueTree loadPatches() { return readTree (patchesFile(), "PATCHES"); }
    static bool savePatches (const juce::ValueTree& t) { return writeTree (patchesFile(), t); }
};
