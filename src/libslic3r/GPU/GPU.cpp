#include "GPU.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/log/trivial.hpp>

#ifdef SLIC3R_GPU
#include "OpenCL.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
#else
#include <dlfcn.h>
#endif
#endif

namespace Slic3r::gpu {

enum Mode { Off = 0, On = 1, Auto = 2 };

struct State
{
    std::mutex  mutex;
    bool        env_set { false };
    int         env_mode { Off };
    bool        cli { false };
    bool        echo { false };
    // SLIC3R_GPU_VERIFY: -1 first call per device, 0 never, 1 every call.
    int         verify { -1 };
    // SLIC3R_GPU_FP64=0: no native doubles.
    bool        fp64 { true };
    std::string filter;
    int         pref_mode { Off };
    std::string pref_filter;
    // Test override; -1 = none.
    int         override_mode { -1 };
    bool        force { false };
    bool        reported_off { false };
#ifdef SLIC3R_GPU
    // Selection result per device filter, kept for the process.
    std::map<std::string, const Device *> selected;
#endif
};

static State &state()
{
    static State *s = [] {
        auto *s = new State();
        auto value = [](const char *name, std::string &out) {
            const char *v = std::getenv(name);
            if (v != nullptr) {
                out = v;
                std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            }
            return v != nullptr;
        };
        auto off = [](const std::string &v) { return v.empty() || v == "0" || v == "off" || v == "false" || v == "no"; };
        std::string v;
        if (value("SLIC3R_GPU", v)) {
            s->env_set  = true;
            s->env_mode = off(v) ? Off : v == "auto" ? Auto : On;
        }
        if (const char *d = std::getenv("SLIC3R_GPU_DEVICE"))
            s->filter = d;
        if (value("SLIC3R_GPU_VERIFY", v))
            s->verify = off(v) ? 0 : 1;
        if (value("SLIC3R_GPU_FP64", v))
            s->fp64 = ! off(v);
        s->echo = s->env_set;
        return s;
    }();
    return *s;
}

static void log_line(const std::string &line, bool important = true)
{
    if (state().echo) {
        BOOST_LOG_TRIVIAL(warning) << line;
        std::fprintf(stderr, "%s\n", line.c_str());
        std::fflush(stderr);
    } else if (important)
        BOOST_LOG_TRIVIAL(warning) << line;
    else
        BOOST_LOG_TRIVIAL(info) << line;
}

void request_from_cli()
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.cli  = true;
    s.echo = true;
}

void configure(const std::string &mode, const std::string &device)
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.pref_mode   = mode == "on" ? On : mode == "auto" ? Auto : Off;
    s.pref_filter = device;
}

static int mode(const State &s)
{
    return s.override_mode >= 0 ? s.override_mode : s.env_set ? s.env_mode : s.cli ? On : s.pref_mode;
}

#ifdef SLIC3R_GPU

Error::Error(cl_int code, const char *call) : std::runtime_error(std::string(call) + " failed (" + std::to_string(code) + ")"), code(code) {}

void check(cl_int err, const char *call)
{
    if (err != CL_SUCCESS)
        throw Error(err, call);
}

