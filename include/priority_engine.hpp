#pragma once

#include <cstdint>
#include <optional>
#include <queue>
#include <string>

#include "json.hpp"

using json = nlohmann::json;

/**
 * @struct DataPacket
 * @brief Represents a single message travelling through the network.
 */
struct DataPacket {
    std::string id;
    uint32_t urgency;
    std::string data;
    std::string senderID;
    uint64_t signature;
    int destinationID;
    int originNodeID;
    int lastHopNodeID;
    int64_t lastForwardedAtMs;

    DataPacket()
        : urgency(0),
          signature(0),
          destinationID(-1),
          originNodeID(-1),
          lastHopNodeID(-1),
          lastForwardedAtMs(0) {}

    DataPacket(const std::string& i, uint32_t u, const std::string& d,
               const std::string& s, uint64_t sig, int dest,
               int origin = -1, int lastHop = -1, int64_t forwardedAt = 0)
        : id(i),
          urgency(u),
          data(d),
          senderID(s),
          signature(sig),
          destinationID(dest),
          originNodeID(origin),
          lastHopNodeID(lastHop),
          lastForwardedAtMs(forwardedAt) {}

    bool operator<(const DataPacket& other) const {
        return urgency < other.urgency;
    }
};

inline void to_json(json& j, const DataPacket& p) {
    j = json{
        {"id", p.id},
        {"urgency", p.urgency},
        {"data", p.data},
        {"senderID", p.senderID},
        {"signature", p.signature},
        {"destinationID", p.destinationID},
        {"originNodeID", p.originNodeID},
        {"lastHopNodeID", p.lastHopNodeID},
        {"lastForwardedAtMs", p.lastForwardedAtMs}
    };
}

inline void from_json(const json& j, DataPacket& p) {
    p.id = j.value("id", "");
    p.urgency = j.value("urgency", 0U);
    p.data = j.value("data", "");
    p.senderID = j.value("senderID", "");
    p.signature = j.value("signature", 0ULL);
    p.destinationID = j.value("destinationID", -1);
    p.originNodeID = j.value("originNodeID", -1);
    p.lastHopNodeID = j.value("lastHopNodeID", -1);
    p.lastForwardedAtMs = j.value("lastForwardedAtMs", static_cast<int64_t>(0));
}

/**
 * @class PriorityEngine
 * @brief A max-heap priority queue for DataPackets.
 */
class PriorityEngine {
private:
    std::priority_queue<DataPacket> pq;

public:
    void push(const DataPacket& p);
    std::optional<DataPacket> pop();
    bool empty() const;
    size_t size() const;
    void clear();
};
