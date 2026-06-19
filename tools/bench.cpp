// Performance benchmark (ADR-0019): time the headless ray-march of a fixed 512³
// volume to 1280×720 and check the median against the ≥30 FPS (≤33.3 ms) contract.
//
// Headless (no GLFW); not on the default ctest gate (hardware-dependent). On an
// RTX 4070-class GPU the median must be ≤ 33.3 ms; on other hardware it still
// reports but the bound is advisory.
//
// Usage:
//   iv_bench                 N=30 timed frames at the default stepCount
//   iv_bench --frames N      use N timed frames
//   iv_bench --step-mult K   multiply the stepCount by K
//   iv_bench --no-early-term disable early-ray termination (every ray marches all
//                            stepCount samples). Teeth: --no-early-term --step-mult 8
//                            forces ~8x the march work and blows the budget (red).
//   iv_bench --advisory      report only; never fail on the bound

#include "iv/error.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using iv::GridDims;
using iv::vk::Context;
using iv::vk::Renderer;
using iv::vk::RenderParams;
using iv::vk::Volume;

constexpr float kBudgetMs = 33.3f; // ≥ 30 FPS (ADR-0019)
constexpr std::uint32_t kSide = 512;
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;
constexpr std::uint32_t kWarmup = 5;

// Fixed vortex column (deterministic; cheap per-voxel — no per-voxel trig) so
// run-to-run timings are comparable (ADR-0007/0019).
std::vector<std::complex<float>> buildField(GridDims d) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(d.nz);
        const float t = (fz - 0.5f) / 0.25f;
        const float env = std::exp(-0.5f * t * t);
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(d.ny);
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(d.nx);
                v[d.index(x, y, z)] = std::complex<float>(fx - 0.5f, fy - 0.5f) * env;
            }
        }
    }
    return v;
}

} // namespace

int main(int argc, char** argv) {
    std::uint32_t frames = 30;
    std::uint32_t stepMult = 1;
    bool advisory = false;
    bool noEarlyTerm = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--step-mult") == 0 && i + 1 < argc) {
            stepMult = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--no-early-term") == 0) {
            noEarlyTerm = true;
        } else if (std::strcmp(argv[i], "--advisory") == 0) {
            advisory = true;
        }
    }
    if (frames == 0) {
        frames = 30;
    }
    if (stepMult == 0) {
        stepMult = 1;
    }

    auto ctx = Context::create();
    if (!ctx) {
        std::fprintf(stderr, "Context::create failed: %s\n", iv::format(ctx.error()).c_str());
        return 1;
    }
    auto renderer = Renderer::create(*ctx);
    if (!renderer) {
        std::fprintf(stderr, "Renderer::create failed: %s\n", iv::format(renderer.error()).c_str());
        return 1;
    }

    const GridDims dims{kSide, kSide, kSide};
    std::printf("building %ux%ux%u field (%.2f GiB host)...\n", dims.nx, dims.ny, dims.nz,
                static_cast<double>(dims.count() * sizeof(std::complex<float>))
                    / (1024.0 * 1024.0 * 1024.0));
    const auto field = buildField(dims);
    auto vol = Volume::create(*ctx, field, dims);
    if (!vol) {
        std::fprintf(stderr, "Volume::create failed: %s\n", iv::format(vol.error()).c_str());
        return 1;
    }

    RenderParams params;
    params.eye = {1.8f, 1.4f, 2.2f};
    params.stepCount *= stepMult;
    if (noEarlyTerm) {
        params.alphaTermination = 2.0f; // unreachable: every ray marches all steps
    }
    std::printf("rendering %ux%u, stepCount=%u (x%u), earlyTerm=%s, warmup=%u, timed=%u\n", kWidth,
                kHeight, params.stepCount, stepMult, noEarlyTerm ? "off" : "on", kWarmup, frames);

    for (std::uint32_t i = 0; i < kWarmup; ++i) {
        auto img = renderer->render(*vol, kWidth, kHeight, params);
        if (!img) {
            std::fprintf(stderr, "warmup render failed: %s\n", iv::format(img.error()).c_str());
            return 1;
        }
    }

    std::vector<double> ms;
    ms.reserve(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        auto img = renderer->render(*vol, kWidth, kHeight, params);
        const auto t1 = std::chrono::steady_clock::now();
        if (!img) {
            std::fprintf(stderr, "render failed: %s\n", iv::format(img.error()).c_str());
            return 1;
        }
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    std::sort(ms.begin(), ms.end());
    const double minMs = ms.front();
    const double maxMs = ms.back();
    const double medMs = ms[ms.size() / 2];
    std::printf("min=%.2f ms  median=%.2f ms (%.1f FPS)  max=%.2f ms\n", minMs, medMs,
                1000.0 / medMs, maxMs);

    const bool pass = medMs <= static_cast<double>(kBudgetMs);
    std::printf("contract (median <= %.1f ms, >=30 FPS): %s%s\n", static_cast<double>(kBudgetMs),
                pass ? "PASS" : "FAIL", advisory ? " (advisory)" : "");
    if (!pass && !advisory) {
        return 1;
    }
    return 0;
}
