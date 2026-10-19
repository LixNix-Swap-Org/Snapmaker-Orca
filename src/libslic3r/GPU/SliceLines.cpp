#include "SliceLines.hpp"

#ifdef SLIC3R_GPU

#include "OpenCL.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <new>
#include <set>
#include <vector>

#include <tbb/task_group.h>

namespace Slic3r::gpu {

static const char *slice_lines_source =
#include "GPU/SliceLines.cl.inc"
    ;

struct SliceLinesCall::Impl
{
    std::unique_ptr<Lease> lease;
    std::string            details;
    bool                   fp64 { false };
};

SliceLinesCall::SliceLinesCall() : m(std::make_unique<Impl>()) {}

SliceLinesCall::~SliceLinesCall() = default;

bool SliceLinesCall::verify() const { return m->lease && m->lease->verify(); }

namespace {
// Two output sets per context, kept for the process: the host decodes one while the kernel writes the other.
struct Chunks
{
    size_t                  records { 0 };
    std::unique_ptr<Buffer> pts[2], codes[2];
};

std::map<cl_context, Chunks> &chunks()
{
    static auto *c = new std::map<cl_context, Chunks>();
    return *c;
}

// Contexts where the native double kernel failed to build or to match the CPU.
std::set<cl_context> &no_fp64()
{
    static auto *c = new std::set<cl_context>();
    return *c;
}

// Mapped sets, unmapped on every exit before the queue drains.
struct Mapped
{
    const Device &dev;
    cl_mem        mem[2][2] {};
    void         *ptr[2][2] {};
    void map(int s, cl_mem pts, cl_mem codes, size_t n)
    {
        const size_t size[2] = { n * 4 * sizeof(int32_t), n * sizeof(uint32_t) };
        mem[s][0] = pts;
        mem[s][1] = codes;
        for (int i = 0; i < 2; ++ i) {
            cl_int err = CL_SUCCESS;
            ptr[s][i]  = dev.cl.EnqueueMapBuffer(dev.queue, mem[s][i], CL_TRUE, CL_MAP_READ, 0, size[i], 0, nullptr, nullptr, &err);
            check(err, "clEnqueueMapBuffer");
        }
    }
    void release(int s)
    {
        for (int i = 0; i < 2; ++ i)
            if (ptr[s][i] != nullptr) {
                dev.cl.EnqueueUnmapMemObject(dev.queue, mem[s][i], ptr[s][i], 0, nullptr, nullptr);
                ptr[s][i] = nullptr;
            }
    }
    ~Mapped()
    {
        release(0);
        release(1);
        dev.cl.Finish(dev.queue);
    }
};

struct Events
{
    const Api            &cl;
    std::vector<cl_event> list;
    ~Events()
    {
        for (cl_event e : list)
            if (e != nullptr)
                cl.ReleaseEvent(e);
    }
    // GPU time of the finished commands, negative when a driver gives none.
    double ms() const
    {
        double ns = 0.;
        for (cl_event e : list) {
            cl_ulong start = 0, end = 0;
            if (e == nullptr || cl.GetEventProfilingInfo(e, CL_PROFILING_COMMAND_START, sizeof(start), &start, nullptr) != CL_SUCCESS ||
                cl.GetEventProfilingInfo(e, CL_PROFILING_COMMAND_END, sizeof(end), &end, nullptr) != CL_SUCCESS)
                return -1.;
            ns += double(end - start);
        }
        return list.empty() ? -1. : ns * 1e-6;
    }
};
} // namespace

bool SliceLinesCall::run(const SliceLinesJob &job)
{
    const size_t records = job.offset[job.num_faces];
    // Smaller calls lost on an M4 Pro starting from an idle clock.
    m->lease = std::make_unique<Lease>("slice_lines", records, 1000000);
    Lease &lease = *m->lease;
    if (! lease) {
        m->lease.reset();
        return false;
    }
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
    const Device &dev = lease.device();
    const Api    &cl  = dev.cl;
    double      build = 0., up = 0., wait = 0., decode = 0., sort = 0., total = 0., kernel_ms = -1.;
    size_t      lines = 0;
    std::string fp64_error;
    try {
        auto t0         = Clock::now();
        auto kernel_for = [&](bool fp64) {
            return lease.kernel(slice_lines_source, std::string(job.host_fma ? "-DHOST_FMA=1" : "-DHOST_FMA=0") + (fp64 ? " -DUSE_FP64=1" : " -DUSE_FP64=0"));
        };
        cl_kernel kernel = nullptr;
        m->fp64          = dev.fp64 && no_fp64().count(dev.context) == 0;
        if (m->fp64) {
            try {
                kernel = kernel_for(true);
            } catch (const Error &e) {
                no_fp64().insert(dev.context);
                m->fp64    = false;
                fp64_error = std::string(", fp64 failed: ") + e.what();
            }
        }
        if (! m->fp64)
            kernel = kernel_for(false);
        build = ms(t0);
        t0    = Clock::now();
        job.prepare();
        const cl_mem_flags in = CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR;
        Buffer v(dev, in, job.num_vertices * 3 * sizeof(float), job.vertices);
        Buffer tri(dev, in, job.num_faces * 3 * sizeof(int32_t), job.indices);
        Buffer zs(dev, in, job.num_zs * sizeof(float), job.zs);
        Buffer first(dev, in, job.num_faces * sizeof(uint32_t), job.first);
        Buffer offset(dev, in, (job.num_faces + 1) * sizeof(uint32_t), job.offset);
        const int zero = 0;
        Buffer    bad(dev, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(int), &zero);
        // At most 1M records per launch, far below display watchdogs (Windows TDR 2 s).
        const size_t chunk = std::max<size_t>(1, std::min<size_t>({ records, size_t(1) << 20, size_t(dev.max_alloc / 16) }));
        Chunks      &out   = chunks()[dev.context];
        if (out.records < chunk) {
            out = Chunks();
            // Pinned memory on discrete GPUs, zero-copy on unified memory.
            const cl_mem_flags flags = CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR;
            for (int s = 0; s < 2; ++ s) {
                out.pts[s]   = std::make_unique<Buffer>(dev, flags, chunk * 4 * sizeof(int32_t));
                out.codes[s] = std::make_unique<Buffer>(dev, flags, chunk * sizeof(uint32_t));
            }
            out.records = chunk;
        }
        size_t wg = 0, simd = 0;
        check(cl.GetKernelWorkGroupInfo(kernel, dev.id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(wg), &wg, nullptr), "clGetKernelWorkGroupInfo");
        cl.GetKernelWorkGroupInfo(kernel, dev.id, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(simd), &simd, nullptr);
        // At most 64, whole SIMD widths (NVIDIA 32, AMD 32 or 64, Intel 8 to 32) where possible.
        size_t local = std::clamp<size_t>(wg, 1, 64);
        if (simd > 0 && simd <= local)
            local -= local % simd;
        auto arg = [&](cl_uint i, size_t size, const void *value) { check(cl.SetKernelArg(kernel, i, size, value), "clSetKernelArg"); };
        arg(0, sizeof(cl_mem), &v.mem);
        arg(1, sizeof(cl_mem), &tri.mem);
        arg(2, sizeof(cl_mem), &zs.mem);
        arg(3, sizeof(cl_mem), &first.mem);
        arg(4, sizeof(cl_mem), &offset.mem);
        arg(10, sizeof(cl_mem), &bad.mem);
        up = ms(t0);
        Events events { cl, {} };
        Mapped mapped { dev };
        auto   decode_set = [&](int s, uint32_t f0, uint32_t f1) {
            const auto t = Clock::now();
            lines += job.decode(f0, f1, static_cast<const int32_t *>(mapped.ptr[s][0]), static_cast<const uint32_t *>(mapped.ptr[s][1]));
            decode += ms(t);
        };
        // Declared after the mapped sets: a running decode ends before they are unmapped.
        tbb::task_group decoding;
        int             prev = -1;
        uint32_t        prev_f0 = 0, prev_f1 = 0;
        for (uint32_t f0 = 0; f0 < job.num_faces;) {
            // The facets whose records fit into one chunk.
            const uint32_t *end = std::upper_bound(job.offset + f0 + 1, job.offset + job.num_faces + 1, job.offset[f0] + uint32_t(chunk));
            const uint32_t  f1  = uint32_t(end - job.offset) - 1;
            if (f1 == f0)
                throw Error(CL_INVALID_BUFFER_SIZE, "chunking");
            const uint32_t rec0 = job.offset[f0], n = job.offset[f1] - rec0;
            if (n > 0) {
                const int s = prev == 0 ? 1 : 0;
                arg(5, sizeof(uint32_t), &f0);
                arg(6, sizeof(uint32_t), &f1);
                arg(7, sizeof(uint32_t), &rec0);
                arg(8, sizeof(cl_mem), &out.pts[s]->mem);
                arg(9, sizeof(cl_mem), &out.codes[s]->mem);
                const size_t global = (size_t(n) + local - 1) / local * local;
                cl_event    *event  = dev.profiling ? &events.list.emplace_back(nullptr) : nullptr;
                check(cl.EnqueueNDRangeKernel(dev.queue, kernel, 1, nullptr, &global, &local, 0, nullptr, event), "clEnqueueNDRangeKernel");
                // The previous chunk is decoded on the worker threads while this one is computed and read back.
                if (prev >= 0)
                    decoding.run([&decode_set, p = prev, a = prev_f0, b = prev_f1] { decode_set(p, a, b); });
                const auto t = Clock::now();
                mapped.map(s, out.pts[s]->mem, out.codes[s]->mem, n);
                wait += ms(t);
                if (prev >= 0) {
                    decoding.wait();
                    mapped.release(prev);
                    job.throw_on_cancel();
                }
                prev    = s;
                prev_f0 = f0;
                prev_f1 = f1;
            }
            f0 = f1;
        }
        if (prev >= 0) {
            decode_set(prev, prev_f0, prev_f1);
            mapped.release(prev);
        }
        // Every kernel finished before its chunk was mapped.
        kernel_ms = events.ms();
        int bad_input = 0;
        check(cl.EnqueueReadBuffer(dev.queue, bad.mem, CL_TRUE, 0, sizeof(int), &bad_input, 0, nullptr, nullptr), "clEnqueueReadBuffer");
        if (bad_input) {
            lease.fail("input out of range");
            m->lease.reset();
            return false;
        }
        const auto t1 = Clock::now();
        job.sort();
        sort  = ms(t1);
        total = ms(t0);
    } catch (const Error &e) {
        lease.fail(e.what());
        m->lease.reset();
        return false;
    } catch (const std::bad_alloc &) {
        lease.fail("out of memory");
        m->lease.reset();
        return false;
    }
    lease.gpu_ms = total;
    char buf[256], kernel_part[32] = "";
    if (kernel_ms >= 0.)
        std::snprintf(kernel_part, sizeof(kernel_part), ", kernel %.1f ms", kernel_ms);
    std::snprintf(buf, sizeof(buf), "%zu facets, %zu planes, %s lines, %s, gpu %.1f ms (up %.1f, wait %.1f, decode %.1f, sort %.1f)%s, build %.1f ms",
                  job.num_faces, job.num_zs, format_count(lines).c_str(), m->fp64 ? "fp64" : "soft", lease.gpu_ms, up, wait, decode, sort, kernel_part, build);
    m->details = buf + fp64_error;
    return true;
}

void SliceLinesCall::finish(size_t mismatches, double cpu_ms, const std::string &mismatch)
{
    if (! m->lease)
        return;
    char cpu[48] = "";
    if (m->lease->verify())
        std::snprintf(cpu, sizeof(cpu), ", verify identical, cpu %.1f ms", cpu_ms);
    // A native double mismatch leaves the integer kernel for the next calls.
    if (mismatches > 0 && m->fp64)
        no_fp64().insert(m->lease->device().context);
    if (mismatches > 0)
        m->lease->fail("verify mismatch " + mismatch + (m->fp64 ? " (fp64)" : ""), ! m->fp64);
    else
        m->lease->done(m->details + cpu, cpu_ms);
    m->lease.reset();
}

} // namespace Slic3r::gpu

#else // SLIC3R_GPU

namespace Slic3r::gpu {

struct SliceLinesCall::Impl {};
SliceLinesCall::SliceLinesCall() {}
SliceLinesCall::~SliceLinesCall() = default;
bool SliceLinesCall::run(const SliceLinesJob &) { return false; }
bool SliceLinesCall::verify() const { return false; }
void SliceLinesCall::finish(size_t, double, const std::string &) {}

} // namespace Slic3r::gpu

#endif // SLIC3R_GPU
