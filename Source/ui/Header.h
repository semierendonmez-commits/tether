// Header shared with the other plug-ins (IKEDAR, Call & Response, MODPORT, SNARESTACK ...): dithered thermal plasma, translucent
// Greiman planes, palette-cycling wordmark, extended underline, status screen.
// Includes the later fixes: caption plates on buttons, thicker wordmark stroke.
#pragma once

#include "Look.h"

namespace ui
{

class PlateButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PlateButton (Skin& s) : skin (s) { setRepaintsOnMouseActivity (true); }
    std::function<void()> onClick;
    std::function<void()> onDragOut;   // drag files out of the plug-in (into the DAW)
    juce::String text, caption;
    bool dimmed = false;

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        if (caption.isNotEmpty())
        {
            auto cap = r.removeFromTop (12);
            auto f = labelFont (8.0f);
            g.setColour (t.panel);
            g.fillRect (cap.withWidth (juce::GlyphArrangement::getStringWidthInt (f, caption) + 8));
            g.setFont (f);
            g.setColour (t.text);
            g.drawText (caption, cap.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto in = r.reduced (1);
        const bool down = isMouseButtonDown();
        g.setColour (isMouseOver() ? t.panel.brighter (0.08f) : t.panel);
        g.fillRect (in);
        draw::bevel (g, in, t, ! down, 2);
        g.setColour (dimmed ? t.textDim : t.text);
        g.setFont (screenFont (18.0f));
        g.drawText (text, in.translated (down ? 1 : 0, down ? 1 : 0), juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        repaint();
        if (! dragStarted && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }
    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onDragOut && ! dragStarted && e.getDistanceFromDragStart() > 8) { dragStarted = true; onDragOut(); }
    }

private:
    Skin& skin;
    bool dragStarted = false;
};

//==============================================================================
class Header : public juce::Component
{
public:
    Header (Skin& s, juce::String word, juce::String subLine) : skin (s), wordmark (std::move (word)), sub (std::move (subLine)) { setOpaque (true); }

    // a voice was triggered / mutated; x position 0..1 along the header
    void burst (float x, float heat)
    {
        if (! skin.motion) return;
        Burst b;
        b.x = x;
        b.y = 0.25f + random.nextFloat() * 0.5f;
        b.heat = 0.6f + 0.4f * heat;
        bursts.push_back (b);
        if (bursts.size() > 24) bursts.erase (bursts.begin());
    }

    void advance (float dt)
    {
        if (skin.motion) time += dt;
        for (auto& b : bursts) b.age += dt;
        bursts.erase (std::remove_if (bursts.begin(), bursts.end(), [] (const Burst& b) { return b.age > 0.9f; }), bursts.end());
    }

    juce::String status[3];
    void setSub (const juce::String& s) { if (s != sub) { sub = s; repaint(); } }
    int statusRight = 372;   // distance of the status screen from the right edge

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        paintPlasma (g, r);
        paintPlanes (g, r);
        paintWordmark (g, juce::Rectangle<int> (20, 8, 420, 74));

        g.setColour (t.accent);
        g.fillRect (22, 80, 640, 2);
        g.setColour (t.accent2);
        g.fillRect (22, 84, 120, 1);
        g.setFont (screenFont (17.0f));
        const auto subCol = t.light ? t.text : t.ramp[5];
        g.setColour (juce::Colours::black.withAlpha (t.light ? 0.0f : 0.5f));
        g.drawText (sub, 23, 86, 680, 16, juce::Justification::centredLeft);
        g.setColour (subCol);
        g.drawText (sub, 22, 85, 680, 16, juce::Justification::centredLeft);

        auto st = juce::Rectangle<int> (getWidth() - statusRight, 14, 206, 70);
        draw::screenWell (g, st, t);
        g.setFont (screenFont (19.0f));
        auto line = st.reduced (6, 3);
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (i == 0 ? t.ink : t.ink.withAlpha (0.75f));
            g.drawText (status[i], line.removeFromTop (21), juce::Justification::centredLeft);
        }
        draw::scanlines (g, st, t, 0.18f);
    }

