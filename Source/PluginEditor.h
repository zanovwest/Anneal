#pragma once

#include "PluginProcessor.h"
#include "ui/AnnealLookAndFeel.h"
#include "ui/Knob.h"
#include "ui/OutputMeter.h"
#include "ui/PerceptionScreen.h"
#include "ui/PresetBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Everything laid out at a fixed base size; the editor scales it uniformly.
class AnnealEditorContent : public juce::Component
{
public:
    static constexpr int baseWidth = 1000, baseHeight = 620;

    explicit AnnealEditorContent (AnnealAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dtSeconds);   // meters + read-outs, called from the editor's timer

    anneal::PerceptionScreen screen;
    anneal::PresetBar presets;

private:
    AnnealAudioProcessor& processor;

    anneal::Knob reveal, smooth, tilt, response, heat, color, focus, mix, output;
    anneal::Toggle match, delta, bypass;
    anneal::OutputMeter meter;

    float shownMatchDb = 0.0f;
    bool shownMatchOn = false;
};

class AnnealEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnnealEditor (AnnealAudioProcessor&);
    ~AnnealEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    AnnealEditorContent& getContent() noexcept { return content; }

private:
    void timerCallback() override;

    anneal::AnnealLookAndFeel lookAndFeel;   // declared first: outlives every child
    AnnealEditorContent content;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealEditor)
};
