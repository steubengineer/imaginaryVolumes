// Demo: open the interactive viewer (ADR-0016/0017/0018) on a complex scalar field —
// either a file you supply or a built-in synthetic vortex. With text built, the labeled,
// camera-tracking plot (box + ticked axes + the phase x magnitude legend) is produced by the
// one-call high-level facade iv::makePlot (ADR-0029); without text, a simple line/quad overlay.
// Left-drag orbits, scroll zooms; keys: Esc quit, L linear/log opacity, C colormap
// (twilight/HSV), R reset, up/down opacity density, left/right log decade window (ADR-0027),
// [ / ] legend opacity thickness (ADR-0030).
//
// Usage:
//   iv_view                                   built-in 128^3 phase vortex
//   iv_view --input FILE --dims NX NY NZ      load a dataset (see format below)
//   iv_view ... --density D                   opacity density scale (default 2.5)
//   iv_view ... --decades N                   log mode showing the top N decades of
//                                             magnitude (ADR-0027); meaningful when N <
//                                             the data's decade span (printed on load).
//                                             Uses a gentler density unless --density is
//                                             given. Adjust live with the left/right
//                                             arrows (which engage at 4 decades from off).
//   iv_view ... --frames N                    render N frames then exit (smoke test)
//
// Input format: a raw, headerless binary file of NX*NY*NZ complex values as
// interleaved (real, imag) 32-bit floats (i.e. numpy complex64), native-endian, in
// x-fastest order — element (x,y,z) at index x + NX*(y + NY*z). From numpy:
//     a.astype(np.complex64).reshape(NZ, NY, NX).tofile("field.bin")   # C-order
// then: iv_view --input field.bin --dims NX NY NZ
//
// Example code exercising the public viewer/plot API; not part of libiv, not on the test gate.

#include "iv/error.hpp"
#include "iv/vk/viewer.hpp"
#include "iv/vk/volume.hpp"

#ifdef IV_VIEW_TEXT
#include "iv/plot.hpp" // iv::makePlot, iv::PlotOptions, iv::Axis (ADR-0029)
#endif

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using iv::GridDims;

// f = w·exp(-((z-½)/0.2)²/2) with w = (x-½) + i(y-½): a phase vortex column faded
// along z (same family as the offscreen demo).
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

// Read NX*NY*NZ interleaved (re,im) float32 values (numpy complex64) into the field.
// std::complex<float> is two contiguous floats, so the bytes map 1:1. Returns nullopt
// on open failure or if the file is smaller than expected.
std::optional<std::vector<std::complex<float>>> loadRawComplex64(const std::string& path,
                                                                 GridDims d) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return std::nullopt;
    }
    const auto need = static_cast<std::streamoff>(d.count() * sizeof(std::complex<float>));
    if (f.tellg() < need) {
        return std::nullopt; // file too small for the given dimensions
    }
    f.seekg(0);
    std::vector<std::complex<float>> v(d.count());
    f.read(reinterpret_cast<char*>(v.data()), need);
    if (f.gcount() != need) {
        return std::nullopt;
    }
    return v;
}

std::string baseName(const std::string& p) {
    const auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

} // namespace

