#pragma once
#include "audio/BuiltInEffects.h"
#include "audio/fx/FxDsp.h"
#include <vector>

namespace fx
{
    // Base of the filter effects. The parameters drive glides (50 ms); while any glide moves, the
    // sections (state-variable filters, Svf) are redesigned every sample - no steps, no zipper noise.
    // Each sample frame: frame() (per-frame ramps), then tick() per channel - by default the
    // cascade of all sections.
    class FilterEffect : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;

        static constexpr int maxSections = 16;
        static constexpr double glideSeconds = 0.05;

        // Magnitude response of the current design (tests, displays): product of all sections.
        virtual double responseAt (double freqHz) const
        {
            double m = 1.0;
            for (int s = 0; s < numSections; ++s) m *= coeffs[(size_t) s].biquad().magnitude (getRate(), freqHz);
            return m;
        }

    protected:
        struct Binding { Glide* glide; juce::RangedAudioParameter* param; bool expo; };

        void bind (Glide& g, juce::RangedAudioParameter* p, bool exponential) { bindings.push_back ({ &g, p, exponential }); }
        static float value (const juce::RangedAudioParameter* p) noexcept { return p->convertFrom0to1 (p->getValue()); }

        // Glide targets from the parameters (default: each bound parameter's value). Plug-in thread,
        // once per block.
        virtual void readTargets() noexcept
        {
            for (auto& b : bindings) b.glide->setTarget (value (b.param));
        }
        // New coefficients from the glides' current values.
        virtual void design() noexcept = 0;
        // Every sample, after the glides advanced (own glides, switch logic); true = redesign.
        virtual bool step() noexcept { return false; }
        // Every sample frame before the channels (per-frame ramps).
        virtual void frame() noexcept {}

        virtual double tick (int c, double x) noexcept
        {
            auto& st = state[(size_t) c];
            for (int s = 0; s < numSections; ++s) x = st[(size_t) s].process (x);
            return x;
        }

        void prepareEffect() override
        {
            for (auto& b : bindings)
            {
                b.glide->prepare (getRate(), glideSeconds, b.expo);
                b.glide->snap (value (b.param));
            }
            onPrepare();
            readTargets();
            for (auto& b : bindings) b.glide->snap (b.glide->getTarget());
            design();
        }
        virtual void onPrepare() {}

        void resetEffect() override
        {
            for (auto& ch : state) for (auto& s : ch) s.reset();
            onReset();
        }
        virtual void onReset() noexcept {}

        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            readTargets();
            for (int i = 0; i < n; ++i)
            {
                bool moved = false;
                for (auto& b : bindings) moved = b.glide->advance() || moved;
                if (step() || moved) design();
                frame();
                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = (float) tick (c, (double) ch[c][i]);
            }
            for (int c = 0; c < numCh; ++c) for (auto& s : state[(size_t) c]) s.flush();
        }

        void setCoeffs (int section, const SvfCoeffs& k) noexcept
        {
            coeffs[(size_t) section] = k;
            for (auto& ch : state) ch[(size_t) section].k = k;
        }

        int numSections = 1;
        std::array<SvfCoeffs, maxSections> coeffs {};
        std::array<std::array<Svf, maxSections>, 2> state {};
        std::vector<Binding> bindings;
    };
}
