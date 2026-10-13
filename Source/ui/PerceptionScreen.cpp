#include "PerceptionScreen.h"
#include "Params.h"
#include "Theme.h"
#include "dsp/ErbScale.h"

namespace anneal
{
using namespace theme;

namespace
{
    const double zMin = erb::hzToCam (20.0);
    const double zMax = erb::hzToCam (20000.0);

    constexpr float specTopDb = -12.0f, specBottomDb = -96.0f;
    constexpr float eqRangeDb = 12.0f;
    constexpr float heatRelFloorDb = -48.0f;    // heat lane: harmonics relative to the output in that band
    constexpr float heatAudibleDb = -110.0f;    // ignore harmonic energy below this absolute level
    constexpr float floorDb = -140.0f;

    constexpr int refreshHz = 30;
    constexpr float levelFallPerTick = 30.0f / refreshHz;   // 30 dB/s
    constexpr float heatFallPerTick  = 45.0f / refreshHz;   // 45 dB/s

    constexpr float gridHz[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };

    const juce::Colour laneFill   { 0xff141311 };
    const juce::Colour outFill    { 0xff3b3731 };
    const juce::Colour outStroke  { 0xff8c8477 };

    juce::String axisLabel (float hz)
    {
        return hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
    }

    juce::String signedDb (float v, int decimals)
    {
        const auto s = juce::String (std::abs (v), decimals);
        if (std::abs (v) < 0.5f * std::pow (10.0f, (float) -decimals)) return juce::String (0.0f, decimals);
        return (v > 0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + s;   // U+2212 minus
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, false).getWidth();
    }

    void laneTitle (juce::Graphics& g, juce::Rectangle<float> lane, const juce::String& title, const juce::String& sub)
    {
        const auto f = caps (11.5f);
        const float x = lane.getX() + 8.0f, y = lane.getY() + 5.0f;

        // Backing plate in the lane colour so traces and bars never run through the text.
        const float w = textWidth (f, title) + 8.0f + textWidth (font (size::caption), sub);
        g.setColour (laneFill.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 4.0f, y - 1.0f, w + 8.0f, 16.0f), 2.0f);

        g.setFont (f);
        g.setColour (colour::muted);
        g.drawText (title, juce::Rectangle<float> (x, y, 200.0f, 14.0f), juce::Justification::centredLeft, false);
        g.setFont (font (size::caption));
        g.setColour (colour::faint);
        g.drawText (sub, juce::Rectangle<float> (x + textWidth (f, title) + 8.0f, y, 260.0f, 14.0f), juce::Justification::centredLeft, false);
    }
}

PerceptionScreen::PerceptionScreen (TripleBuffer<DisplayFrame>& src, juce::AudioProcessorValueTreeState& state)
    : source (src)
{
    inDb.fill (floorDb);
    outDb.fill (floorDb);
    heatDb.fill (floorDb);
    gainDb.fill (0.0f);

    loParam = state.getParameter (ids::rangeLo);
    hiParam = state.getParameter (ids::rangeHi);
    loAttachment = std::make_unique<juce::ParameterAttachment> (*loParam, [this] (float v) { rangeLo = v; repaint(); }, state.undoManager);
    hiAttachment = std::make_unique<juce::ParameterAttachment> (*hiParam, [this] (float v) { rangeHi = v; repaint(); }, state.undoManager);
    loAttachment->sendInitialUpdate();
    hiAttachment->sendInitialUpdate();

    setTooltip ("Drag a range handle to limit where Anneal acts; double-click a handle to reset it. "
                "Horizontal axis: ERB-rate scale (equal steps of the ear's frequency resolution).");
    startTimerHz (refreshHz);
}

PerceptionScreen::~PerceptionScreen()
{
    stopTimer();
}

void PerceptionScreen::resized()
{
    const auto r = getLocalBounds().toFloat();
    plot = r.withTrimmedTop (28.0f).withTrimmedBottom (22.0f).withTrimmedLeft (44.0f).withTrimmedRight (44.0f);

    const float gap = 8.0f, h = plot.getHeight() - 2.0f * gap;
    const float specH = std::round (h * 0.56f), eqH = std::round (h * 0.29f);
    specLane = plot.withHeight (specH);
    eqLane   = { plot.getX(), specLane.getBottom() + gap, plot.getWidth(), eqH };
    heatLane = { plot.getX(), eqLane.getBottom() + gap, plot.getWidth(), plot.getBottom() - (eqLane.getBottom() + gap) };
}

