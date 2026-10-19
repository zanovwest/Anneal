#include "PluginEditor.h"
#include "Params.h"
#include "ui/Theme.h"

using namespace anneal::theme;
using Size = anneal::Knob::Size;
namespace ids = anneal::ids;

namespace
{
    // Deck geometry (base size). Groups follow the signal: AUTO EQ -> HEAT -> SIDECHAIN ->
    // REFERENCE -> OUTPUT.
    constexpr int titleY = 418, dividerTop = 424, dividerBottom = 628;
    constexpr int routingY = 412;   // 26 px pills centred on the 15 px title row
    constexpr int dividerX[] = { 490, 832, 984, 1136, 1288 };

    juce::String minus() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

    juce::String signedDb (float v)
    {
        if (std::abs (v) < 0.05f) return "0.0";
        return (v > 0 ? "+" : minus()) + juce::String (std::abs (v), 1);
    }
}

AnnealEditorContent::AnnealEditorContent (AnnealAudioProcessor& p)
    : screen (p.getDisplay(), p.apvts),
      processor (p),
      reveal   (p.apvts, ids::reveal,   "Reveal",   colour::straw,  Size::primary,   false, "lifts masked detail",
                "Reveal: lifts bands that sit in the masking shadow of louder neighbours"),
      smooth   (p.apvts, ids::smooth,   "Smooth",   colour::blue,   Size::primary,   false, "tames dominant bands",
                "Smooth: pulls down bands that dominate their auditory neighbourhood"),
      tilt     (p.apvts, ids::tilt,     "Tilt",     colour::muted,  Size::secondary, true,  {},
                "Tilt: static slope around 1 kHz, applied inside the range (darker to brighter)"),
      response (p.apvts, ids::response, "Response", colour::muted,  Size::secondary, false, {},
                "Response: how quickly the auto EQ follows the music (release; attack is 3x faster)"),
      guard    (p.apvts, ids::guard,    "Guard",    colour::muted,  Size::secondary, false, {},
                "Guard: lets attacks through. On drum hits and plucks the auto EQ steps back toward 0 dB and Heat "
                "writes no harmonics over the attack, then both return. Off = as before"),
      heat     (p.apvts, ids::heat,     "Heat",     colour::bronze, Size::hero,      false, "harmonics where thin",
                "Heat: spectral saturation; each partial grows its own 2nd-6th harmonics where the spectrum is thin"),
      mix      (p.apvts, ids::mix,      "Mix",      colour::muted,  Size::secondary, false, {},
                "Mix: blend with the latency-aligned dry signal"),
      output   (p.apvts, ids::output,   "Output",   colour::muted,  Size::secondary, true,  {},
                "Output: final gain"),
      color    (p.apvts, ids::color, "Color", "even", "odd", colour::bronze, false,
                "Color: even harmonics (round, 2nd/4th/6th) to odd harmonics (edgier, 3rd/5th)"),
      focus    (p.apvts, ids::focus, "Focus", "body", "air", colour::bronze, true,
                "Focus: concentrates harmonics toward the body (low) or the air (high); centre follows the model"),
      harmonics (p.apvts),
      match    (p.apvts, ids::match,  "MATCH",  colour::straw,  "Loudness match: slow K-weighted gain so you judge tone, not level (shows the gain)"),
      delta    (p.apvts, ids::delta,  "DELTA",  colour::bronze, "Delta: hear only what Anneal adds and removes"),
      bypass   (p.apvts, ids::bypass, "BYPASS", colour::text,   "Bypass (latency-compensated, click-free)"),
      latency  (p.apvts, [&p] (int m) { return p.getLatencyMsForMode (m); }, [&p] { return p.getLatencyMs(); }),
      stereoMode (p.apvts, ids::stereoMode, "", { "Stereo", "Mid/Side" },
                  "Stereo links the EQ curve across left and right. Mid/Side analyses the centre and sides independently. Mono uses linked processing."),
      eqTarget (p.apvts, ids::eqTarget, "EQ: ", { "Both", "Mid", "Side" },
                "In Mid/Side mode, route Reveal, Smooth and Tilt to both components, the centre (Mid), or the sides (Side)."),
      heatTarget (p.apvts, ids::heatTarget, "Heat: ", { "Both", "Mid", "Side" },
                  "In Mid/Side mode, add Heat to both components, the centre (Mid), or the sides (Side)."),
      listen (p.apvts, ids::listen),
      flavour (p.apvts, ids::flavour),
      freeze (p.apvts, ids::freeze, "FREEZE", colour::straw,
              "Freeze: hold the auto-EQ curve where it is; Heat keeps following the music"),
      scMode (p.apvts, ids::scMode, "", { "Off", "Make room", "Unmask" },
              "Sidechain: Make room ducks this track where the key track needs space; Unmask lifts this track where the key "
              "masks it. Route a track to Anneal's sidechain input in your DAW"),
      ceiling (p.apvts, ids::ceiling, "Ceiling: ", { "Off", juce::String::fromUTF8 ("0.0 dB"), juce::String::fromUTF8 ("\xe2\x88\x92" "0.3 dB"),
                                                     juce::String::fromUTF8 ("\xe2\x88\x92" "1.0 dB") },
               "Ceiling: a true-peak limiter at the very end, so Heat and Output can never clip (dBTP, look-ahead 1.4 ms)"),
      scAmount (p.apvts, ids::scAmount, "Amount", colour::violet, Size::primary, false, "connect a key track",
                "Sidechain amount: how far the key ducks (Make room) or lifts (Unmask) this track, up to 9 / 6 dB"),
      undoButton (anneal::IconButton::Glyph::undo, "Undo the last change (Ctrl+Z)"),
      redoButton (anneal::IconButton::Glyph::redo, "Redo (Ctrl+Shift+Z or Ctrl+Y)"),
      refMenu (p.apvts, ids::refMode),
      refAmount (p.apvts, ids::refAmount, "Amount", colour::teal, Size::primary, false, "drop a reference",
                 "Reference amount: how far Match steers the auto EQ toward the reference's tone, up to 6 dB either way"),
      punch (p.apvts, ids::punch, "Punch", colour::rose, Size::secondary, true, {},
             "Punch: makes attacks louder (+) or softer (-) in the bands they hit, up to 8 dB, and leaves the body of the "
             "sound alone. Sharpest in Low latency. Off = as before"),
      width (p.apvts, ids::width, "Width", colour::rose, Size::secondary, false, {},
             "Width: the stereo width of Anneal's output, from mono (0 %) to twice as wide (200 %). 100 % = as before"),
      monoBass (p.apvts, ids::monoBass, "Mono bass", colour::rose, Size::secondary, false, {},
                "Mono bass: makes everything below this frequency mono (a quarter octave either side fades in), "
                "so the low end stays centred and mono-safe. Off = as before")
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &screen, &presets, &reveal, &smooth, &tilt, &response, &guard, &heat, &color, &focus, &harmonics,
             &mix, &output, &match, &delta, &bypass, &meter, &latency, &stereoMode, &eqTarget, &heatTarget, &listen, &flavour,
             &freeze, &scMode, &ceiling, &scAmount, &undoButton, &redoButton, &refMenu, &refAmount, &paintButton,
             &punch, &width, &monoBass })
        addAndMakeVisible (c);
    undoButton.onClick = [&p] { p.undo(); };
    redoButton.onClick = [&p] { p.redo(); };

    // 4.0 Reference: a file dropped on the display or chosen in the menu is analysed off the audio
    // thread; the display draws the reference and says what the loader is doing.
    screen.canLoadFile = [] (const juce::File& f) { return AnnealAudioProcessor::canLoadReferenceFile (f); };
    screen.onFileDropped = [&p] (const juce::File& f) { p.loadReferenceFile (f); };
    refMenu.onLoadFile = [&p] (const juce::File& f) { p.loadReferenceFile (f); };
    refMenu.onClear = [&p] { p.clearReference(); };
    refMenu.hasReference = [this] { return refLoaded; };

    // 4.0 Paint: a pill in the header switches the display's paint mode; each stroke is one undo step.
    paintButton.setClickingTogglesState (true);
    paintButton.setColour (juce::TextButton::buttonOnColourId, colour::text);
    paintButton.setTooltip ("Paint: draw on the display how much of the auto EQ and of Heat acts in each region "
                            "(each stroke is one undo step)");
    paintButton.setTitle ("Paint");
    paintButton.onClick = [this] { screen.setPaintMode (paintButton.getToggleState()); };
    screen.onPaintBegin = [&p] { p.beginPaintStroke(); };
    screen.onPaintChange = [&p] (const anneal::PaintCurves& c) { p.setPaint (c); };
    screen.onPaintEnd = [&p] (bool merge) { p.endPaintStroke (merge); };

    // Factory presets first (also the host's programs), then the user's own (5.0).
    presets.getFactoryCount = [&p] { return p.getNumPrograms(); };
    presets.getCount = [&p] { return p.getNumPrograms() + p.getUserPresetNames().size(); };
    presets.getName = [&p] (int i)
    {
        const int nf = p.getNumPrograms();
        return i < nf ? p.getProgramName (i) : p.getUserPresetNames()[i - nf];
    };
    presets.getChosen = [&p]
    {
        const auto user = p.getUserPresetName();
        if (user.isNotEmpty())
            if (const int i = p.getUserPresetNames().indexOf (user, true); i >= 0)
                return p.getNumPrograms() + i;
        return p.getCurrentProgram();
    };
    presets.getDisplayText = [&p] { return p.getPresetDisplayName(); };
    presets.onChoose = [&p] (int i)
    {
        const int nf = p.getNumPrograms();
        if (i < nf)
        {
            p.setCurrentProgram (i);
            p.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
        }
        else
        {
            p.loadUserPreset (p.getUserPresetNames()[i - nf]);
        }
    };
    presets.onSaveAs = [this] { showSavePresetDialog(); };
    presets.onDelete = [this] (int i) { confirmDeletePreset (i); };
    presets.onOpenFolder = [&p]
    {
        const auto folder = p.getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };
    presets.refresh();
    harmonics.refresh();
    updateActivity();
    tick (0.0f);
}

