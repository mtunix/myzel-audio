/*
    HaliteUiSnapshot: renders the workbench editor to PNG files for visual review, and runs the editor
    self-test (headless with xvfb-run).

    Usage: HaliteUiSnapshot <output dir> [--leakcheck] [--selftest]; HALITE_TIMING_ONLY=1 skips the scenes,
    HALITE_TIMING_ONLY=window runs only the real window's timing (it needs a screen of 2560 x 1600)
*/

#include "plugin/PluginProcessor.h"
#include "ui/HaliteEditor.h"
#include "ui/UiState.h"
#include "ui/UserSettings.h"

#include "Programme.h"
#include "SelfTest.h"

#include "common/testing/SnapshotTool.h"

#include <atomic>
#include <chrono>
#include <thread>

#if JUCE_LINUX
 #include <linux/perf_event.h>
 #include <sys/ioctl.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#endif

namespace
{
using namespace myzel::testing;
using halite::HaliteEditor;
using halite::HaliteProcessor;
namespace params = halite::params;
namespace uistate = halite::ui::uistate;
namespace ui = halite::ui;

void runFrames (HaliteEditor& editor, int frames, double start = 0.0)
{
    for (int i = 0; i < frames; ++i)
        editor.animateFrame (start + (double) i / 60.0);
}

struct Scene
{
    juce::String name;
    int width = 1200, height = 720;
    ui::ThemeChoice theme = ui::ThemeChoice::dark;
    std::function<void (HaliteProcessor&)> setup;
    std::function<void (HaliteEditor&)> after;
    double songSeconds = -1.0;                                                  // ≥ 0: the host plays from there
    std::function<void (HaliteEditor&, Programme&, const std::function<void (double)>& play)> live;    // audio and frames after `after`
};

void runScene (const Scene& scene, const juce::File& dir)
{
    ui::userSettings::setTheme (scene.theme);
    ui::userSettings::setWindowSize ({ scene.width, scene.height });

    // On the heap: the chain's buffers are too large for the stack.
    auto owned = std::make_unique<HaliteProcessor>();
    auto& processor = *owned;
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    // Half a minute before the editor opens (the Cloud's default window), then a second of audio and
    // frames together, so the views scroll as they do in a host.
    Programme programme;
    programme.prepare (processor);
    if (scene.songSeconds >= 0.0)
        programme.playTransport (processor, scene.songSeconds);
    programme.play (processor, 31.0);

    auto editor = std::make_unique<HaliteEditor> (processor);
    editor->setVisible (true);
    auto time = 0.0;
    auto play = [&] (double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
        {
            programme.play (processor, 1.0 / 60.0);
            editor->animateFrame (time += 1.0 / 60.0);
        }
    };
    play (1.0);

    if (scene.after != nullptr)
        scene.after (*editor);
    if (scene.live != nullptr)
        scene.live (*editor, programme, play);

    runFrames (*editor, 2, time + 1.0 / 60.0);
    saveScene (*editor, dir, scene.name.toRawUTF8(), false);

    // What the display held, so a programme that stopped producing esses shows here.
    const auto& m = editor->getModel();
    auto cut = 0;
    for (int i = 0; i < m.esses().size(); ++i)
        cut += m.esses()[i].cutDb >= ui::DisplayModel::cutDrawnDb ? 1 : 0;
    std::printf ("  %d esses (%d cut), %d events\n", m.esses().size(), cut, m.events().size());
    editor.reset();
}

using Setup = std::function<void (HaliteProcessor&)>;
using After = std::function<void (HaliteEditor&)>;

Scene scene (const juce::String& name, int w, int h, ui::ThemeChoice theme, Setup setup = nullptr, After after = nullptr)
{
    Scene s;
    s.name = name;
    s.width = w;
    s.height = h;
    s.theme = theme;
    s.setup = std::move (setup);
    s.after = std::move (after);
    return s;
}

void harsh (HaliteProcessor& p)
{
    setParam (p, params::harshOn, 1.0f);
    p.getUiState().setProperty (uistate::selectedBand, "harsh", nullptr);
}

std::vector<Scene> scenes()
{
    using T = ui::ThemeChoice;
    std::vector<Scene> s;

    for (const auto theme : { T::dark, T::light })
    {
        const juce::String suffix = theme == T::dark ? "-dark" : "-light";
        s.push_back (scene ("01-default" + suffix, 1200, 720, theme));
        s.push_back (scene ("02-minimum" + suffix, 720, 420, theme));
        s.push_back (scene ("03-harsh" + suffix, 1200, 720, theme, harsh));
        s.push_back (scene ("04-advanced" + suffix, 1200, 720, theme,
                       [] (HaliteProcessor& p) { p.getUiState().setProperty (uistate::advancedOpen, true, nullptr); }));
    }

    s.push_back (scene ("10-small", 960, 576, T::dark));
    s.push_back (scene ("11-xl", 1680, 1008, T::dark));
    s.push_back (scene ("12-tall", 900, 1100, T::dark));
    s.push_back (scene ("13-sidebar-collapsed", 1200, 720, T::dark,
                   [] (HaliteProcessor& p) { p.getUiState().setProperty (uistate::eventsShown, false, nullptr); }));
    s.push_back (scene ("14-overlay", 960, 640, T::dark, nullptr, [] (HaliteEditor& e) { e.toggleSidebar(); }));
    s.push_back (scene ("27-minimum-overlay", 720, 420, T::light, nullptr, [] (HaliteEditor& e) { e.toggleSidebar(); }));
    s.push_back (scene ("15-cloud", 1200, 720, T::dark,
                   [] (HaliteProcessor& p) { p.getUiState().setProperty (uistate::displayView, "cloud", nullptr); }));
    s.push_back (scene ("16-wrapped", 860, 600, T::light, harsh));
    s.push_back (scene ("17-minimum-advanced", 720, 420, T::dark,
                   [] (HaliteProcessor& p) { p.getUiState().setProperty (uistate::advancedOpen, true, nullptr); }));
    s.push_back (scene ("18-allround-live", 1200, 720, T::dark, [] (HaliteProcessor& p)
                   {
                       setParam (p, params::detection, 1.0f);
                       setParam (p, params::shape, 1.0f);
                       p.setLatencyMode (halite::LatencyMode::live);
                       p.getUiState().setProperty (uistate::advancedOpen, true, nullptr);
                   }));
    s.push_back (scene ("19-bypass-listen", 1200, 720, T::light, [] (HaliteProcessor& p)
                   {
                       setParam (p, params::bypass, 1.0f);
                       setParam (p, params::listen, 1.0f);
                   }));
    s.push_back (scene ("20-save-dialog", 1200, 720, T::dark, nullptr, [] (HaliteEditor& e) { e.performPresetItem (HaliteEditor::saveAsItem); }));
    s.push_back (scene ("21-typed-entry", 1200, 720, T::light, nullptr, [] (HaliteEditor& e)
                   {
                       e.getControlBar().getDetectLow().openTextEntry();
                       e.getControlBar().getRange (ui::Band::ess).openTextEntry();
                   }));
    s.push_back (scene ("22-help-hover", 1200, 720, T::dark, nullptr, [] (HaliteEditor& e)
                   {
                       auto& k = e.getControlBar().getThreshold (ui::Band::ess);
                       k.mouseEnter (makeEvent (k, k.getLocalBounds().toFloat().getCentre(), {}));
                   }));
    s.push_back (scene ("23-help-dialog", 1200, 720, T::light, nullptr, [] (HaliteEditor& e) { e.performSettingsItem (HaliteEditor::helpItem); }));
    // The host playing (song positions in the list), Learn listening, learned, and with too few esses.
    for (auto* first : { &s[0], &s[4] })
        first->songSeconds = 42.0;
    {
        auto listening = scene ("24-learn-listening-dark", 1200, 720, T::dark);
        listening.live = [] (HaliteEditor& e, Programme&, const std::function<void (double)>& play)
        {
            e.getControlBar().getLearn().onClick();
            play (4.6);
        };
        s.push_back (listening);

        auto learned = scene ("25-learned-light", 1200, 720, T::light);
        learned.songSeconds = 95.5;
        learned.live = [] (HaliteEditor& e, Programme&, const std::function<void (double)>& play)
        {
            e.getControlBar().getLearn().onClick();
            play (8.4);
            e.getController().getProcessor().getLearn().service();
            play (0.1);
        };
        s.push_back (learned);

        auto tooFew = scene ("26-too-few-dark", 1200, 720, T::dark);
        tooFew.live = [] (HaliteEditor& e, Programme& programme, const std::function<void (double)>& play)
        {
            programme.setEsses (false);
            play (0.5);
            e.getControlBar().getLearn().onClick();
            play (8.4);
            e.getController().getProcessor().getLearn().service();
            play (0.1);
        };
        s.push_back (tooFew);
    }

    s.push_back (scene ("05-spectral-dark", 1200, 720, T::dark, [] (HaliteProcessor& p)
                   {
                       setParam (p, params::shape, 1.0f);
                       setParam (p, params::harshOn, 1.0f);
                   }));
    s.push_back (scene ("06-cloud-harsh-light", 1200, 720, T::light, [] (HaliteProcessor& p)
                   {
                       harsh (p);
                       p.getUiState().setProperty (uistate::displayView, "cloud", nullptr);
                   }));
    s.push_back (scene ("07-revealed-light", 1200, 720, T::light, nullptr, [] (HaliteEditor& e)
                   {
                       // The oldest caught ess in the sidebar: choosing it freezes the Timeline and scrolls back.
                       const auto& rows = e.getModel().rows();
                       for (int i = 0; i < (int) rows.size(); ++i)
                           if (rows[(size_t) i].caught)
                           {
                               e.getSidebar().getList().choose (i);
                               break;
                           }
                   }));
    return s;
}

void timeRendering()
{
    ui::userSettings::setTheme (ui::ThemeChoice::dark);
    ui::userSettings::setWindowSize ({ 1200, 720 });

    auto owned = std::make_unique<HaliteProcessor>();
    auto& processor = *owned;
    playProgramme (processor, 1.0);
    auto editor = std::make_unique<HaliteEditor> (processor);
    editor->setVisible (true);
    timeRepaints (*editor);
    timeFrames ([&] (int i) { editor->animateFrame ((double) i / 60.0); });
}

// The editor's budget at 200 % (src/ui/DESIGN.md, Performance), in % of a core at the full clock; it is set on the
// image timing, so the window's figure is printed beside it as a guide, not judged against it.
constexpr double uiBudgetPercent = 5.0;

/** This thread's CPU time in ms where the system gives it (other processes' load then does not count), else wall time. */
double cpuMs()
{
   #if JUCE_LINUX
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec * 1.0e-6;
   #else
    return juce::Time::getMillisecondCounterHiRes();
   #endif
}

/**
    This thread's CPU cycles in user space since construction, where Linux's perf events allow it (a negative
    count elsewhere): unlike CPU time, they do not grow when the governor lowers the clock for a light load.
*/
class CycleCounter
{
public:
    CycleCounter()
    {
       #if JUCE_LINUX
        perf_event_attr attr {};
        attr.type = PERF_TYPE_HARDWARE;
        attr.size = sizeof (attr);
        attr.config = PERF_COUNT_HW_CPU_CYCLES;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        fd = (int) syscall (__NR_perf_event_open, &attr, 0, -1, -1, 0);
       #endif
    }

