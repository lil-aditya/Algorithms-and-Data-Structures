#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "graph_routing.hpp"
#include "hashmap.hpp"
#include "httplib.h"
#include "json.hpp"
#include "packet_store.hpp"
#include "priority_engine.hpp"
#include "rsa_signature.hpp"

using json = nlohmann::json;

extern std::map<int, int> NODE_PORTS;

enum class ChaosMode {
    NORMAL,
    TAMPER,
    SILENT_DROP,
    DELAY,
    FORGE,
    EAVESDROP
};

class Node {
public:
    static constexpr int kNodeCount = 6;
    static constexpr float kQuarantineThreshold = 0.30f;

    Node(int id, Graph& graph, MetadataMap& book, PacketStore& store);
    ~Node();

    void start();

    static std::vector<std::string> sharedLog;
    static std::mutex logMutex;

private:
    int nodeID;
    Keys nodeKeys;
    PriorityEngine inbox;
    Graph& networkMap;
    MetadataMap& addressBook;
    PacketStore& packetStore;

    std::vector<float> trustScores;
    mutable std::mutex trustMutex;
    ChaosMode chaosMode;
    mutable std::mutex chaosMutex;
    int lastObservedLeaderID;

    std::thread serverThread;
    std::thread workerThread;
    std::thread monitorThread;
    std::mutex inboxMutex;
    bool running;

    httplib::Server svr;

    static std::map<int, Node*> registry;
    static std::mutex registryMutex;
    static int leaderNodeID;
    static std::mutex leaderMutex;

    void runServer();
    void runWorker();
    void runMonitor();
    void processPacket(DataPacket packet);
    void logMessage(const std::string& msg);

    void adjustTrust(int subjectNode, float delta, const std::string& reason);
    void setTrustScore(int subjectNode, float value, const std::string& reason);
    std::vector<float> getLocalTrustSnapshot() const;
    std::vector<float> getBroadcastTrustVector() const;
    float getAggregatedTrustForNode(int subjectNode) const;
    bool isLeadershipEligible() const;
    void refreshLeader(const std::string& reason);
    ChaosMode getChaosMode() const;
    void setChaosMode(ChaosMode mode);
    void resetRuntimeState(bool clearPacketHistory);
    json buildNetworkSnapshot() const;
    bool isProbePacket(const DataPacket& packet) const;
    void applyChaosToPacket(DataPacket& packet);

    static int computeLeaderCandidate();
    static std::string chaosModeToString(ChaosMode mode);
    static bool parseChaosMode(const std::string& raw, ChaosMode& mode);
    static int64_t nowMs();
    static std::string formatPath(const std::vector<int>& path);
};
