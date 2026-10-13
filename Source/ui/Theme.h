#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Visual tokens. Palette roles follow steel temper colours (see docs/DESIGN.md):
// straw = lift, bronze = heat, temper blue = cut.
namespace anneal::theme
{
    namespace colour
    {
        inline const juce::Colour panel   { 0xff1a1917 };
        inline const juce::Colour screen  { 0xff100f0e };
        inline const juce::Colour grid    { 0xff26231f };
        inline const juce::Colour divider { 0xff2f2c28 };
        inline const juce::Colour track   { 0xff34312c };
        inline const juce::Colour body    { 0xff24221f };
        inline const juce::Colour text    { 0xffece6da };
        inline const juce::Colour muted   { 0xff8f897e };
        inline const juce::Colour faint   { 0xff5d584f };
        inline const juce::Colour straw   { 0xffe3c072 };
        inline const juce::Colour bronze  { 0xffd9823f };
        inline const juce::Colour blue    { 0xff7898cc };
        inline const juce::Colour warning { 0xffe2553d };
    }

    namespace size
    {
        constexpr int unit = 8;
        constexpr float label = 13.0f;     // control titles
        constexpr float value = 14.0f;     // value read-outs
        constexpr float caption = 12.0f;   // explanatory captions / axis labels
        constexpr float wordmark = 26.0f;
    }

    // Bahnschrift ships with Windows 10/11; JUCE falls back to the default sans elsewhere.
    inline juce::Font font (float height, const juce::String& style = "Regular")
    {
        return juce::Font (juce::FontOptions ("Bahnschrift", style, height));
    }

    inline juce::Font caps (float height, const juce::String& style = "SemiBold")
    {
        return juce::Font (juce::FontOptions ("Bahnschrift", style, height).withKerningFactor (0.08f));
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
