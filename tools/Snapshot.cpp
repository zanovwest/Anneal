// Renders the production editor, fed with real processed audio, to PNG files.
// Usage: AnnealSnapshot [outputDir]

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Params.h"
#include "TestSignals.h"
#include "ui/Icon.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

namespace
{
namespace ids = anneal::ids;

void set (AnnealAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

void feed (AnnealAudioProcessor& p, const testsig::Channels& in, int block, int from, int to)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = from; pos + block <= to; pos += block)
    {
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in[(size_t) c].data() + pos, block);
        p.processBlock (buf, midi);
    }
}

bool writePng (const juce::Image& img, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (img, out);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok;
}

bool shoot (juce::Component& c, const juce::File& file, float scale = 1.0f)
{
    return writePng (c.createComponentSnapshot (c.getLocalBounds(), true, scale), file);
}

void reportFonts()
{
    for (auto [family, style] : { std::pair<const char*, const char*> { "Bahnschrift", "Light" }, { "Segoe UI", "Regular" },
                                  { "Segoe UI", "Semibold" }, { "Segoe UI", "Light" } })
    {
        juce::Font f (juce::FontOptions (family, style, 20.0f));
        auto tf = f.getTypefacePtr();
        juce::String widths;
        for (auto d : { "0", "1", "7", "8" })
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (f, d, 0.0f, 0.0f);
            widths << juce::String (ga.getBoundingBox (0, -1, false).getWidth() > 0 ? ga.getGlyph (0).getRight() : 0.0f, 2) << " ";
        }
        std::printf ("font request %s %-8s -> %s / %s, digit advance: %s\n", family, style,
                     tf != nullptr ? tf->getName().toRawUTF8() : "(null)",
                     tf != nullptr ? tf->getStyle().toRawUTF8() : "", widths.toRawUTF8());
    }
}

struct Scenario
{
    const char* name;
    std::function<void (AnnealAudioProcessor&)> configure;
    float readoutHz;
    bool feedAudio;
};
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // AnnealSnapshot --icons <dir>: render the app icon at every Windows icon size (PNG, alpha).
    if (argc > 2 && juce::String (argv[1]) == "--icons")
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        dir.createDirectory();
        int fails = 0;
        for (int size : { 16, 20, 24, 32, 40, 48, 64, 128, 256, 512 })
        {
            juce::Image img (juce::Image::ARGB, size, size, true);
            {
                juce::Graphics g (img);
                anneal::drawAppIcon (g, juce::Rectangle<float> ((float) size, (float) size));
            }
            fails += writePng (img, dir.getChildFile ("icon_" + juce::String (size) + ".png")) ? 0 : 1;
        }
        return fails;
    }

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("snapshots");
    outDir.createDirectory();
    reportFonts();

    const double fs = 48000.0;
    const int block = 512;
    const auto music = testsig::musicLike (fs, 6.0);
    const int n = (int) music[0].size();

    const Scenario scenarios[] = {
        { "default", [] (AnnealAudioProcessor&) {}, 3150.0f, true },
        { "hot", [] (AnnealAudioProcessor& p)
          {
              set (p, ids::reveal, 75); set (p, ids::smooth, 65); set (p, ids::heat, 85); set (p, ids::color, 62);
              set (p, ids::focus, 55); set (p, ids::tilt, 0.4f); set (p, ids::rangeLo, 60); set (p, ids::rangeHi, 16000);
              set (p, ids::match, 1);
          }, 7200.0f, true },
        { "delta", [] (AnnealAudioProcessor& p)
          {
              set (p, ids::heat, 60); set (p, ids::delta, 1); set (p, ids::output, -4.5f);
          }, -1.0f, true },
        { "empty", [] (AnnealAudioProcessor&) {}, -1.0f, false },
        { "preset", [] (AnnealAudioProcessor& p) { p.setCurrentProgram (4); set (p, ids::heat, 45); }, 9000.0f, true },
        { "midside", [] (AnnealAudioProcessor& p)
          {
              set (p, ids::stereoMode, 1); set (p, ids::eqTarget, 2); set (p, ids::heatTarget, 1);
              set (p, ids::reveal, 75); set (p, ids::smooth, 65); set (p, ids::heat, 70);
          }, 3150.0f, true },
        { "listen", [] (AnnealAudioProcessor& p)
          {
              set (p, ids::reveal, 75); set (p, ids::listen, 1);
          }, -1.0f, true },
        { "listen_heat", [] (AnnealAudioProcessor& p)
          {
              set (p, ids::heat, 70); set (p, ids::listen, 3);
          }, -1.0f, true },
    };

    int failures = 0;
    for (const auto& s : scenarios)
    {
        AnnealAudioProcessor proc;
        s.configure (proc);
        proc.setRateAndBufferSizeDetails (fs, block);
        proc.prepareToPlay (fs, block);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        if (editor == nullptr)
            return 1;
        auto& content = editor->getContent();

        if (s.feedAudio)
        {
            feed (proc, music, block, 0, n - block * 8);
            // Let the meters see a few blocks the way the editor timer would.
            for (int i = 0; i < 8; ++i)
            {
                feed (proc, music, block, n - block * (8 - i), n - block * (7 - i));
                content.tick (1.0f / 30.0f);
            }
        }
        content.screen.refreshNow();
        content.tick (0.0f);   // live read-outs under the knobs, from the frame just taken
        if (s.readoutHz > 0.0f)
            content.screen.setReadoutHz (s.readoutHz);

        const juce::String base = juce::String ("anneal_") + s.name;
        failures += shoot (*editor, outDir.getChildFile (base + "_100.png")) ? 0 : 1;
        if (juce::String (s.name) == "default" || juce::String (s.name) == "midside")
        {
            failures += shoot (*editor, outDir.getChildFile (base + "_150.png"), 1.5f) ? 0 : 1;
            failures += shoot (*editor, outDir.getChildFile (base + "_200.png"), 2.0f) ? 0 : 1;
            editor->setSize (800, 496);
            failures += shoot (*editor, outDir.getChildFile (base + "_min_800x496.png")) ? 0 : 1;
            editor->setSize (1600, 992);
            failures += shoot (*editor, outDir.getChildFile (base + "_max_1600x992.png")) ? 0 : 1;
        }

        std::printf ("%s: latency %.1f ms, match gain %.2f dB, peaks in last frame %d\n", s.name, proc.getLatencyMs(),
                     proc.getMatchGainDb(), proc.getEngine().getLastPeakCount());
    }
    return failures;
}
