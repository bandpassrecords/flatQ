#include "PluginEditor.h"
#include <map>

namespace
{
    // A small "name this" dialog (text field + OK / Cancel). The callback gets the typed name.
    void promptName (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& initial,
                     const juce::String& okText, std::function<void (juce::String)> onOk)
    {
        auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
        w->addTextEditor ("name", initial);
        w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int r) { if (r == 1) onOk (w->getTextEditorContents ("name").trim()); }), true);
    }

    // "Export profile": pick where to save a copy of a library profile as a .flatq file.
    void exportProfileDialog (FlatQProcessor& proc, const juce::File& source, std::unique_ptr<juce::FileChooser>& holder)
    {
        auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (source.getFileNameWithoutExtension() + ".flatq");
        holder = std::make_unique<juce::FileChooser> ("Export profile", start, "*.flatq");
        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [&proc, source] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                juce::String err;
                if (! proc.exportProfile (source, f, err))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not export", err);
            });
    }
}

// ------------------------------------------------------------------------------------------------ look and feel
FlatQLookAndFeel::FlatQLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::backgroundColourId, track);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff262626));
    setColour (juce::ComboBox::outlineColourId, divider);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::BubbleComponent::backgroundColourId, panel);
    setColour (juce::BubbleComponent::outlineColourId, divider);
    setColour (juce::ListBox::backgroundColourId, sidebar);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff5a5a5a));   // the default is blue
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, track);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262626));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffef4444));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, divider);
}

void FlatQLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto c = b.findColour (b.getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (over) c = c.brighter (0.12f);
    if (down) c = c.brighter (0.2f);
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c); g.fillRoundedRectangle (r, 6.f);
    g.setColour (theme::divider); g.drawRoundedRectangle (r, 6.f, 1.f);
}

void FlatQLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) / 2.f;
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, r, r, 0.f, a0, a1, true);
    value.addCentredArc (c.x, c.y, r, r, 0.f, a0, angle, true);
    g.setColour (theme::track); g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId)); g.strokePath (value, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::text);   // pointer
    g.drawLine (c.x + std::sin (angle) * r * 0.30f, c.y - std::cos (angle) * r * 0.30f, c.x + std::sin (angle) * r * 0.72f, c.y - std::cos (angle) * r * 0.72f, 2.f);
}

juce::Slider::SliderLayout FlatQLookAndFeel::getSliderLayout (juce::Slider& s)
{
    auto l = juce::LookAndFeel_V4::getSliderLayout (s);
    const auto st = s.getSliderStyle();
    if (st == juce::Slider::LinearHorizontal) l.sliderBounds = l.sliderBounds.reduced (7, 0);   // thumb radius: the thumb's edge, not its centre,
    else if (st == juce::Slider::LinearVertical) l.sliderBounds = l.sliderBounds.reduced (0, 7);  // lines up with the ends of the groove
    return l;
}

void FlatQLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                                         juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }
    const bool horiz = style == juce::Slider::LinearHorizontal;
    // minPos / maxPos equal sliderPos for single-value sliders, so the groove spans the component instead
    const float thick = 6.f, r = 7.f, lo = (horiz ? (float) x : (float) y) - r, hi = (horiz ? (float) (x + w) : (float) (y + h)) + r;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    auto along = [&] (float a, float b)   // rectangle covering [a, b] along the slider axis
    {
        return horiz ? juce::Rectangle<float> (juce::jmin (a, b), cy - thick / 2, std::abs (b - a), thick)
                     : juce::Rectangle<float> (cx - thick / 2, juce::jmin (a, b), thick, std::abs (b - a));
    };
    g.setColour (theme::track); g.fillRoundedRectangle (along (lo, hi), thick / 2);                                // groove
    float from = horiz ? lo : hi;                                                                              // filled end: left / bottom
    if (s.getProperties().getWithDefault ("bipolar", false))
        from = juce::jlimit (lo, hi, (float) s.getPositionOfValue (0.0));
    g.setColour (s.findColour (juce::Slider::trackColourId).withAlpha (1.f)); g.fillRoundedRectangle (along (from, sliderPos), thick / 2);   // fill
    const float tx = horiz ? sliderPos : cx, ty = horiz ? cy : sliderPos;
    g.setColour (s.findColour (juce::Slider::thumbColourId)); g.fillEllipse (tx - r, ty - r, 2 * r, 2 * r);
    g.setColour (theme::bg); g.drawEllipse (tx - r, ty - r, 2 * r, 2 * r, 1.5f);
}

// ------------------------------------------------------------------------------------------------ response graph
juce::Rectangle<float> ResponseGraph::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (34.f).withTrimmedLeft (34.f).withTrimmedRight (56.f).withTrimmedBottom (22.f);
}

float ResponseGraph::xOf (float f) const
{
    auto p = plotArea();
    return p.getX() + std::log10 (f / 20.f) / 3.f * p.getWidth();
}

void ResponseGraph::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg);
    auto plot = plotArea();

    const float pre = (float) data.preampDb;   // the preamp lives in the output fader: the curves are drawn relative to 0 dB
    const float range = 24.f;                  // fixed +-24 dB: gentler-looking curves; deeper excursions are clipped at the edge
    auto yOf = [&] (float db) { return plot.getBottom() - (db + range) / (2 * range) * plot.getHeight(); };

    // legend: Target = the flat line we aim for; Measured = inverse of the profile (what the speaker/headphone does);
    // EQ = the filter we apply; Result = Measured + EQ, which should land on the flat target
    const juce::Colour measuredCol (0xfffbbf24), eqCol = accent;
    {
        struct Item { const char* name; bool* on; bool visible; juce::Colour col; };
        Item items[5] = { { "Target (flat)", &showTarget, true, good }, { "Measured", &showMeasured, true, measuredCol },
                          { "EQ", &showEq, true, eqCol }, { "Result", &showResult, true, result },
                          { "Boost limit", &showLimit, data.hasCeiling, dim } };
        int x = 14;
        g.setFont (juce::FontOptions (13.f));
        for (int i = 0; i < 5; ++i)
        {
            legendHit[i] = {};
            if (! items[i].visible) continue;
            const int w = 34 + 7 * (int) std::strlen (items[i].name);
            legendHit[i] = { x, 6, w, 24 };
            const float a = *items[i].on ? 1.f : 0.35f;
            auto dot = [&] (float cx, juce::Colour c) { g.setColour (c.withAlpha (a)); g.fillEllipse (cx - 5.f, 18.f - 5.f, 10.f, 10.f); };
            float cx = (float) x + 6.f;
            if (i == 4) { g.setColour (dim.withAlpha (a)); g.drawHorizontalLine (18, (float) x, (float) x + 12.f); cx += 6.f; }
            else { dot (cx, items[i].col); if (data.perChannel && i > 0) { dot (cx + 12.f, items[i].col.brighter (0.5f)); cx += 12.f; } }
            g.setColour (text.withAlpha (a));
            g.drawText (items[i].name, (int) cx + 12, 6, w, 24, juce::Justification::centredLeft);
            x += w + 10;
        }
    }

    // grid
    g.setFont (juce::FontOptions (11.f));
    for (float db = -range; db <= range + 0.1f; db += 6.f)
    {
        g.setColour (db == 0 ? juce::Colour (0xff4a4a4a) : divider);
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + "dB", (int) plot.getRight() + 6, (int) yOf (db) - 7, 48, 14, juce::Justification::centredLeft);
    }
    for (float f : { 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                     1000.f, 2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f })
    {
        const bool major = f == 20.f || f == 100.f || f == 1000.f || f == 10000.f || f == 20000.f;
        g.setColour (major ? juce::Colour (0xff343434) : divider.withAlpha (0.6f));
        g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
    }
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        g.setColour (dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000.f, 0) + " kHz" : juce::String ((int) f) + " Hz",
                    (int) xOf (f) - 28, (int) plot.getBottom() + 3, 56, 14, juce::Justification::centred);
    }

    // bass limit region (draggable)
    {
        const float hx = xOf (bassLimit > 0 ? bassLimit : 20.f);
        if (bassLimit > 0)
        {
            const float fx = xOf (juce::jmax (20.f, bassLimit * 0.5f));
            g.setColour (accent.withAlpha (0.16f)); g.fillRect (plot.getX(), plot.getY(), fx - plot.getX(), plot.getHeight());
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.16f), fx, 0.f, accent.withAlpha (0.04f), hx, 0.f, false));
            g.fillRect (fx, plot.getY(), hx - fx, plot.getHeight());
        }
        g.setColour (accent); g.drawVerticalLine ((int) hx, plot.getY(), plot.getBottom());
        auto grip = juce::Rectangle<float> (hx - 4.f, plot.getCentreY() - 16.f, 8.f, 32.f);
        g.fillRoundedRectangle (grip, 3.f);
        g.setColour (bg); g.drawVerticalLine ((int) hx, grip.getY() + 8.f, grip.getBottom() - 8.f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (11.f));
        g.drawText (bassLimit > 0 ? "Bass limit " + juce::String ((int) bassLimit) + " Hz" : "Bass limit off  (drag)",
                    (int) hx + 8, (int) plot.getY() + 4, 150, 14, juce::Justification::centredLeft);
    }

    if (data.freqs.empty()) { g.setColour (dim); g.drawText ("Select a profile", getLocalBounds(), juce::Justification::centred); return; }

    g.saveState(); g.reduceClipRegion (plot.toNearestInt().expanded (0, 2));
    auto path = [&] (const std::vector<float>& v) {
        juce::Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = xOf (data.freqs[i]), y = juce::jlimit (plot.getY() - 4, plot.getBottom() + 4, yOf (v[i]));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    };
    if (data.hasCeiling && showLimit)
    {
        std::vector<float> ceil (data.ceiling);
        for (auto& c : ceil) c -= pre;
        juce::Path p = path (ceil); juce::Path dashed;
        const float dash[] = { 5.f, 4.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, p, dash, 2);
        g.setColour (dim); g.fillPath (dashed);
    }
    const int nDraw = data.perChannel ? 2 : 1;
    auto shade = [] (juce::Colour c, int ch) { return ch == 0 ? c : c.brighter (0.5f); };   // R is a lighter shade of L
    auto strokeCurve = [&] (const std::vector<float>& v, juce::Colour c, float w)
    { if (v.size() == data.freqs.size()) { g.setColour (c); g.strokePath (path (v), juce::PathStrokeType (w)); } };

    if (showTarget)   // the target is the flat line
    {
        juce::Path flat; flat.startNewSubPath (plot.getX(), yOf (0.f)); flat.lineTo (plot.getRight(), yOf (0.f));
        g.setColour (good.withAlpha (0.25f)); g.strokePath (flat, juce::PathStrokeType (5.f));
        g.setColour (good); g.strokePath (flat, juce::PathStrokeType (1.2f));
    }
    for (int c = 0; c < nDraw; ++c)
    {
        if (showMeasured) strokeCurve (data.measured[(size_t) c], shade (measuredCol, c), 1.6f);
        if (showEq && data.actual[(size_t) c].size() == data.freqs.size())
        {
            std::vector<float> eq (data.actual[(size_t) c]);          // the filter without its preamp
            for (auto& v : eq) v -= pre;
            strokeCurve (eq, shade (eqCol, c), 1.6f);
        }
        if (showResult && data.actual[(size_t) c].size() == data.freqs.size() && data.measured[(size_t) c].size() == data.freqs.size())
        {
            std::vector<float> res (data.freqs.size());      // measured + EQ without the preamp
            for (size_t i = 0; i < res.size(); ++i) res[i] = data.measured[(size_t) c][i] + data.actual[(size_t) c][i] - (float) data.preampDb;
            strokeCurve (res, shade (result, c), 2.2f);
        }
    }
    g.restoreState();
}

