#pragma once
#include "PluginProcessor.h"
#include "MeasurementDb.h"

namespace theme
{
    const juce::Colour bg       { 0xff121418 };   // window / graph
    const juce::Colour panel    { 0xff1a1d22 };   // header, status row, popups
    const juce::Colour sidebar  { 0xff16181c };
    const juce::Colour divider  { 0xff262a31 };
    const juce::Colour track    { 0xff6b7586 };   // unfilled part of sliders: clearly lighter than the panels so the groove reads
    const juce::Colour text     { 0xffe6e8eb };
    const juce::Colour dim      { 0xff8a909a };
    const juce::Colour accent   { 0xff22d3ee };   // selection, active controls
    const juce::Colour good     { 0xff4ade80 };   // correction target (mono) / meters
    const juce::Colour result   { 0xffe879f9 };   // achieved response
    const juce::Colour chanL    { 0xff4cc9f0 };
    const juce::Colour chanR    { 0xfff4a259 };
}

class FlatQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FlatQLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    // Slim groove with a solid accent fill; a slider with the "bipolar" property set fills from 0 instead of from the end.
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

// Log-frequency response plot: legend with toggles on top, target (thick, translucent), achieved response (thin),
// boost ceiling (dashed) and a draggable bass-limit region.
class ResponseGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setData (GraphData d) { data = std::move (d); repaint(); }
    void setBassLimit (float hz) { if (! dragging) { bassLimit = hz; repaint(); } }
    std::function<void (float)> onBassLimit;     // new value in Hz (0 = off)
    std::function<void (bool)> onDragState;      // gesture begin / end
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> plotArea() const;
    float xOf (float f) const;
    GraphData data;
    float bassLimit = 0.f;
    bool dragging = false, showTarget = false, showMeasured = false, showEq = true, showResult = false, showLimit = false;   // only the EQ curve by default
    juce::Rectangle<int> legendHit[5];
};

// Stereo peak meter pair.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (std::array<std::atomic<float>, 2>& s) : src (s) {}
    void paint (juce::Graphics&) override;
private:
    std::array<std::atomic<float>, 2>& src;
    float held[2] {};
};

// Mono check button: two overlapping rings (stereo); a single ring while the output is summed to mono.
class MonoButton : public juce::Button
{
public:
    MonoButton() : juce::Button ("Mono") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        if (over) { g.setColour (juce::Colour (0xff23272e)); g.fillRoundedRectangle (r, 6.f); }
        const auto c = r.getCentre(); const float rad = 8.f;
        const bool on = getToggleState();
        g.setColour (on ? theme::accent : (over ? theme::text : theme::dim));
        if (on) { g.fillEllipse (c.x - rad, c.y - rad, 2 * rad, 2 * rad); g.setColour (theme::accent.withAlpha (0.28f)); g.fillEllipse (c.x - rad, c.y - rad, 2 * rad, 2 * rad);
                  g.setColour (theme::accent); g.drawEllipse (c.x - rad, c.y - rad, 2 * rad, 2 * rad, 1.8f); }
        else { const float off = 5.f;
               g.drawEllipse (c.x - off - rad, c.y - rad, 2 * rad, 2 * rad, 1.6f);
               g.drawEllipse (c.x + off - rad, c.y - rad, 2 * rad, 2 * rad, 1.6f); }
    }
};

