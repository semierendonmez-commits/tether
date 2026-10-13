// The running script's parameters, as the tether mod lists them: drag a bar to set it on norns,
// right-click (or the arrow) to bind it to a slot. A bound parameter is moved through its slot,
// so the host's automation and the panel never fight.
#pragma once

#include "Look.h"
#include "../engine/NornsLink.h"

class TetherProcessor;

namespace ui
{

class ParamList : public juce::Component, private juce::ScrollBar::Listener
{
public:
    ParamList (Skin&, TetherProcessor&);
    ~ParamList() override;

    void pump();                        // the editor's timer: copies the list when it moved
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    std::function<void (const juce::String&)> onHint;
    int visibleCount() const noexcept { return (int) shown.size(); }
    void showBindMenu (int row);        // public for the snapshot tool

    static constexpr int kRowH = 24;

private:
    void scrollBarMoved (juce::ScrollBar*, double) override { repaint(); }
    void refilter();
    int rowAt (juce::Point<int>) const;
    juce::Rectangle<int> rowRect (int visibleIndex) const;
    juce::Rectangle<int> barOf (juce::Rectangle<int> row) const;
    void setFromX (int row, int x, bool start);

    Skin& skin;
    TetherProcessor& proc;
    juce::TextEditor filter;
    juce::ScrollBar bar { true };
    juce::Rectangle<int> listArea;
    std::vector<tether::ParamInfo> all;
    std::vector<int> shown;             // indices into all
    uint32_t serial = ~0u;
    int hover = -1, dragRow = -1, dragSlot = -1;
};

} // namespace ui
