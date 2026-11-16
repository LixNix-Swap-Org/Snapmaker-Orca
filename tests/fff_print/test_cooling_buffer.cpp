#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCode/CoolingBuffer.hpp"

using namespace Slic3r;

// Fill the cooling-relevant fields of a FullPrintConfig for a single extruder.
static void init_cooling_config(FullPrintConfig &config)
{
    config.fan_min_speed.values                 = { 20 };   // %
    config.fan_max_speed.values                 = { 100 };  // %
    config.slow_down_layer_time.values          = { 8 };    // s
    config.fan_cooling_layer_time.values        = { 12 };   // s
    config.close_fan_the_first_x_layers.values  = { 2 };
    config.full_fan_speed_layer.values          = { 0 };
    config.slow_down_min_speed.values           = { 5 };    // mm/s
    config.overhang_fan_speed.values            = { 80 };   // %
    config.reduce_fan_stop_start_freq.values    = { false };
    // Explicitly pin the defaults the assertions depend on, so a future default
    // change cannot silently break the golden values.
    config.slow_down_for_layer_cooling.values   = { true };
    config.auxiliary_fan.value                  = false;
    config.gcode_flavor.value                   = gcfMarlinFirmware;
    // Junction deviation: a lone move starts and stops at rest, so 10 mm/s at 1000 mm/s^2 adds 0.01 s.
    config.machine_max_junction_deviation.values     = { 0.01 };
    config.machine_max_acceleration_extruding.values = { 1000., 1000. };
    config.machine_max_acceleration_x.values         = { 1000., 1000. };
}

// A single extrusion move; F600 == 10 mm/s, so `length` mm takes `length/10` s.
// The ;_EXTRUDE_SET_SPEED marker makes the line count toward the layer time and
// be adjustable (otherwise layer time is forced to 0 in parse_layer_gcode).
// Marker on its own F line, as GCode writes it: the layer timer replaces that line when slowed.
static std::string layer_with_length(float length)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "G1 F600 ;_EXTRUDE_SET_SPEED\nG1 X%.0f Y0 E1\n;_EXTRUDE_END\n", length);
    return std::string(buf);
}

SCENARIO("CoolingBuffer disables fan for the first N layers", "[CoolingBuffer]") {
    GIVEN("close_fan_the_first_x_layers = 2") {
        FullPrintConfig config = FullPrintConfig::defaults();
        init_cooling_config(config);
        GCode gcodegen;
        gcodegen.apply_print_config(config);
        gcodegen.writer().set_extruders({ 0 });
        gcodegen.writer().set_extruder(0);
        CoolingBuffer cb(gcodegen);

        WHEN("layer 0 is processed") {
            std::string out = cb.process_layer(layer_with_length(10.f), 0, true);
            THEN("the fan is off") {
                REQUIRE(out.find("M106 S0") != std::string::npos);
            }
        }
        AND_WHEN("layer 1 is processed") {
            std::string out = cb.process_layer(layer_with_length(10.f), 1, true);
            THEN("the fan is still off") {
                REQUIRE(out.find("M106 S0") != std::string::npos);
            }
        }
        AND_WHEN("layer 2 is processed") {
            std::string out = cb.process_layer(layer_with_length(10.f), 2, true);
            THEN("the fan turns on") {
                REQUIRE(out.find("M106 S255") != std::string::npos);
            }
        }
    }
}

