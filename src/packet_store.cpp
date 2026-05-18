#include "packet_store.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

PacketStore::PacketStore() : db(nullptr) {
    if (sqlite3_open("adipe.db", &db) != SQLITE_OK) {
        db = nullptr;
        return;
    }

    const std::string sqlPackets =
        "CREATE TABLE IF NOT EXISTS packets ("
        "packetID TEXT PRIMARY KEY, currentStatus TEXT, "
        "sourceNodeID INTEGER, destinationNodeID INTEGER, "
        "urgency INTEGER, dropReason TEXT);";

    const std::string sqlEvents =
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, packetID TEXT, "
        "timestamp TEXT, status TEXT, nodeID INTEGER, detail TEXT, "
        "FOREIGN KEY(packetID) REFERENCES packets(packetID));";

    const std::string sqlTrust =
        "CREATE TABLE IF NOT EXISTS trust_scores ("
        "observerNode INTEGER, subjectNode INTEGER, score REAL, "
        "PRIMARY KEY(observerNode, subjectNode));";

    executeSQL(sqlPackets);
    executeSQL(sqlEvents);
    executeSQL(sqlTrust);

    loadFromDB();
}

PacketStore::~PacketStore() {
    if (db) {
        sqlite3_close(db);
    }
}

void PacketStore::executeSQL(const std::string& sql) {
    if (!db) {
        return;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        sqlite3_free(errMsg);
    }
}

void PacketStore::loadFromDB() {
    if (!db) {
        return;
    }

    std::lock_guard<std::mutex> guard(storeMutex);

    const std::string sqlPackets =
        "SELECT packetID, currentStatus, sourceNodeID, destinationNodeID, urgency, dropReason FROM packets;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sqlPackets.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PacketRecord rec;
            rec.packetID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            rec.currentStatus = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            rec.sourceNodeID = sqlite3_column_int(stmt, 2);
            rec.destinationNodeID = sqlite3_column_int(stmt, 3);
            rec.urgency = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
            const char* dropReason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            rec.dropReason = dropReason ? dropReason : "";
            records[rec.packetID] = rec;
        }
    }
    sqlite3_finalize(stmt);

    const std::string sqlEvents =
        "SELECT packetID, timestamp, status, nodeID, detail FROM events ORDER BY id ASC;";
    stmt = nullptr;
    if (sqlite3_prepare_v2(db, sqlEvents.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const std::string packetID =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

            StatusEvent ev;
            ev.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            ev.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            ev.nodeID = sqlite3_column_int(stmt, 3);
            const char* detail = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            ev.detail = detail ? detail : "";

            records[packetID].events.push_back(ev);
        }
    }
    sqlite3_finalize(stmt);
}

std::string PacketStore::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timeValue = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &timeValue);
#else
    localtime_r(&timeValue, &tmBuf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void PacketStore::initPacket(const std::string& packetID, int sourceNode,
                             int destNode, uint32_t urgency) {
    std::lock_guard<std::mutex> guard(storeMutex);

    PacketRecord rec;
    rec.packetID = packetID;
    rec.currentStatus = "RECEIVED";
    rec.sourceNodeID = sourceNode;
    rec.destinationNodeID = destNode;
    rec.urgency = urgency;
    rec.dropReason = "";

    StatusEvent ev;
    ev.timestamp = getTimestamp();
    ev.status = "RECEIVED";
    ev.nodeID = sourceNode;
    ev.detail = "Packet entered the network";
    rec.events.push_back(ev);

    records[packetID] = rec;

    const std::string sqlPackets =
        "INSERT OR REPLACE INTO packets (packetID, currentStatus, sourceNodeID, destinationNodeID, urgency, dropReason) VALUES ('" +
        packetID + "', 'RECEIVED', " + std::to_string(sourceNode) + ", " +
        std::to_string(destNode) + ", " + std::to_string(urgency) + ", '');";
    executeSQL(sqlPackets);

    const std::string sqlEvents =
        "INSERT INTO events (packetID, timestamp, status, nodeID, detail) VALUES ('" +
        packetID + "', '" + ev.timestamp + "', 'RECEIVED', " +
        std::to_string(sourceNode) + ", 'Packet entered the network');";
    executeSQL(sqlEvents);
}

void PacketStore::updateStatus(const std::string& packetID,
                               const std::string& status,
                               int nodeID,
                               const std::string& detail) {
    std::lock_guard<std::mutex> guard(storeMutex);

    auto it = records.find(packetID);
    if (it == records.end()) {
        return;
    }

    it->second.currentStatus = status;

    StatusEvent ev;
    ev.timestamp = getTimestamp();
    ev.status = status;
    ev.nodeID = nodeID;
    ev.detail = detail;
    it->second.events.push_back(ev);

    std::string safeDetail = detail;
    size_t pos = 0;
    while ((pos = safeDetail.find('\'', pos)) != std::string::npos) {
        safeDetail.replace(pos, 1, "''");
        pos += 2;
    }

    if (status == "DROPPED") {
        it->second.dropReason = detail;
    }

    std::string sqlPackets = "UPDATE packets SET currentStatus = '" + status + "'";
    if (status == "DROPPED") {
        sqlPackets += ", dropReason = '" + safeDetail + "'";
    }
    sqlPackets += " WHERE packetID = '" + packetID + "';";
    executeSQL(sqlPackets);

    const std::string sqlEvents =
        "INSERT INTO events (packetID, timestamp, status, nodeID, detail) VALUES ('" +
        packetID + "', '" + ev.timestamp + "', '" + status + "', " +
        std::to_string(nodeID) + ", '" + safeDetail + "');";
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

std::vector<float> PacketStore::getTrustVector(int observerNode, int nodeCount) const {
    std::lock_guard<std::mutex> guard(storeMutex);

    std::vector<float> trust(nodeCount, 1.0f);
    if (!db) {
        if (observerNode >= 0 && observerNode < nodeCount) {
            trust[observerNode] = 1.0f;
        }
        return trust;
    }

    const std::string sql =
        "SELECT subjectNode, score FROM trust_scores WHERE observerNode = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, observerNode);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int subjectNode = sqlite3_column_int(stmt, 0);
            const float score = static_cast<float>(sqlite3_column_double(stmt, 1));
            if (subjectNode >= 0 && subjectNode < nodeCount) {
                trust[subjectNode] = score;
            }
        }
    }
    sqlite3_finalize(stmt);

    if (observerNode >= 0 && observerNode < nodeCount) {
        trust[observerNode] = std::max(trust[observerNode], 1.0f);
    }

    return trust;
}

void PacketStore::upsertTrustScore(int observerNode, int subjectNode, float score) {
    std::lock_guard<std::mutex> guard(storeMutex);
    if (!db) {
        return;
    }

    const std::string sql =
        "INSERT INTO trust_scores (observerNode, subjectNode, score) VALUES (?, ?, ?) "
        "ON CONFLICT(observerNode, subjectNode) DO UPDATE SET score = excluded.score;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, observerNode);
        sqlite3_bind_int(stmt, 2, subjectNode);
        sqlite3_bind_double(stmt, 3, score);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void PacketStore::clearAllState() {
    std::lock_guard<std::mutex> guard(storeMutex);
    records.clear();
    executeSQL("DELETE FROM events;");
    executeSQL("DELETE FROM packets;");
    executeSQL("DELETE FROM trust_scores;");
}
