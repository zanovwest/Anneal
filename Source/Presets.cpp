#include "Presets.h"
#include "Params.h"

#include <cmath>
#include <limits>

namespace anneal::presets
{
const std::array<const char*, numValues> paramIds {
    ids::reveal, ids::smooth, ids::tilt, ids::response, ids::rangeLo, ids::rangeHi,
    ids::heat, ids::color, ids::focus, ids::mix, ids::output, ids::flavour,
    ids::punch, ids::width, ids::monoBass
};

namespace
{
    // reveal %, smooth %, tilt dB/oct, response ms, range low Hz, range high Hz,
    // heat %, color % odd, focus (-100 body .. +100 air), mix %, output dB, flavour (0 = Clean;
    // every factory preset predates flavours and keeps the Clean saturation it was made with),
    // punch %, width %, mono bass Hz (5.0: off, 100 %, off, so every preset sounds as before)
    const Preset factory[] = {
        // The parameter defaults, so a fresh instance shows "Default".
        { "Default",        { 40, 40,  0.0f, 120,  30, 18000, 30, 35,   0, 100,  0.0f, 0, 0, 100, 0 } },
        // Barely there: a little masking relief and warmth.
        { "Gentle",         { 20, 25,  0.0f, 200,  30, 18000, 10, 30,   0, 100,  0.0f, 0, 0, 100, 0 } },
        // Calms what pokes out, warm even harmonics, slow so the bus doesn't pump.
        { "Mix bus glue",   { 30, 45,  0.0f, 180,  30, 16000, 25, 25,   0, 100,  0.0f, 0, 0, 100, 0 } },
        // Light touch for a finished track: slow, a hint of air, blended in.
        { "Master polish",  { 25, 35,  0.2f, 250,  25, 20000, 15, 30,  10,  85,  0.0f, 0, 0, 100, 0 } },
        // Opens up the top of a vocal: more reveal, harmonics toward the air, lows left alone.
        { "Vocal air",      { 50, 50,  0.4f,  90, 150, 20000, 35, 40,  50, 100,  0.0f, 0, 0, 100, 0 } },
        // Low end only: harmonics of the sub so the 808 reads on small speakers.
        { "808 weight",     { 30, 20, -0.3f, 150,  25,  1500, 55, 45, -60, 100, -1.0f, 0, 0, 100, 0 } },
        // Fast response for hits, odd harmonics for edge.
        { "Drum bus snap",  { 45, 55,  0.2f,  40,  40, 18000, 35, 70,  20, 100,  0.0f, 0, 0, 100, 0 } },
        // Darker, rounder, lots of even harmonics in the body, top rolled away.
        { "Lo-fi warmth",   { 20, 60, -0.8f, 300,  60,  9000, 60, 10, -40, 100, -1.5f, 0, 0, 100, 0 } },
    };
    constexpr int numFactory = (int) (sizeof (factory) / sizeof (factory[0]));

    bool equals (juce::AudioProcessorValueTreeState& state, const Preset& p)
    {
        return presets::equals (state, p.values);
    }
}

bool equals (juce::AudioProcessorValueTreeState& state, const Values& values)
{
    for (int i = 0; i < numValues; ++i)
    {
        auto* param = state.getParameter (paramIds[(size_t) i]);
        if (param == nullptr || std::abs (param->getValue() - param->convertTo0to1 (values[(size_t) i])) > 1.0e-3f)
            return false;
    }
    return true;
}

Values currentValues (juce::AudioProcessorValueTreeState& state)
{
    Values v {};
    for (int i = 0; i < numValues; ++i)
        if (auto* param = state.getParameter (paramIds[(size_t) i]))
            v[(size_t) i] = param->convertFrom0to1 (param->getValue());
    return v;
}

void applyValues (juce::AudioProcessorValueTreeState& state, const Values& values)
{
    for (int i = 0; i < numValues; ++i)
        if (auto* param = state.getParameter (paramIds[(size_t) i]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (values[(size_t) i]));
            param->endChangeGesture();
        }
}

juce::File defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Qulic").getChildFile ("Anneal").getChildFile ("Presets");
}

juce::String sanitiseName (const juce::String& name)
{
    // Keep what every file system accepts: no path or wildcard characters, no control characters,
    // no trailing dots or spaces (Windows), single spaces, at most 64 characters.
    juce::String out;
    for (auto c : name)
    {
        if (c < 32 || juce::String ("\\/:*?\"<>|").containsChar (c))
            c = ' ';
        out += juce::String::charToString (c);
    }
    while (out.contains ("  "))
        out = out.replace ("  ", " ");
    out = out.trim().substring (0, 64).trim();
    while (out.endsWithChar ('.') || out.endsWithChar (' '))
        out = out.dropLastCharacters (1);
    static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
                                              "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
                                              "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    if (reserved.contains (out, true))
        out << " preset";
    return out;
}

