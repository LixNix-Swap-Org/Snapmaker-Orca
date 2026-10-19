#pragma once

#define CL_TARGET_OPENCL_VERSION 120
#ifndef CL_SILENCE_DEPRECATION
#define CL_SILENCE_DEPRECATION
#endif
// Third-party headers under -Werror.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <CL/cl.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Slic3r::gpu {

// OpenCL 1.2 entry points, resolved from the system library at run time.
struct Api
{
    decltype(&::clGetPlatformIDs)          GetPlatformIDs;
    decltype(&::clGetPlatformInfo)         GetPlatformInfo;
    decltype(&::clGetDeviceIDs)            GetDeviceIDs;
    decltype(&::clGetDeviceInfo)           GetDeviceInfo;
    decltype(&::clCreateContext)           CreateContext;
    decltype(&::clReleaseContext)          ReleaseContext;
    decltype(&::clCreateCommandQueue)      CreateCommandQueue;
    decltype(&::clReleaseCommandQueue)     ReleaseCommandQueue;
    decltype(&::clCreateBuffer)            CreateBuffer;
    decltype(&::clReleaseMemObject)        ReleaseMemObject;
    decltype(&::clEnqueueWriteBuffer)      EnqueueWriteBuffer;
    decltype(&::clEnqueueReadBuffer)       EnqueueReadBuffer;
    decltype(&::clEnqueueMapBuffer)        EnqueueMapBuffer;
    decltype(&::clEnqueueUnmapMemObject)   EnqueueUnmapMemObject;
    decltype(&::clCreateProgramWithSource) CreateProgramWithSource;
    decltype(&::clBuildProgram)            BuildProgram;
    decltype(&::clGetProgramBuildInfo)     GetProgramBuildInfo;
    decltype(&::clReleaseProgram)          ReleaseProgram;
    decltype(&::clCreateKernel)            CreateKernel;
    decltype(&::clReleaseKernel)           ReleaseKernel;
    decltype(&::clSetKernelArg)            SetKernelArg;
    decltype(&::clGetKernelWorkGroupInfo)  GetKernelWorkGroupInfo;
    decltype(&::clEnqueueNDRangeKernel)    EnqueueNDRangeKernel;
    decltype(&::clFinish)                  Finish;
    decltype(&::clGetEventProfilingInfo)   GetEventProfilingInfo;
    decltype(&::clReleaseEvent)            ReleaseEvent;
};

struct Device
{
    Api              cl;
    cl_platform_id   platform { nullptr };
    cl_device_id     id { nullptr };
    cl_context       context { nullptr };
    cl_command_queue queue { nullptr };
    bool             unified { false };
    // Kernel events carry their GPU time.
    bool             profiling { false };
    // Doubles rounded as on the host, fma included.
    bool             fp64 { false };
    uint64_t         max_alloc { 0 };
    std::string      name;
};

class Error : public std::runtime_error
{
public:
    Error(cl_int code, const char *call);
    cl_int code;
};

void check(cl_int err, const char *call);

struct Buffer
{
    Buffer(const Device &dev, cl_mem_flags flags, size_t size, const void *host = nullptr);
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    const Device &dev;
    cl_mem        mem { nullptr };
};

// One call of a kernel; empty when the call runs on the CPU.
class Lease
{
public:
    Lease(const char *kernel, size_t items, size_t min_items);
    ~Lease();
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;

    explicit operator bool() const { return m_device != nullptr; }
    const Device &device() const { return *m_device; }
    // The kernel is built from source with options on first use, kept for the process.
    cl_kernel kernel(const char *source, const std::string &options);
    // This call has to be compared with the CPU (SLIC3R_GPU_VERIFY or the first call on the device).
    bool verify() const { return m_verify; }
    // Logs the call, which matched the CPU if verify() asked for it; the CPU time of a compared call decides later ones.
    void done(const std::string &details, double cpu_ms = 0.);
    // Logs the reason, disables the kernel for the process unless told otherwise; the caller runs the CPU path.
    void fail(const std::string &reason, bool disable = true);
    double gpu_ms { 0. };

private:
    const char                  *m_name;
    size_t                       m_items;
    const Device                *m_device { nullptr };
    std::unique_lock<std::mutex> m_lock;
    bool                         m_verify { false };
};

std::string format_count(size_t n);

} // namespace Slic3r::gpu