static void *load_library()
{
#ifdef _WIN32
    // System32 only: never the application or working directory.
    return reinterpret_cast<void *>(::LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
#elif defined(__APPLE__)
    return ::dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
#else
    void *lib = ::dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    return lib ? lib : ::dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *symbol(void *lib, const char *name)
{
#ifdef _WIN32
    return reinterpret_cast<void *>(::GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return ::dlsym(lib, name);
#endif
}

// Empty on success, else what is missing.
static std::string load_api(Api &cl)
{
    void *lib = load_library();
    if (lib == nullptr)
        return "no OpenCL library";
#define SLIC3R_CL_RESOLVE(fn) \
    if ((cl.fn = reinterpret_cast<decltype(cl.fn)>(symbol(lib, "cl" #fn))) == nullptr) return "no cl" #fn " in the OpenCL library";
    SLIC3R_CL_RESOLVE(GetPlatformIDs) SLIC3R_CL_RESOLVE(GetPlatformInfo) SLIC3R_CL_RESOLVE(GetDeviceIDs) SLIC3R_CL_RESOLVE(GetDeviceInfo)
    SLIC3R_CL_RESOLVE(CreateContext) SLIC3R_CL_RESOLVE(ReleaseContext) SLIC3R_CL_RESOLVE(CreateCommandQueue) SLIC3R_CL_RESOLVE(ReleaseCommandQueue)
    SLIC3R_CL_RESOLVE(CreateBuffer) SLIC3R_CL_RESOLVE(ReleaseMemObject) SLIC3R_CL_RESOLVE(EnqueueWriteBuffer) SLIC3R_CL_RESOLVE(EnqueueReadBuffer)
    SLIC3R_CL_RESOLVE(EnqueueMapBuffer) SLIC3R_CL_RESOLVE(EnqueueUnmapMemObject) SLIC3R_CL_RESOLVE(CreateProgramWithSource) SLIC3R_CL_RESOLVE(BuildProgram)
    SLIC3R_CL_RESOLVE(GetProgramBuildInfo) SLIC3R_CL_RESOLVE(ReleaseProgram) SLIC3R_CL_RESOLVE(CreateKernel) SLIC3R_CL_RESOLVE(ReleaseKernel)
    SLIC3R_CL_RESOLVE(SetKernelArg) SLIC3R_CL_RESOLVE(GetKernelWorkGroupInfo) SLIC3R_CL_RESOLVE(EnqueueNDRangeKernel) SLIC3R_CL_RESOLVE(Finish)
    SLIC3R_CL_RESOLVE(GetEventProfilingInfo) SLIC3R_CL_RESOLVE(ReleaseEvent)
#undef SLIC3R_CL_RESOLVE
    return {};
}

static std::string trimmed(std::string s)
{
    s.resize(std::strlen(s.c_str()));
    while (! s.empty() && std::isspace((unsigned char)s.back()))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i]))
        ++ i;
    return s.substr(i);
}

static std::string platform_str(const Api &cl, cl_platform_id p, cl_platform_info what)
{
    size_t n = 0;
    if (cl.GetPlatformInfo(p, what, 0, nullptr, &n) != CL_SUCCESS || n == 0)
        return {};
    std::string s(n, '\0');
    return cl.GetPlatformInfo(p, what, n, s.data(), nullptr) == CL_SUCCESS ? trimmed(s) : std::string();
}

static std::string device_str(const Api &cl, cl_device_id d, cl_device_info what)
{
    size_t n = 0;
    if (cl.GetDeviceInfo(d, what, 0, nullptr, &n) != CL_SUCCESS || n == 0)
        return {};
    std::string s(n, '\0');
    return cl.GetDeviceInfo(d, what, n, s.data(), nullptr) == CL_SUCCESS ? trimmed(s) : std::string();
}

template<typename T> static T device_val(const Api &cl, cl_device_id d, cl_device_info what, T fallback)
{
    T v {};
    return cl.GetDeviceInfo(d, what, sizeof(T), &v, nullptr) == CL_SUCCESS ? v : fallback;
}

static bool icontains(const std::string &haystack, const std::string &needle)
{
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != haystack.end();
}

// "OpenCL 1.2 ..." as 120.
static int cl_version(const std::string &s)
{
    int major = 0, minor = 0;
    return std::sscanf(s.c_str(), "OpenCL %d.%d", &major, &minor) == 2 ? major * 100 + minor * 10 : 0;
}

// The OpenCL entry points; missing names what did not load.
static const Api &api(std::string &missing)
{
    static Api               cl {};
    static const std::string why = load_api(cl);
    missing = why;
    return cl;
}

struct Candidate { cl_platform_id platform; cl_device_id id; bool gpu; double score; bool unified, apple, fp64; std::string label, line; uint64_t max_alloc; };

// Usable devices matching the filter, best first; false without an OpenCL platform.
static bool find_candidates(const Api &cl, const std::string &filter, bool log, std::vector<Candidate> &candidates)
{
    cl_uint num_platforms = 0;
    if (cl.GetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS || num_platforms == 0)
        return false;
    std::vector<cl_platform_id> platforms(num_platforms);
    cl.GetPlatformIDs(num_platforms, platforms.data(), nullptr);
    for (cl_platform_id p : platforms) {
        const std::string pname = platform_str(cl, p, CL_PLATFORM_NAME);
        const std::string pver  = platform_str(cl, p, CL_PLATFORM_VERSION);
        cl_uint num_devices = 0;
        if (cl.GetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 0, nullptr, &num_devices) != CL_SUCCESS || num_devices == 0)
            continue;
        std::vector<cl_device_id> devices(num_devices);
        cl.GetDeviceIDs(p, CL_DEVICE_TYPE_ALL, num_devices, devices.data(), nullptr);
        for (cl_device_id d : devices) {
            const std::string name   = device_str(cl, d, CL_DEVICE_NAME);
            const std::string vendor = device_str(cl, d, CL_DEVICE_VENDOR);
            const std::string ext    = device_str(cl, d, CL_DEVICE_EXTENSIONS);
            // CL_DEVICE_BOARD_NAME_AMD: AMD reports a codename as the device name.
            const std::string board  = icontains(ext, "cl_amd_device_attribute_query") ? device_str(cl, d, 0x4038) : std::string();
            std::string label = pname + " | " + vendor + " | " + name;
            if (! board.empty())
                label += " | " + board;
            const cl_device_type type   = device_val<cl_device_type>(cl, d, CL_DEVICE_TYPE, 0);
            const bool           is_gpu = (type & CL_DEVICE_TYPE_GPU) != 0;
            const char          *kind   = is_gpu ? "GPU" : (type & CL_DEVICE_TYPE_CPU) ? "CPU" : "accelerator";
            const bool           named  = ! filter.empty() && icontains(label, filter);
            if ((! filter.empty() && ! named) || (! is_gpu && ! named))
                continue;
            if (! device_val<cl_bool>(cl, d, CL_DEVICE_AVAILABLE, CL_FALSE) || ! device_val<cl_bool>(cl, d, CL_DEVICE_COMPILER_AVAILABLE, CL_FALSE) ||
                cl_version(device_str(cl, d, CL_DEVICE_VERSION)) < 120 || icontains(name, "llvmpipe") || icontains(name, "Basic Render"))
                continue;
            const bool     unified = device_val<cl_bool>(cl, d, CL_DEVICE_HOST_UNIFIED_MEMORY, CL_TRUE) != CL_FALSE;
            const uint64_t global  = device_val<cl_ulong>(cl, d, CL_DEVICE_GLOBAL_MEM_SIZE, 0);
            const cl_uint  cu      = device_val<cl_uint>(cl, d, CL_DEVICE_MAX_COMPUTE_UNITS, 0);
            const cl_uint  clock   = device_val<cl_uint>(cl, d, CL_DEVICE_MAX_CLOCK_FREQUENCY, 0);
            double score = (unified ? 0. : 1000.) + std::min(double(global) / 1e9, 64.) + double(cu) * double(clock) / 1000.;
            // Layered and Mesa drivers after the vendor's own for the same card.
            if (icontains(pname, "OpenCLOn12") || icontains(pname, "Clover") || icontains(pname, "rusticl"))
                score -= 500.;
            char line[512];
            std::snprintf(line, sizeof(line), "[gpu] %s | %s | %s | %s | driver %s | %.1f GB %s | %u CU", pver.c_str(), pname.c_str(),
                          (board.empty() ? name : board + " (" + name + ")").c_str(), kind, device_str(cl, d, CL_DRIVER_VERSION).c_str(),
                          double(global) / 1e9, unified ? "unified" : "discrete", cu);
            const cl_device_fp_config fp64 = CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN | CL_FP_DENORM | CL_FP_FMA;
            candidates.push_back({ p, d, is_gpu, score, unified, icontains(pname, "Apple"),
                                   icontains(ext, "cl_khr_fp64") && (device_val<cl_device_fp_config>(cl, d, CL_DEVICE_DOUBLE_FP_CONFIG, 0) & fp64) == fp64,
                                   label, line, device_val<cl_ulong>(cl, d, CL_DEVICE_MAX_MEM_ALLOC_SIZE, 0) });
            char cand[512];
            std::snprintf(cand, sizeof(cand), "[gpu] candidate %zu: %s | %s score %.0f", candidates.size(), label.c_str(), kind, score);
            if (log)
                log_line(cand, false);
        }
    }
    // Any GPU before other device types; the next one when a device does not open.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) { return std::make_pair(a.gpu, a.score) > std::make_pair(b.gpu, b.score); });
    return true;
}

static const Device *select_device(const std::string &filter, bool discrete_only)
{
    const auto  t0 = std::chrono::steady_clock::now();
    std::string missing;
    const Api  &cl = api(missing);
    if (! missing.empty()) {
        log_line("[gpu] -> CPU: " + missing);
        return nullptr;
    }
    std::vector<Candidate> candidates;
    if (! find_candidates(cl, filter, true, candidates)) {
        log_line("[gpu] -> CPU: no OpenCL platform");
        return nullptr;
    }
    if (candidates.empty()) {
        log_line(filter.empty() ? "[gpu] -> CPU: no OpenCL GPU" : "[gpu] -> CPU: no OpenCL device matching \"" + filter + "\"");
        return nullptr;
    }
    if (discrete_only && candidates.front().unified) {
        log_line("[gpu] -> CPU: auto mode and no discrete GPU");
        return nullptr;
    }
    for (const Candidate &c : candidates) {
        if (discrete_only && c.unified)
            continue;
        cl_int                      err     = CL_SUCCESS;
        const cl_context_properties props[] = { CL_CONTEXT_PLATFORM, cl_context_properties(c.platform), 0 };
        cl_context                  context = cl.CreateContext(props, 1, &c.id, nullptr, nullptr, &err);
        cl_command_queue            queue   = nullptr;
        // Apple's event times are not in ns.
        bool profiling = ! c.apple;
        if (context != nullptr && profiling) {
            queue     = cl.CreateCommandQueue(context, c.id, CL_QUEUE_PROFILING_ENABLE, &err);
            profiling = queue != nullptr;
        }
        if (context != nullptr && queue == nullptr)
            queue = cl.CreateCommandQueue(context, c.id, 0, &err);
        if (queue == nullptr) {
            if (context != nullptr)
                cl.ReleaseContext(context);
            log_line("[gpu] " + c.label + ": context creation failed (" + std::to_string(err) + ")");
            continue;
        }
        // Context and queue stay alive until the process ends: some drivers crash when released at exit.
        auto *dev       = new Device();
        dev->cl         = cl;
        dev->platform   = c.platform;
        dev->id         = c.id;
        dev->context    = context;
        dev->queue      = queue;
        dev->unified    = c.unified;
        dev->profiling  = profiling;
        dev->fp64       = c.fp64 && state().fp64;
        // A failed query counts as 128 MB, the full-profile minimum.
        dev->max_alloc  = c.max_alloc > 0 ? c.max_alloc : uint64_t(128) << 20;
        dev->name       = device_str(cl, c.id, CL_DEVICE_NAME);
        char init[48];
        std::snprintf(init, sizeof(init), " | init %.0f ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        log_line(c.line + init);
        return dev;
    }
    log_line("[gpu] -> CPU: no OpenCL device opened");
    return nullptr;
}

static const std::string &device_filter(const State &s) { return s.filter.empty() ? s.pref_filter : s.filter; }

static const Device *selected_device()
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const int m = mode(s);
    if (m == Off) {
        if (s.echo && ! s.reported_off) {
            s.reported_off = true;
            log_line("[gpu] off");
        }
        return nullptr;
    }
    const std::string &filter = device_filter(s);
    const std::string  key    = filter + (m == Auto ? "\n" : "");
    auto it = s.selected.find(key);
    if (it == s.selected.end())
        it = s.selected.emplace(key, select_device(filter, m == Auto)).first;
    return it->second;
}

bool enabled() { return selected_device() != nullptr; }

std::string device_name()
{
    const Device *d = selected_device();
    return d ? d->name : std::string();
}

std::vector<std::string> device_names()
{
    static std::atomic<bool> logged { false };
    std::vector<std::string> names;
    try {
        std::string            missing;
        const Api             &cl = api(missing);
        std::vector<Candidate> candidates;
        if (missing.empty() && find_candidates(cl, {}, ! logged.exchange(true), candidates))
            for (const Candidate &c : candidates)
                if (std::find(names.begin(), names.end(), c.label) == names.end())
                    names.push_back(c.label);
    } catch (...) {
    }
    return names;
}

Buffer::Buffer(const Device &dev, cl_mem_flags flags, size_t size, const void *host) : dev(dev)
{
    cl_int err = CL_SUCCESS;
    mem = dev.cl.CreateBuffer(dev.context, flags, std::max<size_t>(size, 4), const_cast<void *>(host), &err);
    check(err, "clCreateBuffer");
}

Buffer::~Buffer()
{
    if (mem)
        dev.cl.ReleaseMemObject(mem);
}

// A compared call: its size and the time of both paths.
struct Sample
{
    size_t items { 0 };
    double gpu_ms { 0. }, cpu_ms { 0. };
};

struct KernelState
{
    bool                                                     disabled { false };
    std::set<cl_context>                                     verified;
    // Last compared call per context.
    std::map<cl_context, Sample>                             samples;
    // Last reason logged for running a call on the CPU.
    std::string                                              routed;
    size_t                                                   completed { 0 };
    size_t                                                   calls { 0 };
    size_t                                                   items { 0 };
    double                                                   ms { 0. };
    std::map<std::pair<cl_context, std::string>, cl_kernel> built;
};

static std::mutex                          g_device_mutex;
static std::mutex                          g_kernels_mutex;
static std::map<std::string, KernelState> &kernels()
{
    static auto *k = new std::map<std::string, KernelState>();
    return *k;
}

// Why a call runs on the CPU: the last compared one has to show the GPU a fifth faster, its time scaled up but never down.
static std::string faster_on_cpu(const KernelState &k, cl_context context, size_t items)
{
    auto it = k.samples.find(context);
    if (it == k.samples.end())
        return {};
    const Sample &s     = it->second;
    const double  scale = double(items) / double(s.items);
    if (s.gpu_ms * std::max(1., scale) < 0.8 * s.cpu_ms * scale)
        return {};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "faster on the CPU (%s items: gpu %.1f ms, cpu %.1f ms)", format_count(s.items).c_str(), s.gpu_ms, s.cpu_ms);
    return buf;
}

Lease::Lease(const char *kernel, size_t items, size_t min_items) : m_name(kernel), m_items(items)
{
    const Device *dev = selected_device();
    if (dev == nullptr)
        return;
    const State &s = state();
    std::string  cpu;
    {
        std::lock_guard<std::mutex> lock(g_kernels_mutex);
        KernelState &k = kernels()[kernel];
        if (k.disabled)
            return;
        m_verify = s.verify > 0 || (s.verify < 0 && k.verified.count(dev->context) == 0);
        if (! s.force && items < min_items)
            cpu = "under " + format_count(min_items) + " items";
        else if (! s.force && s.verify <= 0)
            cpu = faster_on_cpu(k, dev->context, items);
        // Each reason once.
        if (! cpu.empty() && cpu == k.routed)
            return;
        if (! cpu.empty())
            k.routed = cpu;
    }
    if (! cpu.empty()) {
        log_line(std::string("[gpu] ") + kernel + " -> CPU: " + cpu, false);
        return;
    }
    m_lock = std::unique_lock<std::mutex>(g_device_mutex, std::try_to_lock);
    if (! m_lock.owns_lock()) {
        log_line(std::string("[gpu] ") + kernel + " -> CPU: busy", false);
        return;
    }
    m_device = dev;
}

Lease::~Lease() = default;

cl_kernel Lease::kernel(const char *source, const std::string &options)
{
    {
        std::lock_guard<std::mutex> lock(g_kernels_mutex);
        const KernelState &k = kernels()[m_name];
        if (auto it = k.built.find({ m_device->context, options }); it != k.built.end())
            return it->second;
    }
    const Api &cl  = m_device->cl;
    cl_int     err = CL_SUCCESS;
    cl_program program = cl.CreateProgramWithSource(m_device->context, 1, &source, nullptr, &err);
    check(err, "clCreateProgramWithSource");
    const std::string opts = "-cl-std=CL1.2 " + options;
    err = cl.BuildProgram(program, 1, &m_device->id, opts.c_str(), nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t n = 0;
        cl.GetProgramBuildInfo(program, m_device->id, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
        std::string build_log(n, '\0');
        if (n > 0)
            cl.GetProgramBuildInfo(program, m_device->id, CL_PROGRAM_BUILD_LOG, n, build_log.data(), nullptr);
        cl.ReleaseProgram(program);
        throw Error(err, ("clBuildProgram: " + trimmed(build_log).substr(0, 2048)).c_str());
    }
    cl_kernel kernel = cl.CreateKernel(program, m_name, &err);
    // The kernel keeps the program alive.
    cl.ReleaseProgram(program);
    check(err, "clCreateKernel");
    std::lock_guard<std::mutex> lock(g_kernels_mutex);
    kernels()[m_name].built[{ m_device->context, options }] = kernel;
    return kernel;
}

void Lease::done(const std::string &details, double cpu_ms)
{
    {
        std::lock_guard<std::mutex> lock(g_kernels_mutex);
        KernelState &k = kernels()[m_name];
        if (m_verify)
            k.verified.insert(m_device->context);
        if (m_verify && m_items > 0 && gpu_ms > 0. && cpu_ms > 0.)
            k.samples[m_device->context] = { m_items, gpu_ms, cpu_ms };
        ++ k.completed;
        ++ k.calls;
        k.items += m_items;
        k.ms += gpu_ms;
    }
    log_line(std::string("[gpu] ") + m_name + ": " + details, false);
}

void Lease::fail(const std::string &reason, bool disable)
{
    if (disable) {
        std::lock_guard<std::mutex> lock(g_kernels_mutex);
        kernels()[m_name].disabled = true;
    }
    log_line(std::string("[gpu] ") + m_name + " -> CPU: " + reason);
}

size_t completed_calls(const char *kernel)
{
    std::lock_guard<std::mutex> lock(g_kernels_mutex);
    auto it = kernels().find(kernel);
    return it == kernels().end() ? 0 : it->second.completed;
}

std::string format_count(size_t n)
{
    char buf[32];
    if (n >= 1000000)
        std::snprintf(buf, sizeof(buf), "%.2fM", double(n) / 1e6);
    else if (n >= 1000)
        std::snprintf(buf, sizeof(buf), "%.1fk", double(n) / 1e3);
    else
        std::snprintf(buf, sizeof(buf), "%zu", n);
    return buf;
}

static void report_kernels(bool log)
{
    std::lock_guard<std::mutex> lock(g_kernels_mutex);
    for (auto &[name, k] : kernels()) {
        if (log && k.calls > 0) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[gpu] summary %s %zu calls, %s items, %.1f ms gpu", name.c_str(), k.calls, format_count(k.items).c_str(), k.ms);
            log_line(buf);
        }
        k.calls = 0;
        k.items = 0;
        k.ms    = 0.;
    }
}

#else // SLIC3R_GPU

bool enabled()
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (mode(s) != Off && ! s.reported_off) {
        s.reported_off = true;
        log_line("[gpu] -> CPU: built without SLIC3R_GPU");
    }
    return false;
}

std::string device_name() { return {}; }

std::vector<std::string> device_names() { return {}; }

size_t completed_calls(const char *) { return 0; }

static void report_kernels(bool) {}

#endif // SLIC3R_GPU

ScopedOverride::ScopedOverride(bool on, const std::string &device, bool force)
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    m_mode           = s.override_mode;
    m_device         = s.filter;
    m_force          = s.force;
    m_verify         = s.verify;
    s.override_mode  = on ? On : Off;
    s.force          = force;
    s.verify         = 1;
    if (! device.empty())
        s.filter = device;
}

