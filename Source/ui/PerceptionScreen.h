#pragma once

#include "dsp/DisplayFrame.h"
#include "dsp/Reference.h"
#include "dsp/TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

namespace anneal
{
    // The main display. Horizontal axis is the ERB-rate (Cam) scale, the same grid the model
    // uses. The large lane shows the live auto-EQ gain (the thing Anneal is doing) drawn over a
    // dimmed in/out spectrum; a short lane below shows the harmonic energy written. Range
    // handles run the full height and are bound to the rangeLo / rangeHi parameters.
    // 4.0: the reference's spectrum as a teal line (a file dropped here becomes the reference),
    // and Paint: in paint mode, dragging in a lane draws how much of the EQ / Heat acts there.
    // 5.2.2: a slim WIDTH lane between them shows the stereo width heard at each frequency (Width,
    // its paint and Mono bass), and in paint mode takes Width's paint.
    class PerceptionScreen : public juce::Component,
                             public juce::SettableTooltipClient,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
    {
    public:
        PerceptionScreen (TripleBuffer<DisplayFrame>& source, juce::AudioProcessorValueTreeState& state);
        ~PerceptionScreen() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        // Snapshot/test helpers: take the newest frame as-is, and place the hover read-out.
        void refreshNow();
        void setReadoutHz (float hz);
        // The range drawn (low, high Hz): the one the engine uses, also when automation has put
        // Range Low above Range High.
        juce::Point<float> getShownRange() const noexcept;

        // How hard Reveal / Smooth / Heat are working (from the engine's frames, with meter
        // ballistics: instant rise, 12 dB/s fall). live = audio is arriving.
        struct Activity
        {
            float liftDb = 0.0f, cutDb = 0.0f, heatRelDb = -200.0f, scDb = 0.0f;
            bool live = false, keyActive = false;
            float matchDb = 0.0f, refSeconds = 0.0f;   // 4.0 reference: strongest Match move, seconds learned
            bool refValid = false;
            float punchDb = 0.0f;   // 5.0: strongest Punch move, dB
        };
        Activity getActivity() const noexcept { return activity; }

        // 4.0 Reference. The curve is the processor's (level-normalised, on the fixed grid); it is
        // drawn while Reference is Learn key or Match, at the level of the output spectrum.
        void setReference (const ReferenceCurve& curve, const juce::String& name);
        void setReferenceStatus (bool analysing, const juce::String& error);
        std::function<bool (const juce::File&)> canLoadFile;
        std::function<void (const juce::File&)> onFileDropped;
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        bool isDropHighlighted() const noexcept { return dropHover; }
        bool hasReference() const noexcept { return refHave; }
        juce::String getReferenceName() const { return refName; }

        // 4.0 Paint. The curves are the processor's; while a stroke is open the screen's copy leads
        // and setPaint() is ignored. A stroke = onPaintBegin, onPaintChange..., onPaintEnd
        // (mergeWithPrevious: a double-click's reset joins the click before it as one undo step).
        void setPaintMode (bool on);
        bool isPaintMode() const noexcept { return paintMode; }
        void setPaint (const PaintCurves& curves);
        const PaintCurves& getPaint() const noexcept { return paintCurves; }
        std::function<void()> onPaintBegin;
        std::function<void (const PaintCurves&)> onPaintChange;
        std::function<void (bool mergeWithPrevious)> onPaintEnd;

        // Geometry, for tests: the EQ, Width and Heat lanes, and where a frequency is drawn.
        juce::Rectangle<float> getEqLane() const noexcept { return mainLane; }
        juce::Rectangle<float> getWidthLane() const noexcept { return widthLane; }
        juce::Rectangle<float> getHeatLane() const noexcept { return heatLane; }
        // 5.2.2: the stereo width heard at a frequency (1 = as is): Width by its painted share, Mono bass.
        float widthAtHz (float hz) const noexcept;
        // 5.2.3: the stereo image drawn at a frequency: the sides' level relative to the centre, dB
        // (-200 = no signal or a mono track).
        float sideRelAtHz (float hz) const noexcept;
        float xForHz (float hz) const noexcept;

    private:
        enum class Handle { none, lo, hi };
        enum class Lane { none, eq, width, heat };

        void timerCallback() override;
        float xForCam (double cam) const noexcept;
        double camForX (float x) const noexcept;
        float valueAtCam (const float* bands, double cam) const noexcept;
        Handle handleAt (float x, float y) const noexcept;
        Lane laneAt (juce::Point<float> p) const noexcept;
        juce::Rectangle<float> laneRect (Lane lane) const noexcept;
        float* paintCurve (Lane lane) noexcept;
        void paintStroke (juce::Point<float> from, juce::Point<float> to, bool erase);
        void updateReferenceOffset (bool instant);

        void paintSpectrum (juce::Graphics&);
        void paintReference (juce::Graphics&);
        void paintEq (juce::Graphics&);
        void paintAttacks (juce::Graphics&);   // 5.0: where Guard and Punch see an attack
        float eqY (float db) const noexcept;
        float specY (float db) const noexcept;
        void paintHeat (juce::Graphics&);
        void paintWidth (juce::Graphics&);     // 5.2.2
        void paintWeights (juce::Graphics&);   // painted-out areas dim; in paint mode, the weight lines
        void paintRange (juce::Graphics&);
        void paintReadout (juce::Graphics&);
        void paintLegend (juce::Graphics&);
        void paintListen (juce::Graphics&);   // what a Listen solo is playing (top right)
        void paintDrop (juce::Graphics&);     // a file is being dragged over the display

        TripleBuffer<DisplayFrame>& source;

        static constexpr int nb = DisplayFrame::numBands;
        std::array<float, nb> inDb {}, outDb {}, gainDb {}, heatDb {}, sideGainDb {};
        bool midSide = false;
        // What is drawn: the levels above, smoothed across bands (about +-0.25 Cam) so tonal
        // content reads as a flowing line rather than one-ERB-wide plateaus. Display only.
        std::array<float, nb> inView {}, outView {}, heatView {};
        std::array<float, nb> keyDb {}, keyView {};   // sidechain key (3.0)
        std::array<float, nb> attack {};               // 5.0: attack per band, 0..1 (held briefly)
        std::array<float, nb> sideRel {}, sideRelView {}; // 5.2.3: the stereo image (Side vs Mid, dB)
        bool stereoImage = false;
        bool attacksOn = false;
        bool zeroLatency = false;                      // 5.1
        bool keyActive = false, frozen = false;
        void updateViews();
        float z0 = 0.0f, dz = 0.0f;
        bool haveFrame = false;
        int idleTicks = 0;

        juce::Rectangle<float> plot, mainLane, widthLane, heatLane;
        Activity activity;

        std::unique_ptr<juce::ParameterAttachment> loAttachment, hiAttachment, listenAttachment, refModeAttachment;
        std::unique_ptr<juce::ParameterAttachment> widthAttachment, monoAttachment;   // 5.2.2: the WIDTH lane
        float widthSetting = 1.0f, monoHz = 0.0f;
        std::unique_ptr<juce::ParameterAttachment> zeroPunchAttachment;   // 5.2.7: what the ZERO LATENCY tag says
        bool zeroPunchOn = true;
        int listenMode = 0, refMode = 0;
        juce::RangedAudioParameter* loParam = nullptr;
        juce::RangedAudioParameter* hiParam = nullptr;
        float rangeLo = 30.0f, rangeHi = 18000.0f;
        Handle dragging = Handle::none, hovered = Handle::none;
        float hoverX = -1.0f;
        float legendBottom = 0.0f;   // the hover read-out sits below the legend's tags

        // 4.0 Reference
        std::array<float, ReferenceCurve::numPoints> refDb {};
        bool refHave = false, refAnalysing = false, dropHover = false;
        juce::String refName, refError;
        float refOffset = 0.0f;   // dB: where the normalised reference sits on the spectrum scale
        bool refOffsetPrimed = false;

        // 4.0 Paint
        PaintCurves paintCurves;
        bool paintMode = false, strokeMerge = false;
        Lane painting = Lane::none;
        juce::Point<float> lastPaint;
    };
}
