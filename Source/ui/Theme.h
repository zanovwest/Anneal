#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Visual tokens (docs/DESIGN.md, v3). Cool near-black graphite so the temper accents read
// clearly: straw = lift, temper blue = cut, bronze = heat. Everything else stays neutral.
namespace anneal::theme
{
    namespace colour
    {
        inline const juce::Colour panel   { 0xff111215 };   // deck
        inline const juce::Colour screen  { 0xff0a0b0d };   // display background
        inline const juce::Colour grid    { 0xff1b1e23 };
        inline const juce::Colour divider { 0xff24272d };
        inline const juce::Colour track   { 0xff2a2e35 };   // knob / fader tracks
        inline const juce::Colour body    { 0xff191b1f };   // raised surfaces: buttons, menus, read-outs
        inline const juce::Colour text    { 0xffeceef1 };
        inline const juce::Colour muted   { 0xff8c939d };
        inline const juce::Colour faint   { 0xff575d67 };
        inline const juce::Colour straw   { 0xfff1c36b };
        inline const juce::Colour bronze  { 0xffff8b3d };
        inline const juce::Colour blue    { 0xff6fa7ff };
        inline const juce::Colour violet  { 0xffa57be0 };   // sidechain key (temper purple)
        inline const juce::Colour teal    { 0xff5fcdbb };   // reference / target (4.0: the grey-teal past light blue)
        inline const juce::Colour rose    { 0xffe8849a };   // 5.0 SHAPE: Punch and Width, attack marks on the display
        inline const juce::Colour warning { 0xffff5a4e };
    }

    namespace size
    {
        constexpr int unit = 8;
        constexpr float label = 12.0f;     // control titles (caps)
        constexpr float value = 14.0f;     // value read-outs
        constexpr float caption = 12.0f;   // explanatory captions / axis labels
        constexpr float wordmark = 22.0f;
    }

    // Segoe UI ships with every Windows; its figures are tabular, so changing values don't jitter.
    // The wordmark alone uses Bahnschrift Light. macOS uses Helvetica Neue (also tabular figures;
    // it has no Semibold, Medium stands in) with Thin for the wordmark; Linux the default sans.
   #if JUCE_WINDOWS
    inline juce::FontOptions uiFace (const juce::String& style, float height) { return juce::FontOptions ("Segoe UI", style, height); }
    inline juce::FontOptions wordmarkFace (float height)                      { return juce::FontOptions ("Bahnschrift", "Light", height); }
   #elif JUCE_MAC
    inline juce::FontOptions uiFace (const juce::String& style, float height)
    {
        return juce::FontOptions ("Helvetica Neue", style == "Semibold" ? juce::String ("Medium") : style, height);
    }
    inline juce::FontOptions wordmarkFace (float height) { return juce::FontOptions ("Helvetica Neue", "Thin", height); }
   #else
    inline juce::FontOptions uiFace (const juce::String& style, float height)
    {
        const auto s = style == "Semibold" ? juce::String ("Bold") : style == "Light" ? juce::String ("Regular") : style;
        return juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), s, height);
    }
    inline juce::FontOptions wordmarkFace (float height) { return uiFace ("Regular", height); }
   #endif

    inline juce::Font font (float height, const juce::String& style = "Regular")
    {
        return juce::Font (uiFace (style, height));
    }

    inline juce::Font caps (float height, const juce::String& style = "Semibold")
    {
        return juce::Font (uiFace (style, height).withKerningFactor (0.09f));
    }

    inline juce::Font wordmarkFont (float height)
    {
        return juce::Font (wordmarkFace (height).withKerningFactor (0.42f));
    }

    inline float advanceOf (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getNumGlyphs() > 0 ? ga.getGlyph (ga.getNumGlyphs() - 1).getRight() : 0.0f;
    }

    // Bahnschrift's figures are proportional and JUCE 8.0.4 cannot request OpenType 'tnum',
    // so changing values are drawn with every digit centred in a fixed-width cell.
    inline float tabularWidths (const juce::Font& f, const juce::String& text, juce::Array<float>* widths = nullptr)
    {
        float cell = 0.0f;
        for (auto d : { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" })
            cell = std::max (cell, advanceOf (f, d));

        float total = 0.0f;
        for (auto ch : text)
        {
            const float w = juce::CharacterFunctions::isDigit (ch) ? cell : advanceOf (f, juce::String::charToString (ch));
            if (widths != nullptr)
                widths->add (w);
            total += w;
        }
        return total;
    }

    inline void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                             juce::Justification justification)
    {
        juce::Array<float> widths;
        const float total = tabularWidths (g.getCurrentFont(), text, &widths);

        float x = area.getX();
        if (justification.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;
        else if (justification.testFlags (juce::Justification::right))          x = area.getRight() - total;

        int i = 0;
        for (auto ch : text)
        {
            const float w = widths[i++];
            g.drawText (juce::String::charToString (ch), juce::Rectangle<float> (x, area.getY(), w + 1.0f, area.getHeight()),
                        juce::Justification::centred, false);
            x += w;
        }
    }
}
