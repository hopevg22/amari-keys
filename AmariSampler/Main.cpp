// Amari Sampler - record any VST3 instrument into a multisample folder for Amari Keys.
#include <juce_audio_utils/juce_audio_utils.h>
#include "MultiSampleRenderer.h"

using namespace juce;

namespace Col
{
    const Colour bg { 0xff0e0e0f }, panel { 0xff1a1a1c }, edge { 0xff2c2c30 }, text { 0xffcfcfd2 }, dim { 0xff7c7c82 }, led { 0xffd2551e };
}

static int parseNote (String s) // "C#4" / "A0" / "60" -> midi, -1 if invalid (C4 = 60)
{
    s = s.trim().toLowerCase();
    if (s.isEmpty()) return -1;
    if (s.containsOnly ("0123456789")) { const int v = s.getIntValue(); return v <= 127 ? v : -1; }
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };
    const juce_wchar c = s[0];
    if (c < 'a' || c > 'g') return -1;
    int n = base[c - 'a'], i = 1;
    if (s[i] == '#') { ++n; ++i; } else if (s[i] == 'b') { --n; ++i; }
    auto rest = s.substring (i);
    if (rest.isEmpty() || ! (CharacterFunctions::isDigit (rest[0]) || rest[0] == '-')) return -1;
    const int m = (rest.getIntValue() + 1) * 12 + n;
    return m >= 0 && m <= 127 ? m : -1;
}

struct DarkLook : LookAndFeel_V4
{
    DarkLook()
    {
        setColour (ResizableWindow::backgroundColourId, Col::bg);
        setColour (TextEditor::backgroundColourId, Col::panel);
        setColour (TextEditor::outlineColourId, Col::edge);
        setColour (TextEditor::focusedOutlineColourId, Col::led);
        setColour (TextEditor::textColourId, Col::text);
        setColour (TextEditor::highlightColourId, Col::led.withAlpha (0.4f));
        setColour (Label::textColourId, Col::dim);
        setColour (TextButton::buttonColourId, Col::panel);
        setColour (TextButton::buttonOnColourId, Col::led);
        setColour (TextButton::textColourOffId, Col::text);
        setColour (ComboBox::backgroundColourId, Col::panel);
        setColour (ComboBox::outlineColourId, Col::edge);
        setColour (ComboBox::textColourId, Col::text);
        setColour (ComboBox::arrowColourId, Col::led);
        setColour (PopupMenu::backgroundColourId, Col::panel);
        setColour (PopupMenu::textColourId, Col::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Col::led);
        setColour (ToggleButton::textColourId, Col::text);
        setColour (ToggleButton::tickColourId, Col::led);
        setColour (ProgressBar::backgroundColourId, Col::panel);
        setColour (ProgressBar::foregroundColourId, Col::led);
        setColour (DocumentWindow::textColourId, Col::text);
    }
};

// ----------------------------------------------------------------------------
class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioProcessorEditor* ed, std::function<void()> onClose)
        : DocumentWindow (ed->getAudioProcessor()->getName(), Col::bg, DocumentWindow::closeButton), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { if (closed) closed(); }
private:
    std::function<void()> closed;
};