    ~CycleCounter()
    {
       #if JUCE_LINUX
        if (fd >= 0)
            close (fd);
       #endif
    }

    double count() const
    {
        long long cycles = -1;
       #if JUCE_LINUX
        if (fd < 0 || read (fd, &cycles, sizeof (cycles)) != (ssize_t) sizeof (cycles))
            return -1.0;
       #endif
        return (double) cycles;
    }

private:
    int fd = -1;
    JUCE_DECLARE_NON_COPYABLE (CycleCounter)
};

/** The clock in GHz while this thread works flat out (cycles over CPU time), or 0 where cycles are not counted. */
double busyClockGHz()
{
    CycleCounter cycles;
    const auto start = cpuMs();
    volatile auto x = 1.0;
    while (cpuMs() - start < 300.0)
        for (int i = 0; i < 100000; ++i)
            x = x * 1.0000001 + 1.0e-9;
    const auto c = cycles.count();
    return c > 0.0 ? c / ((cpuMs() - start) * 1.0e6) : 0.0;
}

/**
    PLAN.md's UI performance check (Behaviour, Rendering), in this thread's CPU time at the clock it runs flat out
    at: with audio arriving, each frame's clock and the repaints of what asked for one (the main view every frame,
    the spectrum and the meters at 30 fps, the sidebar's parts when their data changes), at 200 % into ARGB images
    kept from frame to frame, each asked area cleared and painted under a clip, as JUCE's Linux window paints.
    Prints the share of one core at 60 fps against the plan's budget.
*/
void timeDisplay (ui::DisplayView view)
{
    ui::userSettings::setTheme (ui::ThemeChoice::dark);
    ui::userSettings::setWindowSize ({ 1200, 720 });

    auto owned = std::make_unique<HaliteProcessor>();
    auto& processor = *owned;
    resetParams (processor);
    processor.getUiState().setProperty (uistate::displayView, view == ui::DisplayView::cloud ? "cloud" : "timeline", nullptr);
    Programme programme;
    programme.prepare (processor);
    programme.play (processor, 31.0);

    auto editor = std::make_unique<HaliteEditor> (processor);
    editor->setVisible (true);

    constexpr float scale = 2.0f;
    struct Target
    {
        juce::Component* part;
        std::function<std::uint32_t()> asked;
        std::function<juce::RectangleList<int>()> area;     // what it asked to repaint, where that is not all of it
        juce::Image image;
        std::uint32_t painted = 0;
        double ms = 0.0;
        int paints = 0;
    };

    auto& mainView = editor->getMainView();
    auto& spectrum = editor->getSpectrum();
    auto& meters = editor->getMeters();
    std::vector<Target> targets;
    auto& shown = mainView.current();
    targets.push_back ({ &shown, [&] { return shown.frameRepaints(); }, [&] { return shown.takeAskedArea(); }, {} });
    targets.push_back ({ &spectrum, [&] { return spectrum.frameRepaints(); }, [&] { return spectrum.takeAskedArea(); }, {} });
    targets.push_back ({ &meters, [&] { return meters.frameRepaints(); }, {}, {} });
    auto& sidebar = editor->getSidebar();
    targets.push_back ({ &sidebar.getTotals(), [&] { return sidebar.getTotals().frameRepaints(); }, {}, {} });
    targets.push_back ({ &sidebar.getList(), [&] { return sidebar.getList().frameRepaints(); }, {}, {} });
    targets.push_back ({ &sidebar.getHistogram(), [&] { return sidebar.getHistogram().frameRepaints(); }, {}, {} });
    for (auto& t : targets)
        t.image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) t.part->getWidth() * scale),
                               juce::roundToInt ((float) t.part->getHeight() * scale), true);

    auto time = 0.0, clockMs = 0.0, paintMs = 0.0;
    constexpr int warmUp = 60, frames = 600;
    std::unique_ptr<CycleCounter> cycles;
    auto cpuAtStart = 0.0;

    for (int i = 0; i < warmUp + frames; ++i)
    {
        programme.play (processor, 1.0 / 60.0);

        if (i == warmUp)
        {
            cycles = std::make_unique<CycleCounter>();
            cpuAtStart = cpuMs();
        }
        const auto t0 = cpuMs();
        editor->animateFrame (time += 1.0 / 60.0);
        const auto t1 = cpuMs();
        if (i >= warmUp)
            clockMs += t1 - t0;

        // Only the parts that asked for a repaint this frame, over the area they asked for.
        for (auto& t : targets)
        {
            if (t.asked() == t.painted)
                continue;
            t.painted = t.asked();
            const auto area = t.area != nullptr ? t.area() : juce::RectangleList<int> (t.part->getLocalBounds());

            const auto start = cpuMs();
            for (const auto& r : area)
                t.image.clear ((r.toFloat() * scale).getSmallestIntegerContainer());
            {
                juce::Graphics g (t.image);
                g.addTransform (juce::AffineTransform::scale (scale));
                g.reduceClipRegion (area);
                t.part->paintEntireComponent (g, true);
            }
            const auto ms = cpuMs() - start;
            if (i >= warmUp)
            {
                paintMs += ms;
                t.ms += ms;
                ++t.paints;
            }
        }
    }

    // The clock the loop ran at (the programme's audio included): what the CPU time below is measured at.
    const auto ghz = cycles != nullptr && cycles->count() > 0.0 ? cycles->count() / ((cpuMs() - cpuAtStart) * 1.0e6) : 0.0;

    for (const auto& t : targets)
        std::printf ("  %s: %d repaints in %d frames, %.3f ms each\n", t.part->getTitle().toRawUTF8(), t.paints, frames,
                     t.paints > 0 ? t.ms / t.paints : 0.0);
    const auto perFrame = (clockMs + paintMs) / frames;
    std::printf ("display, %s at 200 %%: frame clock %.3f ms + repaints %.3f ms per frame = %.2f %% of a core at 60 fps, at %.2f GHz "
                 "(budget: under %.0f %%)\n",
                 view == ui::DisplayView::cloud ? "Cloud" : "Timeline", clockMs / frames, paintMs / frames, perFrame * 60.0 / 10.0, ghz,
                 uiBudgetPercent);
}

