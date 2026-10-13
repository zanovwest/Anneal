// Loads a built Anneal.vst3 through JUCE's VST3 *host* and exercises it the way a DAW would:
// scan, instantiate, parameters, latency, processing at several rates/block sizes, state
// save/restore across instances, editor attach/detach, several simultaneous instances.
// This tests the shipped binary, not the source. Usage: AnnealHostCheck path\to\Anneal.vst3
// Exit code = number of failed checks.

#include "TestSignals.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <random>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* name, const juce::String& detail)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("[%s] %-44s %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
    std::fflush (stdout);
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64).equalsIgnoreCase (name))
            return prm;
    return nullptr;
}

// Runs `in` through the plugin in blocks from `nextBlock`; returns false on non-finite output.
bool run (juce::AudioPluginInstance& p, const testsig::Channels& in, testsig::Channels& out, const std::function<int()>& nextBlock)
{
    out = in;
    juce::AudioBuffer<float> buf (2, 8192);
    juce::MidiBuffer midi;
    const int n = (int) in[0].size();
    for (int pos = 0; pos < n;)
    {
        const int b = std::min ({ nextBlock(), n - pos, 8192 });
        buf.setSize (2, b, false, false, true);
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in[(size_t) c].data() + pos, b);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + b, out[(size_t) c].data() + pos);
        pos += b;
    }
    for (auto& ch : out)
        for (float v : ch)
            if (! std::isfinite (v))
                return false;
    return true;
}

