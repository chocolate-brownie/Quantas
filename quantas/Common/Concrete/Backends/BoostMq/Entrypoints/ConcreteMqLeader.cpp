#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Logging/BoostMqReportWriter.hpp"
#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.hpp"
#include "quantas/Common/Logger.hpp"
#include "quantas/Common/LoggingSupport.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct LeaderCliArgs {
    std::string inputPath;
    std::optional<size_t> experimentIndex;
    bool preflightOnly{false};
};

/*
 * Purpose: Load the input JSON and verify that it contains at least one
 * experiment. Used by: `main()` at startup so execution stops early when the
 * input is unusable.
 */
std::optional<nlohmann::json> parseAndLoadConfig(const std::string& inputPath) {
    return quantas::loadRuntimeConfig(inputPath);
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " --experiment <experiment_index> <input_json>\n"
              << "       " << programName << " --preflight <input_json>\n";
}

std::optional<LeaderCliArgs> parseLeaderArgs(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--preflight") {
        return LeaderCliArgs{argv[2], std::nullopt, true};
    }
    if (argc != 4 || std::string(argv[1]) != "--experiment") {
        printUsage(argv[0]);
        return std::nullopt;
    }

    try {
        size_t parsedCharacters = 0;
        const long long experimentIndex = std::stoll(argv[2], &parsedCharacters);
        if (parsedCharacters != std::string(argv[2]).size() || experimentIndex < 0) {
            throw std::runtime_error("experiment index must be a non-negative integer");
        }
        return LeaderCliArgs{argv[3], static_cast<size_t>(experimentIndex), false};
    } catch (const std::exception& ex) {
        std::cerr << "error: invalid experiment index: " << ex.what() << '\n';
        return std::nullopt;
    }
}

/* Select every experiment for preflight, or one requested experiment for a
 * normal run. Return no value when the requested experiment does not exist. */
std::optional<std::vector<size_t>> selectExperimentIndexes(const LeaderCliArgs& cli,
                                                           size_t experimentCount) {
    std::vector<size_t> indexes;

    if (cli.preflightOnly) {
        indexes.reserve(experimentCount);
        for (size_t i = 0; i < experimentCount; ++i)
            indexes.push_back(i);
        return indexes;
    }

    if (*cli.experimentIndex >= experimentCount) {
        std::cerr << "error: experiment index " << *cli.experimentIndex << " is out of range\n";
        return std::nullopt;
    }

    indexes.push_back(*cli.experimentIndex);
    return indexes;
}

/*
 * Readiness-timeout helper: Save the facts known when startup fails, ask any
 * waiting peers to stop, and remove the queues created for this test.
 */
void handleReadinessTimeout(
    quantas::ProcessCoordinatorMQ& coordinator,
    quantas::BoostMqReportWriter::TestReportInfo& testInfo, nlohmann::json& expReport,
    int totalPeers,
    const std::chrono::time_point<std::chrono::high_resolution_clock>& testStartTime) {

    const auto testEndTime = std::chrono::high_resolution_clock::now();
    testInfo.duration = testEndTime - testStartTime;
    testInfo.missingPeers =
        quantas::BoostMqReportWriter::findMissingPeers(totalPeers, testInfo.readyPeers);
    testInfo.success = false;

    QUANTAS_LOG_ERROR("coord") << "leader readiness timed out; readyPeers="
                               << nlohmann::json(testInfo.readyPeers).dump()
                               << " missingPeers=" << nlohmann::json(testInfo.missingPeers).dump();

    expReport["tests"].push_back(quantas::BoostMqReportWriter::makeTestReport(testInfo));
    coordinator.broadcastStopBestEffort();
    coordinator.cleanUp();
}

} // namespace