void AnnealEditorContent::resized()
{
    // Header: wordmark, latency and Undo/Redo left, presets centred, the compare cluster right.
    latency.setBounds (182, 11, anneal::LatencyMenu::captionWidth + 92, 26);
    undoButton.setBounds (348, 11, 32, 26);
    redoButton.setBounds (384, 11, 32, 26);
    presets.setBounds (612, 10, 240, 28);
    stereoMode.setBounds (864, 11, 80, 26);
    paintButton.setBounds (956, 11, 76, 26);
    match.setBounds (1186, 11, 100, 26);
    delta.setBounds (1294, 11, 72, 26);
    bypass.setBounds (1374, 11, 78, 26);

    screen.setBounds (12, 50, 1440, 356);

    const int primaryW = anneal::Knob::preferredWidth (Size::primary), primaryH = anneal::Knob::preferredHeight (Size::primary);
    const int heroW = anneal::Knob::preferredWidth (Size::hero), heroH = anneal::Knob::preferredHeight (Size::hero);
    const int smallH = anneal::Knob::preferredHeight (Size::secondary);

    // AUTO EQ: the two primary rings, their settings stacked beside them; Freeze and the M/S
    // routing on the group's title row.
    freeze.setBounds (252, routingY, 84, 26);
    eqTarget.setBounds (360, routingY, 114, 26);
    reveal.setBounds (26, 442, primaryW, primaryH);
    smooth.setBounds (172, 442, primaryW, primaryH);
    tilt.setBounds (334, 442, 140, smallH);
    response.setBounds (334, 498, 140, smallH);
    guard.setBounds (334, 554, 140, smallH);

    // HEAT: the largest ring; Color with the harmonics it produces, then Focus.
    flavour.setBounds (562, routingY, 122, 26);
    heatTarget.setBounds (700, routingY, 118, 26);
    heat.setBounds (500, 442, heroW, heroH);
    color.setBounds (684, 438, 134, 53);
    harmonics.setBounds (690, 496, 122, 40);
    focus.setBounds (684, 546, 134, 53);

    // SIDECHAIN: the mode, then how much.
    scMode.setBounds (844, 438, 128, 26);
    scAmount.setBounds (838, 474, primaryW, primaryH);

    // REFERENCE (4.0): the mode (with Load / Clear), then how far Match steers.
    refMenu.setBounds (996, 438, 128, 26);
    refAmount.setBounds (990, 474, primaryW, primaryH);

    // OUTPUT: what you hear (Listen), the ceiling, then Mix and Output; the meter at the edge.
    // 5.0 SHAPE: Punch, Width and Mono bass as compact rows (like Tilt, Response and Guard).
    punch.setBounds (1148, 442, 132, smallH);
    width.setBounds (1148, 498, 132, smallH);
    monoBass.setBounds (1148, 554, 132, smallH);

    listen.setBounds (1300, 438, 112, 26);
    ceiling.setBounds (1300, 472, 112, 26);
    mix.setBounds (1302, 510, 104, smallH);
    output.setBounds (1302, 566, 104, smallH);
    meter.setBounds (1420, 438, 32, 190);
}

