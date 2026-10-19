#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::gpu {

// --gpu on the command line; SLIC3R_GPU still decides when set.
void request_from_cli();
// Preferences: mode "off" | "auto" | "on" and a device filter; SLIC3R_GPU and SLIC3R_GPU_DEVICE in the environment win.
void configure(const std::string &mode, const std::string &device);
// True when the GPU is switched on and a device is usable; selects the device on first use.
bool enabled();
// The selected device, empty if none.
std::string device_name();
// "platform | vendor | name" of every usable GPU, best first; empty without OpenCL.
std::vector<std::string> device_names();

// Calls of a kernel that ran on the GPU (and matched the CPU when compared).
size_t completed_calls(const char *kernel);

// Test hook: forces the switch and device filter, compares every call; force drops the size thresholds.
class ScopedOverride
{
public:
    explicit ScopedOverride(bool on, const std::string &device = {}, bool force = true);
    ~ScopedOverride();
    ScopedOverride(const ScopedOverride &) = delete;
    ScopedOverride &operator=(const ScopedOverride &) = delete;

private:
    int         m_mode;
    std::string m_device;
    bool        m_force;
    int         m_verify;
};

// Adds the wall time of its scope to a named slicing step, see report_steps().
class StepTimer
{
public:
    explicit StepTimer(const char *step);
    ~StepTimer();
    StepTimer(const StepTimer &) = delete;
    StepTimer &operator=(const StepTimer &) = delete;

private:
    const char                           *m_step;
    std::chrono::steady_clock::time_point m_start;
};

// Logs "[time] <step> <seconds>" per step since the last report, "[time] total" and the GPU summary.
void report_steps();
// Drops what an unfinished run added since the last report.
void reset_steps();

} // namespace Slic3r::gpu
