// The display feed (src/dsp/feed/NOTES.md): the sample streams, the Timeline's history, the event
// stamps, Spectral's published cut and target, and the message-thread helpers that read them (the
// spectrum, the meters, the cut curves).

#include "ChainRun.h"

#include "dsp/feed/CutCurves.h"
#include "dsp/feed/SpectrumAnalyser.h"
#include "dsp/shapes/spectral/SpectralShape.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace halite;
using namespace halite::testing;
using Catch::Approx;

namespace
{
    /** A voice with esses every 0.4 s (as ChainBehaviourTests), the right channel quieter. */
    Channels voiceWithEsses (double rate, double seconds, unsigned seed = 1)
    {
        const auto n = samplesFor (seconds, rate);
        auto voice = harmonic ({ .f0 = 160.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3200.0, .peakDb = 18.0, .rms = 0.1, .seed = seed }, n, rate);
        for (auto t = 0.2; t + 0.15 < seconds; t += 0.4)
            voice = sum (voice, gated (bandNoise (4500.0, 12000.0, n, rate, seed + (unsigned) (t * 100.0), 0.04), samplesFor (t, rate),
                                       samplesFor (t + 0.15, rate), rate));
        return { voice, scaled (voice, 0.7) };
    }

    /** Called after each block with the chain, so a test can read what it published. */
    using AfterBlock = std::function<void (const dsp::HaliteChain&, int end)>;

    /** The chain block by block (cycling `blocks`), with a timeline per block. */
    Channels runBlocks (dsp::HaliteChain& chain, const Channels& in, dsp::ChainSettings s, const std::vector<int>& blocks,
                        const std::function<dsp::Timeline (int position)>& timeline = {}, const AfterBlock& after = {},
                        const Automation& automate = {})
    {
        const juce::ScopedNoDenormals noDenormals;
        auto out = in;
        const auto n = (int) in[0].size();
        size_t k = 0;
        for (int pos = 0; pos < n;)
        {
            const auto len = std::min (blocks[k++ % blocks.size()], n - pos);
            if (automate)
                automate (pos, s);
            std::array<float*, 2> channels {};
            for (size_t c = 0; c < out.size(); ++c)
                channels[c] = out[c].data() + pos;
            chain.process (channels.data(), (int) out.size(), len, s, nullptr, 0, timeline ? timeline (pos) : dsp::Timeline {});
            pos += len;
            if (after)
                after (chain, pos);
        }
        return out;
    }

    std::vector<dsp::EssEvent> eventsOf (const dsp::HaliteChain& chain)
    {
        std::vector<dsp::EssEvent> events ((size_t) dsp::EventRing::size);
        std::uint64_t cursor = 0;
        events.resize ((size_t) chain.events().read (cursor, events.data(), (int) events.size()));
        return events;
    }

    std::vector<dsp::HistoryRecord> historyOf (const dsp::DisplayFeed& feed, std::int64_t& first)
    {
        std::vector<dsp::HistoryRecord> records ((size_t) dsp::HopHistory::capacity);
        auto cursor = feed.history().oldest();
        const auto n = feed.history().read (cursor, records.data(), (int) records.size());
        records.resize ((size_t) n);
        first = cursor - n;
        return records;
    }

    float dbOf (double power) { return (float) (10.0 * std::log10 (power)); }

    bool exact (float a, float b) { return myzel::dsp::isExactly (a, b); }
}

TEST_CASE ("Feed: the sample streams carry the latency-aligned input and the output, mono twice", "[feed]")
{
    for (const auto numChannels : { 1, 2 })
    {
        constexpr double rate = 48000.0;
        auto in = voiceWithEsses (rate, 2.0);
        in.resize ((size_t) numChannels);
        dsp::HaliteChain chain;
        chain.prepare (rate, 4096, numChannels, LatencyMode::normal);
        const auto out = runBlocks (chain, in, {}, { 512, 100, 1000, 37 });

        const auto& fifo = chain.feed().samples();
        const auto n = (int) in[0].size();
        const auto latency = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
        REQUIRE (fifo.count() == (std::uint64_t) n);

        constexpr int span = 30000;
        std::array<std::vector<float>, dsp::SampleFifo::numStreams> streams;
        float* pointers[dsp::SampleFifo::numStreams];
        for (size_t st = 0; st < streams.size(); ++st)
        {
            streams[st].resize (span);
            pointers[st] = streams[st].data();
        }
        REQUIRE (fifo.read (fifo.count(), span, pointers));

        auto mismatches = 0;
        for (int c = 0; c < 2; ++c)
        {
            const auto source = std::min (c, numChannels - 1);
            for (int i = 0; i < span; ++i)
            {
                const auto t = n - span + i;
                mismatches += ! sameBits (streams[(size_t) dsp::SampleFifo::inputStream (c)][(size_t) i], in[(size_t) source][(size_t) (t - latency)]);
                mismatches += ! sameBits (streams[(size_t) dsp::SampleFifo::outputStream (c)][(size_t) i], out[(size_t) source][(size_t) t]);
            }
        }
        INFO ("channels " << numChannels);
        CHECK (mismatches == 0);
    }
}