/**
    The same check in a real window: the editor on the desktop at a global scale, its own frame clock driving
    it and the window repainting what asked, while a second thread plays the programme in real time as a
    host's audio thread does. Reports the message thread's CPU time over ten seconds (every repaint, the
    window's blit and the message loop count; the audio does not) and the main view's paints a second, which
    the window coalesces below the frames asked for while its last blit is pending. A light load runs at
    whatever clock the governor picks (on this machine often under 1 GHz), so the figure is also given in
    cycles and as the share of a core at `fullGHz`, the clock a thread runs flat out at.
*/
void timeWindow (ui::DisplayView view, float scale, double fullGHz)
{
    // Every case on the one documented screen, so a figure from another X setup is never printed as comparable.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr || display->logicalBounds.getWidth() < 2560 || display->logicalBounds.getHeight() < 1600)
    {
        std::printf ("window timing skipped: it needs a screen of 2560 x 1600 (xvfb-run -s \"-screen 0 2560x1600x24\")\n");
        return;
    }

    ui::userSettings::setTheme (ui::ThemeChoice::dark);
    ui::userSettings::setWindowSize ({ 1200, 720 });

    auto owned = std::make_unique<HaliteProcessor>();
    auto& processor = *owned;
    resetParams (processor);
    processor.getUiState().setProperty (uistate::displayView, view == ui::DisplayView::cloud ? "cloud" : "timeline", nullptr);
    Programme programme;
    programme.prepare (processor);
    programme.play (processor, 31.0);

    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
    auto editor = std::make_unique<HaliteEditor> (processor);
    editor->addToDesktop (0);
    editor->setVisible (true);

    std::atomic<bool> running { true };
    std::thread audio ([&]
    {
        constexpr double blockSeconds = 512.0 / 48000.0;
        auto next = std::chrono::steady_clock::now();
        while (running.load())
        {
            programme.play (processor, blockSeconds);
            next += std::chrono::microseconds ((int) (blockSeconds * 1.0e6));
            std::this_thread::sleep_until (next);
        }
    });

    auto* mm = juce::MessageManager::getInstance();
    mm->runDispatchLoopUntil (1500);

    auto& shown = editor->getMainView().current();
    const auto paints0 = shown.paintsDone();
    CycleCounter cycles;
    const auto cpu0 = cpuMs();
    const auto wall0 = juce::Time::getMillisecondCounterHiRes();
    mm->runDispatchLoopUntil (10000);
    const auto cpu = cpuMs() - cpu0;
    const auto cycleCount = cycles.count();
    const auto wall = juce::Time::getMillisecondCounterHiRes() - wall0;
    const auto paints = shown.paintsDone() - paints0;

    // A figure is only worth printing at the scale it claims: the scale the main view's image was drawn at.
    const auto painted = shown.paintedScale();

    running = false;
    audio.join();
    editor->removeFromDesktop();
    editor.reset();
    juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);

    if (std::abs (painted - scale) > 0.01f)
    {
        std::printf ("window, %s at %d %%: not valid, the window painted at %.2fx (%.2f %% of a core)\n",
                     view == ui::DisplayView::cloud ? "Cloud" : "Timeline", juce::roundToInt (scale * 100.0f), (double) painted,
                     100.0 * cpu / wall);
        return;
    }

    std::printf ("window, %s at %d %%: %.0f ms of the message thread in %.0f ms = %.2f %% of a core; the main view painted "
                 "%.1f times a second at %.2fx\n",
                 view == ui::DisplayView::cloud ? "Cloud" : "Timeline", juce::roundToInt (scale * 100.0f), cpu, wall,
                 100.0 * cpu / wall, (double) paints * 1000.0 / wall, (double) painted);

    if (cycleCount > 0.0 && fullGHz > 0.0)
        std::printf ("  %.0f M cycles a second in user space, the clock %.2f GHz; at the full %.2f GHz %.2f %% of a core (a guide; the budget of %.0f %% is "
                     "the image timing's)\n",
                     cycleCount * 1.0e-3 / wall, cycleCount / (cpu * 1.0e6), fullGHz, 100.0 * cycleCount / (wall * 1.0e6 * fullGHz),
                     uiBudgetPercent);
}

} // namespace

