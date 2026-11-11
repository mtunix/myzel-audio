#include "dsp/models/reel/ReelRecordAmp.h"

#include <algorithm>
#include <cmath>

namespace umber::dsp::reel
{

namespace
{
    double softplus (double x) noexcept
    {
        return x > 30.0 ? x : std::log1p (std::exp (x));
    }

    /** Node equations of the stage, scaled to volts. x = { grid, cathode, plate }. */
    void residual (double vin, double vp0, bool acLoad, const double* x, double* r) noexcept
    {
        using T = TriodeStage;
        const auto vgk = x[0] - x[1];
        const auto ig  = T::gridCurrent (vgk);
        const auto ip  = T::plateCurrent (vgk, x[2] - x[1]);
        const auto iLoad = acLoad ? (x[2] - vp0) / T::loadR : 0.0;

        r[0] = x[0] - vin + T::sourceR * ig;                                // grid: source resistor drop
        r[1] = x[1] - T::cathodeR * (ip + ig);                              // cathode: Ik = Ip + Ig
        r[2] = T::plateR * ((T::supply - x[2]) / T::plateR - iLoad - ip);   // plate KCL (x Rp -> volts)
    }

    double norm (const double* r) noexcept
    {
        return std::sqrt (r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    }

    /** Solves J x = b (3x3) by Gaussian elimination with partial pivoting. */
    bool solve3 (double J[3][3], double* b) noexcept
    {
        for (int col = 0; col < 3; ++col)
        {
            int pivot = col;
            for (int row = col + 1; row < 3; ++row)
                if (std::abs (J[row][col]) > std::abs (J[pivot][col]))
                    pivot = row;

            if (std::abs (J[pivot][col]) < 1.0e-300)
                return false;

            if (pivot != col)
            {
                std::swap (J[pivot], J[col]);
                std::swap (b[pivot], b[col]);
            }

            for (int row = col + 1; row < 3; ++row)
            {
                const auto f = J[row][col] / J[col][col];
                for (int k = col; k < 3; ++k)
                    J[row][k] -= f * J[col][k];
                b[row] -= f * b[col];
            }
        }

        for (int row = 2; row >= 0; --row)
        {
            for (int k = row + 1; k < 3; ++k)
                b[row] -= J[row][k] * b[k];
            b[row] /= J[row][row];
        }

        return true;
    }

    void jacobian (double vin, double vp0, bool acLoad, const double* x, double J[3][3]) noexcept
    {
        for (int j = 0; j < 3; ++j)
        {
            const auto h = 1.0e-7 * std::max (1.0, std::abs (x[j]));
            double xp[3] = { x[0], x[1], x[2] }, xm[3] = { x[0], x[1], x[2] };
            xp[j] += h;
            xm[j] -= h;
            double rp[3], rm[3];
            residual (vin, vp0, acLoad, xp, rp);
            residual (vin, vp0, acLoad, xm, rm);
            for (int i = 0; i < 3; ++i)
                J[i][j] = (rp[i] - rm[i]) / (2.0 * h);
        }
    }

    /** Damped Newton–Raphson with backtracking. Offline use only (table build / tests). */
    TriodeStage::Solution newton (double vin, double vp0, bool acLoad, TriodeStage::Solution guess) noexcept
    {
        double x[3] = { guess.grid, guess.cathode, guess.plate };
        double r[3];
        residual (vin, vp0, acLoad, x, r);
        auto rn = norm (r);
        bool ok = false;

        for (int iter = 0; iter < 200; ++iter)
        {
            if (rn < 1.0e-11)
            {
                ok = true;
                break;
            }

            double J[3][3];
            jacobian (vin, vp0, acLoad, x, J);
            double dx[3] = { -r[0], -r[1], -r[2] };
            if (! solve3 (J, dx))
                break;

            // limit the step to 20 V (keeps the exponentials sane far from the solution)
            const auto biggest = std::max ({ std::abs (dx[0]), std::abs (dx[1]), std::abs (dx[2]) });
            if (biggest > 20.0)
                for (auto& d : dx)
                    d *= 20.0 / biggest;

            double lambda = 1.0;
            bool accepted = false;
            for (int ls = 0; ls < 30; ++ls, lambda *= 0.5)
            {
                double xn[3] = { x[0] + lambda * dx[0], x[1] + lambda * dx[1], x[2] + lambda * dx[2] };
                double rnew[3];
                residual (vin, vp0, acLoad, xn, rnew);
                const auto nn = norm (rnew);
                if (nn < rn || nn < 1.0e-11)
                {
                    std::copy (xn, xn + 3, x);
                    std::copy (rnew, rnew + 3, r);
                    rn = nn;
                    accepted = true;
                    break;
                }
            }

            if (! accepted)
            {
                ok = rn < 1.0e-8;
                break;
            }
        }

        TriodeStage::Solution s;
        s.grid = x[0];
        s.cathode = x[1];
        s.plate = x[2];
        s.converged = ok;

        // implicit-function theorem: J dx/dvin = -dr/dvin = (1, 0, 0)
        double J[3][3];
        jacobian (vin, vp0, acLoad, x, J);
        double d[3] = { 1.0, 0.0, 0.0 };
        s.plateSlope = solve3 (J, d) ? d[2] : 0.0;
        return s;
    }
} // namespace

double TriodeStage::plateCurrent (double vgk, double vpk) noexcept
{
    if (vpk <= 0.0)
        return 0.0;

    const auto e1 = vpk / kp * softplus (kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk)));
    return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
}

double TriodeStage::gridCurrent (double vgk) noexcept
{
    return gridG * std::pow (softplus (gridC * vgk) / gridC, gridXi);
}

TriodeStage::Solution TriodeStage::quiescent() noexcept
{
    Solution guess;
    guess.grid = 0.0;
    guess.cathode = 1.0;
    guess.plate = 0.6 * supply;
    return newton (0.0, 0.0, false, guess);
}

TriodeStage::Solution TriodeStage::solve (double vin, const Solution& guess) noexcept
{
    static const auto q = quiescent();
    return newton (vin, q.plate, true, guess);
}

const RecordAmpCurve& RecordAmpCurve::instance()
{
    static const RecordAmpCurve curve;
    return curve;
}

RecordAmpCurve::RecordAmpCurve()
{
    nodes.resize ((size_t) numIntervals + 1);

    const auto q = TriodeStage::quiescent();
    quiescentPlate = q.plate;
    converged = q.converged;

    const int centre = numIntervals / 2;

    // march outwards from the operating point, warm-starting each solve from its neighbour
    for (int direction : { +1, -1 })
    {
        auto guess = q;
        for (int i = centre; i >= 0 && i <= numIntervals; i += direction)
        {
            const auto vin = -inputRange + i * step;
            const auto s = TriodeStage::solve (vin, guess);
            converged = converged && s.converged;
            nodes[(size_t) i] = { s.plate - quiescentPlate, s.plateSlope };
            guess = s;
        }
    }

    nodes[(size_t) centre].value = 0.0; // exact silence in -> exact silence out
    gainAtZero = nodes[(size_t) centre].slope;
}

} // namespace umber::dsp::reel