TEST_CASE ("Feed: a sample-stream copy the writer overtook is reported, not returned as intact", "[feed]")
{
    dsp::SampleFifo fifo;
    std::vector<float> block (1000);
    std::vector<float> copy (4096);
    float* out[dsp::SampleFifo::numStreams] { copy.data(), nullptr, nullptr, nullptr };

    // Before the first sample the stream reads silence.
    std::fill (block.begin(), block.end(), 1.0f);
    fifo.begin (1000);
    for (int s = 0; s < dsp::SampleFifo::numStreams; ++s)
        fifo.write (s, block.data(), 1000);
    fifo.end (1000);
    REQUIRE (fifo.read (fifo.count(), 2000, out));
    CHECK (exact (copy[999], 0.0f));
    CHECK (exact (copy[1000], 1.0f));

    for (int b = 0; b < 100; ++b)
    {
        for (int i = 0; i < 1000; ++i)
            block[(size_t) i] = (float) (1000 * (b + 1) + i);
        fifo.begin (1000);
        for (int s = 0; s < dsp::SampleFifo::numStreams; ++s)
            fifo.write (s, block.data(), 1000);
        fifo.end (1000);
    }
    CHECK (fifo.read (fifo.count(), 4096, out));
    CHECK (exact (copy[4095], (float) (fifo.count() - 1)));
    CHECK_FALSE (fifo.read (fifo.count() - dsp::SampleFifo::capacity, 10, out));
    CHECK (fifo.read (fifo.oldestSafe() + 10, 10, out));
}

TEST_CASE ("Feed: readers on another thread never accept a sample or record the writer overwrote", "[feed][threads]")
{
    // Every value encodes its own position, so a torn or overwritten copy that passes the check shows.
    dsp::SampleFifo fifo;
    dsp::HopHistory history;
    std::atomic<bool> done { false };

    std::thread writer ([&]
    {
        std::array<float, 256> chunk {};
        std::uint64_t position = 0;
        std::int64_t record = 0;
        while (! done.load())
        {
            for (int i = 0; i < 256; ++i)
                chunk[(size_t) i] = (float) ((position + (std::uint64_t) i) & 0x7fffff);
            fifo.begin (256);
            for (int s = 0; s < dsp::SampleFifo::numStreams; ++s)
                fifo.write (s, chunk.data(), 256);
            fifo.end (256);
            position += 256;
            for (int r = 0; r < 8; ++r, ++record)
            {
                const auto v = (float) (record & 0x7fffff);
                history.push ({ v, v, v, v, v, v });
            }
        }
    });

    long samplesChecked = 0, recordsChecked = 0, bad = 0, refused = 0;
    std::vector<float> copy (4096);
    float* out[dsp::SampleFifo::numStreams] { copy.data(), nullptr, nullptr, copy.data() };
    std::vector<dsp::HistoryRecord> records (2048);
    std::int64_t cursor = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (400);
    std::mt19937 rng (3u);
    while (std::chrono::steady_clock::now() < until)
    {
        // An old window now and then, to meet the writer at the edge.
        const auto end = fifo.count();
        const auto back = rng() % 4 == 0 ? (std::uint64_t) (dsp::SampleFifo::capacity - 2048) : 0;
        const auto at = end > back + 4096 ? end - back : end;
        if (at >= 4096)
        {
            if (fifo.read (at, 4096, out))
            {
                for (int i = 0; i < 4096; ++i)
                    bad += ! exact (copy[(size_t) i], (float) ((at - 4096 + (std::uint64_t) i) & 0x7fffff));
                samplesChecked += 4096;
            }
            else
            {
                ++refused;
            }
        }

        const auto before = cursor;
        const auto n = history.read (cursor, records.data(), (int) records.size());
        for (int i = 0; i < n; ++i)
        {
            const auto expected = (float) ((cursor - n + i) & 0x7fffff);
            bad += ! exact (records[(size_t) i].sibilanceDb, expected) || ! exact (records[(size_t) i].removedPeak, expected);
        }
        recordsChecked += n;
        if (rng() % 8 == 0)
            cursor = std::max<std::int64_t> (0, before - dsp::HopHistory::capacity);
    }
    done = true;
    writer.join();

    INFO (samplesChecked << " samples and " << recordsChecked << " records checked, " << refused << " copies refused");
    CHECK (samplesChecked > 0);
    CHECK (recordsChecked > 0);
    CHECK (bad == 0);
}

