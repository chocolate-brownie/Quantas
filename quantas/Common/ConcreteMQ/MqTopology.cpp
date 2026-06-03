#include "MqTopology.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <string>

namespace quantas {

TopologyResult buildTopology(const nlohmann::json &topology) {
    TopologyResult result;

    const int initialPeers = topology.value("initialPeers", 0);
    if (initialPeers <= 0) { return result; }

    result.assignments.resize(static_cast<size_t>(initialPeers));

    std::vector<interfaceId> ids(static_cast<size_t>(initialPeers));
    std::iota(ids.begin(), ids.end(), 0);

    if (topology.value("identifiers", "") == "random") {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::shuffle(ids.begin(), ids.end(), rng);
    }

    auto addUndirectedEdge = [&](interfaceId a, interfaceId b) {
        if (a == b || a < 0 || b < 0 || a >= initialPeers || b >= initialPeers) return;
        result.assignments[static_cast<size_t>(a)].id = a;
        result.assignments[static_cast<size_t>(b)].id = b;
        result.assignments[static_cast<size_t>(a)].neighbors.insert(b);
        result.assignments[static_cast<size_t>(b)].neighbors.insert(a);
    };

    auto addDirectedEdge = [&](interfaceId from, interfaceId to) {
        if (from < 0 || to < 0 || from >= initialPeers || to >= initialPeers) return;
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
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
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
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
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
        if (it != topology.end() && it->is_object()) {
            for (int i = 0; i < initialPeers; ++i) {
                interfaceId id = ids[static_cast<size_t>(i)];
                result.assignments[static_cast<size_t>(id)].id = id;
            }
            for (const auto &[key, value] : it->items()) {
                int idx = std::stoi(key);
                if (idx < 0 || idx >= initialPeers) continue;
                interfaceId src = ids[static_cast<size_t>(idx)];
                if (!value.is_array()) continue;
                for (const auto &destValue : value) {
                    int neighborIndex = destValue.get<int>();
                    if (neighborIndex < 0 || neighborIndex >= initialPeers) continue;
                    interfaceId dest = ids[static_cast<size_t>(neighborIndex)];
                    addDirectedEdge(src, dest);
                }
            }
        }
    } else {
        for (interfaceId id : ids) { result.assignments[static_cast<size_t>(id)].id = id; }
    }

    for (interfaceId id = 0; id < initialPeers; ++id) {
        result.assignments[static_cast<size_t>(id)].id = id;
        result.assignments[static_cast<size_t>(id)].topologyType = type;
    }

    return result;
}

} // namespace quantas
