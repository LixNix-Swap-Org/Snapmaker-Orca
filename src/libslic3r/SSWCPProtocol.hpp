#ifndef slic3r_SSWCPProtocol_hpp_
#define slic3r_SSWCPProtocol_hpp_

#include "libslic3r/PrintConfig.hpp"
#include "nlohmann/json.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

/// Machine identification result parsed from the firmware system_info response.
struct MachineInfo
{
    std::string              model;
    std::string              device_name;
    std::vector<std::string> nozzle_diameters;
    std::vector<std::string> nozzle_volume_types;
};

// Snapmaker Orca fork: flow types are NozzleVolumeType (nvtStandard / nvtHighFlow = 0 / 1, as
// upstream's fvt values); the wire spelling differs from the config one ("Standard" / "High Flow").
constexpr const char *FLOW_MODE_STANDARD  = "standard";
constexpr const char *FLOW_MODE_HIGH_FLOW = "high_flow";

/// Pure protocol-conversion helpers shared by SSWCP handlers and tests.
/// No wxWidgets dependency — safe to unit-test without a GUI event loop.
namespace SSWCPProtocol {

/// Wire spelling of a nozzle volume type. The firmware knows two flow types only: Hybrid and
/// TPU High Flow (and any value out of range) are sent as "standard".
const char *to_wire(NozzleVolumeType type);

/// Nozzle volume type of a wire string; empty for anything but "standard" / "high_flow".
std::optional<NozzleVolumeType> to_nozzle_volume_type(const std::string &wire);

/// Wire spelling of a flow type as a printer or the device page may spell it: upper and lower
/// case and any separator count as the same word ("high_flow", "High Flow", "high-flow",
/// "HighFlow"). Empty for every other text, "tpu_high_flow" included.
std::optional<std::string> normalize_flow_type(const std::string &text);

/// A flow entry that states nothing: null or a blank string. Next to entries that do state a flow
/// type such an entry reads as "standard"; a payload in which no entry states one reports no flow
/// types at all (firmware older than High Flow), and the tool heads keep what the user chose.
bool is_unstated_flow_type(const nlohmann::json &value);

/// Appends one reported nozzle diameter (number or string) as the preset variant text.
/// Returns false, appending nothing, for any value that is not 0.2 / 0.4 / 0.6 / 0.8.
bool append_nozzle_diameter(const nlohmann::json &value, std::vector<std::string> &out);

/// nozzle_diameter arrives as one value (single-nozzle firmware) or as an array.
void parse_nozzle_diameters(const nlohmann::json &value, std::vector<std::string> &out);

/// Result of parsing the optional nozzle_volume_types field in a machine update payload.
enum class OptionalFlowTypesStatus { Missing, Valid, Invalid };

/// Build a filament_volume_type string array aligned with the logical filament count.
/// Values other than nvtHighFlow are normalized to "standard".
/// If *values* is null, every entry is "standard".
std::vector<std::string> build_filament_volume_types(const std::vector<int>* values, size_t count);

/// Wire strings of the nozzle volume types of *head_count* tool heads (the answer
/// "nozzle_volume_types" of sw_GetFileFilamentMapping). Heads beyond *values* are "standard".
std::vector<std::string> build_nozzle_volume_types(const std::vector<int> &values, size_t head_count);

/// The flow type every filament prints with: the nozzle volume type of the tool head that prints
/// it (*filament_head*, 0-based). This tree has no flow type per filament; a filament without a
/// head entry, or with one that names no tool head, counts as nvtStandard.
std::vector<int> filament_volume_types_by_head(const std::vector<int> &nozzle_volume_types,
                                               const std::vector<size_t> &filament_head, size_t filament_count);

/// Tool head whose nozzle size picks the printer preset of a sync: head 0 if its size offers High
/// Flow, else the first "high_flow" head whose diameter ("0.6") passes *size_offers* (empty: the
/// first "high_flow" head), else 0. *head_count*: number of reported diameters.
size_t sync_reference_head(const std::vector<std::string> &flows, size_t head_count,
                           const std::function<bool(const std::string &diameter)> &size_offers = {},
                           const std::vector<std::string> &diameters = {});

/// Parse an optional flow-types array from a JSON object.
/// - Missing key → Missing (caller treats as "no flow data")
/// - Present but wrong type / short / invalid value → Invalid (caller rejects the payload)
/// - Present and valid → Valid, fills *out*
/// Snapmaker Orca fork: the spelling is read through normalize_flow_type(); an unstated entry
/// (is_unstated_flow_type) is "standard", and an array of unstated entries only is Missing.
OptionalFlowTypesStatus parse_optional_flow_types(const nlohmann::json&     object,
                                                  const char*               key,
                                                  size_t                    count,
                                                  std::vector<std::string>& out);

/// If every cached slot has a non-empty diameter, replace *diameters* with the cached
/// values and replace *flows* only when every cached flow is also valid.
/// Returns false (leaving outputs untouched) when any diameter is empty or slots is empty.
bool select_complete_cached_nozzle_info(const std::vector<std::pair<std::string, std::string>>& slots,
                                        std::vector<std::string>&                               diameters,
                                        std::vector<std::string>&                               flows);

/// Normalize a machine model identifier across all sources (system_info, SSDP, DeviceManager).
/// Applies known firmware aliases (e.g. "lava"/"Snapmaker test" -> "Snapmaker U1").
/// CRITICAL: empty input always returns empty — never defaults to a machine type.
/// Idempotent: normalize(normalize(x)) == normalize(x).
std::string normalize_machine_model(const std::string& model);

/// Parse an extruder object map directly. The objects argument is the value of
/// data.status (printer.objects.query) or data.objects (gateway device snapshot).
/// Returns true if at least one extruder with a valid nozzle_diameter was found.
/// Snapmaker Orca fork: an extruder without a flow type next to extruders that state one is
/// "standard" (upstream dropped every flow type then); without any statement *volume_types*
/// stays empty.
bool parse_extruder_objects(const nlohmann::json& objects, std::vector<std::string>& diameters, std::vector<std::string>& volume_types);

/// sw_FinishFilamentMapping params.event — selects the action to run after the
/// preprint webview closes. Extensible: add a new value here, then handle it in
/// SSWCP_MachineOption_Instance (a dedicated on_finish_* handler + a switch case).
///   0 / absent: close the webview dialog, no further action
///   1: open the custom filament flow regrouping dialog
/// Snapmaker Orca fork: there is no flow type per filament and no regrouping dialog here. Event 1
/// leads to the Flow row of the sidebar nozzle tabs instead, where the flow type of a tool head
/// is chosen.
enum class FinishFilamentMappingEvent {
    None              = 0,
    CustomFlowRegroup = 1,
};

/// params.event of sw_FinishFilamentMapping. A number or a numeric string; absent, null, unknown
/// and malformed values are None, so that the webview always closes without an error.
FinishFilamentMappingEvent parse_finish_filament_mapping_event(const nlohmann::json &params);

/// The "result" member of the printer's cloud/config/response to server.client_manager.request_pin_code
/// (sw_get_pin_code). False, *result* untouched, unless it is a JSON object with "result". Never throws:
/// it runs on the MQTT client's thread, where an exception has no handler.
bool parse_pin_code_response(const std::string &message, nlohmann::json &result);

} // namespace SSWCPProtocol
} // namespace Slic3r

#endif // slic3r_SSWCPProtocol_hpp_
