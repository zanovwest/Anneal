#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace anneal
{
    // Header preset selector: [<] [ name  v ] [>]. Clicking the name opens the factory list;
    // the arrows step through it (wrapping). Draws itself; the owner supplies the data.
    // 5.2.4: .annealpreset files dropped on it are handed to onFilesDropped.
    class PresetBar : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget
    {
    public:
        PresetBar();

        std::function<int()> getCount;                     // number of presets
        std::function<juce::String (int)> getName;         // name of preset i
        std::function<int()> getChosen;                    // index the list ticks / the arrows step from
        std::function<juce::String()> getDisplayText;      // what the box shows, e.g. "Vocal air *"
        std::function<void (int)> onChoose;
        // 5.0 user presets: entries from getFactoryCount() on are the user's.
        std::function<int()> getFactoryCount;
        std::function<void()> onSaveAs;          // show the save dialog
        std::function<void (int)> onDelete;      // delete entry i (one of the user's)
        std::function<void()> onOpenFolder;
        std::function<void()> onCopyFolderPath;                    // 5.2.4
        std::function<void (const juce::StringArray&)> onFilesDropped;

        void refresh();   // call from a timer: repaints only when the shown text changes
        // 5.2.4: a short note in place of the name for a few seconds (what a drop did).
        void showMessage (const juce::String& text, double seconds = 4.0);
        juce::String getShownText() const { return shown; }

        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override;
        bool isDropHighlighted() const noexcept { return dropHover; }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int arrowWidth = 30;

    private:
        enum class Zone { none, prev, name, next };
        Zone zoneAt (juce::Point<int>) const;
        juce::Rectangle<int> zoneBounds (Zone) const;
        void step (int delta);
        void showMenu();

        juce::String shown, message;
        double messageUntil = 0.0;
        Zone hover = Zone::none;
        bool dropHover = false;
    };
}