float PerceptionScreen::xForCam (double cam) const noexcept
{
    return plot.getX() + (float) ((cam - zMin) / (zMax - zMin)) * plot.getWidth();
}

double PerceptionScreen::camForX (float x) const noexcept
{
    return zMin + (double) ((x - plot.getX()) / plot.getWidth()) * (zMax - zMin);
}

float PerceptionScreen::valueAtCam (const float* bands, double cam) const noexcept
{
    if (! haveFrame || dz <= 0.0f)
        return floorDb;
    const double u = juce::jlimit (0.0, (double) (nb - 1), (cam - z0) / dz);
    const int i = juce::jmin ((int) u, nb - 2);
    const float f = (float) (u - i);
    return bands[i] + f * (bands[i + 1] - bands[i]);
}

void PerceptionScreen::timerCallback()
{
    bool changed = false;

    if (const auto* f = source.read())
    {
        z0 = f->z0;
        dz = f->dz;
        for (int b = 0; b < nb; ++b)
        {
            inDb[(size_t) b]   = juce::jmax (f->inDb[b], inDb[(size_t) b] - levelFallPerTick);
            outDb[(size_t) b]  = juce::jmax (f->outDb[b], outDb[(size_t) b] - levelFallPerTick);
            heatDb[(size_t) b] = juce::jmax (f->heatDb[b], heatDb[(size_t) b] - heatFallPerTick);
            gainDb[(size_t) b] = f->gainDb[b];
        }
        haveFrame = true;
        idleTicks = 0;
        changed = true;
    }
    else if (haveFrame && idleTicks < refreshHz * 4)
    {
        // The host stopped processing: let the traces settle, then stop repainting.
        ++idleTicks;
        for (int b = 0; b < nb; ++b)
        {
            inDb[(size_t) b]   = juce::jmax (floorDb, inDb[(size_t) b] - levelFallPerTick);
            outDb[(size_t) b]  = juce::jmax (floorDb, outDb[(size_t) b] - levelFallPerTick);
            heatDb[(size_t) b] = juce::jmax (floorDb, heatDb[(size_t) b] - heatFallPerTick);
            gainDb[(size_t) b] *= 0.85f;
        }
        changed = true;
    }

    if (changed)
        repaint();
}

void PerceptionScreen::refreshNow()
{
    if (const auto* f = source.read())
    {
        z0 = f->z0;
        dz = f->dz;
        std::copy (f->inDb, f->inDb + nb, inDb.begin());
        std::copy (f->outDb, f->outDb + nb, outDb.begin());
        std::copy (f->heatDb, f->heatDb + nb, heatDb.begin());
        std::copy (f->gainDb, f->gainDb + nb, gainDb.begin());
        haveFrame = true;
        repaint();
    }
}

void PerceptionScreen::setReadoutHz (float hz)
{
    hoverX = xForCam (erb::hzToCam (hz));
    repaint();
}