void AnnealEditorContent::paint (juce::Graphics& g)
{
    g.fillAll (colour::panel);

    // Header.
    g.setColour (colour::text);
    g.setFont (wordmarkFont (size::wordmark));
    g.drawText ("ANNEAL", juce::Rectangle<int> (24, 10, 170, 28), juce::Justification::centredLeft, false);


    // Group titles with role dots, hairlines between groups.
    auto groupTitle = [&g] (int x, const juce::String& text, std::initializer_list<juce::Colour> dots)
    {
        int dx = x;
        for (auto c : dots)
        {
            g.setColour (c);
            g.fillEllipse ((float) dx, (float) titleY + 4.5f, 6.0f, 6.0f);
            dx += 9;
        }
        g.setColour (colour::muted);
        g.setFont (caps (11.0f));
        g.drawText (text, juce::Rectangle<int> (dx + 4, titleY, 200, 15), juce::Justification::centredLeft, false);
    };
    groupTitle (28, "AUTO EQ", { colour::straw, colour::blue });
    groupTitle (506, "HEAT", { colour::bronze });
    groupTitle (848, "SIDECHAIN", { colour::violet });
    groupTitle (1000, "REFERENCE", { colour::teal });
    groupTitle (1152, "SHAPE", { colour::rose });
    groupTitle (1304, "OUTPUT", { colour::muted });

    g.setColour (colour::divider);
    for (int x : dividerX)
        g.drawVerticalLine (x, (float) dividerTop, (float) dividerBottom);
}

