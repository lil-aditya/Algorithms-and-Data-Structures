#include "packet_store.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Constructor / Destructor / SQLite Initialization
// ---------------------------------------------------------------------------

PacketStore::PacketStore() : db(nullptr) {
    if (sqlite3_open("adipe.db", &db) != SQLITE_OK) {
        db = nullptr;
        return;
    }

    std::string sqlPackets = "CREATE TABLE IF NOT EXISTS packets ("
                             "packetID TEXT PRIMARY KEY, currentStatus TEXT, "
                             "sourceNodeID INTEGER, destinationNodeID INTEGER, "
                             "urgency INTEGER, dropReason TEXT);";
                             
    std::string sqlEvents = "CREATE TABLE IF NOT EXISTS events ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT, packetID TEXT, "
                            "timestamp TEXT, status TEXT, nodeID INTEGER, detail TEXT, "
                            "FOREIGN KEY(packetID) REFERENCES packets(packetID));";

    executeSQL(sqlPackets);
    executeSQL(sqlEvents);

    loadFromDB();
}

PacketStore::~PacketStore() {
    if (db) {
        sqlite3_close(db);
    }
}

void PacketStore::executeSQL(const std::string& sql) {
    if (!db) return;
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
}

void PacketStore::loadFromDB() {
    if (!db) return;
    std::lock_guard<std::mutex> guard(storeMutex);

    std::string sql = "SELECT packetID, currentStatus, sourceNodeID, destinationNodeID, urgency, dropReason FROM packets;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PacketRecord rec;
            rec.packetID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            rec.currentStatus = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rec.sourceNodeID = sqlite3_column_int(stmt, 2);
            rec.destinationNodeID = sqlite3_column_int(stmt, 3);
            rec.urgency = sqlite3_column_int(stmt, 4);
            const char* dr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.dropReason = dr ? dr : "";
            records[rec.packetID] = rec;
        }
        sqlite3_finalize(stmt);
    }

    sql = "SELECT packetID, timestamp, status, nodeID, detail FROM events ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string packetID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            StatusEvent ev;
            ev.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            ev.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            ev.nodeID = sqlite3_column_int(stmt, 3);
            const char* det = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            ev.detail = det ? det : "";
            
            records[packetID].events.push_back(ev);
        }
        sqlite3_finalize(stmt);
    }
}

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

    // SQLite Persistence
    std::string sqlPackets = "INSERT OR REPLACE INTO packets (packetID, currentStatus, sourceNodeID, destinationNodeID, urgency, dropReason) VALUES ('" + 
                             packetID + "', 'RECEIVED', " + std::to_string(sourceNode) + ", " + 
                             std::to_string(destNode) + ", " + std::to_string(urgency) + ", '');";
    executeSQL(sqlPackets);
    
    std::string sqlEvents = "INSERT INTO events (packetID, timestamp, status, nodeID, detail) VALUES ('" +
                            packetID + "', '" + ev.timestamp + "', 'RECEIVED', " + std::to_string(sourceNode) + ", 'Packet entered the network');";
    executeSQL(sqlEvents);
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

    // Escape single quotes in detail for SQL
    std::string safeDetail = detail;
    size_t pos = 0;
    while ((pos = safeDetail.find("'", pos)) != std::string::npos) {
        safeDetail.replace(pos, 1, "''");
        pos += 2;
    }

    if (status == "DROPPED") {
        it->second.dropReason = detail;
    }

    // SQLite Persistence
    std::string sqlPackets = "UPDATE packets SET currentStatus = '" + status + "'";
    if (status == "DROPPED") {
        sqlPackets += ", dropReason = '" + safeDetail + "'";
    }
    sqlPackets += " WHERE packetID = '" + packetID + "';";
    executeSQL(sqlPackets);

    std::string sqlEvents = "INSERT INTO events (packetID, timestamp, status, nodeID, detail) VALUES ('" +
                            packetID + "', '" + ev.timestamp + "', '" + status + "', " + std::to_string(nodeID) + ", '" + safeDetail + "');";
    executeSQL(sqlEvents);
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
