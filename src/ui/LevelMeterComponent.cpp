#include "ui/LevelMeterComponent.h"
#include "ui/Theme.h"

namespace
{
    // Gitter-/Beschriftungsmarken (dB). 0 dB rechts.
    const int kTicks[] = { -60, -48, -36, -24, -12, -6, 0 };
}

void LevelMeterComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (micvst::theme::meterBack);
    g.fillRect (b);

    auto dbX = [&] (float db) { return b.getX() + meterScale::dbToNorm (db) * b.getWidth(); };

    // Dezente vertikale Gitterlinien an den Skalenmarken.
    g.setColour (micvst::theme::text.withAlpha (0.10f));
    for (int db : kTicks)
        g.drawVerticalLine ((int) dbX ((float) db), b.getY(), b.getBottom());

    // RMS-Balken von links.
    const float rmsDb = juce::Decibels::gainToDecibels (level.rms, meterScale::minDb);
    const float rmsX  = dbX (rmsDb);
    if (rmsX > b.getX())
    {
        g.setColour (micvst::theme::meterLevel);
        g.fillRect (juce::Rectangle<float> (b.getX(), b.getY(), rmsX - b.getX(), b.getHeight()));
    }

    // Peak-Hold als vertikale gelbe Linie. Ab 0 dBFS rot und am rechten Rand gehalten (vorher lag
    // die Linie dann außerhalb und war unsichtbar): so viel Pegel wird beim Wandeln ins
    // 24-Bit-Format des Kabels abgeschnitten (z. B. nach zu viel Kazanç ohne Limiter dahinter).
    const float peakDb = juce::Decibels::gainToDecibels (level.peak, meterScale::minDb);
    if (peakDb > meterScale::minDb)
    {
        const bool over = level.peak >= 1.0f;
        g.setColour (over ? micvst::theme::meterOver : micvst::theme::meterPeak);
        const float px = juce::jmin (dbX (peakDb), b.getRight() - 1.5f);
        g.drawLine (px, b.getY(), px, b.getBottom(), over ? 3.0f : 2.0f);
    }

    g.setColour (micvst::theme::outline);
    g.drawRect (b, 1.0f);
}

void DbScaleComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (micvst::theme::textDim);
    g.setFont (11.0f);

    for (int db : kTicks)
    {
        const float x = b.getX() + meterScale::dbToNorm ((float) db) * b.getWidth();
        // Mittiger Text um die Markierung; an den Rändern (-60 links, 0 rechts) liegt das
        // Textfeld ganz innerhalb der Komponente - vorher ragte es halb hinaus und die beiden
        // Randbeschriftungen waren abgeschnitten (unsichtbar).
        juce::Rectangle<float> r (x - 18.0f, b.getY(), 36.0f, b.getHeight());
        auto just = juce::Justification::centred;
        if (db == kTicks[0]) { r.setX (b.getX());          just = juce::Justification::centredLeft; }
        if (db == 0)         { r.setX (b.getRight() - 36.0f); just = juce::Justification::centredRight; }
        g.drawText (juce::String (db), r, just);
    }
}
