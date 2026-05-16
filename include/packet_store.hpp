#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "json.hpp"
#include "sqlite3.h"

using json = nlohmann::json;

/**
 * @struct StatusEvent
 * @brief A single status change in a packet's lifecycle.
 *
 * Each time a packet's status changes (RECEIVED, SIGNED, VERIFIED, etc.),
 * a new StatusEvent is appended to its record.
 */
struct StatusEvent {
    std::string timestamp;  // HH:MM:SS.mmm
    std::string status;     // RECEIVED, SIGNED, QUEUED, PROCESSING, VERIFIED, FORWARDED, DELIVERED, DROPPED
    int nodeID;             // Which node recorded this event
    std::string detail;     // Extra info (e.g., "signature: 12345", "next hop: Node 1")
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StatusEvent, timestamp, status, nodeID, detail)


/**
 * @struct PacketRecord
 * @brief Full audit trail of a packet's journey through the network.
 *
 * Tracks the packet from injection to delivery (or drop), including
 * every node it passed through and every status transition.
 */
struct PacketRecord {
    std::string packetID;
    std::string currentStatus;  // Latest status
    int sourceNodeID;
    int destinationNodeID;
    uint32_t urgency;
    std::vector<StatusEvent> events;  // Full ordered history
    std::string dropReason;           // Non-empty only if DROPPED
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PacketRecord, packetID, currentStatus,
    sourceNodeID, destinationNodeID, urgency, events, dropReason)


/**
 * @class PacketStore
 * @brief Thread-safe, shared packet lifecycle tracker.
 *
 * All 6 nodes write to the same PacketStore instance, so querying
 * any single node gives the full cross-network journey of a packet.
 *
 * Thread safety: every public method locks storeMutex internally.
 */
class PacketStore {
public:
    PacketStore();
    ~PacketStore();

    /// Initialize tracking for a newly injected packet
    void initPacket(const std::string& packetID, int sourceNode,
                    int destNode, uint32_t urgency);

    /// Record a status change for an existing packet
    void updateStatus(const std::string& packetID, const std::string& status,
                      int nodeID, const std::string& detail = "");

    /// Get one packet's full record as JSON (or error if not found)
    json getPacket(const std::string& packetID) const;

    /// Get all tracked packets as a JSON array
    json getAllPackets() const;

    /// Check whether a packet ID is being tracked
    bool exists(const std::string& packetID) const;

private:
    std::unordered_map<std::string, PacketRecord> records;
    mutable std::mutex storeMutex;
    sqlite3* db;

    /// Returns current wall-clock time as "HH:MM:SS.mmm"
    static std::string getTimestamp();
    
    /// Helper to execute SQL statements
    void executeSQL(const std::string& sql);
    
    /// Helper to load initial data from SQLite
    void loadFromDB();
};
