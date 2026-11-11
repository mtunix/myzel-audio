#include "dsp/models/desk/LineAmp.h"

#include <bit>
#include <cstdint>
#include <mutex>

namespace umber::dsp::desk
{

std::shared_ptr<const LineAmpCurve> LineAmpCurve::get (double L, double un)
{
    // Tiny cache: every instance with the same loop shares one table (normally exactly one).
    static std::mutex lock;
    static std::vector<std::shared_ptr<const LineAmpCurve>> cache;

    const std::scoped_lock guard (lock);

    const auto same = [] (double x, double y) { return std::bit_cast<std::uint64_t> (x) == std::bit_cast<std::uint64_t> (y); };

    for (const auto& c : cache)
        if (same (c->loopGain, L) && same (c->negSwing, un))
            return c;

    cache.push_back (std::make_shared<const LineAmpCurve> (L, un));
    return cache.back();
}

} // namespace umber::dsp::desk
