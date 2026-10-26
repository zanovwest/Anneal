#include "PerceptionScreen.h"
#include "Params.h"
#include "Theme.h"
#include "dsp/ErbScale.h"
#include "dsp/PerceptualModel.h"   // 5.3.2: isNoLimit

namespace anneal
{
using namespace theme;

namespace
{
    const double zMin = erb::hzToCam (20.0);
    const double zMax = erb::hzToCam (20000.0);

    constexpr float specTopDb = -12.0f, specBottomDb = -96.0f;
    constexpr float eqRangeDb = 12.0f;
    constexpr float eqSpan = 0.42f;             // +-12 dB uses 84 % of the main lane's height
    constexpr float heatRelFloorDb = -48.0f;    // heat lane: harmonics relative to the output in that band
    constexpr float heatAudibleDb = -110.0f;    // ignore harmonic energy below this absolute level
    constexpr float floorDb = -140.0f;

    constexpr int refreshHz = 30;
    constexpr float levelFallPerTick = 30.0f / refreshHz;   // 30 dB/s
    constexpr float heatFallPerTick  = 45.0f / refreshHz;   // 45 dB/s
    constexpr float activityFallPerTick = 12.0f / refreshHz; // 12 dB/s

    constexpr float gridHz[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };

    // The spectrum is context, so it sits back as a soft neutral wash; the EQ curve leads.
    const juce::Colour laneFill   { 0xff0a0b0d };
    const juce::Colour specTop    { 0x1ed8dde6 };
    const juce::Colour specBottom { 0x06d8dde6 };
    const juce::Colour outStroke  { 0x80d3d8e0 };
    const juce::Colour inStroke   { 0x38c9ced6 };

    juce::String axisLabel (float hz)
    {
        return hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k" : juce::String (juce::roundToInt (hz));
    }

    juce::String signedDb (float v, int decimals)
    {
        // juce::String (float, 0) means "as many decimals as needed", so whole dB are formatted
        // by rounding (the hover read-out once showed "heat -24.8106 dB").
        auto fmt = [decimals] (float x) { return decimals == 0 ? juce::String (juce::roundToInt (x)) : juce::String (x, decimals); };
        if (std::abs (v) < 0.5f * std::pow (10.0f, (float) -decimals)) return fmt (0.0f);
        return (v > 0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + fmt (std::abs (v));   // U+2212 minus
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, false).getWidth();
    }
}

PerceptionScreen::PerceptionScreen (TripleBuffer<DisplayFrame>& src, juce::AudioProcessorValueTreeState& state)
    : source (src)
{
    inDb.fill (floorDb);
    outDb.fill (floorDb);
    heatDb.fill (floorDb);
    gainDb.fill (0.0f);
    sideRel.fill (-200.0f);
    sideRelView.fill (-200.0f);

    loParam = state.getParameter (ids::rangeLo);
    hiParam = state.getParameter (ids::rangeHi);
    loAttachment = std::make_unique<juce::ParameterAttachment> (*loParam, [this] (float v) { rangeLo = v; repaint(); }, state.undoManager);
    hiAttachment = std::make_unique<juce::ParameterAttachment> (*hiParam, [this] (float v) { rangeHi = v; repaint(); }, state.undoManager);
    loAttachment->sendInitialUpdate();
    hiAttachment->sendInitialUpdate();
    listenAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ids::listen),
        [this] (float v) { listenMode = juce::jlimit (0, 3, juce::roundToInt (v)); repaint(); }, state.undoManager);
    listenAttachment->sendInitialUpdate();
    refModeAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ids::refMode),
        [this] (float v) { refMode = juce::jlimit (0, 2, juce::roundToInt (v)); repaint(); }, state.undoManager);
    refModeAttachment->sendInitialUpdate();
    widthAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ids::width),
        [this] (float v) { widthSetting = v / 100.0f; repaint(); }, state.undoManager);
    widthAttachment->sendInitialUpdate();
    monoAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (ids::monoBass),
        [this] (float v) { monoHz = v >= monoBassMinHz ? v : 0.0f; repaint(); }, state.undoManager);
    monoAttachment->sendInitialUpdate();
    if (auto* zp = state.getParameter (ids::zeroPunch))   // 5.2.7
    {
        zeroPunchAttachment = std::make_unique<juce::ParameterAttachment> (*zp,
            [this] (float v) { zeroPunchOn = v > 0.5f; repaint(); }, state.undoManager);
        zeroPunchAttachment->sendInitialUpdate();
    }
    for (auto [id, value, attachment] : { std::tuple { ids::maxCut, &maxCutDb, &maxCutAttachment },
                                          std::tuple { ids::maxBoost, &maxBoostDb, &maxBoostAttachment } })
        if (auto* lp = state.getParameter (id))   // 5.3.2
        {
            float* v = value;
            *attachment = std::make_unique<juce::ParameterAttachment> (*lp, [this, v] (float x) { *v = x; repaint(); }, state.undoManager);
            (*attachment)->sendInitialUpdate();
        }

    setPaintMode (false);   // sets the tooltip
    startTimerHz (refreshHz);
}

PerceptionScreen::~PerceptionScreen()
{
    stopTimer();
}

void PerceptionScreen::resized()
{
    const auto r = getLocalBounds().toFloat();
    plot = r.withTrimmedTop (34.0f).withTrimmedBottom (24.0f).withTrimmedLeft (18.0f).withTrimmedRight (46.0f);

    // 5.2.2: EQ, a slim WIDTH lane, then HEAT (was EQ and a 52 px HEAT lane, 10 px apart).
    const float heatH = 46.0f, widthH = 28.0f, gap = 8.0f;
    mainLane = plot.withTrimmedBottom (heatH + widthH + 2.0f * gap);
    widthLane = plot.withTop (mainLane.getBottom() + gap).withHeight (widthH);
    heatLane = plot.withTop (widthLane.getBottom() + gap);
}

float PerceptionScreen::xForCam (double cam) const noexcept
{
    return plot.getX() + (float) ((cam - zMin) / (zMax - zMin)) * plot.getWidth();
}

double PerceptionScreen::camForX (float x) const noexcept
{
    return zMin + (double) ((x - plot.getX()) / plot.getWidth()) * (zMax - zMin);
}

float PerceptionScreen::xForHz (float hz) const noexcept
{
    return xForCam (erb::hzToCam (hz));
}

