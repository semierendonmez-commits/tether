// The norns, drawn: the screen mirror (128 x 64, sixteen levels, in the theme's ink) above the
// three keys and three encoders. Click the keys, drag or wheel the encoders; with the panel
// focused, 1 2 3 are the keys and Q/A W/S E/D turn the encoders. Presses on the real norns flash.
#pragma once

#include "Look.h"
#include "../engine/NornsLink.h"

class TetherProcessor;

namespace ui
{

class NornsPanel : public juce::Component
{
public:
    NornsPanel (Skin&, TetherProcessor&);
    ~NornsPanel() override;

    void pump (float dt);              // the editor's timer: screen, flashes
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // tests and the snapshot tool
    bool hasFrame() const noexcept { return frames > 0; }
    juce::Rectangle<int> screenArea() const noexcept { return screenRect; }

    class Encoder;
    class Key;

private:
    void rebuildImage();

    Skin& skin;
    TetherProcessor& proc;
    std::unique_ptr<Encoder> enc[3];
    std::unique_ptr<Key> key[3];
    juce::Rectangle<int> screenRect, body;
    std::array<uint8_t, tether::kScreenW * tether::kScreenH> pix {};
    uint32_t serial = 0;
    int frames = 0, lastTheme = -1;
    juce::Image img;
    bool held[4] {};
    std::array<uint32_t, 6> hwSeen {};
};

} // namespace ui