void AnnealEditorContent::showSavePresetDialog()
{
    // 5.0: name the current settings; saving under an existing name replaces that preset.
    const auto current = processor.getUserPresetName();
    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset",
                                                        "Name for the current settings (an existing preset with this name is replaced):",
                                                        juce::MessageBoxIconType::NoIcon, this);
    presetDialog->addTextEditor ("name", current.isNotEmpty() ? current : juce::String ("My preset"));
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<AnnealEditorContent> safe (this);
    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->presetDialog == nullptr)
            return;
        const auto name = safe->presetDialog->getTextEditorContents ("name");
        safe->presetDialog->setVisible (false);
        if (result == 1 && ! safe->processor.saveUserPreset (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved",
                "Use a name with letters or numbers, and check that this folder can be written:\n"
                    + safe->processor.getUserPresetFolder().getFullPathName(),
                "OK", safe.getComponent());
        safe->presets.refresh();
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->presetDialog.reset(); });
    }), false);
}

void AnnealEditorContent::confirmDeletePreset (int index)
{
    const int nf = processor.getNumPrograms();
    const auto name = processor.getUserPresetNames()[index - nf];
    if (index < nf || name.isEmpty())
        return;
    juce::Component::SafePointer<AnnealEditorContent> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
        "Delete \"" + name + "\"? The file goes to the recycle bin; the current settings stay as they are.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, name] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            safe->processor.deleteUserPreset (name);
            safe->presets.refresh();
        }));
}