TEST_CASE ("Feed: history records hold the audio leaving the chain and the detector's values for it", "[feed][history]")
{
    constexpr double rate = 48000.0;
    const auto in = voiceWithEsses (rate, 3.0);
    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    dsp::ChainSettings s;
    s.detector.harshOn = true;
    s.detector.harshThresholdDb = -20.0f;
    const auto out = runBlocks (chain, in, s, { 512, 100, 1000, 37 });

    const auto& feed = chain.feed();
    const auto latency = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const auto n = (int) in[0].size();
    REQUIRE (feed.recordSamples() == 32);
    REQUIRE (feed.clock() == n);
    // Records close once the next one begins: the open one holds the last input sample out.
    CHECK (feed.history().count() == (n - latency - 1) / 32);

    std::int64_t first = 0;
    const auto records = historyOf (feed, first);
    REQUIRE (first == 0);
    REQUIRE ((std::int64_t) records.size() == feed.history().count());

    auto peakMismatches = 0;
    auto removedError = 0.0, rmsError = 0.0;
    auto largestEss = 0.0f, largestHarsh = 0.0f;
    for (size_t m = 0; m < records.size(); ++m)
    {
        auto inPeak = 0.0f, outPeak = 0.0f, removedPeak = 0.0f;
        std::array<double, 2> squares {};
        for (int c = 0; c < 2; ++c)
            for (int t = (int) m * 32; t < (int) m * 32 + 32; ++t)
            {
                const auto x = in[(size_t) c][(size_t) t], y = out[(size_t) c][(size_t) (t + latency)];
                inPeak = std::max (inPeak, std::abs (x));
                outPeak = std::max (outPeak, std::abs (y));
                removedPeak = std::max (removedPeak, std::abs (x - y));
                squares[(size_t) c] += (double) y * y;
            }
        const auto& r = records[m];
        peakMismatches += ! sameBits (r.inputPeak, inPeak) || ! sameBits (r.outputPeak, outPeak);
        removedError = std::max (removedError, (double) std::abs (r.removedPeak - removedPeak));
        rmsError = std::max (rmsError, std::abs (r.outputRms - std::sqrt (std::max (squares[0], squares[1]) / 32.0)));
        largestEss = std::max (largestEss, r.essDb);
        largestHarsh = std::max (largestHarsh, r.harshDb);
    }
    CHECK (peakMismatches == 0);
    CHECK (removedError < 1.0e-6);
    CHECK (rmsError < 1.0e-6);

    // The events' peaks are the records' reductions; each event's start is in a record that cuts.
    const auto events = eventsOf (chain);
    REQUIRE (events.size() >= 5);
    auto largestEvent = 0.0f;
    for (const auto& e : events)
    {
        largestEvent = std::max (largestEvent, e.peakReductionDb);
        const auto m = (size_t) feed.recordOf (e.elapsed);
        REQUIRE (m < records.size());
        CHECK (records[m].essDb > dsp::EventTracker::startDb);
        if (m > 0)
            CHECK (records[m - 1].essDb <= dsp::EventTracker::startDb);

        // Over the event's records, S peaks where the event says it did.
        auto peakS = dsp::silentDb;
        for (auto r = m; r <= (size_t) feed.recordOf (e.elapsed + (e.end - e.start) - 1) && r < records.size(); ++r)
            peakS = std::max (peakS, records[r].sibilanceDb);
        CHECK (peakS == Approx (e.peakSibilanceDb).margin (1.0e-4));
    }
    CHECK (largestEss == Approx (largestEvent).margin (1.0e-4));
    CHECK (largestEss > 1.0f);
    CHECK (largestHarsh > 0.0f);
}

TEST_CASE ("Feed: a record's output RMS is the de-essed signal's, the louder channel's, before Listen and Output", "[feed][history]")
{
    constexpr double rate = 48000.0;
    constexpr int records = 40, length = 32;

    // `left` and `right` as the input, `removedShare` of it removed, `out` (Listen, Output, Bypass) silent:
    // the records' output RMS and peak, with no latency so record m holds samples [32 m, 32 m + 32).
    const auto run = [&] (const std::vector<float>& left, const std::vector<float>& right, float removedShare, int numChannels)
    {
        dsp::DisplayFeed feed;
        feed.prepare (rate, dsp::HaliteChain::frameSize (rate));
        REQUIRE (feed.recordSamples() == length);

        const auto n = (int) left.size();
        std::vector<float> removedLeft (left), removedRight (right), silence ((size_t) n, 0.0f);
        for (auto* v : { &removedLeft, &removedRight })
            for (auto& x : *v)
                x *= removedShare;
        const std::array<const float*, 2> dry { left.data(), right.data() }, removed { removedLeft.data(), removedRight.data() },
            out { silence.data(), silence.data() };
        for (int pos = 0; pos < n; pos += 100)
        {
            const auto len = std::min (100, n - pos);
            const std::array<const float*, 2> d { dry[0] + pos, dry[1] + pos }, r { removed[0] + pos, removed[1] + pos },
                o { out[0] + pos, out[1] + pos };
            feed.audio (d.data(), r.data(), o.data(), numChannels, len, 0);
        }

        std::int64_t first = 0;
        auto history = historyOf (feed, first);
        REQUIRE (first == 0);
        REQUIRE (history.size() == (size_t) records);
        return history;
    };

    // One more sample than the records, so the last one closes.
    const auto n = records * length + 1;
    const auto sine = [&] (double amplitude, double hz)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
        return x;
    };

    SECTION ("a sine's RMS lies 3 dB under its peak, whole periods to a record")
    {
        for (const auto hz : { 1500.0, 3000.0, 6000.0 })
            for (const auto& r : run (sine (0.5, hz), sine (0.5, hz), 0.0f, 2))
            {
                INFO (hz << " Hz");
                CHECK (r.outputPeak == Approx (0.5).margin (1.0e-6));
                CHECK (r.outputRms == Approx (0.5 / std::sqrt (2.0)).margin (1.0e-6));
                CHECK (20.0 * std::log10 (r.outputRms / r.outputPeak) == Approx (-3.0103).margin (1.0e-3));
            }
    }

    SECTION ("silence reads 0")
    {
        const std::vector<float> zero ((size_t) n, 0.0f);
        for (const auto& r : run (zero, zero, 0.0f, 2))
        {
            CHECK (sameBits (r.outputRms, 0.0f));
            CHECK (sameBits (r.outputPeak, 0.0f));
        }
    }

    SECTION ("the louder channel by RMS, not the one with the larger peak")
    {
        // Left a steady 0.5 (RMS 0.5); right one click of 0.9 a record (RMS 0.9 / sqrt (32)).
        std::vector<float> steady ((size_t) n, 0.5f), clicks ((size_t) n, 0.0f);
        for (int i = 7; i < n; i += length)
            clicks[(size_t) i] = 0.9f;
        for (const auto& r : run (steady, clicks, 0.0f, 2))
        {
            CHECK (r.outputPeak == Approx (0.9));
            CHECK (r.outputRms == Approx (0.5).margin (1.0e-6));
        }
        for (const auto& r : run (clicks, steady, 0.0f, 2))
            CHECK (r.outputRms == Approx (0.5).margin (1.0e-6));

        for (const auto& r : run (sine (0.2, 1500.0), sine (0.6, 1500.0), 0.0f, 2))
            CHECK (r.outputRms == Approx (0.6 / std::sqrt (2.0)).margin (1.0e-6));
    }

    SECTION ("what the bands removed is not in it, nor what Listen and Output send")
    {
        for (const auto& r : run (sine (0.8, 3000.0), sine (0.8, 3000.0), 0.75f, 2))
            CHECK (r.outputRms == Approx (0.2 / std::sqrt (2.0)).margin (1.0e-6));
    }

    SECTION ("mono reads its one channel")
    {
        const std::vector<float> zero ((size_t) n, 0.0f);
        for (const auto& r : run (sine (0.4, 1500.0), zero, 0.0f, 1))
            CHECK (r.outputRms == Approx (0.4 / std::sqrt (2.0)).margin (1.0e-6));
    }
}

