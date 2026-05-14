#include "packet_store.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------

std::string PacketStore::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_val);
#else
    localtime_r(&time_t_val, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PacketStore::initPacket(const std::string& packetID, int sourceNode,
                             int destNode, uint32_t urgency) {
    std::lock_guard<std::mutex> guard(storeMutex);

    PacketRecord rec;
    rec.packetID        = packetID;
    rec.currentStatus   = "RECEIVED";
    rec.sourceNodeID    = sourceNode;
    rec.destinationNodeID = destNode;
    rec.urgency         = urgency;
    rec.dropReason      = "";

    StatusEvent ev;
    ev.timestamp = getTimestamp();
    ev.status    = "RECEIVED";
    ev.nodeID    = sourceNode;
    ev.detail    = "Packet entered the network";
    rec.events.push_back(ev);

    records[packetID] = rec;
}

void PacketStore::updateStatus(const std::string& packetID,
                               const std::string& status,
                               int nodeID,
                               const std::string& detail) {
    std::lock_guard<std::mutex> guard(storeMutex);

    auto it = records.find(packetID);
    if (it == records.end()) return;  // unknown packet — nothing to update

    it->second.currentStatus = status;

    StatusEvent ev;
    ev.timestamp = getTimestamp();
    ev.status    = status;
    ev.nodeID    = nodeID;
    ev.detail    = detail;
    it->second.events.push_back(ev);

    if (status == "DROPPED") {
        it->second.dropReason = detail;
    }
}

json PacketStore::getPacket(const std::string& packetID) const {
    std::lock_guard<std::mutex> guard(storeMutex);

    auto it = records.find(packetID);
    if (it == records.end()) {
        return json{{"error", "Packet not found"}, {"packetID", packetID}};
    }
    return json(it->second);
}

json PacketStore::getAllPackets() const {
    std::lock_guard<std::mutex> guard(storeMutex);

    json result = json::array();
    for (const auto& [id, rec] : records) {
        result.push_back(json(rec));
    }
    return result;
}

bool PacketStore::exists(const std::string& packetID) const {
    std::lock_guard<std::mutex> guard(storeMutex);
    return records.find(packetID) != records.end();
}