// ----------------------------------------------------------------------------
class MainComponent : public Component, private Thread, private Timer
{
public:
    MainComponent() : Thread ("Amari Sampler render")
    {
        setLookAndFeel (&look);
        addDefaultFormatsToManager (formats);

        PropertiesFile::Options o;
        o.applicationName = "Amari Sampler"; o.filenameSuffix = ".settings"; o.folderName = "Amari Labs";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<PropertiesFile> (o);

        title.setText ("AMARI SAMPLER", dontSendNotification);
        title.setFont (FontOptions (22.0f, Font::bold)); title.setColour (Label::textColourId, Col::text);
        sub.setText ("record a VST3 instrument into a sample folder for Amari Keys", dontSendNotification);
        addAndMakeVisible (title); addAndMakeVisible (sub);

        addAndMakeVisible (loadBtn); loadBtn.onClick = [this] { choosePlugin(); };
        addAndMakeVisible (showBtn); showBtn.onClick = [this] { togglePluginWindow(); };
        addAndMakeVisible (pluginLabel); pluginLabel.setColour (Label::textColourId, Col::led);
        pluginLabel.setText ("No instrument loaded", dontSendNotification);

        auto field = [this] (Label& l, const char* caption, TextEditor& e, const String& key, const String& def)
        {
            l.setText (caption, dontSendNotification); addAndMakeVisible (l);
            e.setText (props->getValue (key, def)); addAndMakeVisible (e);
            e.onTextChange = [this, &e, key] { props->setValue (key, e.getText()); updateSummary(); };
        };
        field (nameL, "Sample name", nameE, "name", "KeyscapePiano");
        field (dirL, "Save to folder (a subfolder with the name is created)", dirE, "dir", "F:\\Music Plugins\\Amari Keys\\Samples\\Piano");
        field (loL, "Lowest note", loE, "lo", "A0");
        field (hiL, "Highest note", hiE, "hi", "C8");
        field (velL, "Velocities (soft to loud)", velE, "vels", "30, 55, 80, 105, 127");
        field (holdL, "Hold each note (s)", holdE, "hold", "10");
        field (tailL, "Max release tail (s)", tailE, "tail", "6");
        field (silL, "Silence level (dB)", silE, "sil", "-70");
        addAndMakeVisible (browseBtn);
        browseBtn.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Save samples to", File (dirE.getText()));
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                  [this] (const FileChooser& fc) { if (fc.getResult() != File()) dirE.setText (fc.getResult().getFullPathName()); });
        };

        auto combo = [this] (Label& l, const char* caption, ComboBox& c, StringArray items, const String& key, int def)
        {
            l.setText (caption, dontSendNotification); addAndMakeVisible (l);
            c.addItemList (items, 1); c.setSelectedItemIndex (props->getIntValue (key, def), dontSendNotification);
            c.onChange = [this, &c, key] { props->setValue (key, c.getSelectedItemIndex()); updateSummary(); };
            addAndMakeVisible (c);
        };
        combo (stepL, "Note step", stepC, { "every note", "every 2 semitones", "every 3 semitones", "every 4 semitones", "every 6 semitones", "every octave" }, "step", 1);
        combo (rateL, "Sample rate", rateC, { "44.1 kHz", "48 kHz", "96 kHz" }, "rate", 1);
        combo (bitsL, "Bit depth", bitsC, { "16-bit", "24-bit" }, "bits", 1);
        combo (speedL, "Speed", speedC, { "Real time (safest for streaming pianos)", "Fast (offline)" }, "speed", 0);
        sfzT.setButtonText ("Also write an .sfz file (exact mapping)"); sfzT.setToggleState (props->getBoolValue ("sfz", true), dontSendNotification);
        sfzT.onClick = [this] { props->setValue ("sfz", sfzT.getToggleState()); };
        addAndMakeVisible (sfzT);

        addAndMakeVisible (summary); summary.setColour (Label::textColourId, Col::text);
        addAndMakeVisible (startBtn); startBtn.onClick = [this] { start(); };
        addAndMakeVisible (stopBtn); stopBtn.onClick = [this] { stopFlag = true; };
        startBtn.setColour (TextButton::buttonColourId, Col::led.darker (0.3f));
        addAndMakeVisible (progressBar);
        logBox.setMultiLine (true); logBox.setReadOnly (true); logBox.setScrollbarsShown (true);
        logBox.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 13.0f, Font::plain));
        addAndMakeVisible (logBox);

        setSize (760, 760);
        updateSummary(); updateButtons();
        log ("1) LOAD VST3 and choose your instrument   2) OPEN PLUGIN to pick the sound and switch its effects off   3) START");

        const auto last = props->getValue ("plugin");
        if (last.isNotEmpty() && File (last).exists()) MessageManager::callAsync ([this, last, sp = SafePointer<MainComponent> (this)] { if (sp) loadPlugin (last); });
        startTimerHz (10);
    }

    ~MainComponent() override
    {
        stopFlag = true; stopThread (10000);
        pluginWindow.reset();
        plugin.reset();
        setLookAndFeel (nullptr);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Col::bg);
        g.setColour (Col::panel); g.fillRoundedRectangle (panelArea.toFloat(), 6.0f);
        g.setColour (Col::edge); g.drawRoundedRectangle (panelArea.toFloat(), 6.0f, 1.0f);
        g.setColour (Col::led); g.fillEllipse ((float) getWidth() - 34.0f, 22.0f, 10.0f, 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (48);
        title.setBounds (top.removeFromTop (28)); sub.setBounds (top);
        r.removeFromTop (8);
        auto pr = r.removeFromTop (34);
        loadBtn.setBounds (pr.removeFromLeft (130)); pr.removeFromLeft (8);
        showBtn.setBounds (pr.removeFromRight (150)); pr.removeFromRight (8);
        pluginLabel.setBounds (pr);
        r.removeFromTop (12);

        panelArea = r.removeFromTop (330);
        auto p = panelArea.reduced (12);
        auto row = [&p] (int h) { auto x = p.removeFromTop (h); p.removeFromTop (6); return x; };
        auto labelled = [] (Rectangle<int> a, Label& l, Component& c) { l.setBounds (a.removeFromTop (18)); c.setBounds (a.removeFromTop (26)); };
        auto r1 = row (44); labelled (r1, nameL, nameE);
        auto r2 = row (44); browseBtn.setBounds (r2.removeFromRight (90).withTrimmedTop (18)); r2.removeFromRight (6); labelled (r2, dirL, dirE);
        auto cols = [] (Rectangle<int> a, int n) { Array<Rectangle<int>> out; const int w = (a.getWidth() - (n - 1) * 10) / n; for (int i = 0; i < n; ++i) { out.add (a.removeFromLeft (w)); a.removeFromLeft (10); } return out; };
        auto r3 = cols (row (44), 4); labelled (r3[0], loL, loE); labelled (r3[1], hiL, hiE); labelled (r3[2], stepL, stepC); labelled (r3[3], velL, velE);
        auto r4 = cols (row (44), 4); labelled (r4[0], holdL, holdE); labelled (r4[1], tailL, tailE); labelled (r4[2], silL, silE); labelled (r4[3], rateL, rateC);
        auto r5 = cols (row (44), 2); labelled (r5[0], bitsL, bitsC); labelled (r5[1], speedL, speedC);
        sfzT.setBounds (row (26));
        summary.setBounds (p);

        r.removeFromTop (12);
        auto br = r.removeFromTop (36);
        startBtn.setBounds (br.removeFromLeft (140)); br.removeFromLeft (8);
        stopBtn.setBounds (br.removeFromLeft (100)); br.removeFromLeft (12);
        progressBar.setBounds (br);
        r.removeFromTop (10);
        logBox.setBounds (r);
    }

