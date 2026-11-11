#pragma once

#include <algorithm>
#include <cmath>

namespace cerulean::dsp
{

/**
    The soft clipper of the tank driver ("dwell"), with first-order antiderivative anti-aliasing
    (Parker, Zavalishin & Le Bihan, "Reducing the aliasing of nonlinear waveshaping using
    continuous-time convolution", DAFx 2016).

    f(x) = x (27 + x^2) / (27 + 9 x^2) for |x| <= 3 (Padé tanh: unity slope at 0, reaching +-1
    with zero slope at +-3), sign (x) beyond. Its antiderivative is closed-form:
        F(x) = x^2 / 18 + (4/3) ln (9 x^2 + 27)        (|x| <= 3)
        F(x) = |x| - 3 + F(3)                          (|x| > 3)
    y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) averages f over each sample interval, which
    removes most of the aliasing a pointwise clipper would fold back, without oversampling. It
    delays by half a sample and is evaluated in double (the difference quotient cancels).
*/
class SoftDrive
{
public:
    void reset() noexcept
    {
        xPrev = 0.0;
        fPrev = antiderivative (0.0);
    }

    static double shape (double x) noexcept
    {
        if (x >= 3.0) return 1.0;
        if (x <= -3.0) return -1.0;
        return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
    }

    static double antiderivative (double x) noexcept
    {
        constexpr double atThree = 6.742841636165626;   // F (3) = 0.5 + (4/3) ln 108 (no static init on the audio thread)
        const auto a = std::abs (x);
        if (a > 3.0)
            return a - 3.0 + atThree;
        return x * x / 18.0 + (4.0 / 3.0) * std::log (9.0 * x * x + 27.0);
    }

    float process (float input) noexcept
    {
        const auto x = (double) input;
        const auto f = antiderivative (x);
        const auto dx = x - xPrev;
        const auto y = std::abs (dx) > 1.0e-6 ? (f - fPrev) / dx : shape (0.5 * (x + xPrev));
        xPrev = x;
        fPrev = f;
        return (float) y;
    }

private:
    double xPrev = 0.0, fPrev = 0.0;
};

} // namespace cerulean::dsp
