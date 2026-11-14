#pragma once

#include "dsp/common/BSpline.h"
#include "dsp/common/FrequencyMap.h"
#include "dsp/common/Polynomial.h"

#include <array>
#include <complex>

namespace viridian::dsp
{

/**
    A network block above second order (interacting controls) as a cascade of complex stages. Its
    poles and zeros are tracked from design to design (warm-started Aberth), so each keeps its
    index while a control moves; at a fresh design the poles are paired once (conjugates, then the
    nearest real roots) and each pair, with the zeros nearest it, becomes one stage: the pair's
    digital roots over its zeros', in transposed direct form with complex coefficients. A pair that
    meets and turns complex inside its stage moves smoothly (the stage's coefficients are the
    pair's sums and products); nothing is ever re-grouped. Roots of different stages that meet
    leave a square-root corner in each stage's path, which the coefficients' quartic B-spline
    through the designs (BSpline.h) smooths. Roots beyond a thousand times the sample rate are at
    infinity. The product of the stages is the block's real response and the output is its real
    part (tools/netlist/README.md, "Interacting blocks"). Minimum phase. Real-time safe; not
    thread safe.
*/
class RootCascade
{
public:
    static constexpr int maxRoots = poly::maxDegree;
    static constexpr int maxChannels = 2;
    static constexpr int maxMapDegree = FrequencyMap::maxDegree;
    static constexpr int maxOrder = 2 * maxMapDegree;   // a stage: two analog roots

    /**
        Designs from num / den in sigma = s / omegaRef (ascending). `fresh` drops the tracking (a
        new topology): roots are found cold and paired anew, without a ramp. Otherwise every
        coefficient ramps to the new design over `rampSamples`.
    */
    void design (const FrequencyMap&, const double* num, int numDegree, const double* den, int denDegree,
                 double omegaRef, int rampSamples, bool fresh) noexcept;

    /**
        A fresh design in bounded steps (the cold roots' sweeps, then each root's map, so much per
        step): beginFresh(), then freshStep() until it returns true. The same design as design()
        with `fresh`, to the bit. The map must outlive the steps; process() must not run in
        between.
    */
    void beginFresh (const FrequencyMap&, const double* num, int numDegree, const double* den, int denDegree,
                     double omegaRef) noexcept;
    bool freshStep() noexcept;

    void reset() noexcept;
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    /**
        One channel of each of two cascades of the same structure (a stereo pair with a spread
        between its channels) in one loop, which overlaps their recursions: the same output as
        processing each. Only when both are at rest with real stages; false (nothing processed)
        otherwise.
    */
    static bool processPair (RootCascade& a, double* xa, RootCascade& b, double* xb, int numSamples) noexcept;
    void flushDenormals() noexcept;

    /** The realised digital response of the latest design. */
    std::complex<double> response (double hz) const noexcept;

    /** The analog block the latest design matches. */
    std::complex<double> analogResponse (double hz) const noexcept;

    int numStages() const noexcept { return stages; }
    int numPoles() const noexcept { return poleCount; }

    /** The pole a label holds (in sigma), as tracked from design to design (tests). */
    std::complex<double> pole (int label) const noexcept { return poles[(size_t) label]; }

    /** The labels of the poles one stage holds (-1: none). */
    std::array<int, 2> stagePoles (int stage) const noexcept { return members[(size_t) stage].poles; }
    bool reflectedUnstablePole() const noexcept { return reflected; }

private:
    using Complex = std::complex<double>;

    struct Coefficients
    {
        // b[0 ... order] / (1 + a[1] z^-1 + ...), real and imaginary parts apart.
        std::array<double, maxOrder + 1> br {}, bi {}, ar {}, ai {};
        bool real = true;       // bi and ai are exactly 0: the stage's roots are closed under conjugation
    };

    struct Design
    {
        std::array<Coefficients, maxRoots> stage {};
        double gain = 0.0;
    };

    /** The pole and zero labels one stage holds (-1: none). */
    struct Members
    {
        std::array<int, 2> poles { -1, -1 }, zeros { -1, -1 };
    };

    enum class Job
    {
        idle,
        poles,
        zeros,
        pairing,
        mapPoles,
        mapZeros,
        stages
    };

    void take (const FrequencyMap&, const double* num, int numDegree, const double* den, int denDegree, double omegaRef) noexcept;
    void startRoots (const double* c, int degree) noexcept;
    double infinityRadius() const noexcept;
    void copyDesign (Design& to, const Design& from) const noexcept;
    void restAtTarget() noexcept;
    void shiftPast() noexcept;
    double nextCost() const noexcept;
    void nextItem() noexcept;
    void mapRoots (bool zeros, int from, int to) noexcept;
    void mirrorLowerRoots (bool zeros) noexcept;
    void makeStages() noexcept;
    void pair() noexcept;
    template <int N>
    void blend (double t) noexcept;
    template <int N>
    void run (double* const* channels, int numChannels, int numSamples) noexcept;
    template <int N>
    static void runRealLanes (RootCascade& a, int channelA, double* xa, RootCascade& b, int channelB, double* xb, int numSamples) noexcept;
    bool atRest() const noexcept;
    bool allStagesReal (int channel) const noexcept;
    Complex stageResponse (const Coefficients&, Complex zi) const noexcept;

    int stages = 0, poleCount = 0, numZeros = 0, mapDegree = 1, order = 2;
    double fs = 48000.0, omegaScale = 1.0;
    bool designed = false, reflected = false;
    std::array<double, maxRoots + 1> analogNum {}, analogDen {};
    int analogNumDegree = 0, analogDenDegree = 0;

    std::array<Complex, maxRoots> poles {}, zeros {};
    std::array<bool, maxRoots> poleInfinite {}, zeroInfinite {};
    std::array<Members, maxRoots> members {};
    std::array<FrequencyMap::Track, maxRoots> poleTracks {}, zeroTracks {};
    std::array<std::array<Complex, maxMapDegree>, maxRoots> digitalPoles {}, digitalZeros {};

    const FrequencyMap* jobMap = nullptr;
    Job job = Job::idle;
    poly::RootFinder finder;
    int mapNext = 0;

    // The designs before the latest, in a ring from `oldest`: with it, the coefficients' spline points.
    Design target, now;
    std::array<Design, spline::points - 1> past {};
    int oldest = 0, rampLength = 0, rampPosition = 0, settle = 0;

    struct State
    {
        std::array<double, maxOrder> re {}, im {};
    };
    std::array<std::array<State, maxRoots>, maxChannels> state {};
};

} // namespace viridian::dsp
