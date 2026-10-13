#include "PluginEditor.h"
#include "Params.h"
#include "ui/Theme.h"

using namespace anneal::theme;
using Size = anneal::Knob::Size;
namespace ids = anneal::ids;

namespace
{
    const juce::Rectangle<int> matchReadoutArea { 780, 504, 112, 40 };
    constexpr int dividerX[] = { 392, 656 };
}

AnnealEditorContent::AnnealEditorContent (AnnealAudioProcessor& p)
    : screen (p.getEngine().getDisplay(), p.apvts),
      processor (p),
      reveal   (p.apvts, ids::reveal,   "Reveal",   colour::straw,  Size::primary,   false, "lifts masked detail",
                "Reveal: lifts bands that sit in the masking shadow of louder neighbours"),
      smooth   (p.apvts, ids::smooth,   "Smooth",   colour::blue,   Size::primary,   false, "tames dominance",
                "Smooth: pulls down bands that dominate their auditory neighbourhood"),
      tilt     (p.apvts, ids::tilt,     "Tilt",     colour::muted,  Size::secondary, true,  {},
                "Tilt: static slope around 1 kHz, applied inside the range (darker to brighter)"),
      response (p.apvts, ids::response, "Response", colour::muted,  Size::secondary, false, {},
                "Response: how quickly the auto EQ follows the music (release; attack is 3x faster)"),
      heat     (p.apvts, ids::heat,     "Heat",     colour::bronze, Size::primary,   false, "harmonics where thin",
                "Heat: spectral saturation; each partial grows its own 2nd-6th harmonics where the spectrum is thin"),
      color    (p.apvts, ids::color,    "Color",    colour::bronze, Size::secondary, false, {},
                "Color: even harmonics (round, 2nd/4th/6th) to odd harmonics (edgier, 3rd/5th)"),
      focus    (p.apvts, ids::focus,    "Focus",    colour::bronze, Size::secondary, true,  {},
                "Focus: concentrates harmonics toward the body (low) or the air (high)"),
      mix      (p.apvts, ids::mix,      "Mix",      colour::muted,  Size::secondary, false, {},
                "Mix: blend with the latency-aligned dry signal"),
      output   (p.apvts, ids::output,   "Output",   colour::muted,  Size::secondary, true,  {},
                "Output: final gain"),
      match    (p.apvts, ids::match,  "MATCH",  colour::straw,  "Loudness match: slow K-weighted gain so you judge tone, not level"),
      delta    (p.apvts, ids::delta,  "DELTA",  colour::bronze, "Delta: hear only what Anneal adds and removes"),
      bypass   (p.apvts, ids::bypass, "BYPASS", colour::text,   "Bypass (latency-compensated, click-free)")
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &screen, &presets, &reveal, &smooth, &tilt, &response, &heat, &color, &focus, &mix, &output,
             &match, &delta, &bypass, &meter })
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
}

void AnnealEditorContent::resized()
{
    screen.setBounds (16, 52, 968, 330);

    // AUTO EQ
    reveal.setBounds (24, 432, 120, 146);
    smooth.setBounds (150, 432, 120, 146);
    tilt.setBounds (284, 414, 92, 92);
    response.setBounds (284, 510, 92, 92);

    // HEAT
    heat.setBounds (412, 432, 120, 146);
    color.setBounds (544, 414, 92, 92);
    focus.setBounds (544, 510, 92, 92);

    // OUTPUT
    mix.setBounds (672, 414, 92, 92);
    output.setBounds (672, 510, 92, 92);
    match.setBounds (780, 428, 112, 28);
    delta.setBounds (780, 464, 112, 28);
    meter.setBounds (912, 412, 64, 190);

    presets.setBounds (452, 13, 236, 26);
    bypass.setBounds (892, 13, 92, 26);
}

void AnnealEditorContent::paint (juce::Graphics& g)
{
    g.fillAll (colour::panel);

    // Header.
    const auto wordmark = juce::Font (juce::FontOptions ("Bahnschrift", "Light", size::wordmark).withKerningFactor (0.32f));
    g.setColour (colour::text);
    g.setFont (wordmark);
    g.drawText ("ANNEAL", juce::Rectangle<int> (20, 10, 170, 32), juce::Justification::centredLeft, false);
    g.setColour (colour::muted);
    g.setFont (font (13.5f));
    g.drawText ("perceptual spectral saturator", juce::Rectangle<int> (172, 14, 260, 28), juce::Justification::centredLeft, false);

    g.setFont (caps (11.5f));
    g.drawText ("PRESET", juce::Rectangle<int> (380, 13, 64, 26), juce::Justification::centredRight, false);
    g.drawText ("LATENCY", juce::Rectangle<int> (700, 13, 110, 26), juce::Justification::centredRight, false);
    g.setColour (colour::text);
    g.setFont (font (14.0f));
    drawTabular (g, juce::String (processor.getLatencyMs(), 1) + " ms", juce::Rectangle<float> (814.0f, 13.0f, 70.0f, 26.0f),
                 juce::Justification::centredLeft);

    // Group titles with role swatches, and hairline dividers between groups.
    auto groupTitle = [&g] (int x, const juce::String& text, std::initializer_list<juce::Colour> swatches)
    {
        int sx = x;
        for (auto c : swatches)
        {
            g.setColour (c);
            g.fillRect (sx, 401, 7, 7);
            sx += 10;
        }
        g.setColour (colour::muted);
        g.setFont (caps (12.0f));
        g.drawText (text, juce::Rectangle<int> (sx + 4, 396, 200, 16), juce::Justification::centredLeft, false);
    };
    groupTitle (24, "AUTO EQ", { colour::straw, colour::blue });
    groupTitle (412, "HEAT", { colour::bronze });
    groupTitle (672, "OUTPUT", { colour::muted });

    g.setColour (colour::divider);
    for (int x : dividerX)
        g.drawVerticalLine (x, 398.0f, 604.0f);

    // Live loudness-match gain (measured by the processor).
    const auto r = matchReadoutArea;
    g.setColour (colour::muted);
    g.setFont (caps (11.0f));
    g.drawText ("MATCH GAIN", r.withHeight (16), juce::Justification::centredLeft, false);
    g.setFont (font (size::value));
    if (shownMatchOn)
    {
        g.setColour (colour::text);
        const float v = shownMatchDb;
        const auto s = std::abs (v) < 0.05f ? juce::String ("0.0")
                                            : (v > 0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (v), 1);
        drawTabular (g, s + " dB", r.withTrimmedTop (18).withHeight (20).toFloat(), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (colour::faint);
        g.drawText ("off", r.withTrimmedTop (18).withHeight (20), juce::Justification::centredLeft, false);
    }
}

void AnnealEditorContent::tick (float dt)
{
    const float peaks[2] = { processor.takeOutputPeak (0), processor.takeOutputPeak (1) };
    meter.push (peaks, processor.getTotalNumOutputChannels(), dt);
    presets.refresh();

    const bool on = processor.apvts.getRawParameterValue (ids::match)->load() > 0.5f;
    const float db = processor.getMatchGainDb();
    if (on != shownMatchOn || std::abs (db - shownMatchDb) > 0.05f)
    {
        shownMatchOn = on;
        shownMatchDb = db;
        repaint (matchReadoutArea);
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