float PerceptionScreen::specY (float db) const noexcept
{
    const float v = juce::jlimit (specBottomDb, specTopDb, db);
    return mainLane.getBottom() - (v - specBottomDb) / (specTopDb - specBottomDb) * mainLane.getHeight();
}

float PerceptionScreen::eqY (float db) const noexcept
{
    return mainLane.getCentreY() - juce::jlimit (-eqRangeDb, eqRangeDb, db) / eqRangeDb * (mainLane.getHeight() * eqSpan);
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

void PerceptionScreen::updateViews()
{
    // Three passes of a +-6-band moving average (close to a Gaussian, sigma ~6 bands).
    auto smooth = [] (const std::array<float, nb>& src, std::array<float, nb>& dst)
    {
        constexpr int half = 6, passes = 3;
        std::array<float, nb> tmp = src;
        for (int pass = 0; pass < passes; ++pass)
        {
            float acc = 0.0f;
            int count = 0;
            for (int i = 0; i <= half && i < nb; ++i) { acc += tmp[(size_t) i]; ++count; }
            for (int b = 0; b < nb; ++b)
            {
                dst[(size_t) b] = acc / (float) count;
                const int add = b + half + 1, drop = b - half;
                if (add < nb)   { acc += tmp[(size_t) add]; ++count; }
                if (drop >= 0)  { acc -= tmp[(size_t) drop]; --count; }
            }
            tmp = dst;
        }
    };
    smooth (inDb, inView);
    smooth (outDb, outView);
    smooth (heatDb, heatView);
    smooth (keyDb, keyView);
    smooth (sideRel, sideRelView);
}

void PerceptionScreen::timerCallback()
{
    bool changed = false;

    if (const auto* f = source.read())
    {
        z0 = f->z0;
        dz = f->dz;
        midSide = f->midSide;
        keyActive = f->keyActive;
        frozen = f->frozen;
        activity.matchDb = std::abs (f->matchDb) >= std::abs (activity.matchDb) ? f->matchDb
                         : activity.matchDb * std::pow (10.0f, -activityFallPerTick / 20.0f);
        activity.refSeconds = f->refSeconds;
        activity.refValid = f->refValid;
        activity.punchDb = std::abs (f->punchDb) >= std::abs (activity.punchDb) ? f->punchDb
                         : activity.punchDb * std::pow (10.0f, -activityFallPerTick / 20.0f);
        attacksOn = f->attacks;
        zeroLatency = f->zeroLatency;
        for (int b = 0; b < nb; ++b)
            attack[(size_t) b] = attacksOn ? juce::jmax (f->transient[b], attack[(size_t) b] * 0.7f) : 0.0f;
        stereoImage = f->stereo;   // 5.2.3: eased toward each new image, so it doesn't flicker
        for (int b = 0; b < nb; ++b)
        {
            const float was = sideRel[(size_t) b], now = stereoImage ? f->sideRelDb[b] : -200.0f;
            sideRel[(size_t) b] = was <= -199.0f || now <= -199.0f ? now : was + 0.35f * (now - was);   // signal comes and goes at once
        }
        for (int b = 0; b < nb; ++b)
        {
            keyDb[(size_t) b] = keyActive ? juce::jmax (f->keyDb[b], keyDb[(size_t) b] - levelFallPerTick) : floorDb;
            inDb[(size_t) b]   = juce::jmax (f->inDb[b], inDb[(size_t) b] - levelFallPerTick);
            outDb[(size_t) b]  = juce::jmax (f->outDb[b], outDb[(size_t) b] - levelFallPerTick);
            heatDb[(size_t) b] = juce::jmax (f->heatDb[b], heatDb[(size_t) b] - heatFallPerTick);
            gainDb[(size_t) b] = f->gainDb[b];
            sideGainDb[(size_t) b] = f->sideGainDb[b];
        }
        activity.liftDb = juce::jmax (f->liftDb, activity.liftDb - activityFallPerTick);
        activity.cutDb = juce::jmin (f->cutDb, activity.cutDb + activityFallPerTick);
        activity.heatRelDb = juce::jmax (f->heatRelDb, activity.heatRelDb - activityFallPerTick);
        activity.scDb = std::abs (f->scDb) >= std::abs (activity.scDb) ? f->scDb
                      : activity.scDb * std::pow (10.0f, -activityFallPerTick / 20.0f);
        activity.keyActive = f->keyActive;
        activity.live = true;
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
            sideGainDb[(size_t) b] *= 0.85f;
            attack[(size_t) b] *= 0.7f;
            sideRel[(size_t) b] = juce::jmax (-200.0f, sideRel[(size_t) b] - levelFallPerTick);
        }
        if (idleTicks > refreshHz / 2)
            activity = {};
        changed = true;
    }

    if (changed)
    {
        updateViews();
        updateReferenceOffset (false);
        repaint();
    }
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
        std::copy (f->sideGainDb, f->sideGainDb + nb, sideGainDb.begin());
        midSide = f->midSide;
        keyActive = f->keyActive;
        frozen = f->frozen;
        std::copy (f->keyDb, f->keyDb + nb, keyDb.begin());
        activity = { f->liftDb, f->cutDb, f->heatRelDb, f->scDb, true, f->keyActive, f->matchDb, f->refSeconds, f->refValid, f->punchDb };
        attacksOn = f->attacks;
        zeroLatency = f->zeroLatency;
        std::copy (f->transient, f->transient + nb, attack.begin());
        stereoImage = f->stereo;
        if (stereoImage)
            std::copy (f->sideRelDb, f->sideRelDb + nb, sideRel.begin());
        else
            sideRel.fill (-200.0f);
        haveFrame = true;
        updateViews();
        updateReferenceOffset (true);
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
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colour::screen);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colour::divider);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Frequency grid through both lanes, labels under the plot.
    g.setFont (font (11.0f));
    for (float hz : gridHz)
    {
        const float x = xForCam (erb::hzToCam (hz));
        g.setColour (colour::grid);
        for (const auto& lane : { mainLane, widthLane, heatLane })
            g.drawVerticalLine (juce::roundToInt (x), lane.getY(), lane.getBottom());
        g.setColour (colour::faint);
        g.drawText (axisLabel (hz), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 6.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }
    g.setColour (colour::faint);
    g.drawText ("20k Hz", juce::Rectangle<float> (plot.getRight() - 44.0f, plot.getBottom() + 6.0f, 44.0f, 13.0f), juce::Justification::centredRight, false);

    paintSpectrum (g);
    paintReference (g);
    paintEq (g);
    paintAttacks (g);
    paintHeat (g);
    paintWidth (g);
    paintWeights (g);
    paintRange (g);
    paintLegend (g);
    paintListen (g);
    paintReadout (g);
    paintDrop (g);
}