SCENARIO("CoolingBuffer fan speed three-segment interpolation", "[CoolingBuffer]") {
    GIVEN("slow_down_layer_time=8s, fan_cooling_layer_time=12s") {
        auto run = [](float length, size_t layer_id) {
            FullPrintConfig config = FullPrintConfig::defaults();
            init_cooling_config(config);
            GCode gcodegen;
            gcodegen.apply_print_config(config);
            gcodegen.writer().set_extruders({ 0 });
            gcodegen.writer().set_extruder(0);
            CoolingBuffer cb(gcodegen);
            return cb.process_layer(layer_with_length(length), layer_id, true);
        };

        THEN("a very short layer gets full fan") {
            // 1s layer is slowed to the minimum speed (2s), still below 8s, so the
            // "below slow_down threshold" full-throttle branch fires. For ordinary
            // short layers that CAN stretch to 8s this branch is unreachable.
            REQUIRE(run(10.f, 5).find("M106 S255") != std::string::npos);
        }
        THEN("an intermediate layer interpolates fan (between thresholds)") {
            // 10.01s: 100 - 80 * 0.5025 = 59.8, floored to 59% (S150).
            REQUIRE(run(100.f, 5).find("M106 S150") != std::string::npos);
        }
        THEN("a long layer turns the fan off (above cooling threshold)") {
            REQUIRE(run(150.f, 5).find("M106 S0") != std::string::npos);
        }
        THEN("a layer just under the cooling threshold keeps a low fan") {
            // 11.91s: 100 - 80 * 0.9775 = 21.8, floored to 21% (S53).
            REQUIRE(run(119.f, 5).find("M106 S53") != std::string::npos);
        }
        THEN("a layer just past the cooling threshold turns the fan off") {
            // 12.01s, past fan_cooling_layer_time.
            REQUIRE(run(120.f, 5).find("M106 S0") != std::string::npos);
        }
    }
}

SCENARIO("CoolingBuffer slows a fast layer down to min_print_speed", "[CoolingBuffer]") {
    GIVEN("slow_down_layer_time=8s, slow_down_min_speed=5mm/s") {
        FullPrintConfig config = FullPrintConfig::defaults();
        init_cooling_config(config);
        GCode gcodegen;
        gcodegen.apply_print_config(config);
        gcodegen.writer().set_extruders({ 0 });
        gcodegen.writer().set_extruder(0);
        CoolingBuffer cb(gcodegen);

        WHEN("a 1s layer is processed") {
            std::string out = cb.process_layer(layer_with_length(10.f), 5, true);
            THEN("the feedrate is clamped to 5mm/s (F300)") {
                REQUIRE(out.find("F300") != std::string::npos);
            }
        }
    }
}

SCENARIO("CoolingBuffer ramps fan up to the full speed layer", "[CoolingBuffer]") {
    GIVEN("close_fan_the_first_x_layers=2, full_fan_speed_layer=4") {
        auto run_layer = [](size_t layer_id) {
            FullPrintConfig config = FullPrintConfig::defaults();
            init_cooling_config(config);
            config.full_fan_speed_layer.values = { 4 };
            GCode gcodegen;
            gcodegen.apply_print_config(config);
            gcodegen.writer().set_extruders({ 0 });
            gcodegen.writer().set_extruder(0);
            CoolingBuffer cb(gcodegen);
            return cb.process_layer(layer_with_length(10.f), layer_id, true);
        };

        THEN("layer 2 (mid-ramp) gets half fan") {
            REQUIRE(run_layer(2).find("M106 S127") != std::string::npos);
        }
        THEN("layer 4 (past full_fan_speed_layer) gets full fan") {
            REQUIRE(run_layer(4).find("M106 S255") != std::string::npos);
        }
    }
}

SCENARIO("CoolingBuffer overhang fan override", "[CoolingBuffer]") {
    GIVEN("overhang_fan_speed=80%, a long layer (base fan off)") {
        FullPrintConfig config = FullPrintConfig::defaults();
        init_cooling_config(config);
        GCode gcodegen;
        gcodegen.apply_print_config(config);
        gcodegen.writer().set_extruders({ 0 });
        gcodegen.writer().set_extruder(0);
        CoolingBuffer cb(gcodegen);

        WHEN("a long layer with overhang fan markers is processed") {
            std::string out = cb.process_layer(
                ";_OVERHANG_FAN_START\n"
                "G1 X150 Y0 E1 F600 ;_EXTRUDE_SET_SPEED\n"
                ";_OVERHANG_FAN_END\n",
                5, true);
            THEN("the overhang fan speed (80% -> S204) is emitted") {
                REQUIRE(out.find("M106 S204") != std::string::npos);
            }
        }
    }
}
