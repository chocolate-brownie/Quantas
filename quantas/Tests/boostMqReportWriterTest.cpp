#include "quantas/Common/Concrete/Backends/BoostMq/Logging/BoostMqReportWriter.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    namespace fs = std::filesystem;
    const fs::path testDirectory =
        fs::temp_directory_path() / ("quantas_report_test_" + std::to_string(::getpid()));
    fs::create_directories(testDirectory);

    const fs::path validPath = testDirectory / "peer_0.json";
    const fs::path malformedPath = testDirectory / "peer_1.json";
    {
        std::ofstream valid(validPath);
        valid
            << R"({"transportMetrics":{"sent":10,"received_raw":8,"delivered_to_instream":8,"dropped_backpressure":0}})";

        std::ofstream malformed(malformedPath);
        malformed << "{ invalid json";
    }

    const nlohmann::json validPaths = {{"0", validPath.string()}};
    nlohmann::json peerMetrics;
    assert(quantas::BoostMqReportWriter::readPeerMetrics(validPaths, {0}, peerMetrics));
    assert(peerMetrics.contains("0"));

    const nlohmann::json missingPath = {{"0", (testDirectory / "missing.json").string()}};
    assert(!quantas::BoostMqReportWriter::readPeerMetrics(missingPath, {0}, peerMetrics));
    assert(peerMetrics.empty());

    const nlohmann::json mixedMissingPaths = {
        {"0", validPath.string()},
        {"1", (testDirectory / "missing.json").string()},
    };
    assert(!quantas::BoostMqReportWriter::readPeerMetrics(mixedMissingPaths, {0, 1}, peerMetrics));
    assert(peerMetrics.contains("0"));
    assert(!peerMetrics.contains("1"));

    const nlohmann::json malformedPaths = {{"0", malformedPath.string()}};
    assert(!quantas::BoostMqReportWriter::readPeerMetrics(malformedPaths, {0}, peerMetrics));
    assert(peerMetrics.empty());

    const nlohmann::json mixedMalformedPaths = {
        {"0", validPath.string()},
        {"1", malformedPath.string()},
    };
    assert(
        !quantas::BoostMqReportWriter::readPeerMetrics(mixedMalformedPaths, {0, 1}, peerMetrics));
    assert(peerMetrics.contains("0"));
    assert(!peerMetrics.contains("1"));

    const nlohmann::json reliability = quantas::BoostMqReportWriter::summarizeTransportReliability(
        nlohmann::json{{"0", nlohmann::json{{"transportMetrics",
                                             {
                                                 {"sent", 10},
                                                 {"received_raw", 8},
                                                 {"delivered_to_instream", 8},
                                                 {"dropped_backpressure", 0},
                                             }}}}});
    assert(reliability.at("pending_at_shutdown_total") == 2);
    assert(reliability.at("reliable") == true);

    const nlohmann::json dropped = quantas::BoostMqReportWriter::summarizeTransportReliability(
        nlohmann::json{{"0", nlohmann::json{{"transportMetrics",
                                             {
                                                 {"sent", 10},
                                                 {"received_raw", 8},
                                                 {"delivered_to_instream", 8},
                                                 {"dropped_backpressure", 1},
                                             }}}}});
    assert(dropped.at("reliable") == false);

    const std::string leaderPath =
        quantas::BoostMqReportWriter::makeLeaderReportPath("AltBitdropProb0.txt", 0);
    assert(leaderPath == "results/AltBitdropProb0_EXP1/leader_report.json");
    const nlohmann::json peerPaths =
        quantas::BoostMqReportWriter::makePeerOutputFiles("AltBitdropProb0.txt", 0, 2, 2);
    assert(peerPaths.at("0") == "results/AltBitdropProb0_EXP1/peer_0_TEST2.txt");
    assert(peerPaths.at("1") == "results/AltBitdropProb0_EXP1/peer_1_TEST2.txt");

    fs::remove_all(testDirectory);
    fs::remove_all("results/AltBitdropProb0_EXP1");
    return 0;
}