double peakOf (const testsig::Channels& x)
{
    double m = 0.0;
    for (auto& ch : x)
        for (float v : ch)
            m = std::max (m, (double) std::abs (v));
    return m;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2)
    {
        std::printf ("usage: AnnealHostCheck <path to Anneal.vst3>\n");
        return 1;
    }

    const juce::File bundle = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]).unquoted());
    juce::VST3PluginFormat format;

    // 1. Scan.
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, bundle.getFullPathName());
    check (types.size() == 1, "scan finds exactly one plugin", bundle.getFullPathName());
    if (types.isEmpty())
        return failures;
    const auto& desc = *types[0];
    check (desc.name == "Anneal" && desc.manufacturerName == "Qulic" && ! desc.isInstrument,
           "description", desc.name + " / " + desc.manufacturerName + " / " + desc.category + " / v" + desc.version
               + " / in " + juce::String (desc.numInputChannels) + " out " + juce::String (desc.numOutputChannels));

    // 2. Instantiate.
    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, 48000.0, 512, error);
    check (plugin != nullptr, "instantiate", error.isEmpty() ? juce::String ("ok") : error);
    if (plugin == nullptr)
        return failures;

    // 3. Parameters.
    juce::StringArray names;
    for (auto* prm : plugin->getParameters())
        names.add (prm->getName (64));
    bool haveAll = true;
    for (auto n : { "Reveal", "Smooth", "Tilt", "Response", "Range Low", "Range High", "Heat", "Color", "Focus", "Mix", "Output", "Match", "Delta" })
        haveAll = haveAll && names.contains (n);
    check (haveAll && plugin->getBypassParameter() != nullptr, "all parameters exposed, bypass flagged",
           juce::String (names.size()) + ": " + names.joinIntoString (", "));

    // 4. Latency + exact dry path at Mix 0 through the VST3 parameter path.
    plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);
    const int latency = plugin->getLatencySamples();
    auto* mix = findParam (*plugin, "Mix");
    mix->setValueNotifyingHost (0.0f);
    auto impulse = testsig::silence (2, 16384);
    impulse[0][1000] = impulse[1][1000] = 1.0f;
    testsig::Channels out;
    run (*plugin, testsig::silence (2, 2048), out, [] { return 512; });   // let the parameter land
    plugin->reset();
    run (*plugin, impulse, out, [] { return 512; });
    int peakAt = 0;
    for (int i = 0; i < (int) out[0].size(); ++i)
        if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) peakAt]))
            peakAt = i;
    check (latency == 1024 && peakAt - 1000 == latency, "latency reported and applied (Mix 0 %, Low)",
           "reported " + juce::String (latency) + ", impulse delayed by " + juce::String (peakAt - 1000));
    mix->setValueNotifyingHost (1.0f);

    // 5. Processing at several rates and block sizes, default settings.
    {
        bool ok = true;
        juce::String log;
        std::mt19937 rng (4);
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            plugin->releaseResources();
            plugin->setPlayConfigDetails (2, 2, fs, 1024);
            plugin->prepareToPlay (fs, 1024);
            const auto music = testsig::musicLike (fs, 3.0);
            for (int blk : { 32, 256, 1024 })
                ok = ok && run (*plugin, music, out, [blk] { return blk; });
            ok = ok && run (*plugin, music, out, [&rng] { return std::uniform_int_distribution<int> (1, 1024) (rng); });
            log << juce::String (fs / 1000.0, 1) << "k latency " << plugin->getLatencySamples() << ", peak "
                << juce::String (testsig::toDb (peakOf (out)), 1) << " dB;  ";
        }
        check (ok, "process 44.1/48/96 kHz, blocks 32-1024 + random", log);
    }

    // 6. State: change settings, save, load into a fresh instance.
    {
        findParam (*plugin, "Heat")->setValueNotifyingHost (0.9f);
        findParam (*plugin, "Reveal")->setValueNotifyingHost (0.15f);
        findParam (*plugin, "Delta")->setValueNotifyingHost (1.0f);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);

        auto second = format.createInstanceFromDescription (desc, 48000.0, 512, error);
        second->setStateInformation (state.getData(), (int) state.getSize());
        float worst = 0.0f;
        for (auto n : { "Heat", "Reveal", "Delta" })
            worst = std::max (worst, std::abs (findParam (*second, n)->getValue() - findParam (*plugin, n)->getValue()));
        check (worst < 1.0e-4f, "state restores in a second instance", "state " + juce::String ((int) state.getSize()) + " bytes, max diff " + juce::String (worst, 6));
        findParam (*plugin, "Delta")->setValueNotifyingHost (0.0f);
    }

    // 6a. Latency mode through the host: switch to Precise, the host sees the new latency and the
    //     dry path is still aligned; back to Low.
    {
        auto* lat = findParam (*plugin, "Latency");
        plugin->releaseResources();
        plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
        plugin->prepareToPlay (48000.0, 512);
        auto* mixParam = findParam (*plugin, "Mix");
        mixParam->setValueNotifyingHost (0.0f);
        juce::String log;
        bool ok = lat != nullptr;
        for (float v : { 1.0f, 0.5f, 0.0f })   // Precise, Normal, Low (normalised choice values)
        {
            if (! ok) break;
            lat->setValueNotifyingHost (v);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            auto imp = testsig::silence (2, 16384);
            imp[0][1000] = imp[1][1000] = 1.0f;
            testsig::Channels o;
            run (*plugin, testsig::silence (2, 2048), o, [] { return 512; });
            plugin->reset();
            run (*plugin, imp, o, [] { return 512; });
            int pk = 0;
            for (int i = 0; i < (int) o[0].size(); ++i)
                if (std::abs (o[0][(size_t) i]) > std::abs (o[0][(size_t) pk]))
                    pk = i;
            const int expected = v > 0.75f ? 4096 : v > 0.25f ? 2048 : 1024;
            ok = ok && plugin->getLatencySamples() == expected && pk - 1000 == expected;
            log << plugin->getLatencySamples() << " (impulse +" << (pk - 1000) << ")  ";
        }
        mixParam->setValueNotifyingHost (1.0f);
        check (ok, "latency modes switch through the host", log);
    }

    // 6b. Factory presets as host programs: listed, selectable, and a saved project keeps both its
    //     preset number and its edited knobs (so a host restoring the program re-applies nothing).
    {
        juce::StringArray programs;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            programs.add (plugin->getProgramName (i));
        plugin->setCurrentProgram (4);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const float reveal = findParam (*plugin, "Reveal")->getValue();
        check (programs.size() == 8 && programs[4] == "Vocal air" && plugin->getCurrentProgram() == 4
                   && std::abs (reveal - 0.5f) < 1.0e-3f,
               "factory presets listed and selectable", juce::String (programs.size()) + ": " + programs.joinIntoString (", ")
                   + "; program 4 sets Reveal " + juce::String (reveal * 100.0f, 1) + " %");

        findParam (*plugin, "Reveal")->setValueNotifyingHost (0.2f);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto third = format.createInstanceFromDescription (desc, 48000.0, 512, error);
        third->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const float kept = findParam (*third, "Reveal")->getValue();
        check (third->getCurrentProgram() == 4 && std::abs (kept - 0.2f) < 1.0e-4f,
               "edited preset survives save/load", "program " + juce::String (third->getCurrentProgram())
                   + ", Reveal " + juce::String (kept * 100.0f, 1) + " % (saved 20 %)");
        plugin->setCurrentProgram (0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    // 7. Editor: attach the plugin's real view in an off-screen, borderless window (no taskbar
    //    entry), let its timers run, process audio meanwhile, detach; three times.
    {
        bool ok = plugin->hasEditor();
        juce::String sizes;
        plugin->prepareToPlay (48000.0, 512);
        const auto music = testsig::musicLike (48000.0, 1.0);
        for (int round = 0; round < 3 && ok; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
            ok = editor != nullptr;
            if (! ok)
                break;
            juce::Component holder;
            holder.setBounds (-6000, -6000, editor->getWidth(), editor->getHeight());
            holder.addAndMakeVisible (*editor);
            holder.addToDesktop (0);
            holder.setVisible (true);
            run (*plugin, music, out, [] { return 512; });
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            sizes << editor->getWidth() << "x" << editor->getHeight() << " ";
            holder.removeFromDesktop();
            holder.removeChildComponent (editor.get());
            editor.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        check (ok, "editor attach/run/detach x3 while processing", "editor sizes " + sizes);
    }

    // 8. Several instances at once.
    {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> many;
        for (int i = 0; i < 4; ++i)
            many.push_back (format.createInstanceFromDescription (desc, 48000.0, 256, error));
        bool ok = true;
        const auto music = testsig::musicLike (48000.0, 1.0, 11);
        for (auto& p : many)
        {
            ok = ok && p != nullptr;
            if (! ok)
                break;
            p->setPlayConfigDetails (2, 2, 48000.0, 256);
            p->prepareToPlay (48000.0, 256);
        }
        for (int i = 0; ok && i < 4; ++i)
            ok = run (*many[(size_t) i], music, out, [] { return 256; });
        check (ok, "4 simultaneous instances process", ok ? juce::String ("ok") : error);
        for (auto& p : many)
            if (p != nullptr)
                p->releaseResources();
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures;
}
