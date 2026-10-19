#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Slic3r::gpu {

// Plane-facet intersections of slice_mesh(), one record per (facet, plane) pair of the facet's z range.
struct SliceLinesJob
{
    const float    *vertices { nullptr }; // xyz per vertex, XY scaled
    size_t          num_vertices { 0 };
    const int32_t  *indices { nullptr };  // 3 per facet
    size_t          num_faces { 0 };
    const float    *zs { nullptr };
    size_t          num_zs { 0 };
    const uint32_t *first { nullptr };    // first plane per facet
    const uint32_t *offset { nullptr };   // records before each facet, num_faces + 1 entries
    // The host compiler fuses the interpolation of slice_facet() into one fma.
    bool            host_fma { false };
    // Sets up the lines once the call runs on the GPU.
    std::function<void()> prepare;
    // Turns the records of facets [f0, f1) into lines, returns their number.
    std::function<size_t(uint32_t f0, uint32_t f1, const int32_t *pts, const uint32_t *codes)> decode;
    // Orders the lines once all are decoded.
    std::function<void()> sort;
    std::function<void()> throw_on_cancel;
};

// One call; false from run() leaves the slicing to the CPU.
class SliceLinesCall
{
public:
    SliceLinesCall();
    ~SliceLinesCall();
    bool run(const SliceLinesJob &job);
    // The lines have to be compared with the CPU result before finish().
    bool verify() const;
    // Logs the call with the CPU time of a compared call; a mismatch disables the kernel, or its native doubles.
    void finish(size_t mismatches, double cpu_ms, const std::string &mismatch);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace Slic3r::gpu