ScopedOverride::~ScopedOverride()
{
    State &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.override_mode = m_mode;
    s.filter        = m_device;
    s.force         = m_force;
    s.verify        = m_verify;
}

using Clock = std::chrono::steady_clock;

static std::mutex                                  g_steps_mutex;
static std::vector<std::pair<std::string, double>> g_steps;
static Clock::time_point                           g_steps_since;

StepTimer::StepTimer(const char *step) : m_step(step), m_start(Clock::now())
{
    std::lock_guard<std::mutex> lock(g_steps_mutex);
    if (g_steps.empty())
        g_steps_since = m_start;
}

StepTimer::~StepTimer()
{
    const double seconds = std::chrono::duration<double>(Clock::now() - m_start).count();
    std::lock_guard<std::mutex> lock(g_steps_mutex);
    auto it = std::find_if(g_steps.begin(), g_steps.end(), [this](const auto &s) { return s.first == m_step; });
    if (it == g_steps.end())
        g_steps.emplace_back(m_step, seconds);
    else
        it->second += seconds;
}

void report_steps()
{
    std::vector<std::pair<std::string, double>> steps;
    Clock::time_point                           since;
    {
        std::lock_guard<std::mutex> lock(g_steps_mutex);
        steps.swap(g_steps);
        since = g_steps_since;
    }
    if (steps.empty())
        return;
    char buf[160];
    for (const auto &[step, seconds] : steps) {
        std::snprintf(buf, sizeof(buf), "[time] %s %.2f", step.c_str(), seconds);
        log_line(buf, false);
    }
    std::snprintf(buf, sizeof(buf), "[time] total %.2f", std::chrono::duration<double>(Clock::now() - since).count());
    log_line(buf, false);
    report_kernels(true);
}

void reset_steps()
{
    {
        std::lock_guard<std::mutex> lock(g_steps_mutex);
        g_steps.clear();
    }
    report_kernels(false);
}

} // namespace Slic3r::gpu