// Vertical output fader in total-output dB (preamp + trim). The red part above `limit` is the profile's headroom limit:
// the fader cannot be pushed into it.
class OutputFader : public juce::Slider
{
public:
    OutputFader() : juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox) {}
    bool hasLimit = false; double limit = 0;
    void paint (juce::Graphics& g) override
    {
        const float cx = (float) getWidth() * 0.5f;
        const float yTop = (float) getPositionOfValue (getMaximum()), yBot = (float) getPositionOfValue (getMinimum());
        const float yLim = hasLimit ? (float) getPositionOfValue (limit) : yTop, yVal = (float) getPositionOfValue (getValue());
        g.setColour (theme::track); g.fillRoundedRectangle (cx - 2.5f, yTop, 5.f, yBot - yTop, 2.5f);
        if (hasLimit) { g.setColour (juce::Colour (0xffef4444)); g.fillRoundedRectangle (cx - 2.5f, yTop, 5.f, yLim - yTop + 2.5f, 2.5f); }
        if (hasLimit && yVal > yLim)   // room left to raise the fader: lighter accent between the thumb and the limit
        { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (cx - 2.5f, yLim, 5.f, yVal - yLim); }
        g.setColour (theme::accent.withAlpha (0.8f)); g.fillRoundedRectangle (cx - 2.5f, yVal, 5.f, yBot - yVal, 2.5f);
        const bool over = isMouseOverOrDragging();
        g.setColour (theme::bg); g.fillEllipse (cx - 8.f, yVal - 8.f, 16.f, 16.f);
        g.setColour (over ? theme::accent : theme::text); g.drawEllipse (cx - 8.f, yVal - 8.f, 16.f, 16.f, 2.f);
    }
};

// Speaker icon under the fader: click to mute / unmute the output (slashed and red while muted).
class SpeakerButton : public juce::Button
{
public:
    SpeakerButton() : juce::Button ("Mute") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        if (over) { g.setColour (juce::Colour (0xff23272e)); g.fillRoundedRectangle (r, 6.f); }
        const auto c = r.getCentre(); const bool muted = getToggleState();
        const auto col = muted ? juce::Colour (0xffef4444) : (over ? theme::text : theme::dim);
        juce::Path body; body.addTriangle (c.x - 2.f, c.y - 6.f, c.x - 2.f, c.y + 6.f, c.x - 9.f, c.y);
        body.addRectangle (c.x - 11.f, c.y - 3.5f, 6.f, 7.f);
        g.setColour (col); g.fillPath (body);
        if (! muted)
            for (int i = 1; i <= 2; ++i) { juce::Path arc; arc.addCentredArc (c.x - 3.f, c.y, 4.f * (float) i + 1.f, 4.f * (float) i + 1.f, 0.f, 0.7f, 2.44f, true);
                                           g.strokePath (arc, juce::PathStrokeType (1.5f)); }
        else { g.drawLine (c.x + 1.f, c.y - 5.f, c.x + 9.f, c.y + 5.f, 1.8f); g.drawLine (c.x + 9.f, c.y - 5.f, c.x + 1.f, c.y + 5.f, 1.8f); }
    }
};

// Two opposite arrows under the In meter: click to swap the left and right input channels.
class SwapButton : public juce::Button
{
public:
    SwapButton() : juce::Button ("Swap L/R") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        if (over) { g.setColour (juce::Colour (0xff23272e)); g.fillRoundedRectangle (r, 6.f); }
        const auto c = r.getCentre(); const bool on = getToggleState();
        g.setColour (on ? theme::accent : (over ? theme::text : theme::dim));
        auto arrow = [&] (float y, float dir)   // dir +1 points right, -1 points left
        {
            const float x0 = c.x - 8.f * dir, x1 = c.x + 8.f * dir;
            g.drawLine (x0, y, x1, y, 1.6f);
            juce::Path head; head.startNewSubPath (x1 - 4.f * dir, y - 3.5f); head.lineTo (x1, y); head.lineTo (x1 - 4.f * dir, y + 3.5f);
            g.strokePath (head, juce::PathStrokeType (1.6f));
        };
        arrow (c.y - 4.5f, on ? -1.f : 1.f);
        arrow (c.y + 4.5f, on ? 1.f : -1.f);
    }
};

