#pragma once

#include "PluginProcessor.h"
#include "ui/Header.h"
#include "ui/Widgets.h"
#include "ui/NornsPanel.h"
#include "ui/ParamList.h"

class TetherEditor : public juce::AudioProcessorEditor, public ui::Host, private juce::Timer
{
public:
    static constexpr int kW = 1180, kH = 864;
    explicit TetherEditor (TetherProcessor&);
    ~TetherEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void pump (float dt);                // the snapshot tool drives the animation without a message loop
    void setTheme (int i);
    ui::NornsPanel& panel() { return nornsPanel; }
    ui::ParamList& list() { return paramList; }

    // ui::Host
    bool isLocked (const juce::String&) const override { return false; }
    void setLocked (const juce::String&, bool) override {}
    bool modActive (const juce::String&) const override { return false; }
    float modValue (const juce::String&) const override { return 0.0f; }
    bool canModulate (const juce::String&) const override { return false; }
    void addMod (int, const juce::String&) override {}
    void modMenu (juce::PopupMenu&, const juce::String&) override {}
    void hint (const juce::String& s) override { if (s != hintText) { hintText = s; repaint (hintArea); } }
    bool hasLocks() const override { return false; }

    class SlotCell;

private:
    void timerCallback() override { pump (1.0f / 30.0f); }
    void themeMenu();
    void paintLink (juce::Graphics&, juce::Rectangle<int>);

    TetherProcessor& proc;
    ui::Skin skin;
    ui::Look look { skin };
    ui::Header header { skin, "TETHER", "NORNS  //  SCREEN, KEYS, ENCODERS AND PARAMETERS FROM THE DAW" };
    ui::NornsPanel nornsPanel { skin, proc };
    ui::ParamList paramList { skin, proc };
    std::vector<std::unique_ptr<SlotCell>> cells;

    juce::TextEditor hostField;
    ui::PlateButton loadBtn { skin }, saveBtn { skin }, themeBtn { skin }, refreshBtn { skin }, clearBtn { skin };
    std::unique_ptr<ui::Toggle> linkT;
    std::unique_ptr<ui::MenuCell> syncCell;
    std::unique_ptr<ui::NumberBox> psetBox;

    juce::Rectangle<int> nornsWin, paramsWin, slotsWin, linkWin, hintArea, infoArea;
    juce::String hintText;
    uint32_t paramsSeen = ~0u;
    float lastE[3] { -1, -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TetherEditor)
};