void ResponseGraph::mouseMove (const juce::MouseEvent& e)
{
    const float hx = xOf (bassLimit > 0 ? bassLimit : 20.f);
    setMouseCursor (std::abs ((float) e.x - hx) < 9.f && plotArea().contains (e.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                                                           : juce::MouseCursor::NormalCursor);
}

void ResponseGraph::mouseDown (const juce::MouseEvent& e)
{
    bool* flags[5] = { &showTarget, &showMeasured, &showEq, &showResult, &showLimit };
    for (int i = 0; i < 5; ++i)
        if (legendHit[i].contains (e.getPosition())) { *flags[i] = ! *flags[i]; repaint(); return; }
    const float hx = xOf (bassLimit > 0 ? bassLimit : 20.f);
    if (std::abs ((float) e.x - hx) < 9.f && plotArea().contains (e.position))
    {
        dragging = true;
        if (onDragState) onDragState (true);
    }
}

void ResponseGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    auto p = plotArea();
    float hz = 20.f * std::pow (10.f, (e.position.x - p.getX()) / p.getWidth() * 3.f);
    hz = hz < 24.f ? 0.f : std::round (juce::jmin (hz, 200.f));
    bassLimit = hz;
    if (onBassLimit) onBassLimit (hz);
    repaint();
}

void ResponseGraph::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (onDragState) onDragState (false);
}

// ------------------------------------------------------------------------------------------------ level meter
void LevelMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    const int gap = 3;
    const int nBars = mono ? 1 : 2;
    const float bw = ((float) getWidth() - (float) gap) / 2.f;                       // one channel's width, also in mono
    const float x0 = mono ? ((float) getWidth() - bw) / 2.f : 0.f;                    // mono: that single bar is centred
    for (int c = 0; c < nBars; ++c)
    {
        auto bar = juce::Rectangle<float> (x0 + (bw + gap) * (float) c, 0.f, bw, (float) getHeight());
        g.setColour (divider); g.fillRoundedRectangle (bar, 2.f);
        const float db = juce::Decibels::gainToDecibels (mono ? juce::jmax (src[0].load(), src[1].load()) : src[(size_t) c].load(), -80.f);
        const float pos = juce::jlimit (0.f, 1.f, (db + 60.f) / 63.f);
        held[c] = juce::jmax (pos, held[c] - 0.006f);
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * pos);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffef4444), 0.f, bar.getY(),
                           accent, 0.f, bar.getY() + bar.getHeight() * 0.25f, false));
        g.fillRoundedRectangle (fill, 2.f);
        if (held[c] > 0.01f) { g.setColour (text.withAlpha (0.8f)); g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * held[c], bw, 1.5f); }
    }
}

// ------------------------------------------------------------------------------------------------ status item
void StatusItem::paint (juce::Graphics& g)
{
    using namespace theme;
    if (hover && canClick) { g.setColour (juce::Colour (0xff232323)); g.fillRect (getLocalBounds()); }
    g.setColour (divider); g.drawVerticalLine (0, 10.f, (float) getHeight() - 10.f);
    g.setColour (dim); g.setFont (juce::FontOptions (12.f));
    g.drawText (label, 14, 10, getWidth() - 20, 16, juce::Justification::centredLeft);
    g.setColour (text); g.setFont (juce::FontOptions (13.f, juce::Font::bold));
    g.drawText (value, 14, 30, getWidth() - (canClick ? 34 : 20), 18, juce::Justification::centredLeft);
    if (canClick)   // up chevron
    {
        juce::Path p; const float x = (float) getWidth() - 16.f, y = 40.f;
        p.startNewSubPath (x - 4.f, y + 2.f); p.lineTo (x, y - 2.f); p.lineTo (x + 4.f, y + 2.f);
        g.setColour (dim); g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}

// ------------------------------------------------------------------------------------------------ profile list
ProfileList::ProfileList (FlatQProcessor& p) : proc (p)
{
    box.setRowHeight (40);
    box.setOutlineThickness (0);
    addAndMakeVisible (box);
    refresh();
}

void ProfileList::refresh()
{
    entries.clear();
    bool currentFound = false;
    const auto cur = proc.getProfilePath();
    for (auto& f : FlatQProcessor::userProfileFiles())
    {
        Entry e; e.name = f.getFileNameWithoutExtension(); e.file = f; e.kind = FlatQProcessor::profileKind (f);
        currentFound |= f.getFullPathName() == cur;
        entries.push_back (e);
    }
    if (cur.isNotEmpty() && ! currentFound && juce::File (cur).existsAsFile())   // loaded from somewhere else (e.g. an old session): show it too
    {
        Entry e; e.name = juce::File (cur).getFileNameWithoutExtension(); e.file = juce::File (cur); e.external = true; e.kind = FlatQProcessor::profileKind (e.file);
        entries.insert (entries.begin(), e);
    }
    box.updateContent();
    box.repaint();
    syncSelection();
}

void ProfileList::syncSelection()
{
    const int bi = proc.getBuiltInIndex();
    const auto cur = proc.getProfilePath();
    int sel = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if ((bi >= 0 && entries[(size_t) i].builtIn == bi) || (bi < 0 && cur.isNotEmpty() && entries[(size_t) i].file.getFullPathName() == cur)) sel = i;
    if (sel >= 0) box.selectRow (sel, true, true); else box.deselectAllRows();
}

void ProfileList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    using namespace theme;
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& e = entries[(size_t) row];
    if (selected) { g.setColour (accent.withAlpha (0.14f)); g.fillRect (0, 0, w, h); g.setColour (accent); g.fillRect (0, 0, 3, h); }
    g.setColour (selected ? text : text.withAlpha (0.85f)); g.setFont (juce::FontOptions (12.5f));
    drawKindIcon (g, juce::Rectangle<float> (14.f, ((float) h - 22.f) / 2.f, 22.f, 22.f), e.kind, selected ? accent : dim);
    g.drawFittedText (e.name, 46, 2, w - 84, h - 4, juce::Justification::centredLeft, 2, 0.9f);

    g.setColour (selected ? text : dim);                       // three dots
    for (int i = -1; i <= 1; ++i) g.fillEllipse ((float) w - 16.f - 1.5f, (float) h * 0.5f + (float) i * 5.f - 1.5f, 3.f, 3.f);
    g.setColour (divider); g.drawHorizontalLine (h - 1, 0.f, (float) w);
}

