#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Model.hpp"

#include <limits>

using namespace Slic3r;

TEST_CASE("Assembly transforms preserve usable poses", "[Model][Assembly]")
{
    Transform3d transform = Transform3d::Identity();
    REQUIRE(assemble_transform_is_sane(transform));
    transform.translate(Vec3d(100.0, -200.0, 50.0));
    transform.rotate(Eigen::AngleAxisd(0.7, Vec3d::UnitZ()));
    transform.scale(Vec3d(-2.0, 0.5, 3.0));
    REQUIRE(assemble_transform_is_sane(transform));

    transform = Transform3d::Identity();
    transform.scale(1e-3);
    REQUIRE(assemble_transform_is_sane(transform));
}

TEST_CASE("Assembly transforms reject corrupted poses", "[Model][Assembly]")
{
    Transform3d transform = Transform3d::Identity();
    const auto check_rejected = [&transform]() { REQUIRE_FALSE(assemble_transform_is_sane(transform)); };
    SECTION("Collapsed axis") { transform.linear()(2, 2) = 0.0; check_rejected(); }
    SECTION("Linearly dependent nonzero columns") { transform.linear().col(1) = transform.linear().col(0); check_rejected(); }
    SECTION("Nearly collapsed axis") { transform.linear()(2, 2) = 1e-9; check_rejected(); }
    SECTION("Excessive scaling") { transform.scale(1e7); check_rejected(); }
    SECTION("Excessive translation") { transform.translation().x() = 1e15; check_rejected(); }
    SECTION("Nonfinite translation") { transform.translation().x() = std::numeric_limits<double>::infinity(); check_rejected(); }
    SECTION("Nonfinite linear part") { transform.linear()(0, 0) = std::numeric_limits<double>::quiet_NaN(); check_rejected(); }
}
