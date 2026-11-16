#include "ui/DisplayModel.h"

#include "dsp/feed/CutCurves.h"
#include "plugin/PluginProcessor.h"

namespace halite::ui
{

DisplayModel::DisplayModel (HaliteController& c)
    : controller (c),
      feed (c.getProcessor().getChain().feed()),
      mirror ((size_t) mirrorSize),
      chunk (4096),
      eventChunk ((size_t) dsp::EventRing::size),
      anchorChunk ((size_t) dsp::TransportLog::capacity)
{
    rowList.reserve ((size_t) maxRows);
    view.reserve();
    for (auto& curve : curves)
        curve.assign ((size_t) spectrumPoints, 0.0f);
    target.assign ((size_t) spectrumPoints, dsp::LevelMeters::floorDb);
    restart (feed.sampleRate());
}

void DisplayModel::restart (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    perSecond = rate / std::max (1, feed.recordSamples());
    epoch = feed.epoch();
    ++restarts;

    // Start with the last minute the feed holds, so an editor opens on the recent past.
    cursor = std::max<std::int64_t> (0, feed.history().count() - (std::int64_t) ((keepSeconds + 2.0) * perSecond));
    mirrorStart = mirrorEnd = cursor;
    frozen = false;
    frozenEnd = -1;

    eventList.clear();
    selection.reset();
    anchors.clear();

    candidates.prepare (feed.recordSamples() / rate, tuning);
    candidateMode = controller.detection();
    tracking = false;
    essList.clear();

    frameCut[0] = frameCut[1] = cut[0] = cut[1] = cutHolds[0] = cutHolds[1] = 0.0f;
    levels.prepare (rate);
    meters = {};
    analyser.prepare (rate, spectrumPoints);
    for (auto& curve : curves)
        std::fill (curve.begin(), curve.end(), 0.0f);
    targetShown = false;
    sinceSpectrum = spectrumInterval;
    started = true;
}

void DisplayModel::update (double dt)
{
    if (! started || feed.epoch() != epoch)
        restart (feed.sampleRate());

    // A new mode puts S on another scale: the candidates found so far no longer compare.
    if (const auto mode = controller.detection(); mode != candidateMode)
    {
        candidateMode = mode;
        candidates.reset();
        tracking = false;
        essList.clear();
    }

    readTransport();

    if (! frozen)
    {
        frameHasRecords = false;
        frameCut[0] = frameCut[1] = 0.0f;
        readHistory();
        readEvents();
        prune();
        buildRows();
        followCut (frameCut[0], frameCut[1], dt);

        sinceSpectrum += dt;
        if (sinceSpectrum >= spectrumInterval - 0.002)
        {
            sinceSpectrum = 0.0;
            updateSpectrum();
        }
    }

    updateMeters (dt);
}

void DisplayModel::readHistory()
{
    const auto& history = feed.history();

    for (;;)
    {
        const auto before = cursor;
        const auto n = history.read (cursor, chunk.data(), (int) chunk.size());
        const auto start = cursor - n;

        // The feed moved on past what the mirror holds (a long freeze, a stalled editor): start again there.
        if (start != mirrorEnd)
        {
            mirrorStart = mirrorEnd = start;
            candidates.reset();
            tracking = false;
            ++restarts;
        }

        for (int i = 0; i < n; ++i)
        {
            const auto& r = chunk[(size_t) i];
            mirror[(size_t) ((start + i) & mirrorMask)] = r;
            frameCut[0] = std::max (frameCut[0], r.essDb);
            frameCut[1] = std::max (frameCut[1], r.harshDb);
        }

        mirrorEnd = start + n;
        mirrorStart = std::max (mirrorStart, mirrorEnd - mirrorSize);
        frameHasRecords = frameHasRecords || n > 0;
        findEsses (start, mirrorEnd);

        if (n < (int) chunk.size() || cursor == before)
            break;
    }
}

void DisplayModel::findEsses (std::int64_t from, std::int64_t to)
{
    const auto allround = candidateMode == Detection::allround;
    const auto floorDb = allround ? tuning.floorAbsDb : tuning.floorDb;
    const auto splitDb = allround ? tuning.splitAbsDb : 0.0f;

    for (auto r = from; r < to; ++r)
    {
        const auto& rec = record (r);
        auto peak = 0.0f;
        const auto ended = candidates.push (rec.sibilanceDb, floorDb, splitDb, peak);

        if (ended && tracking)
        {
            running.peakDb = peak;
            running.last = r;
            essList.push (running);
            ++essCount;
        }

        if (ended || ! candidates.open())
            tracking = false;

        if (! candidates.open())
            continue;

        if (! tracking)
        {
            tracking = true;
            running = {};
            running.first = running.peak = r;
            running.peakDb = rec.sibilanceDb;
            running.cutDb = rec.essDb;
        }
        else
        {
            if (rec.sibilanceDb > running.peakDb)
            {
                running.peakDb = rec.sibilanceDb;
                running.peak = r;
            }
            // The hangover is part of the run: the cut's release lags S.
            running.cutDb = std::max (running.cutDb, rec.essDb);
        }
    }
}

void DisplayModel::readEvents()
{
    const auto& ring = controller.getProcessor().getChain().events();
    const auto slack = (std::int64_t) perSecond;

    for (;;)
    {
        const auto n = ring.read (eventCursor, eventChunk.data(), (int) eventChunk.size());

        for (int i = 0; i < n; ++i)
        {
            const auto& e = eventChunk[(size_t) i];
            Event ev;
            ev.number = eventSerial++;
            ev.ess = e;
            ev.first = feed.recordOf (e.elapsed);
            ev.last = feed.recordOf (e.elapsed + std::max<std::int64_t> (0, e.end - e.start));

            // An event arrives a little before its audio leaves; one far outside the mirror is from before a restart.
            if (ev.first >= mirrorStart && ev.first <= newest() + slack)
                eventList.push (ev);
        }

        if (n < (int) eventChunk.size())
            break;
    }
}

void DisplayModel::prune()
{
    // An event's peak is settled once its last record is in.
    for (int i = eventList.size(); --i >= 0 && eventList[i].peak < 0;)
        if (eventList[i].last <= newest())
            eventList[i].peak = peakRecord (eventList[i]);

    const auto oldestKept = newest() - (std::int64_t) (keepSeconds * perSecond);

    while (! eventList.empty() && eventList[0].last < oldestKept)
        eventList.dropFront();
    while (! essList.empty() && essList[0].last < oldestKept)
        essList.dropFront();
}

void DisplayModel::readTransport()
{
    for (;;)
    {
        const auto n = feed.transport().read (transportCursor, anchorChunk.data(), (int) anchorChunk.size());
        for (int i = 0; i < n; ++i)
            if (anchorChunk[(size_t) i].epoch == epoch)
                anchors.push (anchorChunk[(size_t) i]);

        if (n < (int) anchorChunk.size())
            break;
    }
}

double DisplayModel::timeOf (std::int64_t record, bool& songPosition) const noexcept
{
    const auto elapsed = record * feed.recordSamples();
    for (int i = anchors.size(); --i >= 0;)
        if (anchors[i].from <= elapsed)
            return dsp::timeOf (elapsed, anchors[i], rate, songPosition);

    // Before the log's oldest entry kept: the feed's own clock, as a stopped host's events read.
    songPosition = false;
    return (double) elapsed / rate;
}

void DisplayModel::buildRows()
{
    const std::array<std::uint64_t, 5> source { eventSerial, essCount, (std::uint64_t) eventList.size(), (std::uint64_t) essList.size(), restarts };
    if (source == rowSource)
        return;
    rowSource = source;
    ++rowGeneration;

    totalCounts = {};
    for (int i = 0; i < eventList.size(); ++i)
    {
        const auto cutDb = eventList[i].ess.peakReductionDb;
        ++totalCounts.caught;
        totalCounts.averageCutDb += cutDb;
        totalCounts.largestCutDb = std::max (totalCounts.largestCutDb, cutDb);
    }
    if (totalCounts.caught > 0)
        totalCounts.averageCutDb /= (float) totalCounts.caught;

    // Both lists run oldest first: the newest maxRows of their merge, from the back. A candidate cut by the
    // event rule's start level has (or is about to have) its event, which carries the chain's stamp instead.
    rowList.clear();
    auto e = eventList.size() - 1, c = essList.size() - 1;
    while ((e >= 0 || c >= 0) && (int) rowList.size() < maxRows)
    {
        if (c >= 0)
        {
            const auto& ess = essList[c];
            const auto overlapped = e >= 0 && eventList[e].last >= ess.first && eventList[e].first <= ess.last;
            if (overlapped || ess.cutDb >= cutDrawnDb)
            {
                --c;
                continue;
            }
            if (e < 0 || ess.first > eventList[e].first)
            {
                Row r;
                r.span = { ess.first, ess.last };
                r.peakDb = ess.peakDb;
                r.seconds = timeOf (ess.first, r.songPosition);
                rowList.push_back (r);
                --c;
                continue;
            }
        }

        const auto& ev = eventList[e--];
        Row r;
        r.span = spanOf (ev);
        r.peakDb = ev.ess.peakSibilanceDb;
        r.cutDb = ev.ess.peakReductionDb;
        r.seconds = ev.ess.seconds;
        r.songPosition = ev.ess.songPosition;
        r.caught = true;
        rowList.push_back (r);
    }
    std::reverse (rowList.begin(), rowList.end());
}

void DisplayModel::followCut (float essDb, float harshDb, double dt)
{
    const float now[] { essDb, harshDb };

    for (int b = 0; b < 2; ++b)
    {
        cut[b] = std::max (frameHasRecords ? now[b] : 0.0f, cut[b] - cutReleaseDbPerSecond * (float) dt);
        cut[b] = std::max (0.0f, cut[b]);
        cutHolds[b] = std::max (cutHolds[b], now[b]);
    }
}

void DisplayModel::updateMeters (double dt)
{
    if (! levels.update (feed.samples()))
        return;

    for (int which = 0; which < 2; ++which)
    {
        const auto a = which == 0 ? dsp::SampleFifo::inputStream (0) : dsp::SampleFifo::outputStream (0);
        const auto peak = std::max (levels.peakDb (a), levels.peakDb (a + 1));
        auto& m = meters[(size_t) which];

        m.peakDb = std::max (peak, m.peakDb - peakReleaseDbPerSecond * (float) dt);
        m.rmsDb = std::max (levels.rmsDb (a), levels.rmsDb (a + 1));
        m.holdDb = std::max (m.holdDb, peak);
    }
}

void DisplayModel::resetHold (int which) noexcept
{
    if (which == 2)
    {
        cutHolds[0] = cut[0];
        cutHolds[1] = cut[1];
    }
    else if (which == 0 || which == 1)
    {
        auto& m = meters[(size_t) which];
        m.holdDb = m.peakDb;
    }
}

void DisplayModel::updateSpectrum()
{
    auto& processor = controller.getProcessor();
    const auto fresh = analyser.update (feed.samples());

    const auto shape = (Shape) processor.getChain().status().shape.load (std::memory_order_relaxed);
    const auto settings = dsp::CutSettings::from (processor.readSettings().detector, shape, rate);

    const dsp::SpectralView* spectral = nullptr;
    targetShown = false;

    if (shape == Shape::spectral)
    {
        const auto& snapshot = feed.spectralSnapshot();
        if (snapshot.read (view))
        {
            snapshot.acknowledge (view.sequence);
            if (view.active && view.bins > 0)
            {
                spectral = &view;
                analyser.bandLevels (view.target.data(), view.bins, view.binHz, target.data());
                targetShown = std::any_of (target.begin(), target.end(), [] (float db) { return db > dsp::LevelMeters::floorDb + 1.0f; });
            }
        }
    }

    const auto* hz = analyser.frequencies();
    dsp::cutCurves::ess (settings, cut[0], spectral, hz, spectrumPoints, curves[0].data());
    dsp::cutCurves::harsh (settings, cut[1], spectral, hz, spectrumPoints, curves[1].data());

    // Nothing new (a stopped host, a cut long gone): the spectrum asks for no repaint.
    if (fresh || spectral != nullptr || cut[0] > 0.0f || cut[1] > 0.0f || shownCut[0] > 0.0f || shownCut[1] > 0.0f)
        ++spectrumFrames;
    shownCut[0] = cut[0];
    shownCut[1] = cut[1];
}

//==============================================================================
double DisplayModel::secondsBehindLive() const noexcept
{
    if (! frozen)
        return 0.0;

    return (double) std::max<std::int64_t> (0, feed.history().count() - 1 - frozenEnd) / perSecond;
}

void DisplayModel::setFrozen (bool shouldFreeze)
{
    if (shouldFreeze == frozen)
        return;

    frozen = shouldFreeze;
    frozenEnd = newest();
}

const DisplayModel::Event* DisplayModel::findEvent (std::uint64_t number) const noexcept
{
    for (int i = eventList.size(); --i >= 0;)
        if (eventList[i].number == number)
            return &eventList[i];
    return nullptr;
}

std::int64_t DisplayModel::peakRecord (const Event& e) const noexcept
{
    if (e.peak >= 0)
        return e.peak;
    if (! holds (e.first))
        return e.first;

    auto best = e.first;
    auto deepest = -1.0f;
    for (auto r = e.first; r <= std::min (e.last, newest()); ++r)
        if (const auto d = record (r).essDb; d > deepest)
        {
            deepest = d;
            best = r;
        }
    return best;
}

std::int64_t DisplayModel::peakOf (Span span) const noexcept
{
    if (! holds (span.first))
        return span.first;

    auto best = span.first, highest = span.first;
    auto deepest = 0.0f, loudest = record (span.first).sibilanceDb;
    for (auto r = span.first; r <= std::min (span.last, newest()); ++r)
    {
        const auto& rec = record (r);
        if (rec.essDb > deepest)
        {
            deepest = rec.essDb;
            best = r;
        }
        if (rec.sibilanceDb > loudest)
        {
            loudest = rec.sibilanceDb;
            highest = r;
        }
    }
    return deepest > 0.0f ? best : highest;
}

void DisplayModel::reveal (Span span)
{
    selection = span;

    const auto window = std::max<std::int64_t> (1, std::llround (controller.getZoom (controller.getDisplayView()) * perSecond));
    const auto at = peakOf (span);
    const auto end = viewEnd();

    if (at > end - window && at <= end)
        return;

    // Centred where the mirror allows; the view never runs past the newest record.
    frozen = true;
    frozenEnd = std::clamp (at + window / 2, std::min (newest(), oldest() + window), newest());
}

} // namespace halite::ui