void PerceptionScreen::paintSpectrum (juce::Graphics& g)
{
    if (! haveFrame)
        return;

    const auto lane = mainLane;
    auto yFor = [this] (float db) { return specY (db); };

    juce::Path outFillPath, outLine, inLine;
    const float x0 = xForCam (z0), x1 = xForCam (z0 + (nb - 1) * dz);
    outFillPath.startNewSubPath (x0, lane.getBottom());
    for (int b = 0; b < nb; ++b)
    {
        const float x = xForCam (z0 + b * dz);
        const float yo = yFor (outView[(size_t) b]), yi = yFor (inView[(size_t) b]);
        outFillPath.lineTo (x, yo);
        if (b == 0) { outLine.startNewSubPath (x, yo); inLine.startNewSubPath (x, yi); }
        else        { outLine.lineTo (x, yo);          inLine.lineTo (x, yi); }
    }
    outFillPath.lineTo (x1, lane.getBottom());
    outFillPath.closeSubPath();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lane.toNearestInt());
    g.setGradientFill (juce::ColourGradient (specTop, 0.0f, lane.getY(), specBottom, 0.0f, lane.getBottom(), false));
    g.fillPath (outFillPath);
    g.setColour (inStroke);
    g.strokePath (inLine, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (outStroke);
    g.strokePath (outLine, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Sidechain key (3.0): its level per ERB, on the same scale as the spectrum.
    if (keyActive)
    {
        juce::Path keyLine;
        for (int b = 0; b < nb; ++b)
        {
            const float x = xForCam (z0 + b * dz), y = yFor (keyView[(size_t) b]);
            if (b == 0) keyLine.startNewSubPath (x, y);
            else        keyLine.lineTo (x, y);
        }
        g.setColour (colour::violet.withAlpha (0.85f));
        g.strokePath (keyLine, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void PerceptionScreen::paintEq (juce::Graphics& g)
{
    const auto lane = mainLane;
    const float yZero = eqY (0.0f);

    // EQ scale on the right; the zero line is the reference everything moves around.
    for (float db : { 6.0f, -6.0f, 12.0f, -12.0f })
    {
        g.setColour (colour::grid);
        const float dashes[] = { 2.0f, 5.0f };
        g.drawDashedLine (juce::Line<float> (lane.getX(), eqY (db), lane.getRight(), eqY (db)), dashes, 2, 1.0f);
    }
    g.setColour (colour::divider.brighter (0.25f));
    g.drawHorizontalLine (juce::roundToInt (yZero), lane.getX(), lane.getRight());

    g.setFont (font (11.0f));
    const float rx = lane.getRight() + 8.0f;
    for (float db : { 12.0f, 0.0f, -12.0f })   // 5.3: the ±6 lines speak for themselves
    {
        g.setColour (db == 0.0f ? colour::muted : colour::faint);
        g.drawText (db == 0.0f ? juce::String ("0 dB") : signedDb (db, 0),
                    juce::Rectangle<float> (rx, eqY (db) - 7.0f, 38.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    // 5.3.2 Max boost / Max cut: where the auto EQ stops, as dashed lines in the lift and cut colours
    // across the Range (where it acts); only while a limit is set and within the lane's +-12 dB.
    {
        const auto shown = getShownRange();
        const float xLo = juce::jmax (lane.getX(), xForHz (shown.x)), xHi = juce::jmin (lane.getRight(), xForHz (shown.y));
        const float dashes[] = { 6.0f, 4.0f };
        for (auto [db, c] : { std::pair { maxBoostDb, colour::straw }, std::pair { -maxCutDb, colour::blue } })
            if (! isNoLimit (std::abs (db)) && std::abs (db) <= eqRangeDb && xHi > xLo)
            {
                g.setColour (c.withAlpha (0.6f));
                g.drawDashedLine (juce::Line<float> (xLo, eqY (db), xHi, eqY (db)), dashes, 2, 1.2f);
            }
    }

    if (! haveFrame)
    {
        g.setColour (colour::faint);
        g.setFont (font (14.0f));
        g.drawText ("Play some audio to see what Anneal does", lane.withY (yZero - 40.0f).withHeight (24.0f),
                    juce::Justification::centred, false);
        return;
    }

    if (midSide)
    {
        // Mid is solid; Side is dashed. Both keep the lift/cut colour convention.
        for (int c = 0; c < 2; ++c)
        {
            const auto& gains = c == 0 ? gainDb : sideGainDb;
            juce::Path path;
            for (int b = 0; b < nb; ++b)
            {
                const float x = xForCam (z0 + b * dz), y = eqY (gains[(size_t) b]);
                if (b == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            const juce::PathStrokeType stroke (c == 0 ? 2.4f : 1.8f);
            juce::Path dashed;
            const float lengths[] = { 5.0f, 3.0f };
            if (c == 1) stroke.createDashedStroke (dashed, path, lengths, 2);
            for (int sign = 0; sign < 2; ++sign)
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion ((sign == 0 ? lane.withBottom (yZero) : lane.withTop (yZero)).toNearestInt());
                g.setColour (sign == 0 ? colour::straw : colour::blue);
                if (c == 0) g.strokePath (path, stroke); else g.fillPath (dashed);
            }
        }
        return;
    }

    juce::Path curve;
    for (int b = 0; b < nb; ++b)
    {
        const float x = xForCam (z0 + b * dz), y = eqY (gainDb[(size_t) b]);
        if (b == 0) curve.startNewSubPath (x, y);
        else        curve.lineTo (x, y);
    }
    juce::Path fill (curve);
    fill.lineTo (xForCam (z0 + (nb - 1) * dz), yZero);
    fill.lineTo (xForCam (z0), yZero);
    fill.closeSubPath();

    const auto above = lane.withBottom (yZero).toNearestInt();
    const auto below = lane.withTop (yZero).toNearestInt();
    const juce::PathStrokeType halo (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType stroke (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Fill strength grows with distance from 0 dB, so bigger moves read as stronger.
    auto side = [&] (juce::Rectangle<int> clip, juce::Colour c, float farDb)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.04f), 0.0f, yZero, c.withAlpha (0.42f), 0.0f, eqY (farDb), false));
        g.fillPath (fill);
        g.setColour (c.withAlpha (0.14f));
        g.strokePath (curve, halo);
        g.setColour (c);
        g.strokePath (curve, stroke);
    };
    side (above, colour::straw, eqRangeDb * 0.5f);
    side (below, colour::blue, -eqRangeDb * 0.5f);
}

void PerceptionScreen::paintAttacks (juce::Graphics& g)
{
    // 5.0: with Guard or Punch on, a short rose bar at the foot of the EQ lane in each band where an
    // attack is happening now (taller = stronger), so you can see what they work on.
    if (! attacksOn)
        return;
    const float base = mainLane.getBottom() - 1.0f;
    for (int b = 0; b < nb; ++b)
    {
        const float a = attack[(size_t) b];
        if (a < 0.05f)
            continue;
        const float x0 = xForCam (z0 + ((float) b - 0.5f) * dz), x1 = xForCam (z0 + ((float) b + 0.5f) * dz);
        const float h = 3.0f + 9.0f * a;
        g.setColour (colour::rose.withAlpha (0.25f + 0.55f * a));
        g.fillRect (juce::Rectangle<float> (x0, base - h, juce::jmax (1.0f, x1 - x0), h));
    }
}

void PerceptionScreen::paintHeat (juce::Graphics& g)
{
    const auto lane = heatLane;

    // 5.3: no scale numbers here: the lane is a picture of where Heat writes, not a meter.
    g.setColour (colour::divider);
    g.drawHorizontalLine (juce::roundToInt (lane.getBottom()), lane.getX(), lane.getRight());

    if (! haveFrame)
        return;

    // A smooth area (harmonics relative to the output in each band), not a bar code.
    juce::Path area, edge;
    bool open = false;
    for (float x = lane.getX(); x <= lane.getRight(); x += 2.0f)
    {
        const double cam = camForX (x);
        const float h = valueAtCam (heatView.data(), cam);
        float v = 0.0f;
        if (h >= heatAudibleDb)
            v = juce::jlimit (0.0f, 1.0f, (h - valueAtCam (outView.data(), cam) - heatRelFloorDb) / -heatRelFloorDb);
        const float y = lane.getBottom() - v * (lane.getHeight() - 2.0f);
        if (! open) { area.startNewSubPath (x, lane.getBottom()); edge.startNewSubPath (x, y); open = true; }
        area.lineTo (x, y);
        edge.lineTo (x, y);
    }
    area.lineTo (lane.getRight(), lane.getBottom());
    area.closeSubPath();

    g.setGradientFill (juce::ColourGradient (colour::bronze.withAlpha (0.55f), 0.0f, lane.getY(),
                                             colour::bronze.withAlpha (0.10f), 0.0f, lane.getBottom(), false));
    g.fillPath (area);
    g.setColour (colour::bronze);
    g.strokePath (edge, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

float PerceptionScreen::widthAtHz (float hz) const noexcept
{
    // As the engine applies it (SpectralEngine::applyWidth): Width by its painted share, then Mono
    // bass fading from fully mono a quarter octave below its frequency to the full width above.
    const float share = refgrid::sampleAt (paintCurves.width, PaintCurves::numPoints, erb::hzToCam (hz));
    float w = 1.0f + juce::jlimit (0.0f, 1.0f, share) * (widthSetting - 1.0f);
    if (monoHz >= monoBassMinHz && hz < monoHz * 1.18920712f)
    {
        const float lo = monoHz * 0.84089642f;
        const float t = hz <= lo ? 0.0f : juce::jlimit (0.0f, 1.0f, 2.0f * std::log2 (hz / lo));
        w *= t * t * (3.0f - 2.0f * t);
    }
    return w;
}

float PerceptionScreen::sideRelAtHz (float hz) const noexcept
{
    if (! stereoImage || ! haveFrame)
        return -200.0f;
    return valueAtCam (sideRelView.data(), erb::hzToCam (hz));
}

void PerceptionScreen::paintWidth (juce::Graphics& g)
{
    // 5.2.2: the stereo width heard at each frequency, 0 % (mono) at the bottom, 100 % (as is) in the
    // middle, 200 % at the top; the area between the middle and the curve is filled.
    const auto lane = widthLane;

    // 5.2.3: behind it, the stereo image heard as a neutral shade (measured context, like the
    // spectrum): none where the sound is mono, brighter the more of it is in the sides (full at
    // sides as loud as the centre).
    if (stereoImage && haveFrame)
        for (float x = lane.getX(); x < lane.getRight(); x += 2.0f)
        {
            const float rel = valueAtCam (sideRelView.data(), camForX (x + 1.0f));
            const float share = rel <= -100.0f ? 0.0f : 1.0f / (1.0f + std::pow (10.0f, -rel / 10.0f));   // side / (mid + side)
            if (share < 0.005f)
                continue;
            g.setColour (colour::text.withAlpha (0.02f + 0.11f * juce::jmin (1.0f, share / 0.5f)));   // as quiet as the spectrum's wash
            g.fillRect (juce::Rectangle<float> (x, lane.getY(), juce::jmin (2.0f, lane.getRight() - x), lane.getHeight()));
        }
    const float mid = lane.getCentreY();
    g.setFont (font (11.0f));
    g.setColour (colour::faint);
    g.drawText ("100 %", juce::Rectangle<float> (lane.getRight() + 8.0f, mid - 6.5f, 40.0f, 13.0f), juce::Justification::centredLeft, false);
    g.setColour (colour::divider);
    g.drawHorizontalLine (juce::roundToInt (lane.getBottom()), lane.getX(), lane.getRight());

    auto yOf = [&lane] (float w) { return lane.getBottom() - 1.0f - juce::jlimit (0.0f, 2.0f, w) * 0.5f * (lane.getHeight() - 2.0f); };
    juce::Path area, edge;
    bool flat = true;
    for (float x = lane.getX(); x <= lane.getRight(); x += 2.0f)
    {
        const float w = widthAtHz ((float) erb::camToHz (camForX (x)));
        flat = flat && std::abs (w - 1.0f) < 0.005f;
        const float y = yOf (w);
        if (x == lane.getX()) { area.startNewSubPath (x, mid); edge.startNewSubPath (x, y); }
        area.lineTo (x, y);
        edge.lineTo (x, y);
    }
    area.lineTo (lane.getRight(), mid);
    area.closeSubPath();
    if (! flat)
    {
        g.setColour (colour::rose.withAlpha (0.22f));
        g.fillPath (area);
    }
    g.setColour (colour::rose.withAlpha (flat ? 0.35f : 1.0f));
    g.strokePath (edge, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Point<float> PerceptionScreen::getShownRange() const noexcept
{
    return { rangeLo, effectiveRangeHi (rangeLo, rangeHi) };
}

void PerceptionScreen::paintRange (juce::Graphics& g)
{
    const auto shown = getShownRange();
    const float xLo = xForCam (erb::hzToCam (shown.x));
    const float xHi = xForCam (erb::hzToCam (shown.y));

    // Outside the range the EQ and Heat do nothing: dim it in their lanes (Width acts everywhere).
    g.setColour (colour::screen.withAlpha (0.72f));
    for (const auto& lane : { mainLane, heatLane })
    {
        g.fillRect (lane.withRight (juce::jmax (lane.getX(), xLo)));
        g.fillRect (lane.withLeft (juce::jmin (lane.getRight(), xHi)));
    }

    const auto f = font (11.5f);
    const auto loText = formatHz (shown.x), hiText = formatHz (shown.y);
    const float loW = textWidth (f, loText), hiW = textWidth (f, hiText);
    const bool crowded = (xHi - xLo) < loW + hiW + 40.0f;

    auto drawHandle = [&] (float x, Handle which, const juce::String& text, float textW, bool labelRight)
    {
        const bool active = dragging == which || hovered == which;
        const auto c = active ? colour::text : colour::muted;
        g.setColour (c.withAlpha (active ? 0.8f : 0.35f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        // Grip above the plot: a small pill, wide enough to grab.
        const auto grip = juce::Rectangle<float> (10.0f, 18.0f).withCentre ({ x, plot.getY() - 13.0f });
        g.setColour (active ? colour::divider : colour::body);
        g.fillRoundedRectangle (grip, 5.0f);
        g.setColour (c.withAlpha (active ? 1.0f : 0.7f));
        g.drawRoundedRectangle (grip, 5.0f, 1.0f);

        g.setFont (f);
        g.setColour (c);
        const float tx = labelRight ? x + 10.0f : x - 10.0f - textW;
        g.drawText (text, juce::Rectangle<float> (tx, plot.getY() - 21.0f, textW + 2.0f, 16.0f), juce::Justification::centredLeft, false);
    };

    drawHandle (xLo, Handle::lo, loText, loW, ! crowded);
    drawHandle (xHi, Handle::hi, hiText, hiW, false);
}

void PerceptionScreen::paintLegend (juce::Graphics& g)
{
    // Quiet tags: what the big curve is (and the key, the reference, paint), and what the lower lane
    // is. In paint mode the weight lines run behind them, so the tags get a backing.
    auto tag = [&g, this] (juce::Point<float> at, const juce::String& t, juce::Colour c, const juce::String& sub)
    {
        const auto f = caps (11.0f);
        if (paintMode)
        {
            const float w = textWidth (f, t) + 8.0f + textWidth (font (11.0f), sub);
            g.setColour (colour::screen.withAlpha (0.85f));
            g.fillRoundedRectangle (juce::Rectangle<float> (at.x - 4.0f, at.y - 1.0f, w + 8.0f, 16.0f), 3.0f);
        }
        g.setFont (f);
        g.setColour (c);
        g.drawText (t, juce::Rectangle<float> (at.x, at.y, 120.0f, 14.0f), juce::Justification::centredLeft, false);
        g.setFont (font (11.0f));
        g.setColour (colour::faint);
        g.drawText (sub, juce::Rectangle<float> (at.x + textWidth (f, t) + 8.0f, at.y, 560.0f, 14.0f), juce::Justification::centredLeft, false);
    };
    // 5.3: only what you need to read the picture; the explanations live in the tooltips.
    float y = mainLane.getY() + 8.0f;
    if (frozen)
        tag ({ mainLane.getX() + 10.0f, y }, "AUTO EQ FROZEN", colour::straw, {});
    else
        tag ({ mainLane.getX() + 10.0f, y }, "AUTO EQ", colour::muted, midSide ? "Mid solid / Side dashed" : "");
    if (keyActive)
        tag ({ mainLane.getX() + 10.0f, y += 16.0f }, "KEY", colour::violet, {});
    if (zeroLatency)   // 5.2.7: Punch works here too (on whole attacks) unless Punch at Zero is off
        tag ({ mainLane.getX() + 10.0f, y += 16.0f }, "ZERO LATENCY", colour::text,
             zeroPunchOn ? "Heat and Listen rest" : "Heat, Punch and Listen rest");

    // 4.0 Reference: what it is and what it is doing, whenever it is in use or a file is loading.
    juce::String refText;
    const auto dash = juce::String::fromUTF8 (" \xe2\x80\x94 ");
    if (refError.isNotEmpty())
        refText = refError;
    else if (refAnalysing)
        refText = "analysing the dropped file...";
    else if (refMode == 1)
        refText = activity.refSeconds > 0.0f && refHave
                    ? "learning the key" + dash + juce::String (juce::roundToInt (activity.refSeconds)) + " s"
                    : juce::String ("waiting for the sidechain key");
    else if (refMode == 2)   // 5.3: just the reference's name once there is one
        refText = refHave ? refName : juce::String ("drop an audio file here, or learn one from the sidechain key");
    if (refText.isNotEmpty())
        tag ({ mainLane.getX() + 10.0f, y += 16.0f }, "REF", refError.isNotEmpty() ? colour::warning : colour::teal, refText);

    if (paintMode)
    {
        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        tag ({ mainLane.getX() + 10.0f, y += 16.0f }, "PAINT", colour::text,
             "drag in a lane: how much acts there" + dot + "Alt- or right-drag erases" + dot + "double-click resets the lane");
    }
    tag ({ heatLane.getX() + 10.0f, heatLane.getY() + 2.0f }, "HEAT", colour::bronze.withAlpha (0.9f), {});
    // "100 %" in the gutter marks the width lane's middle; in paint mode the lane says what it takes.
    tag ({ widthLane.getX() + 10.0f, widthLane.getY() + 2.0f }, "WIDTH", colour::rose.withAlpha (0.9f),
         paintMode ? juce::String ("how much of Width acts (top = all of it)") : juce::String());
    legendBottom = y + 14.0f;
}

void PerceptionScreen::paintListen (juce::Graphics& g)
{
    if (listenMode == 0)
        return;

    // A pill in the solo's role colour: the curve still shows the full processing, the pill says
    // that only one part of it is being heard.
    const juce::Colour role = listenMode == 1 ? colour::straw : listenMode == 2 ? colour::blue : colour::bronze;
    const juce::String title = listenMode == 1 ? "REVEAL ONLY" : listenMode == 2 ? "SMOOTH ONLY" : "HEAT ONLY";
    const juce::String sub = listenMode == 1 ? "hearing what Reveal lifts"
                           : listenMode == 2 ? "hearing what Smooth cuts" : "hearing only the harmonics";
    const auto tf = caps (11.0f), sf = font (11.0f);
    const float tw = textWidth (tf, title), sw = textWidth (sf, sub);
    const float w = tw + sw + 30.0f, h = 20.0f;
    const auto box = juce::Rectangle<float> (mainLane.getRight() - 10.0f - w, mainLane.getY() + 5.0f, w, h);

    g.setColour (role.withAlpha (0.14f));
    g.fillRoundedRectangle (box, h * 0.5f);
    g.setColour (role.withAlpha (0.8f));
    g.drawRoundedRectangle (box.reduced (0.5f), h * 0.5f, 1.0f);
    g.setFont (tf);
    g.setColour (role.brighter (0.3f));
    g.drawText (title, box.withTrimmedLeft (11.0f).withWidth (tw + 2.0f), juce::Justification::centredLeft, false);
    g.setFont (sf);
    g.setColour (colour::muted);
    g.drawText (sub, box.withTrimmedLeft (19.0f + tw).withWidth (sw + 2.0f), juce::Justification::centredLeft, false);
}

void PerceptionScreen::paintReadout (juce::Graphics& g)
{
    if (! haveFrame || hoverX < plot.getX() || hoverX > plot.getRight())
        return;

    const double cam = camForX (hoverX);
    const float hz = (float) erb::camToHz (cam);

    g.setColour (colour::text.withAlpha (0.22f));
    g.drawVerticalLine (juce::roundToInt (hoverX), plot.getY(), plot.getBottom());

    // Marker where the cursor crosses the EQ curve.
    const float eq = valueAtCam (gainDb.data(), cam);
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ hoverX, eqY (eq) });
    g.setColour (eq >= 0.0f ? colour::straw : colour::blue);
    g.fillEllipse (dot);
    g.setColour (colour::screen);
    g.drawEllipse (dot, 1.0f);

    const float heatAbs = valueAtCam (heatView.data(), cam);
    auto text = formatHz (hz)
              + (midSide ? "    M " + signedDb (eq, 1) + " / S " + signedDb (valueAtCam (sideGainDb.data(), cam), 1) + " dB"
                         : "    EQ " + signedDb (eq, 1) + " dB")
              + "    in " + signedDb (valueAtCam (inView.data(), cam), 1) + " dB"
              + "    heat " + (heatAbs < heatAudibleDb ? juce::String ("none")
                                                       : signedDb (heatAbs - valueAtCam (outView.data(), cam), 0) + " dB");
    // 4.0: the painted share here, in paint mode or wherever it is below 100 %.
    const float wEq = refgrid::sampleAt (paintCurves.eq, PaintCurves::numPoints, cam);
    const float wHeat = refgrid::sampleAt (paintCurves.heat, PaintCurves::numPoints, cam);
    const float wWidth = refgrid::sampleAt (paintCurves.width, PaintCurves::numPoints, cam);
    const float widthHere = widthAtHz (hz);   // 5.2.2
    if (std::abs (widthHere - 1.0f) > 0.005f)
        text << "    width " << juce::roundToInt (widthHere * 100.0f) << " %";
    const float sides = sideRelAtHz (hz);     // 5.2.3: the stereo image heard here
    if (sides > -199.0f)
        text << "    sides " << (sides < -40.0f ? juce::String ("mono") : signedDb (sides, 0) + " dB");
    if (paintMode || wEq < 0.995f || wHeat < 0.995f || wWidth < 0.995f)
        text << "    paint EQ " << juce::roundToInt (wEq * 100.0f) << " % / heat " << juce::roundToInt (wHeat * 100.0f)
             << " % / width " << juce::roundToInt (wWidth * 100.0f) << " %";
    const auto fnt = font (size::caption + 0.5f);
    const float w = tabularWidths (fnt, text) + 20.0f, h = 24.0f;
    const bool left = hoverX > plot.getCentreX();
    auto box = juce::Rectangle<float> (left ? hoverX - 12.0f - w : hoverX + 12.0f, juce::jmax (mainLane.getY() + 30.0f, legendBottom + 6.0f), w, h);
    box = box.constrainedWithin (plot);

    g.setColour (colour::body.withAlpha (0.96f));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colour::divider.brighter (0.2f));
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.setColour (colour::text);
    g.setFont (fnt);
    drawTabular (g, text, box.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
}

//==============================================================================
PerceptionScreen::Handle PerceptionScreen::handleAt (float x, float y) const noexcept
{
    if (y < plot.getY() - 24.0f || y > plot.getBottom())
        return Handle::none;
    const auto shown = getShownRange();
    const float dl = std::abs (x - xForCam (erb::hzToCam (shown.x)));
    const float dh = std::abs (x - xForCam (erb::hzToCam (shown.y)));
    if (juce::jmin (dl, dh) > 7.0f)
        return Handle::none;
    return dl <= dh ? Handle::lo : Handle::hi;
}

PerceptionScreen::Lane PerceptionScreen::laneAt (juce::Point<float> p) const noexcept
{
    // The gaps between the lanes belong to the nearer one.
    if (p.x < plot.getX() - 4.0f || p.x > plot.getRight() + 4.0f)
        return Lane::none;
    const float split1 = (mainLane.getBottom() + widthLane.getY()) * 0.5f;
    const float split2 = (widthLane.getBottom() + heatLane.getY()) * 0.5f;
    if (p.y >= mainLane.getY() && p.y < split1)
        return Lane::eq;
    if (p.y >= split1 && p.y < split2)
        return Lane::width;
    if (p.y >= split2 && p.y <= heatLane.getBottom() + 4.0f)
        return Lane::heat;
    return Lane::none;
}

juce::Rectangle<float> PerceptionScreen::laneRect (Lane lane) const noexcept
{
    return lane == Lane::eq ? mainLane : lane == Lane::width ? widthLane : heatLane;
}

float* PerceptionScreen::paintCurve (Lane lane) noexcept
{
    return lane == Lane::eq ? paintCurves.eq : lane == Lane::width ? paintCurves.width : paintCurves.heat;
}

void PerceptionScreen::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    // In paint mode the range handles are taken by their grips above the plot only.
    hovered = handleAt (p.x, p.y);
    if (paintMode && p.y >= plot.getY())
        hovered = Handle::none;
    hoverX = plot.contains (p) ? p.x : -1.0f;
    setMouseCursor (hovered != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                    : paintMode && laneAt (p) != Lane::none ? juce::MouseCursor::CrosshairCursor
                                                            : juce::MouseCursor::NormalCursor);
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
    if (paintMode && e.position.y >= plot.getY())
        dragging = Handle::none;
    if (dragging == Handle::lo) loAttachment->beginGesture();
    if (dragging == Handle::hi) hiAttachment->beginGesture();

    if (paintMode && dragging == Handle::none)
    {
        painting = laneAt (e.position);
        if (painting == Lane::none)
            return;
        strokeMerge = false;
        if (onPaintBegin != nullptr)
            onPaintBegin();
        lastPaint = e.position;
        paintStroke (e.position, e.position, e.mods.isAltDown() || e.mods.isPopupMenu());
    }
}

void PerceptionScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (painting != Lane::none)
    {
        paintStroke (lastPaint, e.position, e.mods.isAltDown() || e.mods.isPopupMenu());
        lastPaint = e.position;
        hoverX = plot.contains (e.position) ? e.position.x : -1.0f;
        return;
    }
    if (dragging == Handle::none)
        return;

    const float x = juce::jlimit (plot.getX(), plot.getRight(), e.position.x);
    float hz = (float) erb::camToHz (camForX (x));
    if (dragging == Handle::lo)
    {
        // Automation can leave the stored high limit below the range shown by the engine.
        // Preserve that shown limit before moving Low, so neither handle jumps on the drag.
        const float shownHi = getShownRange().y;
        if (rangeHi < shownHi)
            hiAttachment->setValueAsCompleteGesture (shownHi);
        loAttachment->setValueAsPartOfGesture (juce::jlimit (20.0f, juce::jmin (2000.0f, shownHi / 1.5f), hz));
    }
    else
        hiAttachment->setValueAsPartOfGesture (juce::jlimit (juce::jmax (1000.0f, rangeLo * 1.5f), 20000.0f, hz));

    hoverX = x;
    repaint();
}

void PerceptionScreen::mouseUp (const juce::MouseEvent&)
{
    if (painting != Lane::none)
    {
        painting = Lane::none;
        if (onPaintEnd != nullptr)
            onPaintEnd (strokeMerge);
        strokeMerge = false;
    }
    if (dragging == Handle::lo) loAttachment->endGesture();
    if (dragging == Handle::hi) hiAttachment->endGesture();
    dragging = Handle::none;
    repaint();
}

void PerceptionScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Paint: a double-click resets the lane to 100 %. The second press already opened a stroke
    // (mouseUp ends it); it joins the first click's stroke, so the reset is one undo step.
    if (painting != Lane::none)
    {
        std::fill_n (paintCurve (painting), PaintCurves::numPoints, 1.0f);
        strokeMerge = true;
        if (onPaintChange != nullptr)
            onPaintChange (paintCurves);
        repaint();
        return;
    }

    // JUCE delivers down, up, down, double-click, up. The second press has already opened this
    // handle's gesture (mouseUp closes it), so the reset belongs to that gesture instead of
    // nesting a second one for the same parameter inside it.
    const auto handle = handleAt (e.position.x, e.position.y);
    if (handle == Handle::none)
        return;
    auto& attachment = handle == Handle::lo ? *loAttachment : *hiAttachment;
    const auto* param = handle == Handle::lo ? loParam : hiParam;
    const float value = param->convertFrom0to1 (param->getDefaultValue());
    if (dragging == handle)
        attachment.setValueAsPartOfGesture (value);
    else
        attachment.setValueAsCompleteGesture (value);
}
//==============================================================================
// 4.0 Paint

void PerceptionScreen::setPaintMode (bool on)
{
    paintMode = on;
    setTooltip (on ? "Paint: drag in the EQ, Width or Heat lane to set how much of it acts there (top = 100 %, "
                     "bottom = none). Alt- or right-drag erases back to 100 %, double-click resets the lane"
                   : "AUTO EQ: the live gain over the spectrum (straw lifts, blue cuts); dashed lines are the Limits you "
                     "set (how far it may lift and cut). WIDTH: the stereo width you "
                     "hear (100 % in the middle); the shading shows how much of the sound is in the sides. HEAT: where "
                     "harmonics are written, against the output there. Drag a range handle to limit where Anneal acts; "
                     "double-click a handle to reset it. Drop an audio file here to use it as the reference. Horizontal "
                     "axis: ERB-rate scale (equal steps of the ear's frequency resolution).");
    repaint();
}

void PerceptionScreen::setPaint (const PaintCurves& curves)
{
    if (painting != Lane::none || curves == paintCurves)
        return;
    paintCurves = curves;
    repaint();
}

void PerceptionScreen::paintStroke (juce::Point<float> from, juce::Point<float> to, bool erase)
{
    float* curve = paintCurve (painting);
    const auto lane = laneRect (painting);
    constexpr int n = PaintCurves::numPoints;
    auto indexAt = [this] (float x)
    {
        return (float) juce::jlimit (0.0, (double) (n - 1), (camForX (x) - zMin) / (zMax - zMin) * (n - 1));
    };
    // Top = 100 %, bottom = 0 %; the outer 4 % snap, so a fully erased curve is exactly 100 %.
    auto valueAt = [&lane, erase] (float y)
    {
        if (erase)
            return 1.0f;
        const float v = (lane.getBottom() - y) / lane.getHeight();
        return v >= 0.96f ? 1.0f : v <= 0.04f ? 0.0f : v;
    };

    // Every control point the pointer crossed since the last event, the height interpolated along
    // the way, so a fast drag leaves no gaps.
    const float ia = indexAt (from.x), ib = indexAt (to.x);
    const int lo = juce::roundToInt (juce::jmin (ia, ib)), hi = juce::roundToInt (juce::jmax (ia, ib));
    bool changed = false;
    for (int i = lo; i <= hi; ++i)
    {
        const float t = std::abs (ib - ia) < 1.0e-4f ? 1.0f : juce::jlimit (0.0f, 1.0f, ((float) i - ia) / (ib - ia));
        const float v = valueAt (from.y + t * (to.y - from.y));
        changed = changed || curve[i] != v;
        curve[i] = v;
    }
    if (changed && onPaintChange != nullptr)
        onPaintChange (paintCurves);
    repaint();
}

void PerceptionScreen::paintWeights (juce::Graphics& g)
{
    // Painted-out regions dim like outside the Range (fully at 0 %), in the lane they belong to.
    for (auto which : { Lane::eq, Lane::width, Lane::heat })
    {
        const float* curve = paintCurve (which);
        if (PaintCurves::isFull (curve))
            continue;
        const auto lane = laneRect (which);
        for (float x = lane.getX(); x < lane.getRight(); x += 2.0f)
        {
            const float w = refgrid::sampleAt (curve, PaintCurves::numPoints, camForX (x + 1.0f));
            if (w < 0.999f)
            {
                g.setColour (colour::screen.withAlpha (0.72f * (1.0f - w)));
                g.fillRect (juce::Rectangle<float> (x, lane.getY(), juce::jmin (2.0f, lane.getRight() - x), lane.getHeight()));
            }
        }
    }

    if (! paintMode)
        return;

    // Paint mode: each lane's weight as a line (top of the lane = 100 %) with its control points.
    for (auto which : { Lane::eq, Lane::width, Lane::heat })
    {
        const float* curve = paintCurve (which);
        const auto lane = laneRect (which);
        auto yOf = [&lane] (float w) { return lane.getBottom() - w * lane.getHeight(); };
        juce::Path line;
        for (int i = 0; i < PaintCurves::numPoints; ++i)
        {
            const float x = xForCam (refgrid::camAt (i, PaintCurves::numPoints));
            if (i == 0) line.startNewSubPath (x, yOf (curve[i]));
            else        line.lineTo (x, yOf (curve[i]));
        }
        g.setColour (colour::text.withAlpha (0.75f));
        g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i < PaintCurves::numPoints; ++i)
        {
            const auto p = juce::Point<float> (xForCam (refgrid::camAt (i, PaintCurves::numPoints)), yOf (curve[i]));
            g.setColour (colour::screen);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
            g.setColour (colour::text.withAlpha (0.85f));
            g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p), 1.0f);
        }
    }
}

//==============================================================================
// 4.0 Reference

void PerceptionScreen::setReference (const ReferenceCurve& curve, const juce::String& name)
{
    refHave = curve.valid;
    if (refHave)
        std::copy (curve.db, curve.db + ReferenceCurve::numPoints, refDb.begin());
    refName = name;
    refOffsetPrimed = false;
    updateReferenceOffset (true);
    repaint();
}

void PerceptionScreen::setReferenceStatus (bool analysing, const juce::String& error)
{
    if (analysing == refAnalysing && error == refError)
        return;
    refAnalysing = analysing;
    refError = error;
    repaint();
}

void PerceptionScreen::updateReferenceOffset (bool instant)
{
    // The reference is level-normalised; it is drawn where the output's own spectrum would sit
    // after the same normalisation (anneal::normaliseLevels), followed with ~0.5 s of smoothing.
    if (! haveFrame || dz <= 0.0f)
        return;
    const double zLo = erb::hzToCam (50.0), zHi = erb::hzToCam (16000.0);
    const int lo = juce::jlimit (0, nb - 1, (int) std::ceil ((zLo - z0) / dz));
    const int hi = juce::jlimit (lo, nb - 1, (int) std::floor ((zHi - z0) / dz));
    double power = 0.0;
    for (int b = lo; b <= hi; ++b)
        power += std::pow (10.0, outView[(size_t) b] / 10.0);
    const double floorLevel = 10.0 * std::log10 (std::max (power / (hi - lo + 1), 1.0e-30)) - 40.0;
    if (floorLevel < -150.0)
        return;   // silence: keep the last position
    double sum = 0.0;
    for (int b = lo; b <= hi; ++b)
        sum += std::max ((double) outView[(size_t) b], floorLevel);
    const float target = (float) (sum / (hi - lo + 1));
    refOffset = instant || ! refOffsetPrimed ? target : refOffset + 0.07f * (target - refOffset);
    refOffsetPrimed = true;
}

void PerceptionScreen::paintReference (juce::Graphics& g)
{
    if (! haveFrame || ! refHave || refMode == 0 || ! refOffsetPrimed)
        return;
    juce::Path line;
    bool open = false;
    for (float x = mainLane.getX(); x <= mainLane.getRight(); x += 2.0f)
    {
        const float y = specY (refgrid::sampleAt (refDb.data(), ReferenceCurve::numPoints, camForX (x)) + refOffset);
        if (! open) { line.startNewSubPath (x, y); open = true; }
        else        line.lineTo (x, y);
    }
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (mainLane.toNearestInt());
    g.setColour (colour::teal.withAlpha (0.16f));
    g.strokePath (line, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour::teal.withAlpha (0.9f));
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

bool PerceptionScreen::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (canLoadFile != nullptr && canLoadFile (juce::File (f)))
            return true;
    return false;
}

void PerceptionScreen::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHover = true;
    repaint();
}

void PerceptionScreen::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaint();
}

void PerceptionScreen::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
        if (canLoadFile != nullptr && canLoadFile (juce::File (f)))
        {
            if (onFileDropped != nullptr)
                onFileDropped (juce::File (f));
            break;
        }
    repaint();
}

void PerceptionScreen::paintDrop (juce::Graphics& g)
{
    if (! dropHover)
        return;
    const auto r = plot.expanded (6.0f);
    g.setColour (colour::teal.withAlpha (0.08f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour::teal.withAlpha (0.9f));
    const float dashes[] = { 6.0f, 4.0f };
    juce::Path outline, dashed;
    outline.addRoundedRectangle (r.reduced (1.0f), 8.0f);
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
    g.fillPath (dashed);
    const auto card = juce::Rectangle<float> (480.0f, 54.0f).withCentre (r.getCentre());
    g.setColour (colour::body.withAlpha (0.94f));
    g.fillRoundedRectangle (card, 8.0f);
    g.setColour (colour::teal.withAlpha (0.6f));
    g.drawRoundedRectangle (card, 8.0f, 1.0f);
    g.setColour (colour::teal);
    g.setFont (caps (13.0f));
    g.drawText ("DROP TO USE AS THE REFERENCE", r.withHeight (22.0f).withCentre (r.getCentre().translated (0.0f, -10.0f)),
                juce::Justification::centred, false);
    g.setFont (font (12.0f));
    g.setColour (colour::muted);
    g.drawText ("Anneal learns its long-term tone, then Match steers the auto EQ toward it",
                r.withHeight (18.0f).withCentre (r.getCentre().translated (0.0f, 12.0f)), juce::Justification::centred, false);
}
}