void AnnealEditorContent::updateActivity()
{
    // Short sentences: they must fit under the ring at every size.
    const auto a = screen.getActivity();
    auto param = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };

    // 5.0 Punch: how hard the latest attack is shaped (the compact knob's inner arc).
    if (std::abs (param (ids::punch)) < 0.5f)
        punch.setActivity (0.0f, {});
    else
        punch.setActivity (juce::jlimit (0.0f, 1.0f, std::abs (a.punchDb) / 8.0f), juce::String (a.punchDb, 1) + " dB");

    if (param (ids::reveal) < 0.05f)        reveal.setActivity (0.0f, "off");
    else if (! a.live)                      reveal.setActivity (0.0f, {});
    else if (a.liftDb < 0.1f)               reveal.setActivity (0.0f, "nothing masked");
    else                                    reveal.setActivity (a.liftDb / 8.0f, "lifting " + signedDb (a.liftDb) + " dB");

    if (param (ids::smooth) < 0.05f)        smooth.setActivity (0.0f, "off");
    else if (! a.live)                      smooth.setActivity (0.0f, {});
    else if (a.cutDb > -0.1f)               smooth.setActivity (0.0f, "nothing dominant");
    else                                    smooth.setActivity (-a.cutDb / 9.0f, "cutting " + signedDb (a.cutDb) + " dB");

    if (param (ids::heat) < 0.05f)          heat.setActivity (0.0f, "off");
    else if (! a.live)                      heat.setActivity (0.0f, {});
    else if (a.heatRelDb < -90.0f)          heat.setActivity (0.0f, "no harmonics yet");
    else                                    heat.setActivity ((a.heatRelDb + 54.0f) / 48.0f,
                                                              "harmonics " + signedDb (a.heatRelDb) + " dB");

    const int sc = juce::roundToInt (param (ids::scMode));
    if (sc == 0)                                     scAmount.setActivity (0.0f, "off");
    else if (! processor.isSidechainConnected())     scAmount.setActivity (0.0f, "no key connected");
    else if (! a.live)                               scAmount.setActivity (0.0f, {});
    else if (! a.keyActive)                          scAmount.setActivity (0.0f, "key is silent");
    else if (std::abs (a.scDb) < 0.1f)               scAmount.setActivity (0.0f, sc == 1 ? "nothing to duck" : "nothing masked");
    else                                             scAmount.setActivity (std::abs (a.scDb) / (sc == 1 ? 9.0f : 6.0f),
                                                                           (a.scDb < 0.0f ? "ducking " : "lifting ") + signedDb (a.scDb) + " dB");
    scAmount.setAlpha (sc == 0 ? 0.55f : 1.0f);

    // 4.0 Reference: what Learn key has learned, or how far Match is steering right now.
    const int rm = juce::roundToInt (param (ids::refMode));
    if (processor.isAnalysingReference())             refAmount.setActivity (0.0f, "analysing the file");
    else if (rm == 0)                                 refAmount.setActivity (0.0f, "off");
    else if (rm == 1)
    {
        if (! processor.isSidechainConnected())       refAmount.setActivity (0.0f, "no key connected");
        else if (! a.live)                            refAmount.setActivity (0.0f, {});
        else if (a.refSeconds <= 0.0f)                refAmount.setActivity (0.0f, "waiting for the key");
        else                                          refAmount.setActivity (a.refSeconds / 30.0f,
                                                          "learned " + juce::String (juce::roundToInt (a.refSeconds)) + " s");
    }
    else if (! refLoaded)                             refAmount.setActivity (0.0f, "no reference yet");
    else if (! a.live)                                refAmount.setActivity (0.0f, {});
    else if (std::abs (a.matchDb) < 0.1f)             refAmount.setActivity (0.0f, "already matched");
    else                                              refAmount.setActivity (std::abs (a.matchDb) / 6.0f,
                                                          "matching " + signedDb (a.matchDb) + " dB");
    refAmount.setAlpha (rm == 0 ? 0.55f : 1.0f);
}

