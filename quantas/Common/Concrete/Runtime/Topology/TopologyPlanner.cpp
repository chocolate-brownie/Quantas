#include "quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>

namespace quantas {
namespace {

int requirePositiveInteger(const nlohmann::json& object, const char* field,
                           const std::string& path) {
    if (!object.contains(field) || !object[field].is_number_integer()) {
        throw std::runtime_error("error: " + path + " must be a positive integer");
    }

    const int value = object[field].get<int>();
    if (value <= 0)
        throw std::runtime_error("error: " + path + " must be > 0");
    return value;
}

void validateDimensions(const nlohmann::json& topology, const std::string& type, int initialPeers,
                        int& height, int& width) {
    height = requirePositiveInteger(topology, "height", "topology.height");
    width = requirePositiveInteger(topology, "width", "topology.width");

    if (type == "torus" && (height < 2 || width < 2)) {
        throw std::runtime_error("error: torus height and width must both be at least 2");
    }

    if (static_cast<long long>(height) * width != initialPeers) {
        throw std::runtime_error(
            "error: topology.height * topology.width must equal topology.initialPeers");
    }
}

int parsePeerId(const std::string& value, int initialPeers) {
    size_t parsedCharacters = 0;
    long long peerId = 0;
    try {
        peerId = std::stoll(value, &parsedCharacters);
    } catch (const std::exception&) {
        throw std::runtime_error("error: topology.list contains invalid peer ID '" + value + "'");
    }

    if (parsedCharacters != value.size() || std::to_string(peerId) != value || peerId < 0 ||
        peerId >= initialPeers) {
        throw std::runtime_error("error: topology.list contains invalid peer ID '" + value + "'");
    }
    return static_cast<int>(peerId);
}

} // namespace

TopologyResult buildTopology(const nlohmann::json& topology) {
    TopologyResult result;

    const int initialPeers =
        requirePositiveInteger(topology, "initialPeers", "topology.initialPeers");

    result.assignments.resize(static_cast<size_t>(initialPeers));

    std::vector<interfaceId> ids(static_cast<size_t>(initialPeers));
    std::iota(ids.begin(), ids.end(), 0);

    if (topology.value("identifiers", "") == "random") {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::shuffle(ids.begin(), ids.end(), rng);
    }

    auto addUndirectedEdge = [&](interfaceId a, interfaceId b) {
        if (a == b || a < 0 || b < 0 || a >= initialPeers || b >= initialPeers)
            return;
        result.assignments[static_cast<size_t>(a)].id = a;
        result.assignments[static_cast<size_t>(b)].id = b;
        result.assignments[static_cast<size_t>(a)].neighbors.insert(b);
        result.assignments[static_cast<size_t>(b)].neighbors.insert(a);
    };

    auto addDirectedEdge = [&](interfaceId from, interfaceId to) {
        if (from < 0 || to < 0 || from >= initialPeers || to >= initialPeers)
            return;
        result.assignments[static_cast<size_t>(from)].id = from;
        result.assignments[static_cast<size_t>(from)].neighbors.insert(to);
    };

    const std::string type = topology.value("type", "");
    if (type == "complete") {
        for (int i = 0; i < initialPeers; ++i) {
            for (int j = i + 1; j < initialPeers; ++j) {
                interfaceId a = ids[static_cast<size_t>(i)];
                interfaceId b = ids[static_cast<size_t>(j)];
                addUndirectedEdge(a, b);
            }
        }
    } else if (type == "star") {
        for (int i = 1; i < initialPeers; ++i) {
            interfaceId center = ids[0];
            interfaceId leaf = ids[static_cast<size_t>(i)];
            addUndirectedEdge(center, leaf);
        }
    } else if (type == "grid") {
        int height = 0;
        int width = 0;
        validateDimensions(topology, type, initialPeers, height, width);
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                interfaceId current = ids[static_cast<size_t>(idx)];
                if (j + 1 < width) {
                    interfaceId right = ids[static_cast<size_t>(idx + 1)];
                    addUndirectedEdge(current, right);
                }
                if (i + 1 < height) {
                    interfaceId down = ids[static_cast<size_t>(idx + width)];
                    addUndirectedEdge(current, down);
                }
            }
        }
    } else if (type == "torus") {
        int height = 0;
        int width = 0;
        validateDimensions(topology, type, initialPeers, height, width);
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                interfaceId current = ids[static_cast<size_t>(idx)];
                interfaceId right = ids[static_cast<size_t>(i * width + ((j + 1) % width))];
                interfaceId down = ids[static_cast<size_t>(((i + 1) % height) * width + j)];
                addUndirectedEdge(current, right);
                addUndirectedEdge(current, down);
            }
        }
    } else if (type == "chain") {
        for (int i = 0; i < initialPeers - 1; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>(i + 1)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "ring") {
        for (int i = 0; i < initialPeers; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "unidirectionalRing") {
        for (int i = 0; i < initialPeers; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addDirectedEdge(a, b);
        }
    } else if (type == "userList") {
        const auto it = topology.find("list");
        if (it == topology.end() || !it->is_object()) {
            throw std::runtime_error("error: topology.list must be an object for userList");
        }

        for (const auto& [key, value] : it->items()) {
            const int sourceIndex = parsePeerId(key, initialPeers);
            if (!value.is_array()) {
                throw std::runtime_error("error: topology.list['" + key + "'] must be an array");
            }

            const interfaceId source = ids[static_cast<size_t>(sourceIndex)];
            for (const auto& destinationValue : value) {
                if (!destinationValue.is_number_integer()) {
                    throw std::runtime_error("error: topology.list['" + key +
                                             "'] contains a non-integer neighbour ID");
                }

                const long long destinationIndex = destinationValue.get<long long>();
                if (destinationIndex < 0 || destinationIndex >= initialPeers) {
                    throw std::runtime_error("error: topology.list['" + key +
                                             "'] contains an out-of-range neighbour ID");
                }
                if (destinationIndex == sourceIndex) {
                    throw std::runtime_error("error: topology.list['" + key +
                                             "'] contains its own peer ID");
                }

                const interfaceId destination = ids[static_cast<size_t>(destinationIndex)];
                addDirectedEdge(source, destination);
            }
        }
    } else {
        throw std::runtime_error("error: missing or unknown topology.type '" + type + "'");
    }

    for (interfaceId id = 0; id < initialPeers; ++id) {
        result.assignments[static_cast<size_t>(id)].id = id;
        result.assignments[static_cast<size_t>(id)].topologyType = type;
    }

    return result;
}

} // namespace quantas
