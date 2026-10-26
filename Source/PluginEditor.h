#pragma once

#include "PluginProcessor.h"
#include "ui/AnnealLookAndFeel.h"
#include "ui/Fader.h"
#include "ui/FlavourMenu.h"
#include "ui/HarmonicGraph.h"
#include "ui/IconButton.h"
#include "ui/Knob.h"
#include "ui/LatencyMenu.h"
#include "ui/LimitMenu.h"
#include "ui/ListenMenu.h"
#include "ui/LoudnessReadout.h"
#include "ui/OutputMeter.h"
#include "ui/PerceptionScreen.h"
#include "ui/PresetBar.h"
#include "ui/ReferenceMenu.h"
#include "ui/RoutingMenu.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Everything laid out at a fixed base size; the editor scales it uniformly.
// Layout (docs/DESIGN.md, 5.3): header with Undo/Redo, presets, Paint and the compare cluster
// (Match, Delta, Bypass); the perception screen; a control deck in signal order: AUTO EQ, HEAT,
// SIDECHAIN over REFERENCE in one column, SHAPE, OUTPUT. Controls that do nothing in the current
// setting (the Mid/Side routing in Stereo, an Amount whose mode is off) are not shown.
class AnnealEditorContent : public juce::Component
{
public:
    static constexpr int baseWidth = 1464, baseHeight = 640;

    explicit AnnealEditorContent (AnnealAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dtSeconds);   // meters + live read-outs, called from the editor's timer
    float getShownCeilingReductionDb() const noexcept { return meter.getShownReductionDb(); }

    anneal::PerceptionScreen screen;
    anneal::PresetBar presets;

private:
    void updateActivity();
    void showSavePresetDialog();          // 5.0 user presets
    void confirmDeletePreset (int index);

    AnnealAudioProcessor& processor;

    anneal::Knob reveal, smooth, tilt, response, guard, heat, mix, output;
    anneal::Fader color, focus;
    anneal::HarmonicGraph harmonics;
    anneal::Toggle match, delta, bypass;
    anneal::OutputMeter meter;
    anneal::LatencyMenu latency;
    anneal::RoutingMenu stereoMode, eqTarget, heatTarget;
    anneal::ListenMenu listen;
    anneal::FlavourMenu flavour;
    // 3.0
    anneal::Toggle freeze;
    anneal::RoutingMenu scMode, ceiling;
    anneal::Knob scAmount;
    anneal::IconButton undoButton, redoButton;
    // 4.0
    anneal::ReferenceMenu refMenu;
    anneal::Knob refAmount;
    juce::TextButton paintButton { "PAINT" };
    // 5.0
    anneal::Knob punch, width, monoBass;
    std::unique_ptr<juce::AlertWindow> presetDialog;
    anneal::LimitMenu limits;   // 5.3.2 Max boost / Max cut, on the AUTO EQ title row

public:
    anneal::LoudnessReadout loudness;   // 5.3.1, in the header beside MATCH

private:

    // 5.3: Sidechain's and Reference's live line, beside their titles (the Amount rows are compact).
    struct Status { juce::String text; bool active = false; };
    Status scStatus, refStatus;
    void setStatus (Status& s, const juce::String& text, bool active, int titleY);

    juce::String shownMatchText, shownRefError;
    std::uint32_t shownRefSerial = ~0u, shownPaintSerial = ~0u;
    bool refLoaded = false;
    int refErrorTicks = 0;
    float zeroPunchShown = 0.0f;   // 5.2.7: Punch at zero latency, dB (the ring's arc)

public:
    void setPaintMode (bool on);   // the Paint pill and the display's paint mode, together
    bool isPaintMode() const noexcept { return paintButton.getToggleState(); }
};

class AnnealEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnnealEditor (AnnealAudioProcessor&);
    ~AnnealEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    // 4.0: Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo (Cmd on macOS), Escape leaves paint mode,
    // when the host passes keys to the editor.
    bool keyPressed (const juce::KeyPress&) override;

    AnnealEditorContent& getContent() noexcept { return content; }
    float getShownCeilingReductionDb() const noexcept { return content.getShownCeilingReductionDb(); }

private:
    void timerCallback() override;

    AnnealAudioProcessor& annealProcessor;
    anneal::AnnealLookAndFeel lookAndFeel;   // declared first: outlives every child
    AnnealEditorContent content;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealEditor)
};
