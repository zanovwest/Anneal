#include "PluginEditor.h"
#include "Params.h"
#include "ui/Theme.h"

using namespace anneal::theme;
using Size = anneal::Knob::Size;
namespace ids = anneal::ids;

namespace
{
    // Deck geometry (base size). Groups follow the signal: AUTO EQ -> HEAT -> OUTPUT.
    constexpr int titleY = 408, dividerTop = 414, dividerBottom = 604;
    constexpr int routingY = 402;   // 26 px pills centred on the 15 px title row
    constexpr int dividerX[] = { 490, 832 };

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
      flavour (p.apvts, ids::flavour)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &screen, &presets, &reveal, &smooth, &tilt, &response, &guard, &heat, &color, &focus, &harmonics,
             &mix, &output, &match, &delta, &bypass, &meter, &latency, &stereoMode, &eqTarget, &heatTarget, &listen, &flavour })
        addAndMakeVisible (c);

    presets.getCount = [&p] { return p.getNumPrograms(); };
    presets.getName = [&p] (int i) { return p.getProgramName (i); };
    presets.getChosen = [&p] { return p.getCurrentProgram(); };
    presets.getDisplayText = [&p] { return p.getPresetDisplayName(); };
    presets.onChoose = [&p] (int i)
    {
        p.setCurrentProgram (i);
        p.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    };
    presets.refresh();
    harmonics.refresh();
    updateActivity();
    tick (0.0f);
}

void AnnealEditorContent::resized()
{
    // Header: wordmark left, presets centred, the compare cluster right.
    latency.setBounds (182, 11, anneal::LatencyMenu::captionWidth + 92, 26);
    presets.setBounds (380, 10, 240, 28);
    match.setBounds (712, 11, 100, 26);
    delta.setBounds (820, 11, 72, 26);
    bypass.setBounds (900, 11, 78, 26);
    stereoMode.setBounds (626, 11, 80, 26);
    // The routing menus sit on their group's title row (the same row for both).
    eqTarget.setBounds (360, routingY, 114, 26);
    heatTarget.setBounds (700, routingY, 118, 26);
    // The saturation flavour shapes everything in HEAT, so it sits on the group's title row too.
    flavour.setBounds (562, routingY, 122, 26);

    screen.setBounds (12, 50, 976, 346);

    const int primaryW = anneal::Knob::preferredWidth (Size::primary), primaryH = anneal::Knob::preferredHeight (Size::primary);
    const int heroW = anneal::Knob::preferredWidth (Size::hero), heroH = anneal::Knob::preferredHeight (Size::hero);
    const int smallH = anneal::Knob::preferredHeight (Size::secondary);

    // AUTO EQ: the two primary rings, their settings stacked beside them.
    reveal.setBounds (26, 432, primaryW, primaryH);
    smooth.setBounds (172, 432, primaryW, primaryH);
    tilt.setBounds (334, 432, 140, smallH);
    response.setBounds (334, 488, 140, smallH);
    guard.setBounds (334, 544, 140, smallH);

    // HEAT: the largest ring; Color with the harmonics it produces, then Focus.
    // The ring's top lines up with Reveal and Smooth, below the Flavour pill on the title row.
    heat.setBounds (500, 432, heroW, heroH);
    color.setBounds (684, 428, 134, 53);
    harmonics.setBounds (690, 486, 122, 40);
    focus.setBounds (684, 536, 134, 53);

    // OUTPUT: what you hear (Listen), then Mix and Output.
    listen.setBounds (844, 430, 102, 26);
    mix.setBounds (846, 468, 100, smallH);
    output.setBounds (846, 528, 100, smallH);
    meter.setBounds (950, 424, 34, 176);
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
    groupTitle (848, "OUTPUT", { colour::muted });

    g.setColour (colour::divider);
    for (int x : dividerX)
        g.drawVerticalLine (x, (float) dividerTop, (float) dividerBottom);
}

void AnnealEditorContent::updateActivity()
{
    // Short sentences: they must fit under the ring at every size.
    const auto a = screen.getActivity();
    auto param = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };

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
}

void AnnealEditorContent::tick (float dt)
{
    const float peaks[2] = { processor.takeOutputPeak (0), processor.takeOutputPeak (1) };
    meter.push (peaks, processor.getTotalNumOutputChannels(), dt);
    latency.refresh();
    presets.refresh();
    harmonics.refresh();
    updateActivity();

    const bool stereo = processor.getTotalNumInputChannels() > 1;
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
    : AudioProcessorEditor (p), content (p)
{
    // Add the content first: setLookAndFeel() only notifies existing children, and the
    // sliders must rebuild their value fields with this LookAndFeel.
    addAndMakeVisible (content);
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (800, 496, 1600, 992);
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