void ProfileList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) entries.size()) return;
    if (e.mods.isPopupMenu() || e.x >= getWidth() - 30) { showMenu (row, e.getScreenPosition()); return; }
    const auto& en = entries[(size_t) row];
    juce::String err;
    const bool ok = proc.loadProfile (en.file, err);
    if (! ok) report (false, err);
    syncSelection();
}

void ProfileList::report (bool ok, const juce::String& err)
{
    if (! ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Profile", err);
}

void ProfileList::showMenu (int row, juce::Point<int> screenPos)
{
    const auto e = entries[(size_t) row];
    const bool editable = e.builtIn < 0 && ! e.external;
    juce::PopupMenu m;
    m.addSectionHeader (e.name);
    m.addItem (1, "Rename...", editable);
    m.addItem (2, "Duplicate");
    m.addItem (4, "Export...");
    m.addItem (3, "Delete", editable);
    if (! editable) m.addSectionHeader (e.builtIn >= 0 ? "Built-in: duplicate it to edit" : "Outside the library: duplicate it to edit");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)).withParentComponent (getTopLevelComponent()),
        [this, e] (int r)
        {
            syncSelection();   // the row under the three dots was highlighted by the click: show the loaded profile again
            juce::String err;
            if (r == 1) askRename (e);
            else if (r == 2) { report (proc.duplicateProfile (e.builtIn, e.file, err), err); refresh(); }
            else if (r == 3) askDelete (e);
            else if (r == 4) exportProfileDialog (proc, e.file, chooser);
        });
}

void ProfileList::askRename (Entry e)
{
    auto* w = new juce::AlertWindow ("Rename profile", "New name for \"" + e.name + "\":", juce::MessageBoxIconType::NoIcon, getTopLevelComponent());
    w->addTextEditor ("name", e.name);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, e] (int r)
    {
        if (r != 1) return;
        juce::String err;
        report (proc.renameProfile (e.file, w->getTextEditorContents ("name"), err), err);
        refresh();
    }), true);
}

void ProfileList::askDelete (Entry e)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete profile",
        "Delete \"" + e.name + "\"? The file goes to the Trash.", "Delete", "Cancel", getTopLevelComponent(),
        juce::ModalCallbackFunction::create ([this, e] (int r)
        {
            if (r == 0) return;
            juce::String err;
            report (proc.deleteProfile (e.file, err), err);
            refresh();
        }));
}

// ------------------------------------------------------------------------------------------------ popup panels
namespace
{
    // A small stack of labelled parameter controls shown in a call-out bubble.
    class ParamPanel : public juce::Component
    {
    public:
        explicit ParamPanel (juce::AudioProcessorValueTreeState& s, int width = 380) : apvts (s), w (width) {}

