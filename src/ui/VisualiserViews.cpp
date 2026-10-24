#include "ui/VisualiserViews.h"
#include "ui/Theme.h"
#include "Text.h"

using micvst::tr;
namespace theme = micvst::theme;

namespace
{
    // the cached images follow the theme (a theme change normally rebuilds the window anyway)
    juce::uint32 themeKey()
    {
        return theme::rowA.getARGB() ^ (theme::meterBack.getARGB() * 3u) ^ (theme::accent.getARGB() * 7u)
             ^ (theme::textDim.getARGB() * 13u) ^ (theme::outline.getARGB() * 17u) ^ (theme::meterLevel.getARGB() * 19u);
    }

    int engineOf (const juce::Component& c)
    {
        auto* peer = c.getPeer();
        return peer != nullptr ? peer->getCurrentRenderingEngine() : -1;
    }

    juce::Image makeImage (int w, int h)
    {
        // the platform's own image type: a Direct2D bitmap while the graphics card draws, a
        // software image with GDI - drawn every frame without a conversion
        return juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    }

    juce::String dbText (float db, float minDb)
    {
        if (db <= minDb) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));   // —
        return micvst::trNumber (db, 1) + " dB";
    }

    constexpr float cornerRadius = 7.0f;
}

//==============================================================================
SpectrumView::SpectrumView (const micvst::viz::Feed& f) : feed (f)
{
    setOpaque (true);   // (takes the mouse only for its tooltip)
}

void SpectrumView::resized()
{
    plot = getLocalBounds().toFloat().reduced (1.0f);
    imageScale = 0.0f;   // rebuilt for the new size
}

int SpectrumView::pointsWanted() const
{
    return juce::jlimit (48, 640, (int) (plot.getWidth() / 2.0f));
}

float SpectrumView::xOf (double hz) const
{
    const double t = std::log (juce::jmax (1.0, hz) / fMin) / std::log (fMax / fMin);
    return plot.getX() + (float) t * plot.getWidth();
}

float SpectrumView::yOf (float db, double hz) const
{
    const float shown = db + tiltDbPerOctave * (float) std::log2 (juce::jmax (1.0, hz) / 1000.0);
    const float t = juce::jlimit (0.0f, 1.0f, (topDb - shown) / (topDb - bottomDb));
    return plot.getY() + t * plot.getHeight();
}

