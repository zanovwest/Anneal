#include "Params.h"

namespace anneal
{
namespace
{
    using Float = juce::AudioParameterFloat;
    using Attr  = juce::AudioParameterFloatAttributes;

    float leadingNumber (const juce::String& text)
    {
        return text.retainCharacters ("0123456789.-+").getFloatValue();
    }

    std::unique_ptr<Float> percent (const char* id, const char* name, float def)
    {
        return std::make_unique<Float> (juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            Attr().withLabel ("%")
                  .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                  .withValueFromStringFunction ([] (const juce::String& t) { return leadingNumber (t); }));
    }
}

juce::String formatHz (float hz)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
}

float parseHz (const juce::String& text)
{
    const auto lower = text.toLowerCase();
    const float v = leadingNumber (lower);
    return lower.containsChar ('k') ? v * 1000.0f : v;
}

int parseListen (const juce::String& text)
{
    const auto l = text.toLowerCase().trim();
    if (l.containsAnyOf ("0123456789"))
        return juce::jlimit (0, 3, l.getIntValue());
    if (l.contains ("rev") || l.contains ("lift"))                        return 1;
    if (l.contains ("smo") || l.contains ("cut"))                         return 2;
    if (l.contains ("harm") || l.contains ("heat") || l.contains ("sat")) return 3;
    return 0;
}

int parseFlavour (const juce::String& text)
{
    const auto l = text.toLowerCase().trim();
    if (l.containsAnyOf ("0123456789"))
        return juce::jlimit (0, 4, l.getIntValue());
    if (l.contains ("tape"))                                               return 1;
    if (l.contains ("tube") || l.contains ("valve"))                       return 2;
    if (l.contains ("trans") || l.contains ("iron") || l.contains ("xf"))  return 3;
    if (l.contains ("exc") || l.contains ("air"))                          return 4;
    return 0;
}

