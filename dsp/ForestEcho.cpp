#include "ForestEcho.h"

#include <algorithm>
#include <cmath>

namespace pbv
{

namespace
{
    // Reflection times (ms) and gains: a few trunks at 2–9 m.
    constexpr float kTapMs[]   = { 11.0f, 17.3f, 23.9f, 31.1f, 41.7f, 53.3f };
    constexpr float kTapGain[] = { 0.55f, -0.45f, 0.40f, -0.33f, 0.28f, 0.22f };
    // Mutually prime-ish line lengths (ms) for a smooth diffuse tail.
    constexpr float kLineMs[]  = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 79.3f };
}

void ForestEcho::prepare (double sampleRate)
{
    sr = sampleRate;
    preBuf.assign ((size_t) (sr * 0.06) + 2, 0.0f);
    for (int t = 0; t < kTaps; ++t)
    {
        tapDelay[(size_t) t] = (int) std::lround (kTapMs[t] * 0.001 * sr);
        tapGain[(size_t) t] = kTapGain[t];
    }
    for (int i = 0; i < kLines; ++i)
    {
        lineLen[(size_t) i] = std::max (1, (int) std::lround (kLineMs[i] * 0.001 * sr));
        lines[(size_t) i].assign ((size_t) lineLen[(size_t) i], 0.0f);
    }
    // Damping: one-pole lowpass around 6 kHz inside the loop, so leaves soak up highs.
    dampCoeff = (float) std::exp (-2.0 * 3.14159265358979 * 6000.0 / sr);
    decay = -1.0f;
    reset();
}

void ForestEcho::reset()
{
    std::fill (preBuf.begin(), preBuf.end(), 0.0f);
    prePos = 0;
    for (int i = 0; i < kLines; ++i)
    {
        std::fill (lines[(size_t) i].begin(), lines[(size_t) i].end(), 0.0f);
        linePos[(size_t) i] = 0;
        damp[(size_t) i] = 0.0f;
    }
    gain = gainTarget;
}

void ForestEcho::setParams (float wet, float decaySeconds)
{
    gainTarget = wetGain (wet);
    if (decaySeconds != decay)
    {
        decay = decaySeconds;
        for (int i = 0; i < kLines; ++i)
            lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * (lineLen[(size_t) i] / sr) / decay);
    }
}

void ForestEcho::process (const float* in, float* outL, float* outR, int numSamples)
{
    if (gainTarget <= 0.0f && gain <= 1.0e-6f)
    {
        gain = 0.0f;
        return; // fully off: the tail is dropped, like a bypassed reverb
    }

    const int preLen = (int) preBuf.size();
    const float smooth = 0.001f;

    for (int n = 0; n < numSamples; ++n)
    {
        gain += (gainTarget - gain) * smooth;

        preBuf[(size_t) prePos] = in[n];

        // Early reflections from the trees.
        float early = 0.0f;
        for (int t = 0; t < kTaps; ++t)
        {
            int idx = prePos - tapDelay[(size_t) t];
            if (idx < 0) idx += preLen;
            early += preBuf[(size_t) idx] * tapGain[(size_t) t];
        }
        if (++prePos >= preLen) prePos = 0;

        // Read the delay lines, damp them, mix with a Householder matrix.
        float outs[kLines];
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& d = damp[(size_t) i];
            const float raw = lines[(size_t) i][(size_t) linePos[(size_t) i]];
            d = raw + (d - raw) * dampCoeff;
            outs[i] = d * lineGain[(size_t) i];
            sum += outs[i];
        }
        const float h = sum * (2.0f / kLines);

        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float fb = outs[i] - h;
            lines[(size_t) i][(size_t) linePos[(size_t) i]] = early * 0.35f + fb;
            if (++linePos[(size_t) i] >= lineLen[(size_t) i]) linePos[(size_t) i] = 0;
            if (i & 1) r += outs[i]; else l += outs[i];
        }

        outL[n] += gain * (early + l * 0.5f);
        outR[n] += gain * (early * 0.9f + r * 0.5f);
    }
}

} // namespace pbv