void SpectrumView::buildImages (float scale, int engine)
{
    imageScale = scale;
    imageEngine = engine;
    imageTheme = themeKey();
    const int w = juce::roundToInt (getWidth() * scale), h = juce::roundToInt (getHeight() * scale);
    const auto bounds = getLocalBounds().toFloat();
    const auto screen = bounds;

    under = makeImage (w, h);
    {
        juce::Graphics g (under);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (theme::rowA);
        g.setColour (theme::meterBack);
        g.fillRoundedRectangle (screen, cornerRadius);
        // level lines every 12 dB (as seen at 1 kHz, where the tilt is zero)
        g.setColour (theme::text.withAlpha (0.045f));
        for (float db = topDb - 12.0f; db > bottomDb; db -= 12.0f)
            g.fillRect (juce::Rectangle<float> (plot.getX(), std::round (yOf (db, 1000.0)), plot.getWidth(), 1.0f));
        // frequency lines; 100 Hz, 1 kHz, 10 kHz a little stronger
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const bool decade = juce::exactlyEqual (hz, 100.0) || juce::exactlyEqual (hz, 1000.0) || juce::exactlyEqual (hz, 10000.0);
            g.setColour (theme::text.withAlpha (decade ? 0.11f : 0.06f));
            g.fillRect (juce::Rectangle<float> (std::round (xOf (hz)), plot.getY(), 1.0f, plot.getHeight()));
        }
    }

    over = makeImage (w, h);
    {
        juce::Graphics g (over);
        g.addTransform (juce::AffineTransform::scale (scale));
        // the card's colour outside the rounded screen: the curves end in clean corners
        juce::Path corners;
        corners.addRectangle (bounds);
        corners.addRoundedRectangle (screen, cornerRadius);
        corners.setUsingNonZeroWinding (false);
        g.setColour (theme::rowA);
        g.fillPath (corners);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (screen.reduced (0.5f), cornerRadius, 1.0f);

        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.setColour (theme::textDim.withAlpha (0.9f));
        const struct { double hz; const char* text; } labels[] {
            { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
            { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } };
        for (auto& l : labels)
            g.drawText (l.text, juce::Rectangle<float> (xOf (l.hz) + 3.0f, plot.getBottom() - 15.0f, 30.0f, 13.0f),
                        juce::Justification::centredLeft, false);

        // legend
        auto legend = juce::Rectangle<float> (plot.getX() + 9.0f, plot.getY() + 6.0f, 200.0f, 13.0f);
        auto swatch = [&] (juce::Colour c, const juce::String& text)
        {
            g.setColour (c);
            g.fillRoundedRectangle (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
            legend.removeFromLeft (5.0f);
            g.setColour (theme::textDim);
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            g.drawText (text, legend.removeFromLeft (tw + 2.0f), juce::Justification::centredLeft, false);
            legend.removeFromLeft (12.0f);
        };
        swatch (theme::textDim, tr ("Giriş"));
        swatch (theme::accent, tr ("Çıkış"));
    }
}

void SpectrumView::drawCurve (juce::Graphics& g, const std::vector<float>& values, juce::Colour fill, juce::Colour line,
                              float lineWidth, bool glow)
{
    const int n = juce::jmin ((int) values.size(), feed.numPoints());
    if (n < 2) return;
    pts.resize ((size_t) n);
    bool visible = false;
    for (int i = 0; i < n; ++i)
    {
        const double hz = feed.frequencyOf (i);
        pts[(size_t) i] = { xOf (hz), yOf (values[(size_t) i], hz) };
        visible = visible || pts[(size_t) i].y < plot.getBottom() - 0.75f;
    }
    if (! visible) return;   // silence: nothing to draw

    juce::Path curve;
    curve.preallocateSpace (n * 5 + 8);
    curve.startNewSubPath (pts[0]);
    for (int i = 1; i + 1 < n; ++i)   // through the midpoints: a smooth line, no corners
    {
        const auto mid = (pts[(size_t) i] + pts[(size_t) i + 1]) * 0.5f;
        curve.quadraticTo (pts[(size_t) i], mid);
    }
    curve.lineTo (pts[(size_t) n - 1]);

    juce::Path area (curve);
    area.lineTo (pts[(size_t) n - 1].x, plot.getBottom());
    area.lineTo (pts[0].x, plot.getBottom());
    area.closeSubPath();
    g.setGradientFill (juce::ColourGradient (fill, 0.0f, plot.getY(), fill.withMultipliedAlpha (0.10f), 0.0f, plot.getBottom(), false));
    g.fillPath (area);

    if (glow)
    {
        g.setColour (line.withMultipliedAlpha (0.16f));
        g.strokePath (curve, juce::PathStrokeType (lineWidth + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (line);
    g.strokePath (curve, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SpectrumView::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int engine = engineOf (*this);
    if (under.isNull() || ! juce::exactlyEqual (scale, imageScale) || engine != imageEngine || themeKey() != imageTheme)
        buildImages (scale, engine);

    const auto bounds = getLocalBounds().toFloat();
    g.drawImage (under, bounds);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.getSmallestIntegerContainer());
        drawCurve (g, feed.spectrum (micvst::viz::Feed::input),
                   theme::textDim.withAlpha (0.20f), theme::textDim.withAlpha (0.55f), 1.0f, false);
        drawCurve (g, feed.spectrum (micvst::viz::Feed::output),
                   theme::accent.withAlpha (0.42f), theme::accent.brighter (0.25f), 1.6f, true);
    }
    g.drawImage (over, bounds);
}

//==============================================================================
LevelMetersView::LevelMetersView (const micvst::viz::Feed& f) : feed (f)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void LevelMetersView::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    const int half = r.getWidth() / 2;
    for (int gi = 0; gi < 2; ++gi)
    {
        auto area = gi == 0 ? r.removeFromLeft (half) : r;
        auto a = area.withSizeKeepingCentre (juce::jmin (230, area.getWidth()), area.getHeight());
        auto& grp = groups[gi];
        auto text = a.removeFromLeft (juce::jmin (112, a.getWidth() / 2));
        grp.title = text.removeFromTop (18);
        text.removeFromTop (2);
        grp.value = text.removeFromTop (22);
        grp.scale = text;   // (the RMS line goes here)
        a.removeFromLeft (8);
        const int barW = 11, gap = 4;
        auto bars = a.removeFromLeft (2 * barW + gap);
        for (int c = 0; c < 2; ++c)
        {
            auto col = bars.removeFromLeft (barW);
            bars.removeFromLeft (gap);
            grp.clip[c] = col.removeFromTop (4);
            col.removeFromTop (3);
            grp.bars[c] = col;
        }
    }
    imageScale = 0.0f;
}

float LevelMetersView::yOf (float db, const juce::Rectangle<int>& bar) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (0.0f - minDb));
    return (float) bar.getBottom() - t * (float) bar.getHeight();
}

