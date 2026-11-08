#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/MeshRepair.hpp>
#include <string>
#include <vector>

using namespace Slic3r;

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Catch::Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

// The repair behind "Fix Model": a mesh with a missing facet comes back closed at its old volume.
// Hidden; run with the [MeshRepair] tag.
TEST_CASE("CGAL repair closes a mesh with a missing facet", "[.][MeshRepair]") {
    auto check_repair = [](const indexed_triangle_set &closed_its) {
        TriangleMesh closed(closed_its);
        REQUIRE(closed.stats().manifold());
        const float closed_volume = closed.volume();

        indexed_triangle_set open_its = closed_its;
        open_its.indices.pop_back();
        TriangleMesh mesh(open_its);
        REQUIRE(mesh.stats().open_edges > 0);

        std::string error;
        REQUIRE(repair_mesh(mesh, nullptr, &error));
        CHECK(error.empty());
        CHECK(mesh.stats().open_edges == 0);
        CHECK(its_num_open_edges(mesh.its) == 0);
        CHECK(mesh.volume() == Catch::Approx(closed_volume).epsilon(0.01));
    };

    SECTION("20 mm cube") {
        check_repair(its_make_cube(20., 20., 20.));
    }
    SECTION("Prusa.stl from the test data") {
        TriangleMesh loaded;
        const std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        REQUIRE(loaded.ReadSTLFile(path.c_str()));
        check_repair(loaded.its);
    }
}

TEST_CASE("mcut difference handles source splits between cuts", "[MeshBoolean]") {
    TriangleMesh body = make_cube(30., 10., 10.);

    TriangleMesh tool;

    // First cut splits the source into two disconnected components.
    TriangleMesh slab = make_cube(2., 12., 20.);
    slab.translate(Vec3f(14.f, -1.f, -5.f));
    its_merge(tool.its, slab.its);

    // These cuts must still be applied after the source has been split.
    TriangleMesh left_hole = make_cube(4., 4., 20.);
    left_hole.translate(Vec3f(3.f, 3.f, -5.f));
    its_merge(tool.its, left_hole.its);

    TriangleMesh right_hole = make_cube(4., 4., 20.);
    right_hole.translate(Vec3f(21.f, 3.f, -5.f));
    its_merge(tool.its, right_hole.its);

    std::vector<TriangleMesh> result;
    MeshBoolean::mcut::make_boolean(body, tool, result, "A_NOT_B");

    REQUIRE(result.size() == 1);

    const std::vector<indexed_triangle_set> components =
        its_split(result.front().its);

    REQUIRE(components.size() == 2);

    // 3000 - 200 - 160 - 160 = 2480.
    REQUIRE_THAT(
        result.front().volume(),
        Catch::Matchers::WithinRel(2480., 1e-3)
    );
}