        void addSlider (const juce::String& label, const char* id, const juce::String& suffix = {}, const juce::String& tip = {})
        {
            auto* l = labels.add (new juce::Label ({}, label)); addAndMakeVisible (l);
            auto* sl = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
            sl->setTextValueSuffix (suffix);
            sl->setTooltip (tip); l->setTooltip (tip);
            addAndMakeVisible (sl);
            attachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, id, *sl));
            rows.add ({ l, sl });
        }
        void addCombo (const juce::String& label, const char* id, const juce::String& tip = {})
        {
            auto* l = labels.add (new juce::Label ({}, label)); addAndMakeVisible (l);
            auto* cb = combos.add (new juce::ComboBox());
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id))) cb->addItemList (p->choices, 1);
            cb->setTooltip (tip); l->setTooltip (tip);
            addAndMakeVisible (cb);
            comboAttachments.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (apvts, id, *cb));
            rows.add ({ l, cb });
        }
        void addToggle (const juce::String& text, const char* id, const juce::String& tip = {})
        {
            auto* t = toggles.add (new juce::ToggleButton (text)); addAndMakeVisible (t);
            t->setTooltip (tip);
            buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, id, *t));
            rows.add ({ nullptr, t });
        }
        void finish() { setSize (w, 14 + 32 * rows.size() + 6); }
        void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14, 10);
            for (auto& row : rows)
            {
                auto line = r.removeFromTop (32);
                if (row.label) { row.label->setBounds (line.removeFromLeft (130)); row.control->setBounds (line); }
                else row.control->setBounds (line);
            }
        }
    private:
        struct Row { juce::Label* label; juce::Component* control; };
        juce::AudioProcessorValueTreeState& apvts;
        int w;
        juce::Array<Row> rows;
        juce::OwnedArray<juce::Label> labels;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::ComboBox> combos;
        juce::OwnedArray<juce::ToggleButton> toggles;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    };

    // Advanced: one vertical slider per third-octave band showing the CURRENT equalization (profile + smoothing/limits).
    // Dragging a band stores the difference to the designed value as a manual offset. L and R can be edited separately.
    class ManualEqPanel : public juce::Component, private juce::Timer
    {
    public:
        explicit ManualEqPanel (FlatQProcessor& p, std::function<void()> saveAs) : proc (p), onSaveAs (std::move (saveAs))
        {
            const auto& fr = flatq::manualFreqs();
            for (size_t i = 0; i < fr.size(); ++i)
            {
                auto* s = sliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
                s->setRange (-18.0, 18.0, 0.1);
                s->setDoubleClickReturnValue (true, 0.0);   // replaced below: double-click returns to the designed value
                s->getProperties().set ("bipolar", true);   // fill from the 0 dB line
                s->onValueChange = [this, i, s]
                {
                    if (updating) return;
                    proc.setManualBand (ch, (int) i, (float) (s->getValue() - base (ch, i)));
                    readout.setText (freqText (flatq::manualFreqs()[i]) + ":  " + juce::String (s->getValue(), 1) + " dB   (designed " + juce::String (base (ch, i), 1) + ")", juce::dontSendNotification);
                };
                s->addMouseListener (this, false);
                addAndMakeVisible (s);
            }
            title.setFont (juce::FontOptions (15.f, juce::Font::bold));
            readout.setColour (juce::Label::textColourId, theme::dim);
            link.setToggleState (proc.isManualLinked(), juce::dontSendNotification);
            link.onClick = [this]
            {
                proc.setManualLinked (link.getToggleState());
                ch = 0; chL.setToggleState (true, juce::dontSendNotification); updateChannelUi(); refresh (true);
            };
            for (auto* b : { &chL, &chR }) { b->setClickingTogglesState (true); b->setRadioGroupId (41); b->setColour (juce::TextButton::buttonOnColourId, theme::accent); b->setColour (juce::TextButton::textColourOnId, theme::bg); addChildComponent (b); }
            chL.setToggleState (true, juce::dontSendNotification);
            chL.onClick = [this] { ch = 0; refresh (true); };
            chR.onClick = [this] { ch = 1; refresh (true); };
            reset.setTooltip ("Discard the manual changes");
            saveBtn.onClick = [this] { if (onSaveAs) onSaveAs(); };
            addAndMakeVisible (saveBtn);
            reset.onClick = [this] { proc.resetManual(); refresh (true); readout.setText ("Back to the profile's equalization", juce::dontSendNotification); };
            for (auto* c : std::initializer_list<juce::Component*> { &title, &readout, &link, &reset }) addAndMakeVisible (c);
            updateChannelUi();
            graph = proc.getGraph(); lastVersion = proc.getGraphVersion();
            setSize (40 + 25 * (int) fr.size(), 320);
            refresh (true);
            startTimerHz (10);
        }
        static juce::String bandLabel (double f)   // 20, 31.5, 100, 1.25k, 10k ...
        {
            if (f < 1000) return juce::String (f);
            return juce::String (f / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k";
        }
        static juce::String freqText (double f)
        {
            if (f < 1000) return juce::String (f) + " Hz";
            return juce::String (f / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override   // double-click on a slider: back to the designed value
        {
            for (int i = 0; i < sliders.size(); ++i)
                if (e.eventComponent == sliders[i]) { proc.setManualBand (ch, i, 0.f); refresh (true); }
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel);
            g.setFont (juce::FontOptions (9.f)); g.setColour (theme::dim);
            for (int i = 0; i < sliders.size(); ++i)
            {
                const double f = flatq::manualFreqs()[(size_t) i];
                g.drawText (bandLabel (f),
                            sliders[i]->getX() - 4, getHeight() - 20, sliders[i]->getWidth() + 8, 12, juce::Justification::centred);
            }
            for (int db : { 18, 12, 6, 0, -6, -12, -18 })
            {
                const float y = (float) sliderTop + 6.f + (float) (sliderH - 12) * (18.f - (float) db) / 36.f;
                g.setColour (db == 0 ? juce::Colour (0xff4a4a4a) : theme::divider); g.drawHorizontalLine ((int) y, 40.f, (float) getWidth() - 10.f);
                g.setColour (theme::dim); g.drawText ((db > 0 ? "+" : "") + juce::String (db), 4, (int) y - 5, 32, 10, juce::Justification::centredRight);
            }
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (28);
            title.setBounds (top.removeFromLeft (200));
            link.setBounds (top.removeFromLeft (100));
            chL.setBounds (top.removeFromLeft (34).reduced (2, 0)); chR.setBounds (top.removeFromLeft (34).reduced (2, 0));
            reset.setBounds (top.removeFromRight (90)); top.removeFromRight (6);
            saveBtn.setBounds (top.removeFromRight (110));
            readout.setBounds (top.reduced (8, 0));
            r.removeFromBottom (18);
            sliderTop = r.getY(); sliderH = r.getHeight();
            r.removeFromLeft (30);
            const int w = r.getWidth() / sliders.size();
            for (auto* s : sliders) s->setBounds (r.removeFromLeft (w));
        }
    private:
        float base (int c, size_t i) const { return graph.bandBase[(size_t) c][i]; }
        void updateChannelUi()
        {
            chL.setVisible (! proc.isManualLinked()); chR.setVisible (! proc.isManualLinked());
            const auto col = proc.isManualLinked() ? theme::accent : (ch == 0 ? theme::chanL : theme::chanR);
            for (auto* s : sliders) { s->setColour (juce::Slider::thumbColourId, col); s->setColour (juce::Slider::trackColourId, col); }
            title.setText (proc.isManualLinked() ? "Manual EQ (L + R)" : ch == 0 ? "Manual EQ (Left)" : "Manual EQ (Right)", juce::dontSendNotification);
        }
        void refresh (bool force)
        {
            updateChannelUi();
            updating = true;
            for (int i = 0; i < sliders.size(); ++i)
                if (force || ! sliders[i]->isMouseButtonDown())
                    sliders[i]->setValue (base (ch, (size_t) i) + proc.getManualBand (ch, i), juce::dontSendNotification);
            updating = false;
            if (force && readout.getText().isEmpty()) readout.setText ((graph.name.isEmpty() ? juce::String ("No profile") : graph.name) + "  -  dB before preamp", juce::dontSendNotification);
        }
        void timerCallback() override
        {
            const int v = proc.getGraphVersion();
            if (v == lastVersion) return;
            lastVersion = v; graph = proc.getGraph();
            readout.setText ((graph.name.isEmpty() ? juce::String ("No profile") : graph.name) + "  -  dB before preamp", juce::dontSendNotification);
            refresh (false);
        }
        FlatQProcessor& proc;
        GraphData graph;
        juce::OwnedArray<juce::Slider> sliders;
        juce::Label readout, title;
        juce::ToggleButton link { "Link L / R" };
        juce::TextButton chL { "L" }, chR { "R" }, reset { "Reset" }, saveBtn { "Save as..." };
        std::function<void()> onSaveAs;
        int ch = 0, sliderTop = 0, sliderH = 0, lastVersion = -1;
        bool updating = false;
    };

    // About FlatQ: shown when the logo / wordmark is clicked.
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel()
        {
            addAndMakeVisible (mark);
            title.setFont (juce::FontOptions (26.f, juce::Font::bold)); title.setText ("FlatQ", juce::dontSendNotification);
            sub.setText ("by BandPass Records  -  version " + juce::String (JucePlugin_VersionString), juce::dontSendNotification);
            sub.setColour (juce::Label::textColourId, theme::dim);
            credits.setText ("Built with JUCE. Equalization method and measurement data from AutoEq (MIT) and the original measurement "
                             "authors; the measurements are for personal use only.", juce::dontSendNotification);
            credits.setFont (juce::FontOptions (11.f)); credits.setColour (juce::Label::textColourId, theme::dim);
            credits.setJustificationType (juce::Justification::topLeft);
            link.setButtonText ("flatq.bandpassrecords.com");
            link.setURL (juce::URL ("https://flatq.bandpassrecords.com"));
            link.setFont (juce::FontOptions (13.f), false);
            link.setColour (juce::HyperlinkButton::textColourId, theme::accent);
            for (auto* c : std::initializer_list<juce::Component*> { &title, &sub, &credits, &link }) addAndMakeVisible (c);
            setSize (420, 190);
        }
        void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (20, 18);
            auto top = r.removeFromTop (56);
            mark.setBounds (top.removeFromLeft (56).withSizeKeepingCentre (52, 52)); top.removeFromLeft (14);
            title.setBounds (top.removeFromTop (32)); sub.setBounds (top);
            r.removeFromTop (14);
            link.setBounds (r.removeFromTop (24).withWidth (200));
            r.removeFromTop (6);
            credits.setBounds (r);
        }
    private:
        LogoMark mark;
        juce::Label title, sub, credits;
        juce::HyperlinkButton link;
    };

    // Settings: a few persisted preferences (shared by every instance) and shortcuts to the data folders.
    class SettingsPanel : public juce::Component
    {
    public:
        explicit SettingsPanel (juce::AudioProcessorValueTreeState& apvts, bool runsAsPlugin, std::function<void (bool)> tooltipsChanged)
            : onTooltips (std::move (tooltipsChanged)), showSkip (runsAsPlugin)
        {
            loadLast.setButtonText ("Load the last used profile on start");
            loadLast.setToggleState (FlatQProcessor::getSetting ("loadLastProfile", true), juce::dontSendNotification);
            loadLast.onClick = [this] { FlatQProcessor::setSetting ("loadLastProfile", loadLast.getToggleState()); };
            tips.setButtonText ("Show tooltips");
            tips.setToggleState (FlatQProcessor::getSetting ("tooltips", true), juce::dontSendNotification);
            tips.onClick = [this] { FlatQProcessor::setSetting ("tooltips", tips.getToggleState()); if (onTooltips) onTooltips (tips.getToggleState()); };
            skip.setButtonText ("Skip on offline export");
            skip.setTooltip ("Pass the audio through untouched while the host renders offline (e.g. Export Audio Mixdown)");
            skipA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypassexport", skip);
            profilesBtn.setButtonText ("Open profiles folder");
            profilesBtn.onClick = [] { FlatQProcessor::userProfilesDir().createDirectory(); FlatQProcessor::userProfilesDir().revealToUser(); };
            dataBtn.setButtonText ("Open settings folder");
            dataBtn.onClick = [] { FlatQProcessor::appDataDir().createDirectory(); FlatQProcessor::appDataDir().revealToUser(); };
            for (auto* c : std::initializer_list<juce::Component*> { &heading, &loadLast, &tips, &profilesBtn, &dataBtn }) addAndMakeVisible (c);
            if (showSkip) addAndMakeVisible (skip);   // offline rendering only exists inside a host, so the standalone app does not show it
            heading.setText ("Settings", juce::dontSendNotification); heading.setFont (juce::FontOptions (16.f, juce::Font::bold));
            setSize (380, showSkip ? 244 : 214);
        }
        void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 12);
            heading.setBounds (r.removeFromTop (28));
            loadLast.setBounds (r.removeFromTop (30));
            tips.setBounds (r.removeFromTop (30));
            if (showSkip) skip.setBounds (r.removeFromTop (30));
            r.removeFromTop (8);
            profilesBtn.setBounds (r.removeFromTop (32)); r.removeFromTop (8);
            dataBtn.setBounds (r.removeFromTop (32));
        }
    private:
        std::function<void (bool)> onTooltips;
        bool showSkip;
        juce::Label heading;
        juce::ToggleButton loadLast, tips, skip;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> skipA;
        juce::TextButton profilesBtn, dataBtn;
    };

    juce::String smoothName (int i)
    {
        static const char* n[] = { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct", "1/2 oct", "1 oct" };
        return n[juce::jlimit (0, 6, i)];
    }
}