//==============================================================================
void PerceptionScreen::paint (juce::Graphics& g)
{
    g.setColour (colour::screen);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    for (const auto& lane : { specLane, eqLane, heatLane })
    {
        g.setColour (laneFill);
        g.fillRect (lane);
    }

    // Frequency grid through all lanes.
    g.setFont (font (size::caption));
    for (float hz : gridHz)
    {
        const float x = xForCam (erb::hzToCam (hz));
        g.setColour (colour::grid);
        for (const auto& lane : { specLane, eqLane, heatLane })
            g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
        g.setColour (colour::muted);
        g.drawText (axisLabel (hz), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    g.setColour (colour::muted);
    g.drawText ("20k", juce::Rectangle<float> (plot.getRight() - 30.0f, plot.getBottom() + 4.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);
    g.setColour (colour::faint);
    g.drawText ("Hz", juce::Rectangle<float> (4.0f, plot.getBottom() + 4.0f, 36.0f, 14.0f), juce::Justification::centredRight, false);

    paintSpectrum (g);
    paintEq (g);
    paintHeat (g);
    paintRange (g);
    paintLegend (g);
    paintReadout (g);
}

void PerceptionScreen::paintSpectrum (juce::Graphics& g)
{
    const auto lane = specLane;
    auto yFor = [&] (float db)
    {
        const float v = juce::jlimit (specBottomDb, specTopDb, db);
        return lane.getBottom() - (v - specBottomDb) / (specTopDb - specBottomDb) * lane.getHeight();
    };

    g.setFont (font (size::caption));
    for (float db = -24.0f; db >= -84.0f; db -= 24.0f)
    {
        const float y = yFor (db);
        g.setColour (colour::grid);
        g.drawHorizontalLine (juce::roundToInt (y), lane.getX(), lane.getRight());
        g.setColour (colour::muted);
        g.drawText (signedDb (db, 0), juce::Rectangle<float> (4.0f, y - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);
    }

    if (! haveFrame)
    {
        g.setColour (colour::faint);
        g.setFont (font (size::value));
        g.drawText ("Waiting for audio", lane, juce::Justification::centred, false);
        return;
    }

    juce::Path outFillPath, outLine, inLine;
    const float x0 = xForCam (z0), x1 = xForCam (z0 + (nb - 1) * dz);
    outFillPath.startNewSubPath (x0, lane.getBottom());
    for (int b = 0; b < nb; ++b)
    {
        const float x = xForCam (z0 + b * dz);
        const float yo = yFor (outDb[(size_t) b]), yi = yFor (inDb[(size_t) b]);
        outFillPath.lineTo (x, yo);
        if (b == 0) { outLine.startNewSubPath (x, yo); inLine.startNewSubPath (x, yi); }
        else        { outLine.lineTo (x, yo);          inLine.lineTo (x, yi); }
    }
    outFillPath.lineTo (x1, lane.getBottom());
    outFillPath.closeSubPath();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lane.toNearestInt());
    g.setColour (outFill);
    g.fillPath (outFillPath);
    g.setColour (outStroke);
    g.strokePath (outLine, juce::PathStrokeType (1.0f));
    g.setColour (colour::text.withAlpha (0.6f));
    g.strokePath (inLine, juce::PathStrokeType (1.1f));
}

void PerceptionScreen::paintEq (juce::Graphics& g)
{
    const auto lane = eqLane;
    const float yZero = lane.getCentreY();
    auto yFor = [&] (float db) { return yZero - juce::jlimit (-eqRangeDb, eqRangeDb, db) / eqRangeDb * (lane.getHeight() * 0.5f - 2.0f); };

    g.setColour (colour::grid);
    g.drawHorizontalLine (juce::roundToInt (yFor (6.0f)), lane.getX(), lane.getRight());
    g.drawHorizontalLine (juce::roundToInt (yFor (-6.0f)), lane.getX(), lane.getRight());
    g.setColour (colour::faint);
    g.drawHorizontalLine (juce::roundToInt (yZero), lane.getX(), lane.getRight());

    g.setFont (font (size::caption));
    g.setColour (colour::muted);
    const float rx = lane.getRight() + 4.0f;
    g.drawText ("+12", juce::Rectangle<float> (rx, lane.getY() - 2.0f, 36.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("0 dB", juce::Rectangle<float> (rx, yZero - 7.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText (signedDb (-12.0f, 0), juce::Rectangle<float> (rx, lane.getBottom() - 12.0f, 36.0f, 14.0f), juce::Justification::centredLeft, false);

    if (! haveFrame)
        return;

    juce::Path curve;
    for (int b = 0; b < nb; ++b)
    {
        const float x = xForCam (z0 + b * dz), y = yFor (gainDb[(size_t) b]);
        if (b == 0) curve.startNewSubPath (x, y);
        else        curve.lineTo (x, y);
    }
    juce::Path fill (curve);
    fill.lineTo (xForCam (z0 + (nb - 1) * dz), yZero);
    fill.lineTo (xForCam (z0), yZero);
    fill.closeSubPath();

    const auto above = lane.withBottom (yZero).toNearestInt();
    const auto below = lane.withTop (yZero).toNearestInt();
    const juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (above);
        g.setColour (colour::straw.withAlpha (0.22f));
        g.fillPath (fill);
        g.setColour (colour::straw);
        g.strokePath (curve, stroke);
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (below);
        g.setColour (colour::blue.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (colour::blue);
        g.strokePath (curve, stroke);
    }
}

void PerceptionScreen::paintHeat (juce::Graphics& g)
{
    const auto lane = heatLane;

    g.setFont (font (size::caption));
    g.setColour (colour::muted);
    const float rx = lane.getRight() + 4.0f;
    g.drawText ("0 dB", juce::Rectangle<float> (rx, lane.getY() - 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText (signedDb (heatRelFloorDb, 0), juce::Rectangle<float> (rx, lane.getBottom() - 12.0f, 36.0f, 14.0f), juce::Justification::centredLeft, false);

    if (! haveFrame)
        return;

    for (float x = lane.getX(); x < lane.getRight(); x += 2.0f)
    {
        const double cam = camForX (x + 1.0f);
        const float h = valueAtCam (heatDb.data(), cam);
        if (h < heatAudibleDb)
            continue;
        const float rel = h - valueAtCam (outDb.data(), cam);
        const float v = juce::jlimit (0.0f, 1.0f, (rel - heatRelFloorDb) / -heatRelFloorDb);
        if (v <= 0.01f)
            continue;
        const float bh = v * (lane.getHeight() - 2.0f);
        g.setColour (colour::bronze.withAlpha (0.3f + 0.7f * v));
        g.fillRect (x, lane.getBottom() - bh, 1.6f, bh);
    }
}

void PerceptionScreen::paintRange (juce::Graphics& g)
{
    const float xLo = xForCam (erb::hzToCam (rangeLo));
    const float xHi = xForCam (erb::hzToCam (rangeHi));

    g.setColour (colour::screen.withAlpha (0.62f));
    g.fillRect (plot.withRight (xLo));
    g.fillRect (plot.withLeft (xHi));

    const auto f = font (size::caption);
    const auto loText = formatHz (rangeLo), hiText = formatHz (rangeHi);
    const float loW = textWidth (f, loText), hiW = textWidth (f, hiText);
    const bool crowded = (xHi - xLo) < loW + hiW + 30.0f;

    auto drawHandle = [&] (float x, Handle which, const juce::String& text, float textW, bool labelRight)
    {
        const bool active = dragging == which || hovered == which;
        const auto c = active ? colour::text : colour::muted;
        const float dashes[] = { 4.0f, 3.0f };
        g.setColour (c.withAlpha (active ? 0.9f : 0.6f));
        g.drawDashedLine (juce::Line<float> (x, plot.getY(), x, plot.getBottom()), dashes, 2, 1.0f);

        // Grip tab in the header row.
        const auto grip = juce::Rectangle<float> (7.0f, 16.0f).withCentre ({ x, plot.getY() - 9.0f });
        g.setColour (colour::body);
        g.fillRoundedRectangle (grip, 2.0f);
        g.setColour (c);
        g.drawRoundedRectangle (grip, 2.0f, 1.0f);

        g.setFont (f);
        g.setColour (c);
        const float tx = labelRight ? x + 8.0f : x - 8.0f - textW;
        g.drawText (text, juce::Rectangle<float> (tx, plot.getY() - 17.0f, textW + 2.0f, 16.0f), juce::Justification::centredLeft, false);
    };

    drawHandle (xLo, Handle::lo, loText, loW, ! crowded);
    drawHandle (xHi, Handle::hi, hiText, hiW, false);
}

void PerceptionScreen::paintLegend (juce::Graphics& g)
{
    laneTitle (g, specLane, "SPECTRUM", "dB per ERB");
    laneTitle (g, eqLane, "AUTO EQ", "live gain");
    laneTitle (g, heatLane, "HEAT", "harmonics written, relative to output");

    // Keys, right-aligned inside each lane.
    g.setFont (font (size::caption));
    auto key = [&] (juce::Rectangle<float> lane, float offsetFromRight, const juce::String& text, std::function<void (juce::Rectangle<float>)> swatch)
    {
        const auto r = juce::Rectangle<float> (lane.getRight() - offsetFromRight, lane.getY() + 5.0f, 70.0f, 14.0f);
        swatch (r.withWidth (14.0f).reduced (0.0f, 3.0f));
        g.setColour (colour::muted);
        g.drawText (text, r.withTrimmedLeft (19.0f), juce::Justification::centredLeft, false);
    };

    key (specLane, 140.0f, "input", [&] (juce::Rectangle<float> s)
    {
        g.setColour (colour::text.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (s.getCentreY()), s.getX(), s.getRight());
    });
    key (specLane, 70.0f, "output", [&] (juce::Rectangle<float> s)
    {
        g.setColour (outFill.brighter (0.2f));
        g.fillRect (s);
    });
    key (eqLane, 140.0f, "lift", [&] (juce::Rectangle<float> s)
    {
        g.setColour (colour::straw);
        g.fillRect (s.withTrimmedBottom (s.getHeight() * 0.5f));
    });
    key (eqLane, 70.0f, "cut", [&] (juce::Rectangle<float> s)
    {
        g.setColour (colour::blue);
        g.fillRect (s.withTrimmedTop (s.getHeight() * 0.5f));
    });
}

void PerceptionScreen::paintReadout (juce::Graphics& g)
{
    if (! haveFrame || hoverX < plot.getX() || hoverX > plot.getRight())
        return;

    const double cam = camForX (hoverX);
    const float hz = (float) erb::camToHz (cam);

    g.setColour (colour::text.withAlpha (0.35f));
    g.drawVerticalLine (juce::roundToInt (hoverX), plot.getY(), plot.getBottom());

    const float heatAbs = valueAtCam (heatDb.data(), cam);
    const auto text = formatHz (hz)
                    + "    in " + signedDb (valueAtCam (inDb.data(), cam), 1) + " dB"
                    + "    EQ " + signedDb (valueAtCam (gainDb.data(), cam), 1) + " dB"
                    + "    heat " + (heatAbs < heatAudibleDb ? juce::String ("none")
                                                             : signedDb (heatAbs - valueAtCam (outDb.data(), cam), 0) + " dB rel");
    const auto f = font (size::caption + 0.5f);
    const float w = tabularWidths (f, text) + 16.0f, h = 20.0f;
    const bool left = hoverX > plot.getCentreX();
    auto box = juce::Rectangle<float> (left ? hoverX - 10.0f - w : hoverX + 10.0f, specLane.getY() + 24.0f, w, h);
    box = box.constrainedWithin (plot);

    g.setColour (colour::body.withAlpha (0.94f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (colour::divider);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    g.setColour (colour::text);
    g.setFont (f);
    drawTabular (g, text, box.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
}

//==============================================================================
PerceptionScreen::Handle PerceptionScreen::handleAt (float x, float y) const noexcept
{
    if (y < plot.getY() - 26.0f || y > plot.getBottom())
        return Handle::none;
    const float dl = std::abs (x - xForCam (erb::hzToCam (rangeLo)));
    const float dh = std::abs (x - xForCam (erb::hzToCam (rangeHi)));
    if (juce::jmin (dl, dh) > 6.0f)
        return Handle::none;
    return dl <= dh ? Handle::lo : Handle::hi;
}

void PerceptionScreen::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    hovered = handleAt (p.x, p.y);
    hoverX = plot.contains (p) ? p.x : -1.0f;
    setMouseCursor (hovered != Handle::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void PerceptionScreen::mouseExit (const juce::MouseEvent&)
{
    hovered = Handle::none;
    hoverX = -1.0f;
    repaint();
}

void PerceptionScreen::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position.x, e.position.y);
    if (dragging == Handle::lo) loAttachment->beginGesture();
    if (dragging == Handle::hi) hiAttachment->beginGesture();
}

void PerceptionScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;

    const float x = juce::jlimit (plot.getX(), plot.getRight(), e.position.x);
    float hz = (float) erb::camToHz (camForX (x));
    if (dragging == Handle::lo)
        loAttachment->setValueAsPartOfGesture (juce::jlimit (20.0f, juce::jmin (2000.0f, rangeHi / 1.5f), hz));
    else
        hiAttachment->setValueAsPartOfGesture (juce::jlimit (juce::jmax (1000.0f, rangeLo * 1.5f), 20000.0f, hz));

    hoverX = x;
    repaint();
}

void PerceptionScreen::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::lo) loAttachment->endGesture();
    if (dragging == Handle::hi) hiAttachment->endGesture();
    dragging = Handle::none;
    repaint();
}

void PerceptionScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    switch (handleAt (e.position.x, e.position.y))
    {
        case Handle::lo: loAttachment->setValueAsCompleteGesture (loParam->convertFrom0to1 (loParam->getDefaultValue())); break;
        case Handle::hi: hiAttachment->setValueAsCompleteGesture (hiParam->convertFrom0to1 (hiParam->getDefaultValue())); break;
        case Handle::none: break;
    }
}
}