// "Calibration Enabled / Disabled" with a power icon (parameter "bypass": on = disabled).
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("Calibration") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        if (over) { g.setColour (juce::Colour (0xff23272e)); g.fillRoundedRectangle (r, 6.f); }
        const bool enabled = ! getToggleState();
        const auto col = enabled ? theme::good : theme::dim;
        const juce::Point<float> c (r.getX() + 24.f, r.getCentreY());
        juce::Path p; p.addCentredArc (c.x, c.y, 9.f, 9.f, 0.f, 0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
        p.startNewSubPath (c.x, c.y - 12.f); p.lineTo (c.x, c.y - 1.f);
        g.setColour (col); g.strokePath (p, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (juce::FontOptions (12.f)); g.setColour (theme::dim);
        g.drawText ("Calibration", (int) r.getX() + 46, (int) r.getCentreY() - 16, 90, 16, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.f, juce::Font::bold)); g.setColour (enabled ? theme::text : theme::dim);
        g.drawText (enabled ? "Enabled" : "Disabled", (int) r.getX() + 46, (int) r.getCentreY(), 90, 16, juce::Justification::centredLeft);
    }
};

// Card shown over the graph while calibration is disabled, with a button to turn it back on.
class DisabledCard : public juce::Component
{
public:
    DisabledCard()
    {
        enable.setColour (juce::TextButton::buttonColourId, theme::accent);
        enable.setColour (juce::TextButton::textColourOffId, theme::bg);
        enable.onClick = [this] { if (onEnable) onEnable(); };
        addAndMakeVisible (enable);
    }
    std::function<void()> onEnable;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (theme::panel.withAlpha (0.96f)); g.fillRoundedRectangle (r, 10.f);
        g.setColour (theme::dim.withAlpha (0.5f)); g.drawRoundedRectangle (r, 10.f, 1.f);
        g.setColour (theme::text); g.setFont (juce::FontOptions (19.f, juce::Font::bold));
        g.drawText ("Calibration disabled", 0, 18, getWidth(), 26, juce::Justification::centred);
        g.setColour (theme::dim); g.setFont (juce::FontOptions (13.f));
        g.drawText ("The EQ is bypassed: the audio passes through unchanged.", 12, 48, getWidth() - 24, 20, juce::Justification::centred);
    }
    void resized() override { enable.setBounds (getLocalBounds().withSizeKeepingCentre (190, 34).withY (getHeight() - 56)); }
private:
    juce::TextButton enable { "Enable calibration" };
};

// Clickable "LABEL / value ^" cell in the bottom status row.
class StatusItem : public juce::Component, public juce::SettableTooltipClient
{
public:
    StatusItem (juce::String l, bool clickable) : label (std::move (l)), canClick (clickable) {}
    void setValue (const juce::String& v) { if (v != value) { value = v; repaint(); } }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (canClick && onClick && contains (e.getPosition())) onClick(); }
private:
    juce::String label, value;
    bool canClick, hover = false;
};

// Profile library: built-in profiles, the user's profiles (saved in Application Support/FlatQ/Profiles) and, if a
// profile was loaded from elsewhere, that one. Each row has a coloured type tag and a three-dots menu
// (rename / duplicate / delete; built-in and external profiles can only be duplicated).
class ProfileList : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ProfileList (FlatQProcessor& p);
    void resized() override { box.setBounds (getLocalBounds()); }
    void refresh();          // rebuild the rows (reads the library folder)
    void syncSelection();    // highlight the loaded profile (no disk access)
private:
    struct Entry { juce::String name, tag; int builtIn = -1; juce::File file; bool external = false; };
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override {}
    int getNumRows() override { return (int) entries.size(); }
    juce::String getTooltipForRow (int row) override;
    void showMenu (int row, juce::Point<int> screenPos);
    void askRename (Entry);
    void askDelete (Entry);
    void report (bool ok, const juce::String& err);
    FlatQProcessor& proc;
    std::vector<Entry> entries;
    juce::ListBox box { {}, this };
};

