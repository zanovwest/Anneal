#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace anneal
{
// The Reference menu (REFERENCE group, 4.0), bound to the "refMode" parameter, with two extra
// actions under the modes: load a reference file (the same as dropping one on the display) and
// clear the reference. Off is a plain raised pill; Learn key and Match light it in teal.
class ReferenceMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int numChoices = 3;

    ReferenceMenu (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, numChoices - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        setTooltip ("Reference: Learn key learns the tone of the track routed to the sidechain input; Match steers "
                    "the auto EQ toward the reference. Drop an audio file on the display (or load one here) to use it");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (const juce::File&)> onLoadFile;
    std::function<void()> onClear;
    std::function<bool()> hasReference;

    int getChoice() const noexcept { return choice; }

    static juce::String pillText (int c)
    {
        switch (c)
        {
            case 1:  return "Learn key";
            case 2:  return "Match";
            default: return "Reference: off";
        }
    }

    static juce::String menuText (int c)
    {
        switch (c)
        {
            case 1:  return "Learn key: learn the sidechain's tone";
            case 2:  return "Match: steer toward the reference";
            default: return "Off: the auto EQ as before";
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = choice > 0;
        g.setColour (on ? colour::teal.withAlpha (0.18f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? colour::teal.withAlpha (0.9f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (on ? colour::teal.brighter (0.3f) : colour::text);
        g.setFont (font (12.0f, "Semibold"));
        g.drawText (pillText (choice), r.withTrimmedLeft (10).withTrimmedRight (17), juce::Justification::centredLeft, false);
        const float x = r.getRight() - 11, y = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x - 3, y - 1.5f, x + 3, y - 1.5f, x, y + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Reference");
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (i + 1, menuText (i), true, i == choice);
        menu.addSeparator();
        menu.addItem (loadItem, "Load a reference file...");
        menu.addItem (clearItem, "Clear the reference", hasReference != nullptr && hasReference());
        juce::Component::SafePointer<ReferenceMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                           [safe] (int selected)
        {
            if (safe == nullptr || selected <= 0)
                return;
            if (selected == loadItem)
                safe->chooseFile();
            else if (selected == clearItem)
            {
                if (safe->onClear != nullptr)
                    safe->onClear();
            }
            else
                safe->attachment->setValueAsCompleteGesture ((float) (selected - 1));
        });
    }

private:
    static constexpr int loadItem = 10, clearItem = 11;

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a reference recording", juce::File(),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<ReferenceMenu> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult().existsAsFile() && safe->onLoadFile != nullptr)
                safe->onLoadFile (fc.getResult());
        });
    }

    int choice = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