int parseReference (const juce::String& text)
{
    const auto l = text.toLowerCase().trim();
    if (l.containsAnyOf ("0123456789"))
        return juce::jlimit (0, 2, l.getIntValue());
    if (l.contains ("learn") || l.contains ("key")) return 1;
    if (l.contains ("match") || l.contains ("ref"))  return 2;
    return 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (percent (ids::reveal, "Reveal", 40.0f));
    layout.add (percent (ids::smooth, "Smooth", 40.0f));

    layout.add (std::make_unique<Float> (juce::ParameterID { ids::tilt, 1 }, "Tilt",
        juce::NormalisableRange<float> (-1.5f, 1.5f, 0.01f), 0.0f,
        Attr().withLabel ("dB/oct")
              .withStringFromValueFunction ([] (float v, int)
              {
                  if (std::abs (v) < 0.005f) return juce::String ("0.00 dB/oct");
                  return (v > 0 ? "+" : "") + juce::String (v, 2) + " dB/oct";
              })
              .withValueFromStringFunction ([] (const juce::String& t) { return leadingNumber (t); })));

    {
        juce::NormalisableRange<float> r (20.0f, 500.0f, 1.0f);
        r.setSkewForCentre (120.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { ids::response, 1 }, "Response", r, 120.0f,
            Attr().withLabel ("ms")
                  .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })
                  .withValueFromStringFunction ([] (const juce::String& t) { return leadingNumber (t); })));
    }

    auto hzAttr = Attr().withLabel ("Hz")
                        .withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
                        .withValueFromStringFunction ([] (const juce::String& t) { return parseHz (t); });
    {
        juce::NormalisableRange<float> r (20.0f, 2000.0f, 0.1f);
        r.setSkewForCentre (200.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { ids::rangeLo, 1 }, "Range Low", r, 30.0f, hzAttr));
    }
    {
        juce::NormalisableRange<float> r (1000.0f, 20000.0f, 1.0f);
        r.setSkewForCentre (4472.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { ids::rangeHi, 1 }, "Range High", r, 18000.0f, hzAttr));
    }

    layout.add (percent (ids::heat, "Heat", 30.0f));

    layout.add (std::make_unique<Float> (juce::ParameterID { ids::color, 1 }, "Color",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
        Attr().withLabel ("% odd")
              .withStringFromValueFunction ([] (float v, int)
              {
                  if (v < 0.5f)  return juce::String ("Even");
                  if (v > 99.5f) return juce::String ("Odd");
                  if (std::abs (v - 50.0f) < 0.5f) return juce::String ("Even + Odd");
                  return juce::String (juce::roundToInt (v)) + " % odd";
              })
              .withValueFromStringFunction ([] (const juce::String& t)
              {
                  const auto l = t.toLowerCase();
                  if (l.containsAnyOf ("0123456789")) return leadingNumber (l);
                  if (l.contains ("even") && l.contains ("odd")) return 50.0f;
                  return l.contains ("odd") ? 100.0f : 0.0f;
              })));

    layout.add (std::make_unique<Float> (juce::ParameterID { ids::focus, 1 }, "Focus",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int)
              {
                  const int a = juce::roundToInt (std::abs (v));
                  if (a == 0) return juce::String ("Broad");
                  return (v < 0 ? "Body " : "Air ") + juce::String (a);
              })
              .withValueFromStringFunction ([] (const juce::String& t)
              {
                  const auto l = t.toLowerCase();
                  const float n = std::abs (leadingNumber (l));
                  if (l.contains ("body")) return -n;
                  if (l.contains ("air"))  return n;
                  return l.contains ("broad") ? 0.0f : leadingNumber (l);
              })));

    layout.add (percent (ids::mix, "Mix", 100.0f));

    layout.add (std::make_unique<Float> (juce::ParameterID { ids::output, 1 }, "Output",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB")
              .withStringFromValueFunction ([] (float v, int)
              {
                  if (std::abs (v) < 0.05f) return juce::String ("0.0 dB");
                  return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
              })
              .withValueFromStringFunction ([] (const juce::String& t) { return leadingNumber (t); })));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::match, 1 }, "Match", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::delta, 1 }, "Delta", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    // Latency mode. New instances start at Low; projects saved before 0.2.0 load as Precise (the
    // only engine they had), see AnnealAudioProcessor::setStateInformation. Not automatable:
    // a latency change makes the host re-align every track.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::latency, 1 }, "Latency",
        juce::StringArray { "Low", "Normal", "Precise" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Transient guard. 0 % = off, which is how every earlier version behaved (and what projects
    // saved before 0.2.2 load with, see AnnealAudioProcessor::setStateInformation).
    layout.add (std::make_unique<Float> (juce::ParameterID { ids::guard, 1 }, "Guard",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        Attr().withLabel ("%")
              .withStringFromValueFunction ([] (float v, int)
              {
                  return v < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " %";
              })
              .withValueFromStringFunction ([] (const juce::String& t)
              {
                  const auto l = t.toLowerCase();
                  if (! l.containsAnyOf ("0123456789"))
                      return l.contains ("off") ? 0.0f : 100.0f;
                  return leadingNumber (l);
              })));

    // Appended: existing host indices and parameter IDs stay unchanged.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::stereoMode, 1 }, "Stereo Mode",
        juce::StringArray { "Stereo (linked)", "Mid/Side" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::eqTarget, 1 }, "EQ Target",
        juce::StringArray { "Both", "Mid", "Side" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::heatTarget, 1 }, "Heat Target",
        juce::StringArray { "Both", "Mid", "Side" }, 0));

    // Listen (solo one part of what Anneal does). Off is how every earlier version sounded, and
    // what projects saved before 0.2.5 load with (AnnealAudioProcessor::setStateInformation).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::listen, 1 }, "Listen",
        juce::StringArray { "Off", "Reveal only", "Smooth only", "Heat only" }, 0,
        juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([] (const juce::String& t)
        {
            return parseListen (t);
        })));

    // Saturation flavour: which harmonics Heat writes and where. Clean is Anneal's own law, the
    // only one before 0.2.6, and what older projects load with (setStateInformation).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::flavour, 1 }, "Flavour",
        juce::StringArray { "Clean", "Tape", "Tube", "Transformer", "Exciter" }, 0,
        juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([] (const juce::String& t)
        {
            return parseFlavour (t);
        })));

    // 3.0. All default to what earlier versions did, and older projects load with exactly that
    // (AnnealAudioProcessor::setStateInformation).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::scMode, 1 }, "Sidechain",
        juce::StringArray { "Off", "Make room", "Unmask" }, scOff));
    layout.add (percent (ids::scAmount, "Sidechain Amount", 50.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::ceiling, 1 }, "Ceiling",
        juce::StringArray { "Off", "0.0 dBTP", "-0.3 dBTP", "-1.0 dBTP" }, 0));

    // 4.0 Reference match. Off (the default, and what older projects load with) leaves the auto EQ
    // exactly as before; Learn key listens to the sidechain; Match steers toward the reference.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::refMode, 1 }, "Reference",
        juce::StringArray { "Off", "Learn key", "Match" }, 0,
        juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([] (const juce::String& t)
        {
            return parseReference (t);
        })));
    layout.add (percent (ids::refAmount, "Reference Amount", 50.0f));
    return layout;
}
}
