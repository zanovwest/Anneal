#pragma once

#include "PluginProcessor.h"
#include "ui/AnnealLookAndFeel.h"
#include "ui/Fader.h"
#include "ui/FlavourMenu.h"
#include "ui/HarmonicGraph.h"
#include "ui/IconButton.h"
#include "ui/Knob.h"
#include "ui/LatencyMenu.h"
#include "ui/ListenMenu.h"
#include "ui/OutputMeter.h"
#include "ui/PerceptionScreen.h"
#include "ui/PresetBar.h"
#include "ui/RoutingMenu.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Everything laid out at a fixed base size; the editor scales it uniformly.
// Layout (docs/DESIGN.md, 3.0): header with Undo/Redo, presets and the compare cluster (Match,
// Delta, Bypass); the perception screen; a control deck in signal order: AUTO EQ, HEAT,
// SIDECHAIN, OUTPUT.
class AnnealEditorContent : public juce::Component
{
public:
    static constexpr int baseWidth = 1160, baseHeight = 640;

    explicit AnnealEditorContent (AnnealAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dtSeconds);   // meters + live read-outs, called from the editor's timer
    float getShownCeilingReductionDb() const noexcept { return meter.getShownReductionDb(); }

    anneal::PerceptionScreen screen;
    anneal::PresetBar presets;

private:
    void updateActivity();

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

    juce::String shownMatchText;
};

class AnnealEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnnealEditor (AnnealAudioProcessor&);
    ~AnnealEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    AnnealEditorContent& getContent() noexcept { return content; }
    float getShownCeilingReductionDb() const noexcept { return content.getShownCeilingReductionDb(); }

private:
    void timerCallback() override;

    anneal::AnnealLookAndFeel lookAndFeel;   // declared first: outlives every child
    AnnealEditorContent content;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealEditor)
};