TEST_CASE ("Feed: one record per span of input at any rate, latency and change of latency", "[feed][history]")
{
    for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        for (const auto mode : { LatencyMode::normal, LatencyMode::live })
        {
            const auto in = voiceWithEsses (rate, 1.0);
            dsp::HaliteChain chain;
            chain.prepare (rate, 4096, 2, mode);
            dsp::ChainSettings s;
            s.detector.latency = mode;
            runBlocks (chain, in, s, { 777, 64 });

            const auto& feed = chain.feed();
            const auto length = feed.recordSamples();
            CHECK (length == 32 * (int) std::ceil (rate / 50000.0));
            const auto end = feed.clock() - dsp::HaliteChain::latencySamples (mode, rate);
            INFO ("rate " << rate << ", mode " << (int) mode);
            CHECK (feed.history().count() == (end - 1) / length);
        }

    // Normal to Live skips a frame of input, Live to Normal repeats one: the records stay one per span.
    constexpr double rate = 48000.0;
    const auto in = voiceWithEsses (rate, 2.0);
    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    std::int64_t last = 0;
    auto backwards = 0;
    runBlocks (chain, in, {}, { 512 }, {},
               [&] (const dsp::HaliteChain& c, int)
               {
                   backwards += c.feed().history().count() < last;
                   last = c.feed().history().count();
               },
               [] (int position, dsp::ChainSettings& s)
               {
                   s.detector.latency = position > 30000 && position < 60000 ? LatencyMode::live : LatencyMode::normal;
               });
    CHECK (backwards == 0);
    const auto end = chain.feed().clock() - dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    CHECK (chain.feed().history().count() == (end - 1) / 32);
}

TEST_CASE ("Feed: events carry the song position while the host plays, the elapsed time otherwise", "[feed][events]")
{
    constexpr double rate = 48000.0;
    const auto in = voiceWithEsses (rate, 4.0);
    constexpr std::int64_t songStart = 1234567, jumpTo = 9000000;
    const auto stopAt = samplesFor (1.4, rate), jumpAt = samplesFor (2.6, rate);

    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    runBlocks (chain, in, {}, { 512 },
               [&] (int position) -> dsp::Timeline
               {
                   // Plays from songStart, stops (the host's position stays), plays again from jumpTo.
                   if (position < stopAt)
                       return { songStart + position, true, true };
                   if (position < jumpAt)
                       return { songStart + stopAt, true, false };
                   return { jumpTo + position - jumpAt, true, true };
               });

    const auto events = eventsOf (chain);
    REQUIRE (events.size() >= 8);
    auto played = 0, stopped = 0, after = 0;
    std::int64_t previous = -1;
    for (const auto& e : events)
    {
        CHECK (e.elapsed > previous);
        previous = e.elapsed;
        if (e.elapsed < stopAt - 4096)
        {
            ++played;
            CHECK (e.songPosition);
            CHECK (e.start - e.elapsed == songStart);
            CHECK (e.seconds == Approx ((double) e.start / rate));
        }
        else if (e.elapsed > stopAt + 4096 && e.elapsed < jumpAt - 4096)
        {
            ++stopped;
            CHECK_FALSE (e.songPosition);
            CHECK (e.seconds == Approx ((double) e.elapsed / rate));
        }
        else if (e.elapsed > jumpAt + 4096)
        {
            ++after;
            CHECK (e.songPosition);
            CHECK (e.start - e.elapsed == jumpTo - jumpAt);
            CHECK (e.seconds == Approx ((double) e.start / rate));
        }
    }
    CHECK (played >= 2);
    CHECK (stopped >= 2);
    CHECK (after >= 2);

    // No timeline (the Standalone): elapsed time from the first sample.
    dsp::HaliteChain standalone;
    standalone.prepare (rate, 4096, 2, LatencyMode::normal);
    runBlocks (standalone, in, {}, { 333 });
    for (const auto& e : eventsOf (standalone))
    {
        CHECK_FALSE (e.songPosition);
        CHECK (e.elapsed == e.start);
        CHECK (e.seconds == Approx ((double) e.start / rate));
    }

    // A prepare at the same rate keeps the clock; a new rate starts it again, and says so.
    const auto epoch = standalone.feed().epoch();
    const auto clock = standalone.feed().clock();
    standalone.prepare (rate, 512, 2, LatencyMode::live);
    CHECK (standalone.feed().epoch() == epoch);
    CHECK (standalone.feed().clock() == clock);
    standalone.prepare (96000.0, 512, 2, LatencyMode::normal);
    CHECK (standalone.feed().epoch() == epoch + 1);
    CHECK (standalone.feed().clock() == 0);
    CHECK (standalone.feed().history().count() == 0);
}