int main (int argc, char* argv[])
{
    return runSnapshotTool (argc, argv, {
        .pluginName = "Halite",
        .makeProcessor = [] { return std::make_unique<HaliteProcessor>(); },
        .runSelfTest = runUiSelfTest,
        .renderScenes = [] (const juce::File& dir)
        {
            // HALITE_TIMING_ONLY=1: the timing alone, without the scenes.
            if (juce::SystemStats::getEnvironmentVariable ("HALITE_TIMING_ONLY", {}).isNotEmpty())
                return;
            for (const auto& scene : scenes())
                runScene (scene, dir);
        },
        .timeRendering = []
        {
            // HALITE_TIMING_ONLY=window: the real window's timing alone (run it with a screen of 2560 x 1600 or more).
            if (juce::SystemStats::getEnvironmentVariable ("HALITE_TIMING_ONLY", {}) != "window")
            {
                timeRendering();
                timeDisplay (ui::DisplayView::timeline);
                timeDisplay (ui::DisplayView::cloud);
            }
            const auto fullGHz = busyClockGHz();
            timeWindow (ui::DisplayView::timeline, 2.0f, fullGHz);
            timeWindow (ui::DisplayView::cloud, 2.0f, fullGHz);
            timeWindow (ui::DisplayView::timeline, 1.0f, fullGHz);
        },
    });
}
