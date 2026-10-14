// Forest echo: sparse early reflections (tree trunks) feeding an 8-line
// feedback delay network. Mono in, stereo out, wet signal only.
#pragma once

#include <array>
#include <vector>

namespace pbv
{

class ForestEcho
{
public:
    void prepare (double sampleRate);
    void reset();

    // wet: 0..1 panel value (0 = off). decaySeconds: time to fall 60 dB.
    void setParams (float wet, float decaySeconds);

    // Adds the echo of `in` to outL/outR.
    void process (const float* in, float* outL, float* outR, int numSamples);

    // Gain applied to the wet signal for a given panel value. The default
    // sparrow value (0.47) puts the echo about 19 dB below the note, averaged over
    // the sparrow's pitches (a pure tone's tail swings about +-6 dB with pitch).
    static float wetGain (float wet) { return wet * wet * 1.6f; }

private:
    static constexpr int kLines = 8;
    static constexpr int kTaps = 6;

    double sr = 48000.0;
    std::vector<float> preBuf;
    int prePos = 0;
    std::array<int, kTaps> tapDelay {};
    std::array<float, kTaps> tapGain {};

    std::array<std::vector<float>, kLines> lines;
    std::array<int, kLines> lineLen {};
    std::array<int, kLines> linePos {};
    std::array<float, kLines> lineGain {};
    std::array<float, kLines> damp {};

    float gain = 0.0f, gainTarget = 0.0f;
    float dampCoeff = 0.3f;
    float decay = -1.0f;
};

} // namespace pbv