private:
    struct Burst { float x = 0, y = 0, heat = 1, age = 0; };

    void paintPlasma (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto& t = skin.t();
        constexpr int px = 4;
        const int w = r.getWidth() / px + 1, h = r.getHeight() / px + 1;
        if (img.getWidth() != w || img.getHeight() != h)
            img = juce::Image (juce::Image::RGB, w, h, false);

        const float tm = time;
        const int levels = 7;
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float fx = (float) x / (float) w, fy = (float) y / (float) h;
                float v = std::sin (fx * 9.0f + tm * 0.7f)
                        + std::sin ((fy * 5.0f + fx * 3.0f) - tm * 0.9f)
                        + std::sin (std::sqrt ((fx - 0.6f) * (fx - 0.6f) * 40.0f + (fy - 0.5f) * (fy - 0.5f) * 8.0f) * 6.0f - tm * 1.3f);
                v = (v + 3.0f) / 6.0f;
                v *= 0.55f;
                for (const auto& b : bursts)
                {
                    const float dx = (fx - b.x) * 4.0f, dy = (fy - b.y) * 1.2f;
                    const float life = 1.0f - b.age / 0.9f;
                    v += b.heat * life * std::exp (-(dx * dx + dy * dy) * (8.0f + b.age * 6.0f));
                }
                v *= 1.0f - 0.35f * juce::jlimit (0.0f, 1.0f, (fx - 0.55f) * 2.0f);
                const float q = v * (float) levels + (float) draw::bayer (x, y) / 16.0f;
                const float qv = juce::jlimit (0.0f, (float) levels, std::floor (q)) / (float) levels;
                bd.setPixelColour (x, y, t.light ? t.thermal (0.45f + 0.55f * qv) : t.thermal (qv));
            }

        juce::Graphics::ScopedSaveState ss (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (img, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        g.setColour (juce::Colours::black.withAlpha (t.light ? 0.0f : 0.25f));
        for (int y = r.getY(); y < r.getBottom(); y += 2) g.fillRect (r.getX(), y, r.getWidth(), 1);
        g.setColour (t.panelLo.darker (0.5f));
        g.fillRect (r.removeFromBottom (2));
    }

    void paintPlanes (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto& t = skin.t();
        auto plane = [&] (juce::Colour col, float x, float y, float w, float h, float rot)
        {
            juce::Path p;
            p.addRectangle (x, y, w, h);
            p.applyTransform (juce::AffineTransform::rotation (rot, x + w * 0.5f, y + h * 0.5f));
            g.setColour (col);
            g.fillPath (p);
        };
        const float rx = (float) r.getX();
        plane (t.accent2.withAlpha (0.22f), rx + 380.0f, 6.0f, 230.0f, 62.0f, -0.10f);
        plane (t.accent.withAlpha (0.16f),  rx + 460.0f, 30.0f, 170.0f, 64.0f, 0.16f);
        plane (t.panel.withAlpha (0.35f),   rx + 12.0f, 62.0f, 420.0f, 14.0f, -0.03f);
    }

    // the wordmark's outline and its dither screen never change: built once, reused every frame
    void buildWordmark (juce::Rectangle<int> r)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (labelFont (60.0f), wordmark, 0.0f, 0.0f);
        juce::Path path;
        ga.createPath (path);
        auto pb = path.getBounds();
        const float sx = juce::jmin (1.18f, (float) r.getWidth() / juce::jmax (1.0f, pb.getWidth()));
        path.applyTransform (juce::AffineTransform::translation (-pb.getX(), -pb.getY())
                                .scaled (sx, 1.0f)
                                .translated ((float) r.getX(), (float) r.getY() + 6.0f));
        {
            juce::Path thick;
            juce::PathStrokeType (3.2f, juce::PathStrokeType::mitered).createStrokedPath (thick, path);
            path.addPath (thick);
            path.setUsingNonZeroWinding (true);
        }
        wmPath = path;
        wmRect = r;
        const auto b = wmPath.getBounds().getSmallestIntegerContainer();
        wmDither = juce::Image (juce::Image::ARGB, juce::jmax (1, b.getWidth()), juce::jmax (1, b.getHeight()), true);
        juce::Image mask (juce::Image::ARGB, wmDither.getWidth(), wmDither.getHeight(), true);
        {
            juce::Graphics mg (mask);
            mg.setColour (juce::Colours::white);
            mg.fillPath (wmPath, juce::AffineTransform::translation ((float) -b.getX(), (float) -b.getY()));
        }
        for (int y = 0; y < wmDither.getHeight(); y += 2)
            for (int x = ((y + b.getY()) / 2 & 1) * 2; x < wmDither.getWidth(); x += 4)
                if (mask.getPixelAt (x, y).getAlpha() > 128) wmDither.setPixelAt (x, y, juce::Colours::black.withAlpha (0.22f));
        wmDitherPos = b.getPosition();
    }

    void paintWordmark (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto& t = skin.t();
        if (r != wmRect || wmPath.isEmpty()) buildWordmark (r);
        const juce::Path& path = wmPath;
        const auto pb = path.getBounds();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (path, juce::AffineTransform::translation (4.0f, 4.0f));

        const float cyc = std::fmod (time * 0.25f, 1.0f);
        juce::ColourGradient grad;
        grad.point1 = { pb.getX(), pb.getY() };
        grad.point2 = { pb.getRight(), pb.getBottom() };
        grad.isRadial = false;
        for (int i = 0; i <= 12; ++i)
        {
            const float f = (float) i / 12.0f;
            const float ph = std::fmod (f + cyc, 1.0f);
            const float tri = ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
            grad.addColour (f, t.thermal (0.25f + 0.75f * tri));
        }
        g.setGradientFill (grad);
        g.fillPath (path);

        if (skin.motion)
        {
            const float sy = pb.getY() + std::fmod (time * 34.0f, pb.getHeight() + 30.0f) - 15.0f;
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (path);
            g.setColour (t.ramp[5].withAlpha (0.85f));
            g.fillRect (pb.getX(), sy, pb.getWidth(), 2.0f);
            g.setColour (t.ramp[4].withAlpha (0.35f));
            g.fillRect (pb.getX(), sy - 6.0f, pb.getWidth(), 6.0f);
        }

        g.drawImageAt (wmDither, wmDitherPos.x, wmDitherPos.y);

        g.setColour (t.light ? t.text : juce::Colours::black);
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }

    Skin& skin;
    juce::String wordmark, sub;
    juce::Image img;
    juce::Path wmPath;
    juce::Rectangle<int> wmRect;
    juce::Image wmDither;
    juce::Point<int> wmDitherPos;
    float time = 0.0f;
    std::vector<Burst> bursts;
    juce::Random random;
};

} // namespace ui
