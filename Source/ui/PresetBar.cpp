#include "PresetBar.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

PresetBar::PresetBar()
{
    setTooltip ("Factory presets (also in your DAW's preset menu) and your own. * = changed since loading it. "
                "Drop .annealpreset files here to add them to your presets");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PresetBar::refresh()
{
    auto text = getDisplayText ? getDisplayText() : juce::String();
    if (message.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < messageUntil)
        text = message;
    if (text != shown)
    {
        shown = text;
        repaint();
    }
}

void PresetBar::showMessage (const juce::String& text, double seconds)
{
    message = text;
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 1000.0 * seconds;
    refresh();
}

bool PresetBar::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".annealpreset"))
            return onFilesDropped != nullptr;
    return false;
}

void PresetBar::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    if (onFilesDropped != nullptr)
        onFilesDropped (files);
}

juce::Rectangle<int> PresetBar::zoneBounds (Zone z) const
{
    auto r = getLocalBounds();
    switch (z)
    {
        case Zone::prev: return r.removeFromLeft (arrowWidth);
        case Zone::next: return r.removeFromRight (arrowWidth);
        case Zone::name: return r.reduced (arrowWidth, 0);
        case Zone::none: break;
    }
    return {};
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<int> p) const
{
    for (auto z : { Zone::prev, Zone::next, Zone::name })
        if (zoneBounds (z).contains (p))
            return z;
    return Zone::none;
}

void PresetBar::paint (juce::Graphics& g)
{
    // One pill: arrows at the ends, the name in the middle with a drop-down mark.
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (colour::body);
    g.fillRoundedRectangle (r, rad);
    if (hover != Zone::none)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (zoneBounds (hover));
        g.setColour (colour::divider);
        g.fillRoundedRectangle (r, rad);
    }
    g.setColour (hover != Zone::none ? colour::muted.withAlpha (0.6f) : colour::divider);
    g.drawRoundedRectangle (r, rad, 1.0f);
    g.setColour (colour::divider);
    for (auto z : { Zone::prev, Zone::next })
    {
        const auto zb = zoneBounds (z).toFloat();
        const float lx = z == Zone::prev ? zb.getRight() : zb.getX();
        g.drawVerticalLine (juce::roundToInt (lx), r.getY() + 6.0f, r.getBottom() - 6.0f);
    }

    auto chevron = [&g] (juce::Rectangle<float> zr, bool left, juce::Colour c)
    {
        const auto m = zr.getCentre();
        const float w = 3.0f, h = 4.5f, dx = left ? w : -w;
        juce::Path p;
        p.startNewSubPath (m.x + dx * 0.5f, m.y - h);
        p.lineTo (m.x - dx * 0.5f, m.y);
        p.lineTo (m.x + dx * 0.5f, m.y + h);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    };
    chevron (zoneBounds (Zone::prev).toFloat().withTrimmedLeft (3.0f), true, hover == Zone::prev ? colour::text : colour::muted);
    chevron (zoneBounds (Zone::next).toFloat().withTrimmedRight (3.0f), false, hover == Zone::next ? colour::text : colour::muted);

    const auto name = zoneBounds (Zone::name).toFloat();
    g.setColour (colour::text);
    g.setFont (font (14.0f, "Semibold"));
    g.drawFittedText (shown, name.reduced (12.0f, 0.0f).withTrimmedRight (12.0f).toNearestInt(),
                      juce::Justification::centred, 1, 0.85f);

    const float cx = name.getRight() - 14.0f, cy = name.getCentreY();
    juce::Path tri;
    tri.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (hover == Zone::name ? colour::text : colour::muted);
    g.fillPath (tri);

    // 5.2.4: a preset file held over the bar: a dashed outline and what dropping it does.
    if (dropHover)
    {
        g.setColour (colour::body);
        g.fillRoundedRectangle (r, rad);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r.reduced (1.0f), rad - 1.0f);
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (colour::text);
        g.fillPath (dashed);
        g.setFont (caps (11.0f));
        g.drawText ("DROP TO ADD THE PRESET", r.toNearestInt(), juce::Justification::centred, false);
    }
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover)
    {
        hover = z;
        repaint();
    }
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hover = Zone::none;
    repaint();
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.getPosition()))
    {
        case Zone::prev: step (-1); break;
        case Zone::next: step (+1); break;
        case Zone::name: showMenu(); break;
        case Zone::none: break;
    }
}

void PresetBar::step (int delta)
{
    const int n = getCount ? getCount() : 0;
    if (n <= 0 || ! onChoose)
        return;
    const int from = getChosen ? getChosen() : 0;
    onChoose (((from + delta) % n + n) % n);
    refresh();
}

void PresetBar::showMenu()
{
    const int n = getCount ? getCount() : 0;
    if (n <= 0 || ! getName)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("FACTORY PRESETS");
    const int chosen = getChosen ? getChosen() : -1;
    const int nf = getFactoryCount ? juce::jlimit (0, n, getFactoryCount()) : n;
    for (int i = 0; i < nf; ++i)
        menu.addItem (i + 1, getName (i), true, i == chosen);
    if (n > nf)
    {
        menu.addSectionHeader ("YOUR PRESETS");
        for (int i = nf; i < n; ++i)
            menu.addItem (i + 1, getName (i), true, i == chosen);
    }
    constexpr int saveId = 100001, deleteId = 100002, folderId = 100003, copyPathId = 100004;
    if (onSaveAs != nullptr || onOpenFolder != nullptr)
        menu.addSeparator();
    if (onSaveAs != nullptr)
        menu.addItem (saveId, juce::String::fromUTF8 ("Save current settings as\xe2\x80\xa6"));
    if (onDelete != nullptr && chosen >= nf && chosen < n)
        menu.addItem (deleteId, juce::String::fromUTF8 ("Delete \xe2\x80\x9c") + getName (chosen) + juce::String::fromUTF8 ("\xe2\x80\x9d\xe2\x80\xa6"));
    if (onOpenFolder != nullptr)
        menu.addItem (folderId, "Open the presets folder");
    if (onCopyFolderPath != nullptr)
        menu.addItem (copyPathId, "Copy the presets folder path");

    juce::Component::SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withMinimumWidth (getWidth())
                                                  .withStandardItemHeight (24),
                        [safe, chosen] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == saveId)        { if (safe->onSaveAs) safe->onSaveAs(); }
                            else if (result == deleteId) { if (safe->onDelete) safe->onDelete (chosen); }
                            else if (result == folderId) { if (safe->onOpenFolder) safe->onOpenFolder(); }
                            else if (result == copyPathId) { if (safe->onCopyFolderPath) safe->onCopyFolderPath(); }
                            else if (safe->onChoose)     safe->onChoose (result - 1);
                            if (safe != nullptr)
                                safe->refresh();
                        });
}
}
