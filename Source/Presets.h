#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

// Factory presets. Each sets the tone controls; Match, Delta and Bypass are listening aids and stay
// as they are. Presets are also the host's program list (FL Studio's preset menu).
namespace anneal::presets
{
    // The parameters a preset sets, in the order of Preset::values.
    inline constexpr int numValues = 15;
    extern const std::array<const char*, numValues> paramIds;

    struct Preset
    {
        const char* name;
        std::array<float, numValues> values;   // plain values (%, dB/oct, ms, Hz, ...)
    };

    int count() noexcept;
    const Preset& get (int index) noexcept;   // index is clamped

    // Message thread. Sets every value as one gesture per parameter, so hosts record it.
    void apply (juce::AudioProcessorValueTreeState& state, int index);

    // The preset the current settings equal (preferring `preferred` when several do), or -1.
    int findMatching (juce::AudioProcessorValueTreeState& state, int preferred = -1);

    // 5.0 user presets: one XML file per preset (<name>.annealpreset) in a folder, by default the
    // user's application-data folder (Windows: %APPDATA%\Qulic\Anneal\Presets). They hold the same
    // values a factory preset sets. Message thread.
    using Values = std::array<float, numValues>;
    inline constexpr const char* userExtension = ".annealpreset";
    juce::File defaultUserFolder();
    juce::String sanitiseName (const juce::String& name);    // what a file name can hold; empty if nothing usable
    juce::StringArray userNames (const juce::File& folder);   // sorted, case-insensitive
    Values currentValues (juce::AudioProcessorValueTreeState& state);
    bool equals (juce::AudioProcessorValueTreeState& state, const Values& values);
    void applyValues (juce::AudioProcessorValueTreeState& state, const Values& values);   // one gesture per parameter
    bool writeUser (const juce::File& folder, const juce::String& name, const Values& values);
    // Fills in the values the file has (a preset from an older version may lack some: those keep
    // what `values` held). False when the file is missing or isn't an Anneal preset.
    bool readUser (const juce::File& folder, const juce::String& name, Values& values);
    bool deleteUser (const juce::File& folder, const juce::String& name, bool toTrash = true);   // recycle bin when possible
}
