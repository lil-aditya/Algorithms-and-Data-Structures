#pragma once
#include <queue>
#include <string>
#include <optional>
#include <cstdint>

#include "json.hpp"

using json = nlohmann::json;
using namespace std;


/**
 * @struct DataPacket
 * @brief Represents a single message travelling through the network.
 */
struct DataPacket {
    string id;
    uint32_t urgency;     // Higher = more urgent
    string data;          // The actual message payload
    string senderID;      // ID of the node that signed this packet
    uint64_t signature;
    int destinationID;    // Target node ID for routing

    /// Default constructor (required by nlohmann/json for deserialization)
    DataPacket() : urgency(0), signature(0), destinationID(-1) {}

    /// Full constructor
    DataPacket(const string& i, uint32_t u, const string& d, 
               const string& s, uint64_t sig, int dest)
        : id(i), urgency(u), data(d), senderID(s), signature(sig), destinationID(dest) {}

    /// Priority comparison: lower urgency = lower priority
    bool operator<(const DataPacket& other) const {
        return urgency < other.urgency;
    }
};

// JSON serialization macro — maps all struct fields to/from JSON
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DataPacket, id, urgency, data, senderID, signature, destinationID)


/**
 * @class PriorityEngine
 * @brief A max-heap priority queue for DataPackets (highest urgency popped first).
 */
class PriorityEngine {
private:
    priority_queue<DataPacket> pq;
public:
    void push(const DataPacket &p);
    optional<DataPacket> pop();
    bool empty() const;
    size_t size() const;
};