/* --------------------------- Leader runtime --------------------------- */
int main(int argc, char* argv[]) {
    /* Validate the command line and select normal execution or validation-only  preflight mode.
     * Both modes operate on one JSON input file. */
    const auto cli = parseLeaderArgs(argc, argv);
    if (!cli)
        return 1;

    /* Load and validate the top-level configuration before acquiring any MQ  resources or starting
     * an experiment. */
    auto config = parseAndLoadConfig(cli->inputPath);
    if (!config)
        return 1;

    /* Select every experiment for preflight, or one requested experiment for a  normal run. Return
     * no value when the requested experiment does not exist. */
    const auto experimentIndexes = selectExperimentIndexes(*cli, (*config)["experiments"].size());
    if (!experimentIndexes)
        return 1;

    /* Use the shared coordinator to own the leader's rendezvous protocol and  the lifecycle of its
     * BoostMQ resources. */
    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();

    /* Process each configured experiment independently so failures can be  reported with the
     * corresponding experiment index. */
    for (const size_t expIndex : *experimentIndexes) {
        try {
            const nlohmann::json& experiment = (*config)["experiments"].at(expIndex);

            /* Parse backend-independent runtime settings, then validate the  BoostMQ queue sizes
             * and topology payloads before creating queues. */
            quantas::RuntimeExperimentConfig exp =
                quantas::parseRuntimeExperiment(*config, expIndex);

            auto boostConfig = quantas::parseBoostMqConfig(experiment, exp.initialPeers);
            quantas::preflightBoostMqQueues(boostConfig, expIndex);

            const auto [assignments] = quantas::buildTopology(exp.topology);
            quantas::validateBoostMqAssignmentPayloads(assignments, boostConfig, expIndex);

            if (cli->preflightOnly)
                continue;

            /* Initialize timing, output paths, and coordinator state for this  experiment before
             * any peers enter the rendezvous. */
            std::chrono::time_point<std::chrono::high_resolution_clock> expStartTime, expEndTime;
            std::chrono::duration<double> expDuration{};
            expStartTime = std::chrono::high_resolution_clock::now();

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
            const std::string reportPath =
                quantas::BoostMqReportWriter::makeLeaderReportPath(logFileBase, expIndex);

            coordinator.configureExperiment(expIndex, exp.initialPeerType, true, exp.initialPeers,
                                            quantas::NO_PEER_ID, logFileBase,
                                            quantas::StopMode::FixedRounds, boostConfig);

            QUANTAS_LOG_INFO("coord")
                << "leader starting rendezvous for experiment " << expIndex
                << " with totalPeers=" << exp.initialPeers << " tests=" << exp.tests;

            nlohmann::json expReport =
                quantas::BoostMqReportWriter::makeBaseExperimentReport(expIndex, exp, boostConfig);
            expReport["inputFile"] = cli->inputPath;
            expReport["expectedPeers"] =
                quantas::BoostMqReportWriter::expectedPeers(exp.initialPeers);

            bool allTestsSucceeded = true;
            bool experimentTimedOut = false;

            /* Run every test in the experiment through a fresh rendezvous and  collect an
             * independent result for the final experiment report. */
            for (int testIndex = 0; testIndex < exp.tests; ++testIndex) {
                const int testNumber = testIndex + 1;
                quantas::BoostMqReportWriter::TestReportInfo testInfo;
                testInfo.testIndex = static_cast<size_t>(testIndex);

                QUANTAS_LOG_INFO("coord")
                    << "leader starting experiment " << expIndex << " test " << testNumber;

                std::chrono::time_point<std::chrono::high_resolution_clock> testStartTime;
                std::chrono::time_point<std::chrono::high_resolution_clock> testEndTime;

                testStartTime = std::chrono::high_resolution_clock::now();

                /* Wait for all peers, distribute their topology assignments, start the run
                 * together, and wait for completion or timeout. */
                coordinator.createBarrier();

                coordinator.waitForAllReady(testInfo.readyPeers, testInfo.readyTimedOut);

                /* A readiness timeout means the test cannot start. Save the failure evidence,
                 * stop waiting peers, clean this test's queues, and leave the test loop. */
                if (testInfo.readyTimedOut) {
                    allTestsSucceeded = false;
                    experimentTimedOut = true;
                    handleReadinessTimeout(coordinator, testInfo, expReport, exp.initialPeers,
                                           testStartTime);
                    break;
                }

                auto [assignments] = quantas::buildTopology(exp.topology);
                coordinator.sendAssignments(assignments);
                coordinator.broadcastStart();

                const auto [completedPeers, completionTimedOut] =
                    coordinator.waitForAllDone(std::chrono::milliseconds(exp.doneTimeoutMs));

                testInfo.completedPeers = completedPeers;
                testInfo.completionTimedOut = completionTimedOut;

                testEndTime = std::chrono::high_resolution_clock::now();
                testInfo.duration = testEndTime - testStartTime;

                /* Record completion state and peer output locations, then  aggregate peer transport
                 * metrics into the test report. */
                testInfo.missingPeers = quantas::BoostMqReportWriter::findMissingPeers(
                    exp.initialPeers, testInfo.completedPeers);
                testInfo.peerOutputFiles = quantas::BoostMqReportWriter::makePeerOutputFiles(
                    logFileBase, expIndex, testNumber, exp.initialPeers);

                testInfo.success = !testInfo.completionTimedOut && testInfo.missingPeers.empty();
                allTestsSucceeded = allTestsSucceeded && testInfo.success;

                testInfo.peerMetrics = quantas::BoostMqReportWriter::readCompletedPeerMetrics(
                    testInfo.peerOutputFiles, testInfo.completedPeers);
                testInfo.transportReliability =
                    quantas::BoostMqReportWriter::summarizeTransportReliability(
                        testInfo.peerMetrics);
                expReport["tests"].push_back(
                    quantas::BoostMqReportWriter::makeTestReport(testInfo));

                /* Stop peers best-effort after a timeout and always release  this test's
                 * coordinator resources before continuing. */
                if (testInfo.completionTimedOut) {
                    experimentTimedOut = true;
                    coordinator.broadcastStopBestEffort();
                }

                coordinator.cleanUp();

                if (experimentTimedOut) {
                    QUANTAS_LOG_ERROR("coord")
                        << "leader timed out in experiment " << expIndex << " test " << testNumber;
                    break;
                }

                QUANTAS_LOG_INFO("coord")
                    << "leader completed experiment " << expIndex << " test " << testNumber;
            }

            /* Finalize and persist the experiment-level duration and success  result after all
             * tests finish or the first timeout occurs. */
            expEndTime = std::chrono::high_resolution_clock::now();
            expDuration = expEndTime - expStartTime;
            expReport["durationSeconds"] = expDuration.count();
            expReport["success"] = allTestsSucceeded;
            quantas::BoostMqReportWriter::writeLeaderReport(reportPath, expReport);
            QUANTAS_LOG_INFO("coord") << "leader report written to " << reportPath;

            if (experimentTimedOut)
                return 1;
        } catch (const std::exception& ex) {
            /* Release any partially initialized MQ state and fail with the  experiment index when
             * configuration or runtime work throws. */
            coordinator.cleanUp();
            std::cerr << "error: leader failed at experiment " << expIndex << ": " << ex.what()
                      << '\n';

            return 1;
        }
    }

    /* Confirm that validation-only mode checked every configured experiment without starting a
     * rendezvous. */
    if (cli->preflightOnly) {
        QUANTAS_LOG_INFO("preflight") << "all BoostMQ queue preflight checks passed";
    }

    return 0;
}