int main(int argc, char** argv) {
    std::uint32_t frames = 0; // 0 => run until the window closes
    const char* input = nullptr;
    GridDims dims{128, 128, 128};
    bool haveDims = false;
    float density = 2.5f;
    bool densitySet = false;
    float decades = 0.0f; // ADR-0027: log decade window (0 = full range)

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (std::strcmp(argv[i], "--dims") == 0 && i + 3 < argc) {
            dims.nx = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            dims.ny = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            dims.nz = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            haveDims = true;
        } else if (std::strcmp(argv[i], "--density") == 0 && i + 1 < argc) {
            density = std::strtof(argv[++i], nullptr);
            densitySet = true;
        } else if (std::strcmp(argv[i], "--decades") == 0 && i + 1 < argc) {
            decades = std::strtof(argv[++i], nullptr);
        }
    }

    if (input != nullptr && !haveDims) {
        std::fprintf(stderr, "--input requires --dims <nx> <ny> <nz>\n");
        return 1;
    }
    if (dims.nx == 0 || dims.ny == 0 || dims.nz == 0) {
        std::fprintf(stderr, "--dims values must be positive\n");
        return 1;
    }

    // Build or load the field.
    std::vector<std::complex<float>> field;
    std::string title;
    if (input != nullptr) {
        auto loaded = loadRawComplex64(input, dims);
        if (!loaded) {
            std::fprintf(stderr,
                         "failed to read '%s' as %ux%ux%u complex64 (%zu values, %zu bytes)\n",
                         input, dims.nx, dims.ny, dims.nz, static_cast<std::size_t>(dims.count()),
                         static_cast<std::size_t>(dims.count()) * sizeof(std::complex<float>));
            return 1;
        }
        field = std::move(*loaded);
        title = baseName(input);
    } else {
        field = buildVortex(dims);
        title = "phase vortex";
    }

    // Report the data's dynamic range (host-side) so a meaningful --decades can be chosen: the
    // log decade window only changes the image when N < the data's own decade span.
    {
        float mx = 0.0f;
        float mn = 0.0f;
        bool any = false;
        for (const auto& z : field) {
            const float m = std::abs(z);
            if (m > mx) {
                mx = m;
            }
            if (m > 0.0f && (!any || m < mn)) {
                mn = m;
                any = true;
            }
        }
        const double span =
            (any && mx > mn) ? std::log10(static_cast<double>(mx) / static_cast<double>(mn)) : 0.0;
        std::printf("magnitude range: [%.4g, %.4g]  (%.1f decades of dynamic range)\n",
                    static_cast<double>(any ? mn : 0.0f), static_cast<double>(mx), span);
    }

    // A high density saturates opacity and hides the transfer function, so when the decade
    // window is engaged but no density was given, use a gentler default (linear keeps 2.5).
    if (decades > 0.0f && !densitySet) {
        density = 1.0f;
    }

#ifdef IV_VIEW_TEXT
    // High-level facade (ADR-0029): one call builds the window + volume + the labeled,
    // camera-tracking plot (box + ticked axes, ADR-0024/0026) and the phase x magnitude
    // legend (ADR-0028). The viewer's keys still edit params() live; the legend tracks them.
    iv::PlotOptions opts;
    opts.width = 1000u;
    opts.height = 1000u;
    opts.densityScale = density;
    opts.logDecades = decades;
    if (decades > 0.0f) {
        opts.opacityMode = 1u; // log mode, so the decade window is visible
    }
    if (input != nullptr) {
        // Loaded data: label the axes with voxel-index extents (no physical units known).
        opts.axes.x = iv::Axis{0.0, static_cast<double>(dims.nx - 1), "x", "", {}, {}};
        opts.axes.y = iv::Axis{0.0, static_cast<double>(dims.ny - 1), "y", "", {}, {}};
        opts.axes.z = iv::Axis{0.0, static_cast<double>(dims.nz - 1), "z", "", {}, {}};
    } else {
        opts.axes.x = iv::Axis{0.0, 1.0, "x", "", {}, {}};
        opts.axes.y = iv::Axis{0.0, 1.0, "y", "", {}, {}};
        opts.axes.z = iv::Axis{-1.0, 1.0, "z", "", {}, {}};
    }
    opts.axes.title = title;

    auto viewer = iv::makePlot(std::span<const std::complex<float>>(field), dims, opts);
    if (!viewer) {
        std::fprintf(stderr, "makePlot failed: %s\n", iv::format(viewer.error()).c_str());
        return 1;
    }
#else
    // Text disabled: build the viewer + volume by hand and draw a small screen-space overlay
    // (ADR-0021) — a white center crosshair (lines) and a translucent cyan corner quad.
    auto viewer = iv::vk::Viewer::create(iv::vk::Viewer::Options{1000u, 1000u});
    if (!viewer) {
        std::fprintf(stderr, "Viewer::create failed: %s\n", iv::format(viewer.error()).c_str());
        return 1;
    }
    auto vol = iv::vk::Volume::create(viewer->context(), field, dims);
    if (!vol) {
        std::fprintf(stderr, "Volume::create failed: %s\n", iv::format(vol.error()).c_str());
        return 1;
    }
    viewer->setVolume(std::move(*vol));
    viewer->params().background = {0.05f, 0.05f, 0.07f, 1.0f};
    viewer->params().densityScale = density;
    viewer->params().logDecades = decades;
    if (decades > 0.0f) {
        viewer->params().opacityMode = 1u;
    }
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
#endif

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