TEST_CASE ("Feed: the transport log stamps any record as its event is stamped, with an entry per start, stop and jump", "[feed][events]")
{
    constexpr double rate = 48000.0;
    const auto in = voiceWithEsses (rate, 4.0);
    constexpr std::int64_t songStart = 1234567, jumpTo = 9000000;
    const auto stopAt = samplesFor (1.4, rate), jumpAt = samplesFor (2.6, rate);

    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    runBlocks (chain, in, {}, { 512, 300 },
               [&] (int position) -> dsp::Timeline
               {
                   if (position < stopAt)
                       return { songStart + position, true, true };
                   if (position < jumpAt)
                       return { songStart + stopAt, true, false };
                   return { jumpTo + position - jumpAt, true, true };
               });

    std::vector<dsp::TransportLog::Entry> log ((size_t) dsp::TransportLog::capacity);
    std::uint64_t cursor = 0;
    log.resize ((size_t) chain.feed().transport().read (cursor, log.data(), (int) log.size()));

    // Playing from the first block, stopped (the host's position stays: no entry per block), playing after a jump.
    REQUIRE (log.size() == 3);
    CHECK ((log[0].from == 0 && log[0].playing && log[0].offset == -songStart));
    CHECK ((! log[1].playing && std::abs (log[1].from - stopAt) < 512));
    CHECK ((log[2].playing && log[2].offset == jumpAt - jumpTo && std::abs (log[2].from - jumpAt) < 512));

    auto inForce = [&] (std::int64_t elapsed)
    {
        auto e = log.front();
        for (const auto& entry : log)
            if (entry.from <= elapsed)
                e = entry;
        return e;
    };

    auto checked = 0;
    for (const auto& e : eventsOf (chain))
    {
        // An event is stamped when it is pushed, after its hangover: one that straddles a change is left out.
        if (std::abs (e.elapsed - stopAt) < 4096 || std::abs (e.elapsed - jumpAt) < 4096)
            continue;
        auto song = false;
        const auto seconds = dsp::timeOf (e.elapsed, inForce (e.elapsed), rate, song);
        CHECK (song == e.songPosition);
        CHECK (seconds == Approx (e.seconds).margin (1.0e-9));
        ++checked;
    }
    CHECK (checked >= 6);

    // A new rate restarts the clock; the next entry carries the new epoch, so a reader drops the old ones.
    chain.prepare (96000.0, 512, 2, LatencyMode::normal);
    runBlocks (chain, { std::vector<float> (2048), std::vector<float> (2048) }, {}, { 512 });
    dsp::TransportLog::Entry next[4];
    REQUIRE (chain.feed().transport().read (cursor, next, 4) == 1);
    CHECK ((next[0].epoch == chain.feed().epoch() && next[0].from == 0 && ! next[0].playing));
}