// ------------------------------------------------------------------------------------------------ add-profile screen
AddProfileScreen::AddProfileScreen (FlatQProcessor& p) : proc (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    auto& db = MeasurementDb::get();

    title.setFont (juce::FontOptions (22.f, juce::Font::bold));
    otherTitle.setFont (juce::FontOptions (16.f, juce::Font::bold));
    for (auto* l : { &brandL, &formL, &targetL }) { l->setFont (juce::FontOptions (11.f)); l->setColour (juce::Label::textColourId, theme::dim); }
    resultsInfo.setFont (juce::FontOptions (12.f)); resultsInfo.setColour (juce::Label::textColourId, theme::dim);

    {   // brands grouped by their first letter, in sub-menus (hundreds of brands in one flat list would be unusable)
        auto& root = *brandPick.getRootMenu();
        root.addItem (1, "All brands");
        std::map<juce::String, juce::PopupMenu> groups;
        int id = 2;
        for (auto& b : db.brands())
        {
            const auto c = b.name.substring (0, 1).toUpperCase();
            groups[juce::CharacterFunctions::isLetter (c[0]) ? c : juce::String ("0-9")].addItem (id++, b.name + "  (" + juce::String (b.count) + ")");
        }
        for (auto& g : groups) root.addSubMenu (g.first, g.second);
        brandPick.setSelectedId (1, juce::dontSendNotification);
        brandPick.onChange = [this]
        {
            const int sel = brandPick.getSelectedId();
            selectedBrand = sel >= 2 ? MeasurementDb::get().brands()[(size_t) sel - 2].name : juce::String();
            updateSearch();
        };
    }
    formFilter.addItemList ({ "All", "Over-ear", "In-ear", "Earbud" }, 1);
    formFilter.setSelectedId (1, juce::dontSendNotification);
    formFilter.onChange = [this] { updateSearch(); };
    targetPick.addItem ("Auto", 1);
    for (int i = 0; i < FlatQProcessor::numTargets(); ++i) targetPick.addItem (FlatQProcessor::targetName (i), i + 2);
    targetPick.setSelectedId (1, juce::dontSendNotification);

    searchBox.setTextToShowWhenEmpty ("Search by name, brand or source", theme::dim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff262626));
    searchBox.setColour (juce::TextEditor::textColourId, theme::text);
    searchBox.setColour (juce::TextEditor::outlineColourId, theme::divider);
    searchBox.setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
    searchBox.setColour (juce::TextEditor::highlightedTextColourId, theme::text);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    searchBox.setFont (juce::FontOptions (15.f));
    searchBox.setJustification (juce::Justification::centredLeft);
    searchBox.setIndents (10, 0);
    searchBox.setBorder ({ 0, 0, 0, 0 });
    searchBox.onTextChange = [this] { updateSearch(); };
    searchBox.onEscapeKey = [this] { if (onClose) onClose(); };

    results.onPick = [this] (int idx) { pickMeasurement (idx); };

    back.onClick = [this] { if (onClose) onClose(); };
    loadFile.onClick = [this] { chooseProfileFile(); };
    importCsv.onClick = [this] { chooseCsv(); };
    for (auto* b : { &loadFile, &importCsv }) { b->setColour (juce::TextButton::buttonColourId, theme::accent); b->setColour (juce::TextButton::textColourOffId, theme::bg); }

    targetPick.setTooltip ("The curve the correction aims for. Auto = Harman over-ear 2018 for over-ear, Harman in-ear 2019 for in-ear. Not used by finished AutoEq results.");

    for (auto* c : std::initializer_list<juce::Component*> { &back, &title, &otherTitle, &brandL, &formL, &targetL, &resultsInfo, &searchBox,
                                                              &brandPick, &formFilter, &targetPick, &results, &loadFile, &importCsv })
        addAndMakeVisible (c);
    const bool has = db.available();   // without the pack there is nothing to search: only the two import cards remain
    for (auto* c : std::initializer_list<juce::Component*> { &brandL, &formL, &targetL, &resultsInfo, &searchBox, &brandPick, &formFilter, &targetPick, &results })
        c->setVisible (has);
    results.setVisible (false);
}

void AddProfileScreen::open()
{
    searchBox.clear();
    brandPick.setSelectedId (1, juce::dontSendNotification); selectedBrand = {};
    updateSearch();
    setVisible (true);
    toFront (true);
    if (MeasurementDb::get().available()) searchBox.grabKeyboardFocus(); else grabKeyboardFocus();
}

bool AddProfileScreen::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void AddProfileScreen::report (bool ok, const juce::String& what, const juce::String& err)
{
    if (! ok) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, what, err);
}

void AddProfileScreen::updateSearch()
{
    searching = MeasurementDb::get().available() && (searchBox.getText().trim().length() >= 2 || selectedBrand.isNotEmpty());
    results.setVisible (searching);
    if (searching)
    {
        static const char* forms[] = { "", "over-ear", "in-ear", "earbud" };
        auto hits = MeasurementDb::get().search (searchBox.getText(), forms[juce::jlimit (0, 3, formFilter.getSelectedId() - 1)], selectedBrand, 500);
        resultsInfo.setText (hits.empty() ? "No measurements found" : juce::String (hits.size()) + (hits.size() >= 500 ? "+ results (refine the search)" : " results"), juce::dontSendNotification);
        results.setHits (std::move (hits));
    }
    else resultsInfo.setText ({}, juce::dontSendNotification);
    repaint();
}

void AddProfileScreen::pickMeasurement (int idx)
{
    juce::String err;
    const bool ok = proc.importMeasurement (idx, targetPick.getSelectedId() - 2, err);
    report (ok, "Could not import", err);
    if (ok && onAdded) onAdded();
}

void AddProfileScreen::chooseProfileFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Select a profile", juce::File(), "*.flatq;*.tmreq");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String err;
            const bool ok = proc.importProfileFile (f, err);   // converted and saved in the library, then loaded
            report (ok, "Could not load profile", err);
            if (ok && onAdded) onAdded();
        });
}

void AddProfileScreen::chooseCsv()
{
    auto start = juce::File ("~/workspace/github/AutoEq-4.1.2/measurements");
    chooser = std::make_unique<juce::FileChooser> ("Select a measurement (.csv)", start.isDirectory() ? start : juce::File(), "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) importCsvFlow (f); });
}

void AddProfileScreen::importCsvFlow (const juce::File& measurement)
{
    // target menu: bundled AutoEq targets (Harman first) + any CSV file
    juce::PopupMenu m;
    m.addSectionHeader ("Target for " + measurement.getFileNameWithoutExtension());
    juce::Array<int> order;
    for (int i = 0; i < FlatQProcessor::numTargets(); ++i) order.add (i);
    std::stable_sort (order.begin(), order.end(), [] (int a, int b) {
        const bool ha = FlatQProcessor::targetName (a).startsWithIgnoreCase ("Harman"), hb = FlatQProcessor::targetName (b).startsWithIgnoreCase ("Harman");
        return ha != hb ? ha : false; });
    for (int i : order) m.addItem (i + 2, FlatQProcessor::targetName (i));
    m.addSeparator();
    m.addItem (1, "Choose a target CSV file...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&importCsv).withParentComponent (this).withMinimumWidth (260),
        [this, measurement] (int r)
        {
            if (r <= 0) return;
            auto run = [this, measurement] (const juce::String& csv, const juce::String& label)
            {
                juce::String err;
                const bool ok = proc.importAutoEq (measurement, csv, label, err);
                report (ok, "Could not import", err);
                if (ok && onAdded) onAdded();
            };
            if (r >= 2) { run (FlatQProcessor::targetCsvText (r - 2), FlatQProcessor::targetName (r - 2)); return; }
            chooser = std::make_unique<juce::FileChooser> ("Select a target (.csv)", measurement.getParentDirectory(), "*.csv");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [run] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) run (f.loadFileAsString(), f.getFileNameWithoutExtension()); });
        });
}

void AddProfileScreen::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (64);
    back.setBounds (top.removeFromLeft (112).reduced (16, 16));
    title.setBounds (top.reduced (8, 0));

    auto body = r.reduced (32, 24);
    const int rightW = juce::jlimit (280, 360, body.getWidth() / 3);
    auto right = body.removeFromRight (rightW);
    body.removeFromRight (32);
    auto left = body;

    searchBox.setBounds (left.removeFromTop (42)); left.removeFromTop (12);
    auto fr = left.removeFromTop (52);
    const int cw = (fr.getWidth() - 20) / 3;
    for (auto pr : { std::pair<juce::Label*, juce::Component*> { &brandL, &brandPick }, { &formL, &formFilter }, { &targetL, &targetPick } })
    {
        auto col = fr.removeFromLeft (cw); fr.removeFromLeft (10);
        pr.first->setBounds (col.removeFromTop (18).withTrimmedLeft (2));
        pr.second->setBounds (col);
    }
    left.removeFromTop (8);
    resultsInfo.setBounds (left.removeFromTop (22).withTrimmedLeft (2));
    resultsRect = left;
    results.setBounds (left);

    otherTitle.setBounds (right.removeFromTop (28)); right.removeFromTop (8);
    cardA = right.removeFromTop (150); right.removeFromTop (16);
    cardB = right.removeFromTop (150);
    loadFile.setBounds (cardA.reduced (16).removeFromBottom (34));
    importCsv.setBounds (cardB.reduced (16).removeFromBottom (34));
}

