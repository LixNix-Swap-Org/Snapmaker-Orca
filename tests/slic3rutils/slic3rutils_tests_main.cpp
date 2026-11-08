#include <catch2/catch_all.hpp>

#include <boost/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/plugin/PythonInterpreter.hpp>

#include "catch2/interfaces/catch_interfaces_reporter.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/OrcaCloudServiceAgent.hpp"

namespace {

// Finalizes the embedded interpreter once, after the last test case, and never between cases.
// CPython cannot be reinitialized after Py_Finalize (a second init aborts inside _datetime), and
// the app likewise finalizes it only at exit, so the per-test fixtures tear down only the
// PluginManager. Running at the end of the run, rather than from a static destructor, also keeps
// shutdown()'s logging ahead of boost::log's teardown.
class PythonInterpreterRunEnd final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded(const Catch::TestRunStats&) override
    {
        if (!Slic3r::PythonInterpreter::instance().is_initialized())
            return;
        Slic3r::PluginManager::instance().shutdown();
        Slic3r::PythonInterpreter::instance().shutdown();
    }
};

// Runs the body in a directory of its own, so that a file a defect creates under a relative name
// is found there instead of wherever the suite happens to have been started.
class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory(const boost::filesystem::path& dir)
        : m_previous(boost::filesystem::current_path())
    {
        boost::filesystem::current_path(dir);
    }
    ~ScopedWorkingDirectory()
    {
        boost::system::error_code ec;
        boost::filesystem::current_path(m_previous, ec);
    }
    ScopedWorkingDirectory(const ScopedWorkingDirectory&)            = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

private:
    boost::filesystem::path m_previous;
};

nlohmann::json flat_session_json(const nlohmann::json& fields)
{
    nlohmann::json session = {
        {"access_token", "test-token"},
        {"user_id", "test-user-id"}
    };
    session.update(fields);
    return session;
}

} // namespace

CATCH_REGISTER_LISTENER(PythonInterpreterRunEnd)

TEST_CASE("Check SSL certificates paths", "[Http][NotWorking]") {
    
    Slic3r::Http g = Slic3r::Http::get("https://github.com/");
    
    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });
    
    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });
    
    g.perform_sync();
    
    REQUIRE(status == 200);
}

TEST_CASE("Storing an Orca cloud session writes nothing without a data directory", "[OrcaCloudServiceAgent]")
{
    // The keychain-probe marker belongs in the data directory; without set_data_dir() (CLI, test
    // suites) nothing may land in the current working directory.
    REQUIRE(Slic3r::data_dir().empty());

    const boost::filesystem::path dir = boost::filesystem::temp_directory_path() /
                                        boost::filesystem::unique_path("orca-cloud-agent-%%%%-%%%%");
    boost::filesystem::create_directories(dir);

    {
        ScopedWorkingDirectory cwd(dir);

        Slic3r::OrcaCloudServiceAgent agent("");
        REQUIRE(agent.set_user_session(flat_session_json({{"refresh_token", "test-refresh-token"}}), false));

        CHECK(boost::filesystem::is_empty(dir));
    }

    boost::system::error_code ec;
    boost::filesystem::remove_all(dir, ec);
}

TEST_CASE("Http digest authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://httpbingo.org/digest-auth/auth/guest/guest");

    g.auth_digest("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}

TEST_CASE("Http basic authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://httpbingo.org/basic-auth/guest/guest");

    g.auth_basic("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}
