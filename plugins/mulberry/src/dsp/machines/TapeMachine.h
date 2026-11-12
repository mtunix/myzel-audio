#pragma once

#include "dsp/common/Modulation.h"
#include "dsp/common/TapeHeads.h"
#include "dsp/common/TapeLosses.h"
#include "dsp/common/TapeRecord.h"
#include "dsp/common/Transport.h"
#include "dsp/machines/Machine.h"

#include <utility>

namespace mulberry::dsp
{

/**
    What Loop and Reel share: a transport per line, the tape (position-tracked heads), the 8x
    record path with the Jiles-Atherton core, and the playback losses at the instantaneous speed.

    A machine says, per block, how fast the tape should run and where its heads are (as a
    distance per unit of the plan's Time fraction); this class does the rest. Every read is a
    head at a distance on the tape: its pitch is exactly the ratio of the speeds when it plays
    and when it was recorded (TapeTrack), and its tone follows the tape speed (TapeLosses, in the
    record path: the losses depend only on the recorded wavelength; corners every 32 samples).
*/
class TapeMachine : public EchoMachine
{
public:
    explicit TapeMachine (uint32_t seed) : randomSeed (seed) {}

    void prepare (double sampleRate, SlotMemory& memory) override;
    void reset() noexcept override;
    void process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                  const MachineSettings& settings, int numSamples) noexcept override;
    double predictedPassGain (double hz, const MachineSettings& settings) const noexcept override;
    float currentTimeMs (int line) const noexcept override;

    /** The speed of a line's tape (m/s, with wobble), for tests. */
    double tapeSpeed (int line) const noexcept { return transports[(size_t) line].getSpeed(); }

protected:
    /** Per block: the machine's targets. Distances are metres of tape for one Time. */
    struct Setup
    {
        std::array<double, 2> speed {};             // transport target, m/s
        std::array<double, 2> distance {};          // head distance for fraction 1 (target), metres
        TapeGeometry geometry;
        double lossSpeedMin = 0.0, lossSpeedMax = 1.0e9;    // losses hold beyond the physical range
        TapeRecordPath::Voicing record;
        Transport::Wobble wobble;                   // nominal (Wobble 0.5), scaled by the chain's Wobble
        double wobbleRandom = 0.5;                  // nominal random share
        double hissDb = -70.0;                      // RMS at Noise 50 %
        double wear = 0.0;                          // Loop: dropouts and the erase ghost
        double loopLength = 0.0;                    // Loop: metres of tape in the loop
        int brushes = 1;                            // Oil Can: pickups per head, spread over an arc
        double brushSpread = 0.0;                   // the arc, as a fraction of the head's distance
        double brushTilt = 0.0;                     // 0: equal pickups; > 0: the first one dominates
        double leakSeconds = 0.0;                   // Oil Can: charge leaks with dwell time (0: none)
    };

    static constexpr int maxBrushes = 5;

    virtual Setup setup (const MachineSettings& s) const noexcept = 0;
    virtual Transport::Mechanics mechanics() const noexcept = 0;

    /** Head distance glides (Reel's sliding head); Loop's heads are fixed: no glide. */
    virtual bool headsSlide() const noexcept { return false; }

    /** The dropout gain of a head reading at a tape position (Loop's wear map). */
    float dropout (double position) const noexcept;

    uint32_t randomSeed;
    double fs = 48000.0;

private:
    static constexpr int historyLength = 16;

    struct Line
    {
        TapeTrack track;
        std::array<std::array<TapeTrack::Head, maxBrushes>, ReadList::maxReads> heads {};
        TapeTrack::Head ghost;
        TapeRecordPath record;
        InertialGlide distance;                         // metres for fraction 1
        std::array<double, historyLength> positions {}; // record-head positions, for the path's latency
        int historyIndex = 0;
        WhiteNoise hiss;
    };

    void updateLosses (const Setup& s) noexcept;

    std::array<Line, 2> lines;
    std::array<Transport, 2> transports;
    std::array<double, 2> positions {};     // tape under the record head, metres
    double dualMix = 0.0;
    bool snapNext = true;
    double inverseLoopLength = 0.0;
    Setup current;
    std::array<float, 512> wearMap {};
    int lossCountdown = 0;
    double lossSpeed = 0.0;
    std::array<double, maxBrushes> brushOffset {};
    std::array<float, maxBrushes> brushWeight {};
    std::array<std::array<std::array<float, maxBrushes>, ReadList::maxReads>, 2> leakGain {};
    std::array<std::pair<double, float>, ReadList::maxReads> dropoutMemo {};
};

} // namespace mulberry::dsp
