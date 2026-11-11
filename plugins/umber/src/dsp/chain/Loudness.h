#pragma once

#include <cmath>
#include <vector>

namespace umber::dsp
{

/**
    ITU-R BS.1770-4 K-weighted mean-square loudness at 48 kHz (offline measurement only:
    used by the Auto gain calibration tool and the tests, never on the audio thread).
*/
class KWeightedLoudness
{
public:
    /** K-weighted RMS in dB of x, ignoring the first `skip` samples (filter/model settling). */
    static double measureDb (const float* x, int numSamples, int skip)
    {
        Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        Biquad rlb   { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

        double sum = 0.0;
        int count = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const double v = rlb.process (shelf.process (x[i]));
            if (i >= skip)
            {
                sum += v * v;
                ++count;
            }
        }

        return 10.0 * std::log10 (sum / (count > 0 ? count : 1) + 1.0e-30);
    }

    static double measureDb (const std::vector<float>& x, int skip)
    {
        return measureDb (x.data(), (int) x.size(), skip);
    }

private:
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        double process (double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
};

} // namespace umber::dsp
