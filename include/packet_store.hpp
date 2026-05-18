#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "json.hpp"
#include "sqlite3.h"

using json = nlohmann::json;

struct StatusEvent {
    std::string timestamp;
    std::string status;
    int nodeID;
    std::string detail;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StatusEvent, timestamp, status, nodeID, detail)

struct PacketRecord {
    std::string packetID;
    std::string currentStatus;
    int sourceNodeID;
    int destinationNodeID;
    uint32_t urgency;
    std::vector<StatusEvent> events;
    std::string dropReason;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PacketRecord, packetID, currentStatus,
    sourceNodeID, destinationNodeID, urgency, events, dropReason)

class PacketStore {
public:
    PacketStore();
    ~PacketStore();

    void initPacket(const std::string& packetID, int sourceNode,
                    int destNode, uint32_t urgency);
    void updateStatus(const std::string& packetID, const std::string& status,
                      int nodeID, const std::string& detail = "");

    json getPacket(const std::string& packetID) const;
    json getAllPackets() const;
    bool exists(const std::string& packetID) const;

    std::vector<float> getTrustVector(int observerNode, int nodeCount) const;
    void upsertTrustScore(int observerNode, int subjectNode, float score);
    void clearAllState();

private:
    std::unordered_map<std::string, PacketRecord> records;
    mutable std::mutex storeMutex;
    sqlite3* db;

    static std::string getTimestamp();
    void executeSQL(const std::string& sql);
    void loadFromDB();
};
