// Demo: open the interactive viewer (ADR-0016/0017/0018) on a sample complex
// field. Left-drag orbits, scroll zooms; keys: Esc quit, L linear/log opacity,
// C colormap (twilight/HSV), R reset camera.
//
// Usage:
//   iv_view              open the window and run until closed
//   iv_view --frames N   render N frames then exit (verification; reports
//                        validation cleanliness) — used to smoke-test the present
//                        loop on a display without manual interaction.
//
// Example code exercising the public viewer API; not part of libiv, not on the
// test gate.

#include "iv/error.hpp"
#include "iv/vk/viewer.hpp"
#include "iv/vk/volume.hpp"

#include <array>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using iv::GridDims;

// f = w·exp(-((z-½)/0.2)²/2) with w = (x-½) + i(y-½): a phase vortex column faded
// along z (same family as the offscreen demo), 128³.
std::vector<std::complex<float>> buildVortex(GridDims d) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        const float fz = (static_cast<float>(z) + 0.5f) / static_cast<float>(d.nz);
        const float t = (fz - 0.5f) / 0.2f;
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
    std::uint32_t frames = 0; // 0 => run until the window closes
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    auto viewer = iv::vk::Viewer::create();
    if (!viewer) {
        std::fprintf(stderr, "Viewer::create failed: %s\n", iv::format(viewer.error()).c_str());
        return 1;
    }

    const GridDims dims{128, 128, 128};
    const auto field = buildVortex(dims);
    auto vol = iv::vk::Volume::create(viewer->context(), field, dims);
    if (!vol) {
        std::fprintf(stderr, "Volume::create failed: %s\n", iv::format(vol.error()).c_str());
        return 1;
    }
    viewer->setVolume(std::move(*vol));
    viewer->params().background = {0.05f, 0.05f, 0.07f, 1.0f};
    viewer->params().densityScale = 2.5f;

    // A small screen-space overlay (ADR-0021): a white center crosshair (lines) and a
    // translucent cyan corner quad (triangles) — exercises the overlay graphics pass
    // in the windowed path. M7 replaces this with the bounding box / axes / legend.
    {
        auto& ov = viewer->overlay();
        const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 0.8f};
        auto line = [&](float x, float y) { return iv::vk::OverlayVertex{{x, y, 0.0f}, white}; };
        ov.lines = {line(-0.1f, 0.0f), line(0.1f, 0.0f), line(0.0f, -0.1f), line(0.0f, 0.1f)};
        const std::array<float, 4> cyan{0.0f, 1.0f, 1.0f, 0.4f};
        auto q = [&](float x, float y) { return iv::vk::OverlayVertex{{x, y, 0.0f}, cyan}; };
        ov.triangles = {q(-1.0f, -1.0f), q(-0.6f, -1.0f), q(-0.6f, -0.6f),
                        q(-1.0f, -1.0f), q(-0.6f, -0.6f), q(-1.0f, -0.6f)};
    }

    iv::Status status;
    if (frames > 0) {
        // Render in two halves with a resize between them to exercise swapchain
        // recreation on the display (ADR-0017 verification).
        const std::uint32_t half = frames / 2;
        status = viewer->runFrames(half);
        if (status && half < frames) {
            viewer->requestResize(960, 540);
            status = viewer->runFrames(frames - half);
        }
    } else {
        status = viewer->run();
    }
    if (!status) {
        std::fprintf(stderr, "viewer loop failed: %s\n", iv::format(status.error()).c_str());
        return 1;
    }

    const bool clean = viewer->context().validationClean();
    std::printf("viewer finished; validation %s (%u messages)\n", clean ? "CLEAN" : "DIRTY",
                viewer->context().validationMessageCount());
    return clean ? 0 : 2;
}