void AddProfileScreen::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg);
    g.setColour (panel); g.fillRect (0, 0, getWidth(), 64);
    g.setColour (divider); g.drawHorizontalLine (63, 0.f, (float) getWidth());

    auto card = [&] (juce::Rectangle<int> r, const juce::String& head, const juce::String& desc)
    {
        g.setColour (panel); g.fillRoundedRectangle (r.toFloat(), 10.f);
        g.setColour (divider); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.f, 1.f);
        auto in = r.reduced (16);
        g.setColour (text); g.setFont (juce::FontOptions (14.f, juce::Font::bold));
        g.drawText (head, in.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (dim); g.setFont (juce::FontOptions (12.f));
        g.drawFittedText (desc, in.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);   // 1.0: wrap, never squash the glyphs
    };
    card (cardA, "Profile file", "Load a profile from disk: a FlatQ profile (.flatq) or an RME TotalMix profile (.tmreq).");
    card (cardB, "Measurement CSV", "Import your own measurement (a CSV with frequency and raw columns, like the AutoEq ones). You choose the target next.");

    g.setColour (dim); g.setFont (juce::FontOptions (13.f));
    if (! MeasurementDb::get().available())
        g.drawFittedText ("The measurement database is not part of this build.\nUse a profile file or your own CSV.", getLocalBounds().withTrimmedRight (400).reduced (32, 80), juce::Justification::topLeft, 4, 1.0f);
    else if (! searching)
        g.drawFittedText (juce::String (MeasurementDb::get().size()) + " measurements and profiles from AutoEq and others.\nType a name above, or pick a brand, to list them.",
                          resultsRect.reduced (2, 6), juce::Justification::topLeft, 4, 1.0f);
}

// ------------------------------------------------------------------------------------------------ editor
FlatQEditor::FlatQEditor (FlatQProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);
    for (auto* c : std::initializer_list<juce::Component*> { &graph, &profiles, &addButton, &title, &subtitle, &kindIcon, &inClip, &outClip, &logoMark, &logo, &brand, &inL, &outL, &trimL,
                                                              &inMeter, &outMeter, &trim, &volumeIcon, &swap, &bypass, &dryWet, &dryWetL, &notices, &aboutArea, &menuDots, &sidebarBtn, &mono, &latency, &filter, &length, &smoothing, &limits, &preamp, &manual })
        addAndMakeVisible (c);

    logo.setFont (juce::FontOptions (26.f, juce::Font::bold));
    brand.setFont (juce::FontOptions (11.f)); brand.setColour (juce::Label::textColourId, theme::dim);
    title.setFont (juce::FontOptions (24.f)); title.setMinimumHorizontalScale (0.7f);
    subtitle.setFont (juce::FontOptions (13.f)); subtitle.setColour (juce::Label::textColourId, theme::dim);
    for (auto* l : { &inL, &outL, &trimL }) { l->setJustificationType (juce::Justification::centred); l->setColour (juce::Label::textColourId, theme::dim); l->setFont (juce::FontOptions (12.f)); }
    trim.setTooltip ("Output volume. The red zone is above this profile\'s safe limit. Double-click to reset.");
    addButton.onClick = [this] { openAddScreen(); };
    saveAsBtn.onClick = [this] { saveAsFlow(); };
    addChildComponent (saveAsBtn);
    addScreen.onClose = [this] { addScreen.setVisible (false); };
    addScreen.onAdded = [this] { profiles.refresh(); addScreen.setVisible (false); };
    addChildComponent (addScreen);   // last child = on top of everything


    dryWet.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    dryWet.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    dryWetL.setColour (juce::Label::textColourId, theme::dim); dryWetL.setJustificationType (juce::Justification::centredRight);
    dryWetA = std::make_unique<SA> (p.apvts, "drywet", dryWet);
    for (auto* l : { &logo, &brand }) l->setInterceptsMouseClicks (false, false);   // clicks go to the About area on top
    logoMark.setInterceptsMouseClicks (false, false);
    aboutArea.setTooltip ("About FlatQ");
    aboutArea.onClick = [this] { if (! consumeJustClosed (aboutArea)) showAbout(); };   // a second click closes it instead of reopening
    sidebarVisible = FlatQProcessor::getSetting ("sidebarVisible", true);
    sidebarBtn.barVisible = sidebarVisible;
    sidebarBtn.setTooltip ("Hide or show the left bar");
    sidebarBtn.onClick = [this]
    {
        sidebarVisible = ! sidebarVisible;
        sidebarBtn.barVisible = sidebarVisible;
        FlatQProcessor::setSetting ("sidebarVisible", sidebarVisible);
        applySidebarVisibility();
    };
    menuDots.setTooltip ("Menu");
    menuDots.onClick = [this] { if (! consumeJustClosed (menuDots)) showMenu(); };
    tooltipWindow.setMillisecondsBeforeTipAppears (FlatQProcessor::getSetting ("tooltips", true) ? 450 : 100000000);
    notices.setVisible (false);
    volumeIcon.setTooltip ("Mute / unmute the output");
    swap.setTooltip ("Swap the left and right input channels");
    muteA = std::make_unique<BA> (p.apvts, "mute", volumeIcon);
    swapA = std::make_unique<BA> (p.apvts, "swap", swap);
    trim.setRange (-78.0, 6.0, 0.1);   // fixed total-dB scale: the profile moves the limit, so the red zone size is dynamic
    trim.onDragStart = [this] { if (auto* prm = proc.apvts.getParameter ("trim")) prm->beginChangeGesture(); };
    trim.onDragEnd   = [this] { if (auto* prm = proc.apvts.getParameter ("trim")) prm->endChangeGesture(); };
    trim.onValueChange = [this]
    {
        if (faderSync) return;
        double v = trim.getValue();
        if (trim.hasLimit && v > trim.limit) { v = trim.limit; faderSync = true; trim.setValue (v, juce::dontSendNotification); faderSync = false; }
        if (auto* prm = proc.apvts.getParameter ("trim")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (v - lastPreamp)));
        trim.repaint();
    };
    bypassA = std::make_unique<BA> (p.apvts, "bypass", bypass);
    mono.setTooltip ("Mono check: sums left and right");
    monoA = std::make_unique<BA> (p.apvts, "mono", mono);

    graph.onDragState = [this] (bool begin)
    {
        if (auto* prm = proc.apvts.getParameter ("basslimit")) { if (begin) prm->beginChangeGesture(); else prm->endChangeGesture(); }
    };
    graph.onBassLimit = [this] (float hz)
    {
        if (auto* prm = proc.apvts.getParameter ("basslimit")) prm->setValueNotifyingHost (prm->convertTo0to1 (hz > 0.f ? hz : 20.f));   // 0 from the graph = off = bottom of the range
    };

    filter.onClick = [this] { showFilterMenu(); };
    length.onClick = [this]
    {
        juce::PopupMenu m;
        const int cur = choiceIndex ("taps");
        const char* names[] = { "4096 taps", "8192 taps", "16384 taps", "32768 taps  (finer bass)", "65536 taps  (finest, slow to design)" };
        for (int i = 0; i < 5; ++i) m.addItem (i + 1, names[i], true, i == cur);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&length).withParentComponent (this).withMinimumWidth (160),
                         [this] (int r) { markClosed (length); if (r > 0) setChoice ("taps", r - 1); });
    };
    smoothing.onClick = [this]
    {
        auto panel = std::make_unique<ParamPanel> (proc.apvts);
        panel->addCombo ("Smoothing", "smooth", "Fractional-octave smoothing of the correction. Wider = gentler, less sensitive to narrow peaks and dips in the measurement.");
        panel->addCombo ("Treble smoothing", "treblesmooth", "Smoothing width used above about 6 kHz (blended in from 1.5 kHz). \"Same\" uses the normal width. AutoEq uses 2 octaves here.");
        panel->finish();
        showPanel (std::move (panel), smoothing);
    };
    limits.onClick = [this]
    {
        auto panel = std::make_unique<ParamPanel> (proc.apvts, 420);
        panel->addToggle ("Respect max boost", "clamp", "Limit boosts to the global maximum and the profile's own ceiling");
        panel->addSlider ("Max boost", "maxboost", " dB", "Global limit for boosts. The profile's own ceiling (dashed line) also applies.");
        panel->addSlider ("Max slope", "maxslope", {}, "Limits how fast the BOOSTS may rise, in dB per octave: the further left, the stricter (the maximum is off). Narrow boosts, usually in the treble, are cut first; cuts are never limited, so the curve can sit a little lower there. AutoEq uses 18 dB/oct.");
        panel->addSlider ("Bass limit", "basslimit", {}, "Correction fades out below this frequency (the lowest setting is off). Use it when the speaker cannot reproduce deep bass. Also draggable on the graph.");
        panel->addSlider ("Treble gain", "treblegain", " %", "Scales the correction above about 4-8 kHz. 100 % = untouched, 0 % = no treble correction.");
        panel->finish();
        showPanel (std::move (panel), limits);
    };
    manual.onClick = [this] { showPanel (std::make_unique<ManualEqPanel> (proc, [safe = juce::Component::SafePointer<FlatQEditor> (this)] { if (safe != nullptr) safe->saveAsFlow(); }), manual); };
    preamp.onClick = [this]
    {
        auto panel = std::make_unique<ParamPanel> (proc.apvts, 340);
        panel->addToggle ("Auto preamp (safe headroom)", "autopreamp", "Lower the overall gain so the EQ never boosts above 0 dB");
        panel->finish();
        showPanel (std::move (panel), preamp);
    };

    for (auto* it : { &filter, &length, &smoothing, &limits, &preamp, &manual })   // second click closes instead of reopening
    {
        auto open = std::move (it->onClick);
        it->onClick = [this, it, open]
        {
            const bool justClosed = closedItem == it && juce::Time::getMillisecondCounter() - closedMs < 400;
            closedItem = nullptr;
            if (! justClosed) open();
        };
    }

    filter.setTooltip ("Low latency IIR: approximate, no delay. Linear phase FIR: exact, delayed. Minimum phase FIR: exact, no delay.");
    preamp.setTooltip ("Gain reduction that keeps the EQ from boosting above 0 dB");
    inClip.setTooltip ("Input clipping: lights up at 0 dBFS. Click to reset.");
    outClip.setTooltip ("Output clipping: lights up at 0 dBFS. Click to reset.");
    inClip.onReset  = [this] { proc.inClipped = false; inClip.setClipped (false); };
    outClip.onReset = [this] { proc.outClipped = false; outClip.setClipped (false); };
    subtitle.setTooltip ("Preamp: the gain reduction that keeps the EQ from clipping. Max fit error: the largest gap between the target and the filter.");

    if (! sidebarVisible) applySidebarVisibility();

    setResizable (true, true);
    setResizeLimits (860, 520, 1600, 1000);
    setSize (1220, 660);   // a little wider than before: more room for the graph
    startTimerHz (30);
    timerCallback();
}