juce::StringArray userNames (const juce::File& folder)
{
    juce::StringArray names;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + userExtension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool writeUser (const juce::File& folder, const juce::String& name, const Values& values)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty() || ! folder.createDirectory())
        return false;
    juce::XmlElement root ("AnnealPreset");
    root.setAttribute ("version", 1);
    root.setAttribute ("name", clean);
    for (int i = 0; i < numValues; ++i)
    {
        auto* p = root.createNewChildElement ("PARAM");
        p->setAttribute ("id", paramIds[(size_t) i]);
        p->setAttribute ("value", (double) values[(size_t) i]);
    }
    return folder.getChildFile (clean + userExtension).replaceWithText (root.toString());
}

namespace
{
    bool readFile (const juce::File& file, Values& values);

    // Two presets read over a NaN background are the same when every value they set is the same.
    bool sameValues (const Values& a, const Values& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (! (std::isnan (a[i]) && std::isnan (b[i])) && ! (std::abs (a[i] - b[i]) < 1.0e-6f))
                return false;
        return true;
    }
}

bool readUser (const juce::File& folder, const juce::String& name, Values& values)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;
    return readFile (folder.getChildFile (clean + userExtension), values);
}

juce::String importUser (const juce::File& folder, const juce::File& file, juce::String& error)
{
    error.clear();
    const auto shownName = file.getFileName();
    if (! file.existsAsFile() || ! file.hasFileExtension (userExtension))
    {
        error = shownName + juce::String::fromUTF8 (" isn\xe2\x80\x99t an Anneal preset (.annealpreset)");
        return {};
    }
    Values incoming;
    incoming.fill (std::numeric_limits<float>::quiet_NaN());
    if (file.getSize() > 1024 * 1024 || ! readFile (file, incoming))
    {
        error = shownName + juce::String::fromUTF8 (" isn\xe2\x80\x99t an Anneal preset");
        return {};
    }
    const auto base = sanitiseName (file.getFileNameWithoutExtension());
    if (base.isEmpty() || ! folder.createDirectory())
    {
        error = shownName + juce::String::fromUTF8 (" couldn\xe2\x80\x99t be added");
        return {};
    }
    for (int n = 1; n < 1000; ++n)
    {
        const auto name = n == 1 ? base : sanitiseName (base.substring (0, 58) + " " + juce::String (n));
        const auto dest = folder.getChildFile (name + userExtension);
        if (dest == file)
            return name;   // dropped from the presets folder itself
        if (! dest.existsAsFile())
        {
            if (file.copyFileTo (dest))
                return name;
            error = shownName + juce::String::fromUTF8 (" couldn\xe2\x80\x99t be copied");
            return {};
        }
        Values there;
        there.fill (std::numeric_limits<float>::quiet_NaN());
        if (readFile (dest, there) && sameValues (there, incoming))
            return name;   // already there
    }
    error = shownName + ": too many presets with that name";
    return {};
}

namespace
{
bool readFile (const juce::File& file, Values& values)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("AnnealPreset"))
        return false;
    int found = 0;
    for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = p->getStringAttribute ("id");
        for (int i = 0; i < numValues; ++i)
            if (id == paramIds[(size_t) i] && p->hasAttribute ("value"))
            {
                const double v = p->getDoubleAttribute ("value");
                if (std::isfinite (v))
                {
                    values[(size_t) i] = (float) v;
                    ++found;
                }
            }
    }
    return found > 0;
}
}

bool deleteUser (const juce::File& folder, const juce::String& name, bool toTrash)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;
    const auto file = folder.getChildFile (clean + userExtension);
    if (! file.existsAsFile())
        return false;
    return (toTrash && file.moveToTrash()) || file.deleteFile();
}

int count() noexcept { return numFactory; }

const Preset& get (int index) noexcept
{
    return factory[juce::jlimit (0, numFactory - 1, index)];
}

void apply (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& p = get (index);
    for (int i = 0; i < numValues; ++i)
        if (auto* param = state.getParameter (paramIds[(size_t) i]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (p.values[(size_t) i]));
            param->endChangeGesture();
        }
}

int findMatching (juce::AudioProcessorValueTreeState& state, int preferred)
{
    if (juce::isPositiveAndBelow (preferred, numFactory) && equals (state, factory[preferred]))
        return preferred;
    for (int i = 0; i < numFactory; ++i)
        if (equals (state, factory[i]))
            return i;
    return -1;
}
}