void AnnealEditorContent::setPaintMode (bool on)
{
    paintButton.setToggleState (on, juce::dontSendNotification);
    screen.setPaintMode (on);
}

void AnnealEditorContent::tick (float dt)
{
    const float peaks[2] = { processor.takeOutputPeak (0), processor.takeOutputPeak (1) };
    meter.push (peaks, processor.getMainBusNumOutputChannels(), dt);
    meter.pushReduction (processor.takeCeilingReductionDb(), dt);
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
    latency.refresh();
    presets.refresh();
    harmonics.refresh();

    // 4.0: a new reference (a file, a project, or Learn key a few times a second) and paint
    // (undo/redo, a project) reach the display; a failed file's reason shows for 6 s.
    processor.pollLearnedReference();
    if (processor.getReferenceSerial() != shownRefSerial)
    {
        const auto r = processor.getReference();
        shownRefSerial = processor.getReferenceSerial();
        refLoaded = r.curve.valid;
        screen.setReference (r.curve, r.name);
    }
    if (processor.getPaintSerial() != shownPaintSerial)
    {
        shownPaintSerial = processor.getPaintSerial();
        screen.setPaint (processor.getPaint());
    }
    const auto error = processor.getReferenceError();
    if (error != shownRefError)
    {
        shownRefError = error;
        refErrorTicks = 0;
    }
    screen.setReferenceStatus (processor.isAnalysingReference(), refErrorTicks++ < 180 ? error : juce::String());
    updateActivity();

    const bool stereo = processor.getMainBusNumInputChannels() > 1;
    stereoMode.setEnabled (stereo);
    const bool ms = stereo && processor.apvts.getRawParameterValue (ids::stereoMode)->load() > 0.5f;
    eqTarget.setEnabled (ms);
    heatTarget.setEnabled (ms);

    // A listen solo replaces what Delta would play, so Delta steps back while one is on.
    delta.setAlpha (listen.getChoice() > 0 ? 0.4f : 1.0f);

    // The match gain lives in its button: you see what the comparison is compensating.
    const bool on = processor.apvts.getRawParameterValue (ids::match)->load() > 0.5f;
    const auto text = on ? "MATCH " + signedDb (processor.getMatchGainDb()) : juce::String ("MATCH");
    if (text != shownMatchText)
    {
        shownMatchText = text;
        match.setButtonText (text);
    }
}

//==============================================================================
AnnealEditor::AnnealEditor (AnnealAudioProcessor& p)
    : AudioProcessorEditor (p), annealProcessor (p), content (p)
{
    // Add the content first: setLookAndFeel() only notifies existing children, and the
    // sliders must rebuild their value fields with this LookAndFeel.
    addAndMakeVisible (content);
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (1098, 480, 2196, 960);   // 0.75x to 1.5x of 1464 x 640
    setWantsKeyboardFocus (true);            // clicking the editor lets Ctrl+Z / Ctrl+Y reach it
    getConstrainer()->setFixedAspectRatio ((double) AnnealEditorContent::baseWidth / AnnealEditorContent::baseHeight);
    setSize (AnnealEditorContent::baseWidth, AnnealEditorContent::baseHeight);

    startTimerHz (30);
}

AnnealEditor::~AnnealEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AnnealEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::panel);
}

void AnnealEditor::resized()
{
    const float scale = (float) getWidth() / (float) AnnealEditorContent::baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, AnnealEditorContent::baseWidth, AnnealEditorContent::baseHeight);
}

void AnnealEditor::timerCallback()
{
    content.tick (1.0f / 30.0f);
}

bool AnnealEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == juce::KeyPress::escapeKey && content.isPaintMode())
    {
        content.setPaintMode (false);
        return true;
    }
    if (! mods.isCommandDown() || mods.isAltDown())
        return false;
    const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    if (c == 'z' && ! mods.isShiftDown())
    {
        annealProcessor.undo();
        return true;
    }
    if ((c == 'z' && mods.isShiftDown()) || (c == 'y' && ! mods.isShiftDown()))
    {
        annealProcessor.redo();
        return true;
    }
    return false;
}