private:
    // ---- plugin loading ------------------------------------------------------
    void choosePlugin()
    {
        if (isThreadRunning()) return;
        Array<File> found;
        std::function<void (const File&, int)> walk = [&] (const File& d, int depth)
        {
            for (auto& e : RangedDirectoryIterator (d, false, "*", File::findFilesAndDirectories))
            {
                auto f = e.getFile();
                if (f.hasFileExtension ("vst3")) found.add (f);
                else if (f.isDirectory() && depth < 3) walk (f, depth + 1);
            }
        };
       #if JUCE_WINDOWS
        walk (File ("C:\\Program Files\\Common Files\\VST3"), 0);
       #elif JUCE_MAC
        walk (File ("/Library/Audio/Plug-Ins/VST3"), 0);
       #else
        walk (File::getSpecialLocation (File::userHomeDirectory).getChildFile (".vst3"), 0);
       #endif
        std::sort (found.begin(), found.end(), [] (const File& a, const File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        PopupMenu m;
        for (int i = 0; i < found.size(); ++i) m.addItem (i + 1, found[i].getFileNameWithoutExtension());
        if (found.isEmpty()) m.addItem (-2, "(no VST3 found in the standard folder)", false);
        m.addSeparator();
        m.addItem (-1, "Browse for a .vst3...");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (loadBtn), [this, found] (int r)
        {
            if (r > 0) loadPlugin (found[r - 1].getFullPathName());
            else if (r == -1)
            {
                chooser = std::make_unique<FileChooser> ("Choose a VST3 instrument", File(), "*.vst3");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectDirectories,
                                      [this] (const FileChooser& fc) { auto f = fc.getResult(); if (f.hasFileExtension ("vst3")) loadPlugin (f.getFullPathName()); });
            }
        });
    }

    void loadPlugin (const String& path)
    {
        pluginWindow.reset();
        plugin.reset();
        OwnedArray<PluginDescription> types;
        for (auto* fmt : formats.getFormats()) fmt->findAllTypesForFile (types, path);
        if (types.isEmpty()) { log ("Could not open " + path); pluginLabel.setText ("No instrument loaded", dontSendNotification); updateButtons(); return; }
        const PluginDescription* desc = types[0];
        for (auto* t : types) if (t->isInstrument) { desc = t; break; }
        String err;
        plugin = formats.createPluginInstance (*desc, sampleRate(), 512, err);
        if (! plugin) { log ("Failed to load " + desc->name + ": " + err); updateButtons(); return; }
        plugin->enableAllBuses();
        plugin->prepareToPlay (sampleRate(), 512); // let it initialise and start loading its default sound
        props->setValue ("plugin", path);
        pluginLabel.setText (desc->name + "  (" + desc->manufacturerName + ")" + (desc->isInstrument ? "" : "  - not marked as an instrument"), dontSendNotification);
        log ("Loaded " + desc->name + ". Open it, choose the sound, and switch off its reverb/delay.");
        updateButtons();
    }

    void togglePluginWindow()
    {
        if (pluginWindow) { pluginWindow.reset(); updateButtons(); return; }
        if (! plugin) return;
        if (auto* ed = plugin->createEditorIfNeeded())
            pluginWindow = std::make_unique<PluginWindow> (ed, [this] { MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)] { if (sp) { sp->pluginWindow.reset(); sp->updateButtons(); } }); });
        else log ("This plugin has no editor window.");
        updateButtons();
    }

    // ---- settings ------------------------------------------------------------
    double sampleRate() const { const double r[] = { 44100.0, 48000.0, 96000.0 }; return r[jlimit (0, 2, rateC.getSelectedItemIndex())]; }

    bool readSettings (MultiSampleRenderer::Settings& s, String& problem) const
    {
        s.name = nameE.getText().trim();
        s.outDir = File (dirE.getText().trim()).getChildFile (MultiSampleRenderer::safeName (s.name));
        s.lowNote = parseNote (loE.getText()); s.highNote = parseNote (hiE.getText());
        const int steps[] = { 1, 2, 3, 4, 6, 12 }; s.step = steps[jlimit (0, 5, stepC.getSelectedItemIndex())];
        s.velocities.clear();
        StringArray v; v.addTokens (velE.getText(), ", ;", ""); v.removeEmptyStrings();
        for (auto& t : v) { const int x = t.getIntValue(); if (x >= 1 && x <= 127) s.velocities.addIfNotAlreadyThere (x); }
        s.velocities.sort();
        s.holdSeconds = jlimit (0.2, 30.0, holdE.getText().getDoubleValue());
        s.maxTailSeconds = jlimit (0.2, 20.0, tailE.getText().getDoubleValue());
        s.silenceDb = (float) jlimit (-100.0, -30.0, silE.getText().getDoubleValue());
        s.sampleRate = sampleRate();
        s.bitDepth = bitsC.getSelectedItemIndex() == 0 ? 16 : 24;
        s.realtimePace = speedC.getSelectedItemIndex() == 0;
        s.writeSfz = sfzT.getToggleState();
        if (s.name.isEmpty()) problem = "Enter a sample name.";
        else if (dirE.getText().trim().isEmpty() || ! File::isAbsolutePath (dirE.getText().trim())) problem = "Choose a folder to save to.";
        else if (s.lowNote < 0 || s.highNote < 0) problem = "Notes must look like A0, C#4 or C8.";
        else if (s.lowNote > s.highNote) problem = "Lowest note is above the highest note.";
        else if (s.velocities.isEmpty()) problem = "Enter at least one velocity (1-127).";
        return problem.isEmpty();
    }

    void updateSummary()
    {
        MultiSampleRenderer::Settings s; String problem;
        if (! readSettings (s, problem)) { summary.setText (problem, dontSendNotification); return; }
        const int n = s.notes().size() * s.velocities.size();
        const double perNote = s.holdSeconds + 0.5 * s.maxTailSeconds + 0.5;
        const double secs = n * perNote;
        const double mb = n * (s.holdSeconds + 0.4 * s.maxTailSeconds) * s.sampleRate * 2 * (s.bitDepth / 8) / 1048576.0;
        summary.setText (String (s.notes().size()) + " notes x " + String (s.velocities.size()) + " velocities = " + String (n) + " samples"
                         + "   |   about " + String (roundToInt (mb)) + " MB on disk"
                         + (s.realtimePace ? "   |   about " + String (roundToInt (secs / 60.0)) + " min" : String()),
                         dontSendNotification);
    }

    void updateButtons()
    {
        const bool busy = isThreadRunning();
        loadBtn.setEnabled (! busy);
        showBtn.setEnabled (plugin != nullptr);
        showBtn.setButtonText (pluginWindow ? "CLOSE PLUGIN" : "OPEN PLUGIN");
        startBtn.setEnabled (plugin != nullptr && ! busy);
        stopBtn.setEnabled (busy);
        for (Component* c : std::initializer_list<Component*> { &nameE, &dirE, &loE, &hiE, &velE, &holdE, &tailE, &silE, &stepC, &rateC, &bitsC, &speedC, &sfzT, &browseBtn })
            c->setEnabled (! busy);
    }

    // ---- rendering -----------------------------------------------------------
    void start()
    {
        if (! plugin || isThreadRunning()) return;
        String problem;
        if (! readSettings (settings, problem)) { log (problem); return; }
        if (settings.outDir.isDirectory() && settings.outDir.getNumberOfChildFiles (File::findFiles, "*.wav") > 0)
        {
            auto opts = MessageBoxOptions::makeOptionsOkCancel (MessageBoxIconType::QuestionIcon, "Folder already has samples",
                "\"" + settings.outDir.getFullPathName() + "\" already contains WAV files. Files with the same names will be replaced.", "Continue", "Cancel", this);
            AlertWindow::showAsync (opts, [sp = SafePointer<MainComponent> (this)] (int r) { if (sp && r == 1) sp->begin(); });
            return;
        }
        begin();
    }

    void begin()
    {
        stopFlag = false; progress = 0.0;
        logBox.clear();
        startThread();
        updateButtons();
    }

    void run() override
    {
        MultiSampleRenderer r;
        r.stopFlag = &stopFlag;
        r.onProgress = [this] (double p) { progress = p; };
        r.onLog = [this] (const String& t) { const ScopedLock l (logLock); pendingLog.add (t); };
        r.run (*plugin, settings);
        // leave the plugin ready to be played/edited again
        plugin->prepareToPlay (sampleRate(), 512);
    }

    void timerCallback() override
    {
        StringArray lines;
        { const ScopedLock l (logLock); lines.swapWith (pendingLog); }
        for (auto& t : lines) log (t);
        if (wasRunning && ! isThreadRunning()) updateButtons();
        wasRunning = isThreadRunning();
    }

    void log (const String& t)
    {
        logBox.moveCaretToEnd();
        logBox.insertTextAtCaret (Time::getCurrentTime().toString (false, true, true, true) + "  " + t + "\n");
    }

    DarkLook look;
    AudioPluginFormatManager formats;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<PropertiesFile> props;
    std::unique_ptr<FileChooser> chooser;
    MultiSampleRenderer::Settings settings;
    std::atomic<bool> stopFlag { false };
    double progress = 0.0;
    bool wasRunning = false;
    CriticalSection logLock; StringArray pendingLog;
    Rectangle<int> panelArea;

    Label title, sub, pluginLabel, summary;
    TextButton loadBtn { "LOAD VST3" }, showBtn { "OPEN PLUGIN" }, browseBtn { "BROWSE" }, startBtn { "START" }, stopBtn { "STOP" };
    Label nameL, dirL, loL, hiL, velL, holdL, tailL, silL, stepL, rateL, bitsL, speedL;
    TextEditor nameE, dirE, loE, hiE, velE, holdE, tailE, silE, logBox;
    ComboBox stepC, rateC, bitsC, speedC;
    ToggleButton sfzT;
    ProgressBar progressBar { progress };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

// ----------------------------------------------------------------------------
class AmariSamplerApp : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Amari Sampler"; }
    const String getApplicationVersion() override { return "0.1"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

    struct MainWindow : DocumentWindow
    {
        MainWindow() : DocumentWindow ("Amari Sampler", Col::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (700, 640, 1400, 1400);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (AmariSamplerApp)