void FlatQEditor::saveAsFlow()
{
    juce::Component::SafePointer<FlatQEditor> safe (this);
    promptName (this, "Save as profile", "Save the current equalization, with your changes, as a new profile.",
                title.getText() + " (edited)", "Save", [safe] (juce::String name)
    {
        if (safe == nullptr || name.isEmpty()) return;
        juce::String err;
        if (! safe->proc.saveEqualizationAs (name, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save", err);
        safe->profiles.refresh();
    });
}

void FlatQEditor::showAbout() { showPanel (std::make_unique<AboutPanel>(), aboutArea); }

void FlatQEditor::showSettings()
{
    showPanel (std::make_unique<SettingsPanel> (proc.apvts, proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone, [this] (bool on) { tooltipWindow.setMillisecondsBeforeTipAppears (on ? 450 : 100000000); }), menuDots);
}

void FlatQEditor::showMenu()
{
    juce::PopupMenu m;
    m.addItem (3, "Add profile...");
    m.addItem (4, "Save equalization as profile...", proc.getGraphVersion() > 0 && proc.getProfilePath().isNotEmpty());
    m.addItem (5, "Export current profile...", proc.getProfilePath().isNotEmpty());
    m.addSeparator();
    m.addItem (1, "Settings...");
    m.addItem (2, "About FlatQ");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuDots).withParentComponent (this),
                     [this] (int r) { markClosed (menuDots); if (r == 1) showSettings(); else if (r == 2) showAbout(); else if (r == 3) openAddScreen(); else if (r == 4) saveAsFlow(); else if (r == 5) exportProfileDialog (proc, juce::File (proc.getProfilePath()), exportChooser); });
}

void FlatQEditor::applySidebarVisibility()
{
    for (auto* c : std::initializer_list<juce::Component*> { &logoMark, &logo, &brand, &aboutArea, &addButton, &profiles })
        c->setVisible (sidebarVisible);
    sidebarBtn.repaint();
    repaint();
}

FlatQEditor::~FlatQEditor()
{
    if (callout != nullptr) { callout->removeComponentListener (this); callout->dismiss(); }
    setLookAndFeel (nullptr);
}

int FlatQEditor::choiceIndex (const char* id) const { return (int) proc.apvts.getRawParameterValue (id)->load(); }

void FlatQEditor::setChoice (const char* id, int index)
{
    if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
}

void FlatQEditor::showPanel (std::unique_ptr<juce::Component> c, juce::Component& from)
{
    c->setLookAndFeel (&laf);   // the call-out is a separate window: it would otherwise fall back to the default look
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (c), from.getScreenBounds(), nullptr);
    callout = &box; calloutItem = &from;
    box.addComponentListener (this);
}

void FlatQEditor::showFilterMenu()
{
    juce::PopupMenu m;
    const int cur = choiceIndex ("mode");
    m.addItem (1, "Low latency (IIR)  -  ~0 latency, approximate", true, cur == 0);
    m.addItem (2, "Linear phase (FIR)  -  exact, latency = taps/2", true, cur == 1);
    m.addItem (3, "Minimum phase (FIR)  -  exact, ~0 latency", true, cur == 2);
    m.addSeparator();
    m.addSectionHeader ("IIR resolution (low latency mode)");
    m.addItem (11, "Normal  -  6 bands/oct, light on CPU", true, choiceIndex ("iirres") == 0);
    m.addItem (12, "High  -  48 bands/oct, ~0.1 dB error, ~8x the CPU", true, choiceIndex ("iirres") == 1);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&filter).withParentComponent (this),
                     [this] (int r) { markClosed (filter); if (r >= 1 && r <= 3) setChoice ("mode", r - 1); else if (r >= 11) setChoice ("iirres", r - 11); });
}