void LevelMetersView::buildImage (float scale, int engine)
{
    imageScale = scale;
    imageEngine = engine;
    imageTheme = themeKey();
    back = makeImage (juce::roundToInt (getWidth() * scale), juce::roundToInt (getHeight() * scale));
    juce::Graphics g (back);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.fillAll (theme::background);
    const auto card = getLocalBounds().toFloat();
    g.setColour (theme::rowA);
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
    // the dividing line between input and output
    g.setColour (theme::outline.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (card.getCentreX(), card.getY() + 14.0f, 1.0f, card.getHeight() - 28.0f));

    for (int gi = 0; gi < 2; ++gi)
    {
        const auto& grp = groups[gi];
        g.setColour (theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (gi == 0 ? tr ("Giriş") : tr ("Çıkış"), grp.title, juce::Justification::centredLeft, true);
        for (int c = 0; c < 2; ++c)
        {
            g.setColour (theme::meterBack);
            g.fillRect (grp.bars[c]);
            g.fillRect (grp.clip[c]);
        }
        // scale marks beside the bars
        const int right = grp.bars[1].getRight();
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (float db : { 0.0f, -12.0f, -24.0f, -48.0f })
        {
            const float y = yOf (db, grp.bars[0]);
            g.setColour (theme::text.withAlpha (0.14f));
            for (int c = 0; c < 2; ++c)
                g.fillRect (juce::Rectangle<float> ((float) grp.bars[c].getX(), std::round (y), (float) grp.bars[c].getWidth(), 1.0f));
            g.setColour (theme::textDim.withAlpha (0.85f));
            g.drawText (juce::String ((int) db), juce::Rectangle<float> ((float) right + 5.0f, y - 6.0f, 26.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}

void LevelMetersView::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int engine = engineOf (*this);
    if (back.isNull() || ! juce::exactlyEqual (scale, imageScale) || engine != imageEngine || themeKey() != imageTheme)
        buildImage (scale, engine);
    g.drawImage (back, getLocalBounds().toFloat());

    const double now = feed.now();
    for (int gi = 0; gi < 2; ++gi)
    {
        const auto& grp = groups[gi];
        float hold = micvst::viz::floorDb, rms = micvst::viz::floorDb;
        for (int c = 0; c < 2; ++c)
        {
            const auto& lv = feed.level (gi, c);
            const auto& bar = grp.bars[c];
            hold = juce::jmax (hold, lv.holdDb);
            rms = juce::jmax (rms, lv.rmsDb);
            // green, turning yellow towards -6 dB and red at full scale
            juce::ColourGradient grad (theme::meterLevel, 0.0f, (float) bar.getBottom(), theme::meterOver, 0.0f, (float) bar.getY(), false);
            grad.addColour (juce::jlimit (0.0, 1.0, (double) ((-12.0f - minDb) / -minDb)), theme::meterLevel);
            grad.addColour (juce::jlimit (0.0, 1.0, (double) ((-4.0f - minDb) / -minDb)), theme::meterPeak);
            const float yPeak = yOf (lv.peakDb, bar), yRms = yOf (lv.rmsDb, bar);
            if (yPeak < (float) bar.getBottom())
            {
                g.setGradientFill (grad);
                g.setOpacity (0.38f);
                g.fillRect (juce::Rectangle<float> ((float) bar.getX(), yPeak, (float) bar.getWidth(), (float) bar.getBottom() - yPeak));
                g.setOpacity (1.0f);
            }
            if (yRms < (float) bar.getBottom())
            {
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float> ((float) bar.getX(), yRms, (float) bar.getWidth(), (float) bar.getBottom() - yRms));
            }
            if (lv.holdDb > minDb)
            {
                g.setColour (lv.holdDb >= -0.01f ? theme::meterOver : theme::meterPeak);
                g.fillRect (juce::Rectangle<float> ((float) bar.getX(), juce::jmin ((float) bar.getBottom() - 2.0f, yOf (lv.holdDb, bar)),
                                                    (float) bar.getWidth(), 2.0f));
            }
            if (lv.clipUntil > now)
            {
                g.setColour (theme::meterOver);
                g.fillRect (grp.clip[c]);
            }
        }
        g.setColour (theme::textBright);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (dbText (hold, minDb), grp.value, juce::Justification::centredLeft, false);
        g.setColour (theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText ("RMS " + dbText (rms, minDb), grp.scale.withHeight (16), juce::Justification::centredLeft, false);
    }
}
