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
    mutable std::mutex controlMutex;
    int currentLeaderID;
    int64_t leaderEpoch;
    int64_t lastLeaderHeartbeatMs;
    bool electionInProgress;
    int64_t electionStartedAtMs;
    int64_t controlGeneration;
    std::vector<float> authoritativeTrustScores;
    std::vector<std::vector<float>> latestTrustMatrix;
    std::vector<int64_t> reportReceivedAtMs;
    int trustVersion;
    int64_t lastTrustSyncMs;
    std::vector<int64_t> lastPeerSuccessMs;
    int nextProbeTargetID;
    int probeSequence;
    int64_t nextProbeAtMs;
    int64_t nextHeartbeatAtMs;
    int64_t nextTrustReportAtMs;
    int64_t nextTrustSyncAtMs;
    int64_t nextRecoveryAtMs;

    std::thread serverThread;
    std::thread workerThread;
    std::thread monitorThread;
    std::mutex inboxMutex;
    bool running;

    httplib::Server svr;

    static std::map<int, Node*> registry;
    static std::mutex registryMutex;

    void runServer();
    void runWorker();
    void runMonitor();
    void processPacket(DataPacket packet);
    void logMessage(const std::string& msg);

    void adjustTrust(int subjectNode, float delta, const std::string& reason);
    void setTrustScore(int subjectNode, float value, const std::string& reason);
    std::vector<float> getLocalTrustSnapshot() const;
    std::vector<float> getRoutingTrustVector() const;
    std::vector<std::vector<float>> getLatestTrustMatrix() const;
    bool isLeadershipEligible() const;
    ChaosMode getChaosMode() const;
    void setChaosMode(ChaosMode mode);
    void resetRuntimeState(bool clearPacketHistory);
    json buildNetworkSnapshot() const;
    bool isProbePacket(const DataPacket& packet) const;
    void applyChaosToPacket(DataPacket& packet);
    void startElection(const std::string& reason, int64_t minimumEpoch = 0);
    void announceCoordinator(const std::string& reason);
    void acceptCoordinator(int leaderID, int64_t epoch,
                           const std::vector<float>& trustScores,
                           const std::vector<std::vector<float>>& trustMatrix,
                           int incomingTrustVersion,
                           const std::string& reason);
    void broadcastLeaderHeartbeat();
    void submitTrustReport();
    void recomputeAuthoritativeTrust(const std::string& reason);
    void broadcastTrustSync(const std::string& reason);
    void dispatchAutonomousProbe();
    void recordPeerSuccess(int subjectNode);
    void applyTrustRecoveryTick();
    bool isCurrentLeader() const;
    int getCurrentLeaderID() const;
    int64_t getLeaderEpoch() const;
    int64_t getLastLeaderHeartbeatMs() const;
    uint64_t signControlPayload(const std::string& payload) const;
    bool verifyControlPayload(int senderNode, const std::string& payload, uint64_t signature) const;
    bool postJsonToNode(int targetNodeID, const std::string& path, const json& payload,
                        json* response = nullptr, int timeoutMs = 1200) const;

    static std::string chaosModeToString(ChaosMode mode);
    static bool parseChaosMode(const std::string& raw, ChaosMode& mode);
    static int64_t nowMs();
    static std::string formatPath(const std::vector<int>& path);
    static std::string serializeTrustScores(const std::vector<float>& scores);
};