void FlatQEditor::refreshStatus()
{
    auto& a = proc.apvts;
    const double fs = juce::jmax (1.0, proc.getSampleRate_());
    latency.setValue (juce::String (proc.getLatencySamples() / fs * 1000.0, 1) + " ms");
    const int mode = choiceIndex ("mode");
    filter.setValue (mode == 0 ? (choiceIndex ("iirres") == 1 ? "Low lat. HD" : "Low latency") : mode == 1 ? "Linear" : "Min phase");
    static const char* taps[] = { "4096", "8192", "16384", "32768", "65536" };
    length.setValue (mode == 0 ? "n/a" : juce::String (taps[juce::jlimit (0, 4, choiceIndex ("taps"))]));
    const int sm = choiceIndex ("smooth"), ts = choiceIndex ("treblesmooth");
    static const char* tsn[] = { "", "1/6", "1/3", "1/2", "1", "2" };
    smoothing.setValue (smoothName (sm) + (ts > 0 ? juce::String (" / ") + tsn[ts] + " hf" : juce::String()));
    const float slope = a.getRawParameterValue ("maxslope")->load(), bass = a.getRawParameterValue ("basslimit")->load();
    juce::String l = a.getRawParameterValue ("clamp")->load() > 0.5f ? "+" + juce::String ((int) a.getRawParameterValue ("maxboost")->load()) + " dB" : "No limit";
    if (slope < 35.5f || bass > 20.5f) l << " *";   // * = more limits active (see tooltip)
    limits.setTooltip (juce::String ("Max boost ") + juce::String (a.getRawParameterValue ("maxboost")->load(), 1) + " dB, slope " + (slope < 35.5f ? juce::String ((int) slope) + " dB/oct" : "off") + ", bass limit " + (bass > 20.5f ? juce::String ((int) bass) + " Hz" : "off"));
    limits.setValue (l);
    preamp.setValue ((a.getRawParameterValue ("autopreamp")->load() > 0.5f ? "" : "Man. ") + juce::String (lastPreamp, 1) + " dB");
    { const int n = proc.manualEditedCount(); manual.setValue (n == 0 ? "Current" : "Edited " + juce::String (n)); }
    {   // output fader: total output gain = preamp + trim, limited by the profile's headroom while auto preamp is on
        const bool safe = a.getRawParameterValue ("autopreamp")->load() > 0.5f;
        const double pre = juce::jmax (lastPreamp, trim.getMinimum());   // 0 when auto preamp is off
        faderSync = true;
        if (std::abs (axisPre - pre) > 1e-6) { axisPre = pre; trim.setDoubleClickReturnValue (true, pre); }
        trim.hasLimit = safe; trim.limit = pre;
        trim.muted = a.getRawParameterValue ("mute")->load() > 0.5f;
        if (! trim.isMouseButtonDown()) trim.setValue (pre + (double) a.getRawParameterValue ("trim")->load(), juce::dontSendNotification);
        faderSync = false;
        trim.repaint();
        trimL.setText (trim.muted ? juce::String ("Muted") : juce::String (trim.getValue(), 1) + " dB", juce::dontSendNotification);
        trimL.setColour (juce::Label::textColourId, trim.muted || (trim.hasLimit && trim.getValue() >= trim.limit - 0.05) ? juce::Colour (0xffef4444) : theme::text);
    }
    graph.setBassLimit (bass > 20.5f ? bass : 0.f);
    {   // once the equalization was edited by hand, offer to keep it as a profile
        const bool edited = proc.manualEditedCount() > 0;
        if (edited != saveAsBtn.isVisible()) { saveAsBtn.setVisible (edited); resized(); }
    }
    outMeter.mono = a.getRawParameterValue ("mono")->load() > 0.5f;   // mono output: one bar instead of two
    {   // options that change the sound but are not visible: flag them in the middle of the graph
        auto on = [&a] (const char* id) { return a.getRawParameterValue (id)->load() > 0.5f; };
        auto turnOff = [this] (const char* id) { return [this, id] { if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (0.f); }; };
        std::vector<NoticeStack::Notice> list;
        juce::String sig;
        if (on ("bypass")) { list.push_back ({ "Calibration disabled", "The EQ is bypassed: the audio passes through unchanged.", "Enable calibration", turnOff ("bypass") }); sig << "b"; }
        if (on ("mute"))   { list.push_back ({ "Output muted", "No sound is leaving the plugin.", "Unmute", turnOff ("mute") }); sig << "m"; }
        if (on ("mono"))   { list.push_back ({ "Mono is on", "Left and right are summed to mono, (L + R) / 2.", "Turn off mono", turnOff ("mono") }); sig << "o"; }
        if (on ("swap"))   { list.push_back ({ "Left and right swapped", "The left and right input channels are exchanged.", "Swap back", turnOff ("swap") }); sig << "s"; }
        if (sig != noticeSig)
        {
            noticeSig = sig;
            notices.setNotices (std::move (list));
            graph.setAlpha (on ("bypass") ? 0.3f : 1.f);
            resized();
        }
    }
}

void FlatQEditor::timerCallback()
{
    inMeter.repaint(); outMeter.repaint();
    inClip.setClipped (proc.inClipped.load()); outClip.setClipped (proc.outClipped.load());
    const int v = proc.getGraphVersion();
    if (v != lastVersion)
    {
        lastVersion = v;
        auto d = proc.getGraph();
        const auto key = juce::String (proc.getBuiltInIndex()) + "|" + proc.getProfilePath();
        if (key != lastProfileKey) { lastProfileKey = key; profiles.refresh(); }
        else profiles.syncSelection();
        title.setText (d.name.isEmpty() ? "No profile" : d.name, juce::dontSendNotification);
        subtitle.setText (d.freqs.empty() ? "" : juce::String::formatted ("%s   |   preamp %.1f dB   |   max fit error %.1f dB",
                          d.perChannel ? "Independent L/R" : "Same on both channels", d.preampDb, d.maxErrDb), juce::dontSendNotification);
        lastPreamp = d.preampDb;
        kindIcon.setKind (d.freqs.empty() ? juce::String() : d.kind);
        graph.setData (std::move (d));
    }
    refreshStatus();
}

void FlatQEditor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg);
    g.setColour (sidebar); g.fillRect (0, 0, sidebarW(), getHeight());
    g.setColour (panel); g.fillRect (sidebarW(), getHeight() - statusH, getWidth() - sidebarW() - stripW, statusH);
    g.fillRect (sidebarW(), 0, getWidth() - sidebarW() - stripW, headerH);
    g.setColour (sidebar); g.fillRect (getWidth() - stripW, 0, stripW, getHeight());
    g.setColour (divider);
    if (sidebarVisible) g.drawVerticalLine (sidebarW() - 1, 0.f, (float) getHeight());
    g.drawVerticalLine (getWidth() - stripW, 0.f, (float) getHeight());
    g.drawHorizontalLine (headerH - 1, (float) sidebarW(), (float) (getWidth() - stripW));
    g.drawHorizontalLine (getHeight() - statusH, (float) sidebarW(), (float) (getWidth() - stripW));
    if (sidebarVisible) g.drawHorizontalLine (64, 0.f, (float) sidebarW());
}

void FlatQEditor::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (sidebarW());
    auto strip = r.removeFromRight (stripW);

    auto head64 = side.removeFromTop (64).reduced (16, 0);
    aboutArea.setBounds (head64);
    logoMark.setBounds (head64.removeFromLeft (36).withSizeKeepingCentre (36, 36));
    head64.removeFromLeft (10);
    logo.setBounds (head64.removeFromTop (42).withTrimmedTop (8));    // FlatQ, with the maker's name under it 
    brand.setBounds (head64.removeFromTop (16).withTrimmedLeft (3));
    addButton.setBounds (side.removeFromBottom (52).reduced (12, 8));
    profiles.setBounds (side);
    addScreen.setBounds (getLocalBounds());

    auto head = r.removeFromTop (headerH);
    menuDots.setBounds (juce::Rectangle<int> (head.getRight() - 42, head.getY() + 12, 30, 30));   // top right
    sidebarBtn.setBounds (menuDots.getBounds().translated (-36, 0));
    saveAsBtn.setBounds (juce::Rectangle<int> (sidebarBtn.getX() - 172, sidebarBtn.getY() + 1, 164, 28));                               // hide / show the left bar, left of the dots
    auto titleRow = head.removeFromTop (44).withTrimmedTop (12).withTrimmedRight (88 + (saveAsBtn.isVisible() ? 172 : 0)).reduced (20, 0);
    kindIcon.setBounds (titleRow.removeFromLeft (28).withSizeKeepingCentre (26, 26));   // headphone / speaker, left of the loaded profile
    titleRow.removeFromLeft (10);
    title.setBounds (titleRow);
    subtitle.setBounds (head.reduced (20, 0).withHeight (20).withTrimmedLeft (38));

    auto status = r.removeFromBottom (statusH);
    StatusItem* items[] = { &latency, &filter, &length, &smoothing, &limits, &preamp, &manual };
    const int w = status.getWidth() / 7;
    for (auto* it : items) it->setBounds (status.removeFromLeft (w));
    auto dwRow = r.removeFromBottom (48);                 // Dry / Wet under the graph, on the right
    dryWet.setBounds (dwRow.removeFromRight (122).reduced (0, 4));
    dryWetL.setBounds (dwRow.removeFromRight (84));
    graph.setBounds (r);
    notices.setBounds (r.withSizeKeepingCentre (NoticeStack::kWidth, notices.preferredHeight()));

    // right strip: In / Out meters and the output trim fader
    strip.removeFromTop (headerH - 26);   // (the "skip on export" switch now lives in Settings)
    auto labels = strip.removeFromTop (22);
    bypass.setBounds (strip.removeFromBottom (60).reduced (10, 8));
    auto icons = strip.removeFromBottom (34);
    strip.removeFromBottom (6); strip.removeFromTop (6);
    auto cols = strip.reduced (16, 0);
    auto inCol = cols.removeFromLeft (30); cols.removeFromLeft (10);
    auto outCol = cols.removeFromLeft (30); cols.removeFromLeft (8);
    inL.setBounds (inCol.withY (labels.getY()).withHeight (22).expanded (6, 0));
    outL.setBounds (outCol.withY (labels.getY()).withHeight (22).expanded (6, 0));
    trimL.setBounds (cols.withY (labels.getY()).withHeight (22).expanded (14, 0));   // output level at the top of the fader
    inClip.setBounds (inCol.withHeight (14)); outClip.setBounds (outCol.withHeight (14));   // clip lights above the meters
    inMeter.setBounds (inCol.withTrimmedTop (19)); outMeter.setBounds (outCol.withTrimmedTop (19));
    trim.setBounds (cols);
    auto iconBox = [&icons] (juce::Rectangle<int> col) { return col.withY (icons.getY()).withHeight (icons.getHeight()).withSizeKeepingCentre (34, 28); };
    swap.setBounds (iconBox (inCol));          // under In
    mono.setBounds (iconBox (outCol));         // under Out
    volumeIcon.setBounds (iconBox (cols));     // under the fader
}