// Search results from the embedded measurement pack: name on top, source / form / rig under it.
class MeasurementList : public juce::Component, private juce::ListBoxModel
{
public:
    MeasurementList() { box.setRowHeight (46); box.setOutlineThickness (0); addAndMakeVisible (box); }
    void resized() override { box.setBounds (getLocalBounds()); }
    void setHits (std::vector<int> h) { hits = std::move (h); box.updateContent(); box.deselectAllRows(); box.repaint(); box.scrollToEnsureRowIsOnscreen (0); }
    std::function<void (int)> onPick;   // index into MeasurementDb
private:
    int getNumRows() override { return (int) hits.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) hits.size()) return;
        const auto& e = MeasurementDb::get().entry (hits[(size_t) row]);
        if (selected) { g.setColour (theme::accent.withAlpha (0.14f)); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text); g.setFont (juce::FontOptions (12.5f));
        g.drawText (e.name, 14, 5, w - 24, 18, juce::Justification::centredLeft, true);
        g.setColour (theme::dim); g.setFont (juce::FontOptions (11.f));
        g.drawText (e.source + "  -  " + e.form + (e.rig.isNotEmpty() ? "  -  " + e.rig : juce::String()), 14, 24, w - 24, 16, juce::Justification::centredLeft, true);
        g.setColour (theme::divider); g.drawHorizontalLine (h - 1, 0.f, (float) w);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int) hits.size() && onPick) onPick (hits[(size_t) row]); }
    juce::String getTooltipForRow (int row) override
    {
        if (row < 0 || row >= (int) hits.size()) return {};
        const auto& e = MeasurementDb::get().entry (hits[(size_t) row]);
        return e.name + " - measured by " + e.source + " (" + e.form + "). Click to create a profile from this measurement with the chosen target.";
    }
    std::vector<int> hits;
    juce::ListBox box { {}, this };
};

class FlatQEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ComponentListener
{
public:
    explicit FlatQEditor (FlatQProcessor&);
    ~FlatQEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void refreshStatus();
    // A click on a status item while its popup is open dismisses the popup (on mouse-down); the matching mouse-up must not reopen it.
    void markClosed (StatusItem& it) { closedItem = &it; closedMs = juce::Time::getMillisecondCounter(); }
    void componentBeingDeleted (juce::Component&) override { if (calloutItem) markClosed (*calloutItem); calloutItem = nullptr; }
    StatusItem *closedItem = nullptr, *calloutItem = nullptr;
    juce::uint32 closedMs = 0;
    juce::Component::SafePointer<juce::CallOutBox> callout;
    void showFilterMenu();
    void showPanel (std::unique_ptr<juce::Component>, juce::Component& from);
    void setChoice (const char* id, int index);
    int choiceIndex (const char* id) const;

    FlatQLookAndFeel laf;   // declared first: destroyed last
    juce::TooltipWindow tooltipWindow { this, 450 };
    FlatQProcessor& proc;
    ResponseGraph graph;
    ProfileList profiles { proc };
    juce::TextEditor searchBox;
    juce::ComboBox formFilter, targetPick;
    juce::Label resultsInfo;
    MeasurementList results;
    bool searching = false;
    void updateSearch();
    juce::TextButton loadButton { "+  Load profile file..." }, importButton { "+  Import AutoEq measurement..." };
    void importAutoEqFlow (const juce::File& measurement);
    juce::Label title, subtitle, logo { {}, "FlatQ" }, inL { {}, "In" }, outL { {}, "Out" }, trimL;
    LevelMeter inMeter { proc.inPeak }, outMeter { proc.outPeak };
    OutputFader trim;
    SpeakerButton volumeIcon;
    SwapButton swap;
    DisabledCard disabledCard;
    PowerButton bypass;
    bool faderSync = false;
    double axisPre = 1e9;
    MonoButton mono;
    juce::ToggleButton skipExport { "Skip on export" };
    StatusItem latency { "Latency", false }, filter { "Filter", true }, length { "FIR length", true },
               smoothing { "Smoothing", true }, limits { "Limits", true }, preamp { "Preamp", true }, manual { "Manual EQ", true };
    int lastVersion = -1;
    juce::String lastProfileKey;
    double lastPreamp = 0;
    static constexpr int sidebarW = 230, stripW = 150, headerH = 72, statusH = 56;
    std::unique_ptr<juce::FileChooser> chooser;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<BA> bypassA, skipA, monoA, muteA, swapA;
};
