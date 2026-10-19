#include <catch2/catch_all.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>

#include "libslic3r/GPU/GPU.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include "test_utils.hpp"

using namespace Slic3r;

namespace sd {
#include "libslic3r/GPU/SliceLines.cl"
}

namespace {

double to_double(const sd::sd_dbl &d)
{
    return d.m == 0 ? 0. : std::ldexp(double(d.m), d.e) * (d.neg ? -1. : 1.);
}

// Exact equality is the property under test.
uint64_t bits(double d)
{
    uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

struct Expected
{
    double  t;
    int     side;
    int64_t fused, rounded;
};

Expected native(float z, float az, float bz, float a, float b)
{
    Expected e;
    e.t    = (double(z) - double(bz)) / (double(az) - double(bz));
    e.side = e.t <= 0. ? -1 : e.t >= 1. ? 1 : 0;
    e.fused = int64_t(std::floor(std::fma(double(a) - double(b), e.t, double(b)) + 0.5));
    volatile double product = (double(a) - double(b)) * e.t;
    e.rounded = int64_t(std::floor(double(b) + product + 0.5));
    return e;
}

std::vector<float> plane_zs(const indexed_triangle_set &its, size_t n)
{
    float min_z = its.vertices.front().z(), max_z = min_z;
    for (const stl_vertex &v : its.vertices) {
        min_z = std::min(min_z, v.z());
        max_z = std::max(max_z, v.z());
    }
    std::vector<float> zs;
    for (size_t i = 0; i < n; ++ i)
        zs.push_back(min_z + (max_z - min_z) * float(i + 1) / float(n + 1));
    // Planes through vertices.
    for (size_t i = 0; i < its.vertices.size(); i += 7)
        zs.push_back(its.vertices[i].z());
    std::sort(zs.begin(), zs.end());
    zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
    return zs;
}

} // namespace

TEST_CASE("Soft double interpolation matches native double", "[GPU]")
{
    std::mt19937                          rng(42);
    std::uniform_real_distribution<float> coord(-3e8f, 3e8f), height(0.f, 300.f);
    std::uniform_int_distribution<int>    pick(0, 3);
    size_t checked = 0;
    for (int i = 0; i < 2000000; ++ i) {
        float az = height(rng), bz = height(rng), z, a = coord(rng), b = coord(rng);
        switch (pick(rng)) {
        case 0: z = height(rng); break;
        case 1: z = std::nextafter(az, bz); break;
        case 2: z = std::nextafter(bz, az); break;
        default:
            // Large exponent gaps and small magnitudes.
            bz = std::ldexp(bz, -int(rng() % 120));
            a  = std::ldexp(a, -int(rng() % 40));
            b  = std::round(b / 1e4f) + 0.5f;
            z  = std::nextafter(bz, az);
            break;
        }
        if (! ((az < z && z < bz) || (bz < z && z < az)))
            continue;
        const Expected e   = native(z, az, bz, a, b);
        int            bad = 0;
        sd::sd_dbl     t;
        const int      side = sd::sd_param(z, az, bz, &t, &bad);
        REQUIRE(bad == 0);
        REQUIRE(bits(to_double(t)) == bits(e.t));
        REQUIRE(side == e.side);
        if (side == 0) {
            REQUIRE(sd::sd_lerp(a, b, t, 1, &bad) == e.fused);
            REQUIRE(sd::sd_lerp(a, b, t, 0, &bad) == e.rounded);
            REQUIRE(bad == 0);
        }
        ++ checked;
    }
    CHECK(checked > 1000000);
}

TEST_CASE("GPU plane slicing yields the CPU lines", "[GPU]")
{
    const std::string name = GENERATE("20mm_cube.obj", "pyramid.obj", "frog_legs.obj", "sloping_hole.obj");
    const size_t      planes = GENERATE(300, 1000);
    const indexed_triangle_set its = load_model(name).its;
    const std::vector<float>   zs  = plane_zs(its, planes);
    MeshSlicingParams          params;
    std::vector<Polygons>      cpu, gpu_result;
    {
        gpu::ScopedOverride off(false);
        cpu = slice_mesh(its, zs, params);
    }
    gpu::ScopedOverride on(true);
    if (! gpu::enabled())
        SKIP("no OpenCL device");
    const size_t calls = gpu::completed_calls("slice_lines");
    gpu_result = slice_mesh(its, zs, params);
    CHECK(gpu::completed_calls("slice_lines") == calls + 1);
    CHECK(gpu_result == cpu);
}

TEST_CASE("GPU slicing works on a second context of the same device", "[GPU]")
{
    const indexed_triangle_set its = load_model("pyramid.obj").its;
    const std::vector<float>   zs  = plane_zs(its, 300);
    std::vector<Polygons>      cpu;
    {
        gpu::ScopedOverride off(false);
        cpu = slice_mesh(its, zs, MeshSlicingParams());
    }
    std::string device;
    {
        gpu::ScopedOverride on(true);
        if (! gpu::enabled())
            SKIP("no OpenCL device");
        device = gpu::device_name();
        CHECK(slice_mesh(its, zs, MeshSlicingParams()) == cpu);
    }
    // A new filter creates a new context.
    gpu::ScopedOverride named(true, device);
    REQUIRE(gpu::enabled());
    const size_t calls = gpu::completed_calls("slice_lines");
    CHECK(slice_mesh(its, zs, MeshSlicingParams()) == cpu);
    CHECK(gpu::completed_calls("slice_lines") == calls + 1);
}

TEST_CASE("GPU leaves calls under its size threshold to the CPU", "[GPU]")
{
    const indexed_triangle_set its = load_model("pyramid.obj").its;
    const std::vector<float>   zs  = plane_zs(its, 300);
    std::vector<Polygons>      cpu;
    {
        gpu::ScopedOverride off(false);
        cpu = slice_mesh(its, zs, MeshSlicingParams());
    }
    gpu::ScopedOverride on(true, {}, false);
    if (! gpu::enabled())
        SKIP("no OpenCL device");
    const size_t calls = gpu::completed_calls("slice_lines");
    CHECK(slice_mesh(its, zs, MeshSlicingParams()) == cpu);
    CHECK(gpu::completed_calls("slice_lines") == calls);
}

TEST_CASE("GPU switch falls back to the CPU without a matching device", "[GPU]")
{
    const indexed_triangle_set its = load_model("pyramid.obj").its;
    const std::vector<float>   zs  = plane_zs(its, 200);
    std::vector<Polygons>      cpu;
    {
        gpu::ScopedOverride off(false);
        cpu = slice_mesh(its, zs, MeshSlicingParams());
    }
    gpu::ScopedOverride on(true, "no such device 7f3a");
    CHECK_FALSE(gpu::enabled());
    CHECK(gpu::device_name().empty());
    CHECK(slice_mesh(its, zs, MeshSlicingParams()) == cpu);
}

TEST_CASE("GPU switch off leaves the GPU unused", "[GPU]")
{
    gpu::ScopedOverride off(false);
    CHECK_FALSE(gpu::enabled());
}

TEST_CASE("GPU device is selected when one is present", "[GPU]")
{
    gpu::ScopedOverride on(true);
    if (! gpu::enabled())
        SKIP("no OpenCL device");
    CHECK_FALSE(gpu::device_name().empty());
}

TEST_CASE("GPU preference switches the GPU", "[GPU]")
{
    if (std::getenv("SLIC3R_GPU") != nullptr)
        SKIP("SLIC3R_GPU is set");
    bool available = false;
    {
        gpu::ScopedOverride on(true);
        available = gpu::enabled();
    }
    gpu::configure("off", "");
    CHECK_FALSE(gpu::enabled());
    gpu::configure("on", "");
    CHECK(gpu::enabled() == available);
    gpu::configure("off", "");
}