TEST_CASE ("Feed: Split and Wide cut curves follow the analog prototype at the reduction", "[feed][cut]")
{
    constexpr double rate = 48000.0;
    const std::vector<float> hz { 200.0f, 1000.0f, 2262.7f, 3200.0f, 4525.5f, 6000.0f, 7348.0f, 11000.0f, 15119.0f, 18000.0f, 19500.0f };
    std::vector<float> cut (hz.size());
    const auto at = [&] (float f) { return cut[(size_t) (std::find (hz.begin(), hz.end(), f) - hz.begin())]; };

    dsp::DetectorSettings d;
    auto c = dsp::CutSettings::from (d, Shape::split, rate);
    REQUIRE (c.essKind == dsp::SplitKind::bell);

    dsp::cutCurves::ess (c, 6.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
    CHECK (at (6000.0f) == Approx (-6.0).margin (0.02));
    CHECK (at (11000.0f) == Approx (-6.0).margin (0.02));
    CHECK (at (3200.0f) > -0.1f);           // a third of an octave under Detect Low
    CHECK (at (15119.0f) > -0.1f);          // a third of an octave over Detect High
    CHECK (at (200.0f) > -0.01f);
    for (auto v : cut)
        CHECK (v <= 0.0f);

    dsp::cutCurves::ess (c, 0.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
    for (auto v : cut)
        CHECK (exact (v, 0.0f));
    dsp::cutCurves::ess (c, 40.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
    CHECK (at (7348.0f) == Approx (-24.0).margin (0.2));      // Range's top; the elliptic cut ripples in its band

    // Detect High at 0.4 fs or above: a high shelf from Detect Low.
    d.detectHighHz = 20000.0f;
    c = dsp::CutSettings::from (d, Shape::split, rate);
    REQUIRE (c.essKind == dsp::SplitKind::highShelf);
    dsp::cutCurves::ess (c, 6.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
    CHECK (at (19500.0f) == Approx (-6.0).margin (0.02));
    CHECK (at (6000.0f) == Approx (-6.0).margin (0.02));

    // Wide: the Ess band over every frequency.
    c = dsp::CutSettings::from ({}, Shape::wide, rate);
    dsp::cutCurves::ess (c, 5.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
    for (auto v : cut)
        CHECK (exact (v, -5.0f));

    // Harsh: Split's bell over its band in Split and Wide.
    for (const auto shape : { Shape::split, Shape::wide })
    {
        c = dsp::CutSettings::from ({}, shape, rate);
        dsp::cutCurves::harsh (c, 3.0f, nullptr, hz.data(), (int) hz.size(), cut.data());
        CHECK (at (3200.0f) == Approx (-3.0).margin (0.02));
        CHECK (at (1000.0f) > -0.05f);
        CHECK (at (7348.0f) > -0.05f);
    }
}

TEST_CASE ("Feed: Spectral cut curves come from the published gains, the deepest bin under each point", "[feed][cut]")
{
    dsp::SpectralView v;
    v.reserve();
    v.active = true;
    v.bins = 257;
    v.binHz = 93.75;
    std::fill (v.essGain.begin(), v.essGain.end(), 1.0f);
    std::fill (v.harshGain.begin(), v.harshGain.end(), 1.0f);
    for (int k = 50; k <= 120; ++k)
        v.essGain[(size_t) k] = 0.5f;
    v.essGain[100] = 0.1f;      // one deep bin, 9375 Hz
    v.harshGain[34] = 0.25f;

    const auto c = dsp::CutSettings::from ({}, Shape::spectral, 48000.0);
    std::vector<float> dense (1000), sparse (12), cut (1000);
    for (size_t i = 0; i < dense.size(); ++i)
        dense[i] = (float) dsp::SpectrumAnalyser::frequencyAt ((double) i / 999.0);
    for (size_t i = 0; i < sparse.size(); ++i)
        sparse[i] = (float) dsp::SpectrumAnalyser::frequencyAt ((double) i / 11.0);

    dsp::cutCurves::ess (c, 0.0f, &v, dense.data(), 1000, cut.data());
    const auto nearest = [&] (double f) { return (size_t) std::lround (dsp::SpectrumAnalyser::proportionOf (f) * 999.0); };
    CHECK (cut[nearest (7000.0)] == Approx (-6.0206).margin (0.01));
    CHECK (exact (cut[nearest (2000.0)], 0.0f));
    CHECK (exact (cut[nearest (15000.0)], 0.0f));
    CHECK (*std::min_element (cut.begin(), cut.end()) == Approx (-20.0).margin (0.01));

    // Twelve points over the axis: each spans several bins and keeps the deepest.
    dsp::cutCurves::ess (c, 0.0f, &v, sparse.data(), 12, cut.data());
    CHECK (*std::min_element (cut.begin(), cut.begin() + 12) == Approx (-20.0).margin (0.01));

    dsp::cutCurves::harsh (c, 0.0f, &v, dense.data(), 1000, cut.data());
    CHECK (*std::min_element (cut.begin(), cut.end()) == Approx (-12.04).margin (0.01));

    // Not active (Spectral not running yet): no cut, whatever the reduction.
    v.active = false;
    dsp::cutCurves::ess (c, 6.0f, &v, dense.data(), 1000, cut.data());
    CHECK (exact (*std::min_element (cut.begin(), cut.end()), 0.0f));
}

TEST_CASE ("Feed: Spectral publishes its cut and target while it runs, and lets an unread cut go", "[feed][spectral]")
{
    constexpr double rate = 48000.0;
    const auto in = voiceWithEsses (rate, 3.0);
    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    dsp::ChainSettings s;
    s.shape = Shape::spectral;

    dsp::SpectralView view;
    view.reserve();
    auto reads = 0, active = 0, cutting = 0, outsideTouched = 0, targetMissing = 0;
    const auto bandFirst = (int) std::ceil (4500.0 / 93.75), bandLast = (int) std::floor (12000.0 / 93.75);
    runBlocks (chain, in, s, { 512 }, {},
               [&] (const dsp::HaliteChain& c, int)
               {
                   const auto& snapshot = c.feed().spectralSnapshot();
                   if (! snapshot.read (view))
                       return;
                   snapshot.acknowledge (view.sequence);
                   ++reads;
                   if (! view.active)
                       return;
                   ++active;
                   REQUIRE (view.bins == 257);
                   REQUIRE (view.binHz == Approx (93.75));
                   auto deepest = 1.0f;
                   for (int k = 0; k < view.bins; ++k)
                   {
                       if (k < bandFirst - 1 || k > bandLast + 1)
                           outsideTouched += ! exact (view.essGain[(size_t) k], 1.0f);
                       else
                           deepest = std::min (deepest, view.essGain[(size_t) k]);
                   }
                   cutting += deepest < 0.9f;
                   for (int k = bandFirst; k <= bandLast; ++k)
                       targetMissing += ! (view.target[(size_t) k] > 0.0f);
               });
    CHECK (reads > 200);
    CHECK (active > 200);
    CHECK (cutting > 10);
    CHECK (outsideTouched == 0);
    CHECK (targetMissing == 0);

    // Nobody reads: a cut stays for the next reader, read in the middle of an ess, and is let go
    // within about 100 ms after it, not held for whoever comes last.
    dsp::HaliteChain unread;
    unread.prepare (rate, 4096, 2, LatencyMode::normal);
    auto partial = in;
    for (auto& channel : partial)
        channel.resize ((size_t) samplesFor (2.7, rate));
    runBlocks (unread, partial, s, { 512 });
    REQUIRE (unread.feed().spectralSnapshot().read (view));
    CHECK (view.active);
    CHECK (*std::min_element (view.essGain.begin(), view.essGain.begin() + view.bins) < 0.9f);

    Channels quiet (2, Samples ((size_t) samplesFor (0.2, rate), 0.0f));
    runBlocks (unread, quiet, s, { 512 });
    REQUIRE (unread.feed().spectralSnapshot().read (view));
    CHECK (view.active);
    CHECK (exact (*std::min_element (view.essGain.begin(), view.essGain.begin() + view.bins), 1.0f));

    // Split runs: the next publication says Spectral does not.
    s.shape = Shape::split;
    runBlocks (chain, quiet, s, { 512 });
    REQUIRE (chain.feed().spectralSnapshot().read (view));
    CHECK_FALSE (view.active);
}

TEST_CASE ("Feed: Spectral's Allround target reads on the spectrum's scale", "[feed][spectral][spectrum]")
{
    // In Allround the target is the signal's own smoothed spectrum: on steady noise its band levels
    // must meet the display spectrum's, or the thin line would float above or below the fill.
    constexpr double rate = 48000.0;
    const auto n = samplesFor (1.5, rate);
    const auto noise = bandNoise (300.0, 16000.0, n, rate, 7u, 0.05);
    Channels in { noise, scaled (bandNoise (300.0, 16000.0, n, rate, 8u, 0.05), 1.0) };

    dsp::HaliteChain chain;
    chain.prepare (rate, 4096, 2, LatencyMode::normal);
    dsp::ChainSettings s;
    s.shape = Shape::spectral;
    s.detector.detection = Detection::allround;
    runBlocks (chain, in, s, { 512 });

    dsp::SpectralView view;
    view.reserve();
    REQUIRE (chain.feed().spectralSnapshot().read (view));
    REQUIRE (view.active);

    dsp::SpectrumAnalyser analyser;
    analyser.prepare (rate, 400);
    REQUIRE (analyser.update (chain.feed().samples()));
    std::vector<float> target ((size_t) analyser.numPoints());
    analyser.bandLevels (view.target.data(), view.bins, view.binHz, target.data());

    auto sum = 0.0;
    auto count = 0;
    for (int p = 0; p < analyser.numPoints(); ++p)
    {
        const auto f = analyser.frequencies()[p];
        if (f < 2000.0f || f > 10000.0f)
            continue;
        sum += target[(size_t) p] - analyser.inputDb()[p];
        ++count;
    }
    REQUIRE (count > 50);
    INFO ("target − spectrum, mean over 2–10 kHz: " << sum / count << " dB");
    CHECK (std::abs (sum / count) < 1.0);
}

TEST_CASE ("Feed: the spectrum reads band levels in dB of mean square, and the meters peaks and RMS", "[feed][spectrum]")
{
    constexpr double rate = 48000.0;
    dsp::SampleFifo fifo;
    const auto write = [&fifo] (const std::function<float (std::uint64_t, int stream)>& signal, int n)
    {
        std::vector<float> block ((size_t) n);
        const auto start = fifo.count();
        fifo.begin (n);
        for (int st = 0; st < dsp::SampleFifo::numStreams; ++st)
        {
            for (int i = 0; i < n; ++i)
                block[(size_t) i] = signal (start + (std::uint64_t) i, st);
            fifo.write (st, block.data(), n);
        }
        fifo.end (n);
    };

    // A 1 kHz sine at 0.5 in, silence out: the band around it holds the sine's mean square.
    const auto sine = [] (std::uint64_t t, int st) { return st < 2 ? 0.5f * (float) std::sin (2.0 * std::numbers::pi * 1000.0 * (double) t / rate) : 0.0f; };
    write (sine, 8192);
    dsp::SpectrumAnalyser analyser;
    analyser.prepare (rate, 600);
    REQUIRE (analyser.fftSize() == 4096);
    REQUIRE (analyser.update (fifo));
    CHECK_FALSE (analyser.update (fifo));       // nothing new
    const auto peak = *std::max_element (analyser.inputDb(), analyser.inputDb() + analyser.numPoints());
    CHECK (peak == Approx (dbOf (0.125)).margin (0.1));
    CHECK (exact (*std::max_element (analyser.outputDb(), analyser.outputDb() + analyser.numPoints()), -150.0f));

    dsp::LevelMeters meters;
    meters.prepare (rate);
    write (sine, samplesFor (2.0, rate));
    REQUIRE (meters.update (fifo));
    CHECK (meters.peakDb (dsp::SampleFifo::inputStream (1)) == Approx (-6.0206).margin (0.01));
    CHECK (meters.rmsDb (dsp::SampleFifo::inputStream (0)) == Approx (dbOf (0.125)).margin (0.1));
    CHECK (exact (meters.peakDb (dsp::SampleFifo::outputStream (0)), -150.0f));

    // Silence: the spectrum falls with its release, about 4.3 dB per 300 ms once the window is past the sine.
    analyser.update (fifo);
    write ([] (std::uint64_t, int) { return 0.0f; }, 4096);
    analyser.update (fifo);
    const auto before = *std::max_element (analyser.inputDb(), analyser.inputDb() + analyser.numPoints());
    for (int k = 0; k < 9; ++k)
    {
        write ([] (std::uint64_t, int) { return 0.0f; }, 1600);
        analyser.update (fifo);
    }
    const auto after = *std::max_element (analyser.inputDb(), analyser.inputDb() + analyser.numPoints());
    CHECK (before - after == Approx (10.0 * std::log10 (std::exp (1.0))).margin (0.2));

    // White noise of variance σ²: a band a sixth of an octave wide holds σ² · width / (fs / 2).
    analyser.clear();
    std::mt19937 rng (5u);
    std::normal_distribution<float> gauss (0.0f, 0.1f);
    write ([&] (std::uint64_t, int) { return gauss (rng); }, 4096);
    REQUIRE (analyser.update (fifo));
    // The mean in power: a mean in dB would read low by the estimate's own spread.
    auto ratio = 0.0;
    auto count = 0;
    for (int p = 0; p < analyser.numPoints(); ++p)
    {
        const auto f = (double) analyser.frequencies()[p];
        if (f < 1000.0 || f > 15000.0)
            continue;
        const auto width = f * (std::exp2 (1.0 / 12.0) - std::exp2 (-1.0 / 12.0));
        ratio += std::pow (10.0, (analyser.inputDb()[p] - dbOf (0.01 * width / (rate / 2.0))) / 10.0);
        ++count;
    }
    INFO ("mean error " << dbOf (ratio / count) << " dB");
    CHECK (std::abs (dbOf (ratio / count)) < 0.5);

    // Any per-bin spectrum on the same scale: white noise as a 512-point frame's bins.
    std::vector<float> bins (257, 0.01f / 256.0f), levels ((size_t) analyser.numPoints());
    bins[0] = bins[256] = 0.005f / 256.0f;
    analyser.bandLevels (bins.data(), 257, 93.75, levels.data());
    for (int p = 0; p < analyser.numPoints(); p += 50)
    {
        const auto f = (double) analyser.frequencies()[p];
        const auto width = f * (std::exp2 (1.0 / 12.0) - std::exp2 (-1.0 / 12.0));
        CHECK (levels[(size_t) p] == Approx (dbOf (0.01 * width / (rate / 2.0))).margin (0.01));
    }
}

TEST_CASE ("Perf: the display feed's own cost on the audio thread", "[.perf]")
{
    // What the feed adds to processBlock, timed alone: the sample streams and history records per
    // chunk, the detector's values per hop, and Spectral's fold and publication with both bands
    // cutting in every frame (the most it does). Pin it to one core:
    // taskset -c 2 HaliteTests_reduction "Perf: the display feed*"
    struct Cutting final : dsp::ReductionReader
    {
        float depthAt (std::int64_t, int) const noexcept override { return 0.3f; }
        float largestTarget (std::int64_t, std::int64_t, int) const noexcept override { return 6.0f; }
    };

    for (const auto rate : { 48000.0, 192000.0 })
    {
        const juce::ScopedNoDenormals noDenormals;
        constexpr int chunk = 256;
        const auto frame = dsp::HaliteChain::frameSize (rate);
        const auto in = voiceWithEsses (rate, 10.0);
        const auto n = (int) in[0].size();

        dsp::SpectralShape shape;
        shape.prepare (rate, frame);
        shape.start (2, true);
        dsp::DisplayFeed feed;
        feed.prepare (rate, frame);
        const Cutting reader;
        dsp::SpectralSettings spectral;
        spectral.harsh = true;
        std::array<std::array<float, chunk>, 2> ess {}, harsh {}, removed {};
        std::array<float*, 2> essOut { ess[0].data(), ess[1].data() }, harshOut { harsh[0].data(), harsh[1].data() };

        auto best = 1.0e300;
        for (int pass = 0; pass < 5; ++pass)
        {
            auto seconds = 0.0;
            dsp::DetectorFrame f;
            for (int pos = 0; pos + chunk <= n; pos += chunk)
            {
                const std::array<const float*, 2> x { in[0].data() + pos, in[1].data() + pos };
                shape.process (x.data(), essOut.data(), harshOut.data(), chunk, pos, {}, spectral, reader, { 0, 0 });
                for (size_t c = 0; c < 2; ++c)
                    for (int i = 0; i < chunk; ++i)
                        removed[c][(size_t) i] = ess[c][(size_t) i] + harsh[c][(size_t) i];
                const std::array<const float*, 2> r { removed[0].data(), removed[1].data() };

                const auto start = std::chrono::steady_clock::now();
                for (int h = 0; h < chunk / dsp::Detector::hopSize; ++h)
                {
                    f.reductionPosition = pos + (h + 1) * dsp::Detector::hopSize;
                    f.reductions[0].essDb = (float) h;
                    feed.hop (f, 0);
                }
                feed.audio (x.data(), r.data(), x.data(), 2, chunk, frame);
                feed.spectral (&shape, Detection::voice, chunk);
                seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            }
            best = std::min (best, seconds);
        }
        std::printf ("Display feed at %.1f kHz, stereo, both Spectral bands cutting: %.4f %% of one core\n", rate / 1000.0,
                     100.0 * best / ((double) n / rate));
    }
}
