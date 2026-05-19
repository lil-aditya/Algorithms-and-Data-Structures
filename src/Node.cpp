#include "Node.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {
constexpr int kMonitorIntervalMs = 400;
constexpr int kChaosDelayMs = 1600;
constexpr int kHighLatencyThresholdMs = 1200;
constexpr int kLeaderHeartbeatIntervalMs = 800;
constexpr int kLeaderTimeoutMs = 2400;
constexpr int kElectionWaitMs = 1500;
constexpr int kTrustReportIntervalMs = 1800;
constexpr int kTrustSyncIntervalMs = 1800;
constexpr int kTrustReportFreshnessMs = 6500;
constexpr int kProbeIntervalMs = 6000;
constexpr int kTrustRecoveryIntervalMs = 2600;
constexpr float kObserverCredibilityFloor = 0.25f;

float clampTrust(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

bool hasMeaningfulDiff(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) {
        return true;
    }

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::fabs(lhs[i] - rhs[i]) >= 0.01f) {
            return true;
        }
    }

    return false;
}

std::string serializeScores(const std::vector<float>& scores) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    for (size_t i = 0; i < scores.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << scores[i];
    }

    return oss.str();
}

std::string serializeTrustMatrix(const std::vector<std::vector<float>>& matrix) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    for (size_t row = 0; row < matrix.size(); ++row) {
        if (row > 0) {
            oss << ';';
        }

        for (size_t col = 0; col < matrix[row].size(); ++col) {
            if (col > 0) {
                oss << ',';
            }
            oss << matrix[row][col];
        }
    }

    return oss.str();
}

std::string buildElectionPayload(int candidateID, int64_t generation, int64_t epoch,
                                 int64_t initiatedAtMs) {
    return "election|" + std::to_string(candidateID) + "|" + std::to_string(generation) + "|" +
           std::to_string(epoch) + "|" +
           std::to_string(initiatedAtMs);
}

std::string buildHeartbeatPayload(int leaderID, int64_t generation, int64_t epoch,
                                  int64_t generatedAtMs) {
    return "heartbeat|" + std::to_string(leaderID) + "|" + std::to_string(generation) + "|" +
           std::to_string(epoch) + "|" +
           std::to_string(generatedAtMs);
}

std::string buildTrustReportPayload(int reporterID, int leaderID, int64_t generation,
                                    int64_t epoch, int64_t generatedAtMs,
                                    const std::vector<float>& scores) {
    return "trust-report|" + std::to_string(reporterID) + "|" + std::to_string(leaderID) + "|" +
           std::to_string(generation) + "|" + std::to_string(epoch) + "|" +
           std::to_string(generatedAtMs) + "|" +
           serializeScores(scores);
}

std::string buildTrustSyncPayload(int leaderID, int64_t generation, int64_t epoch,
                                  int trustVersion, int64_t generatedAtMs,
                                  const std::vector<float>& scores,
                                  const std::vector<std::vector<float>>& matrix) {
    return "trust-sync|" + std::to_string(leaderID) + "|" + std::to_string(generation) + "|" +
           std::to_string(epoch) + "|" + std::to_string(trustVersion) + "|" +
           std::to_string(generatedAtMs) + "|" +
           serializeScores(scores) + "|" + serializeTrustMatrix(matrix);
}
} // namespace

std::vector<std::string> Node::sharedLog;
std::mutex Node::logMutex;
std::map<int, Node*> Node::registry;
std::mutex Node::registryMutex;

Node::Node(int id, Graph& graph, MetadataMap& book, PacketStore& store)
    : nodeID(id),
      nodeKeys(generateKeys(id)),
      networkMap(graph),
      addressBook(book),
      packetStore(store),
      trustScores(store.getTrustVector(id, kNodeCount)),
      chaosMode(ChaosMode::NORMAL),
      lastObservedLeaderID(-1),
      currentLeaderID(-1),
      leaderEpoch(0),
      lastLeaderHeartbeatMs(0),
      electionInProgress(false),
      electionStartedAtMs(0),
      controlGeneration(1),
      authoritativeTrustScores(kNodeCount, 1.0f),
      latestTrustMatrix(kNodeCount, std::vector<float>(kNodeCount, 1.0f)),
      reportReceivedAtMs(kNodeCount, 0),
      trustVersion(0),
      lastTrustSyncMs(0),
      lastPeerSuccessMs(kNodeCount, nowMs()),
      nextProbeTargetID((id + 1) % kNodeCount),
      probeSequence(0),
      nextProbeAtMs(nowMs() + (id * 350)),
      nextHeartbeatAtMs(0),
      nextTrustReportAtMs(nowMs() + 500 + (id * 150)),
      nextTrustSyncAtMs(0),
      nextRecoveryAtMs(nowMs() + 1800),
      running(true) {
    if (trustScores.size() != kNodeCount) {
        trustScores.assign(kNodeCount, 1.0f);
    }
    trustScores[nodeID] = 1.0f;
    authoritativeTrustScores = trustScores;
    latestTrustMatrix[nodeID] = trustScores;
    reportReceivedAtMs[nodeID] = nowMs();

    const std::string idStr = std::to_string(nodeID);
    addressBook.insert(idStr + "_pub_e", std::to_string(nodeKeys.e));
    addressBook.insert(idStr + "_pub_n", std::to_string(nodeKeys.n));

    for (int i = 0; i < kNodeCount; ++i) {
        packetStore.upsertTrustScore(nodeID, i, trustScores[i]);
    }

    std::lock_guard<std::mutex> guard(registryMutex);
    registry[nodeID] = this;
}

Node::~Node() {
    running = false;
    svr.stop();

    if (serverThread.joinable()) {
        serverThread.join();
    }
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    std::lock_guard<std::mutex> guard(registryMutex);
    registry.erase(nodeID);
}

void Node::start() {
    logMessage("[Node " + std::to_string(nodeID) + "] Starting...");
    serverThread = std::thread(&Node::runServer, this);
    workerThread = std::thread(&Node::runWorker, this);
    monitorThread = std::thread(&Node::runMonitor, this);
}

void Node::logMessage(const std::string& msg) {
    std::lock_guard<std::mutex> guard(logMutex);
    std::cout << msg << std::endl;
    sharedLog.push_back(msg);
}

int64_t Node::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Node::serializeTrustScores(const std::vector<float>& scores) {
    return serializeScores(scores);
}

uint64_t Node::signControlPayload(const std::string& payload) const {
    return signData(payload, nodeKeys);
}

bool Node::verifyControlPayload(int senderNode, const std::string& payload, uint64_t signature) const {
    try {
        const Keys senderKeys = generateKeys(senderNode);
        return verifySignature(payload, signature, senderKeys);
    } catch (const std::exception&) {
        return false;
    }
}

bool Node::postJsonToNode(int targetNodeID, const std::string& path, const json& payload,
                          json* response, int timeoutMs) const {
    const auto portIt = NODE_PORTS.find(targetNodeID);
    if (portIt == NODE_PORTS.end()) {
        return false;
    }

    httplib::Client cli("127.0.0.1", portIt->second);
    cli.set_connection_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
    cli.set_read_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);
    cli.set_write_timeout(timeoutMs / 1000, (timeoutMs % 1000) * 1000);

    if (auto res = cli.Post(path.c_str(), payload.dump(), "application/json")) {
        if (res->status != 200) {
            return false;
        }

        if (response) {
            try {
                *response = json::parse(res->body);
            } catch (const std::exception&) {
                *response = json::object();
            }
        }

        return true;
    }

    return false;
}

std::string Node::chaosModeToString(ChaosMode mode) {
    switch (mode) {
        case ChaosMode::NORMAL: return "NORMAL";
        case ChaosMode::TAMPER: return "TAMPER";
        case ChaosMode::SILENT_DROP: return "SILENT_DROP";
        case ChaosMode::DELAY: return "DELAY";
        case ChaosMode::FORGE: return "FORGE";
        case ChaosMode::EAVESDROP: return "EAVESDROP";
    }
    return "NORMAL";
}

bool Node::parseChaosMode(const std::string& raw, ChaosMode& mode) {
    std::string upper;
    upper.reserve(raw.size());
    for (char ch : raw) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    if (upper == "NORMAL") {
        mode = ChaosMode::NORMAL;
        return true;
    }
    if (upper == "TAMPER") {
        mode = ChaosMode::TAMPER;
        return true;
    }
    if (upper == "SILENT_DROP") {
        mode = ChaosMode::SILENT_DROP;
        return true;
    }
    if (upper == "DELAY") {
        mode = ChaosMode::DELAY;
        return true;
    }
    if (upper == "FORGE") {
        mode = ChaosMode::FORGE;
        return true;
    }
    if (upper == "EAVESDROP") {
        mode = ChaosMode::EAVESDROP;
        return true;
    }

    return false;
}

ChaosMode Node::getChaosMode() const {
    std::lock_guard<std::mutex> guard(chaosMutex);
    return chaosMode;
}

bool Node::isCurrentLeader() const {
    std::lock_guard<std::mutex> guard(controlMutex);
    return currentLeaderID == nodeID;
}

int Node::getCurrentLeaderID() const {
    std::lock_guard<std::mutex> guard(controlMutex);
    return currentLeaderID;
}

int64_t Node::getLeaderEpoch() const {
    std::lock_guard<std::mutex> guard(controlMutex);
    return leaderEpoch;
}

int64_t Node::getLastLeaderHeartbeatMs() const {
    std::lock_guard<std::mutex> guard(controlMutex);
    return lastLeaderHeartbeatMs;
}

void Node::recordPeerSuccess(int subjectNode) {
    if (subjectNode < 0 || subjectNode >= kNodeCount) {
        return;
    }

    std::lock_guard<std::mutex> guard(controlMutex);
    if (subjectNode < static_cast<int>(lastPeerSuccessMs.size())) {
        lastPeerSuccessMs[subjectNode] = nowMs();
    }
}

void Node::adjustTrust(int subjectNode, float delta, const std::string& reason) {
    if (subjectNode < 0 || subjectNode >= kNodeCount) {
        return;
    }

    float previous = 1.0f;
    float updated = 1.0f;
    {
        std::lock_guard<std::mutex> guard(trustMutex);
        previous = trustScores[subjectNode];
        updated = clampTrust(previous + delta);
        trustScores[subjectNode] = updated;
    }

    packetStore.upsertTrustScore(nodeID, subjectNode, updated);

    const std::vector<float> snapshot = getLocalTrustSnapshot();
    const bool expedite = updated < kQuarantineThreshold ||
        updated + 0.05f < previous ||
        subjectNode == nodeID;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        latestTrustMatrix[nodeID] = snapshot;
        reportReceivedAtMs[nodeID] = nowMs();
        if (currentLeaderID == nodeID && expedite) {
            nextTrustSyncAtMs = 0;
        } else if (currentLeaderID != nodeID && expedite) {
            nextTrustReportAtMs = 0;
        }
    }

    if (delta > 0.0f) {
        recordPeerSuccess(subjectNode);
    }

    if (std::fabs(updated - previous) >= 0.01f) {
        std::ostringstream oss;
        oss << "[Node " << nodeID << "] Trust[" << subjectNode << "] "
            << previous << " -> " << updated << " (" << reason << ")";
        logMessage(oss.str());
    }
}

void Node::setTrustScore(int subjectNode, float value, const std::string& reason) {
    if (subjectNode < 0 || subjectNode >= kNodeCount) {
        return;
    }

    float previous = 1.0f;
    float updated = clampTrust(value);
    {
        std::lock_guard<std::mutex> guard(trustMutex);
        previous = trustScores[subjectNode];
        trustScores[subjectNode] = updated;
    }

    packetStore.upsertTrustScore(nodeID, subjectNode, updated);

    const std::vector<float> snapshot = getLocalTrustSnapshot();
    const bool expedite = updated < kQuarantineThreshold ||
        std::fabs(updated - previous) >= 0.10f ||
        subjectNode == nodeID;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        latestTrustMatrix[nodeID] = snapshot;
        reportReceivedAtMs[nodeID] = nowMs();
        if (currentLeaderID == nodeID && expedite) {
            nextTrustSyncAtMs = 0;
        } else if (currentLeaderID != nodeID && expedite) {
            nextTrustReportAtMs = 0;
        }
    }

    if (updated > previous) {
        recordPeerSuccess(subjectNode);
    }

    if (std::fabs(updated - previous) >= 0.01f) {
        std::ostringstream oss;
        oss << "[Node " << nodeID << "] Trust[" << subjectNode << "] forced to "
            << updated << " (" << reason << ")";
        logMessage(oss.str());
    }
}

std::vector<float> Node::getLocalTrustSnapshot() const {
    std::lock_guard<std::mutex> guard(trustMutex);
    return trustScores;
}

std::vector<float> Node::getRoutingTrustVector() const {
    bool useAuthoritative = false;
    std::vector<float> authoritative;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        useAuthoritative = authoritativeTrustScores.size() == kNodeCount &&
            (lastTrustSyncMs > 0 || currentLeaderID == nodeID || trustVersion > 0);
        authoritative = authoritativeTrustScores;
    }

    if (useAuthoritative) {
        return authoritative;
    }

    return getLocalTrustSnapshot();
}

std::vector<std::vector<float>> Node::getLatestTrustMatrix() const {
    std::lock_guard<std::mutex> guard(controlMutex);
    return latestTrustMatrix;
}

bool Node::isLeadershipEligible() const {
    const ChaosMode mode = getChaosMode();
    if (mode == ChaosMode::TAMPER || mode == ChaosMode::FORGE || mode == ChaosMode::SILENT_DROP) {
        return false;
    }

    const std::vector<float> routingTrust = getRoutingTrustVector();
    return nodeID < static_cast<int>(routingTrust.size()) &&
           routingTrust[nodeID] >= kQuarantineThreshold;
}

void Node::setChaosMode(ChaosMode mode) {
    {
        std::lock_guard<std::mutex> guard(chaosMutex);
        chaosMode = mode;
    }

    float selfTrust = 1.0f;
    if (mode == ChaosMode::TAMPER || mode == ChaosMode::FORGE || mode == ChaosMode::SILENT_DROP) {
        selfTrust = 0.05f;
    } else if (mode == ChaosMode::DELAY) {
        selfTrust = 0.45f;
    } else if (mode == ChaosMode::EAVESDROP) {
        selfTrust = 0.75f;
    }

    setTrustScore(nodeID, selfTrust,
        "Self-health updated after chaos mode change to " + chaosModeToString(mode));
    logMessage("[Node " + std::to_string(nodeID) + "] Chaos mode set to " + chaosModeToString(mode));

    if (!isLeadershipEligible() && isCurrentLeader()) {
        {
            std::lock_guard<std::mutex> guard(controlMutex);
            currentLeaderID = -1;
            lastLeaderHeartbeatMs = 0;
            electionInProgress = false;
        }
        logMessage("[Leader Election] Node " + std::to_string(nodeID) +
                   " stepped down after becoming ineligible");
    }
}

bool Node::isProbePacket(const DataPacket& packet) const {
    return packet.data.rfind("PROBE", 0) == 0 || packet.data.find("HEALTH_PING") != std::string::npos;
}

std::string Node::formatPath(const std::vector<int>& path) {
    std::ostringstream oss;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            oss << "->";
        }
        oss << path[i];
    }
    return oss.str();
}

void Node::applyChaosToPacket(DataPacket& packet) {
    const ChaosMode mode = getChaosMode();

    if (mode == ChaosMode::DELAY) {
        logMessage("[Node " + std::to_string(nodeID) + "] Chaos DELAY: holding packet " + packet.id);
        std::this_thread::sleep_for(std::chrono::milliseconds(kChaosDelayMs));
        return;
    }

    if (mode == ChaosMode::TAMPER) {
        packet.data += "::TAMPERED_BY_NODE_" + std::to_string(nodeID);
        logMessage("[Node " + std::to_string(nodeID) + "] Chaos TAMPER: mutated packet " + packet.id);
        return;
    }

    if (mode == ChaosMode::FORGE) {
        packet.senderID = std::to_string(nodeID);
        packet.signature = signData(packet.data, nodeKeys);
        logMessage("[Node " + std::to_string(nodeID) + "] Chaos FORGE: re-signed packet " + packet.id +
                   " as sender " + packet.senderID);
    }
}

void Node::startElection(const std::string& reason, int64_t minimumEpoch) {
    if (!running || !isLeadershipEligible()) {
        return;
    }

    int64_t epoch = 0;
    int64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        const int64_t now = nowMs();
        const bool freshLeaderPresent =
            currentLeaderID != -1 &&
            currentLeaderID >= nodeID &&
            lastLeaderHeartbeatMs > 0 &&
            now - lastLeaderHeartbeatMs <= kLeaderTimeoutMs &&
            minimumEpoch <= leaderEpoch;

        if (freshLeaderPresent) {
            return;
        }

        if (electionInProgress && now - electionStartedAtMs < kElectionWaitMs) {
            return;
        }

        if (currentLeaderID == nodeID && lastLeaderHeartbeatMs > 0) {
            return;
        }

        electionInProgress = true;
        electionStartedAtMs = now;
        currentLeaderID = -1;
        leaderEpoch = std::max(leaderEpoch + 1, minimumEpoch + 1);
        lastLeaderHeartbeatMs = 0;
        epoch = leaderEpoch;
        generation = controlGeneration;
    }

    logMessage("[Election] Node " + std::to_string(nodeID) + " starting election for epoch " +
               std::to_string(epoch) + " (" + reason + ")");

    const int64_t initiatedAtMs = nowMs();
    const std::string payloadToSign =
        buildElectionPayload(nodeID, generation, epoch, initiatedAtMs);
    const json request = {
        {"candidateID", nodeID},
        {"generation", generation},
        {"epoch", epoch},
        {"initiatedAtMs", initiatedAtMs},
        {"signature", signControlPayload(payloadToSign)}
    };

    bool higherEligibleAck = false;
    for (int target = nodeID + 1; target < kNodeCount; ++target) {
        json response;
        if (postJsonToNode(target, "/election", request, &response, 900) &&
            response.value("ack", false)) {
            higherEligibleAck = true;
        }
    }

    if (!higherEligibleAck) {
        bool sameGeneration = false;
        {
            std::lock_guard<std::mutex> guard(controlMutex);
            sameGeneration = controlGeneration == generation;
        }
        if (!sameGeneration) {
            return;
        }
        announceCoordinator("no higher eligible node acknowledged " + reason);
        return;
    }

    const int64_t deadline = nowMs() + kElectionWaitMs;
    while (running && nowMs() < deadline) {
        {
            std::lock_guard<std::mutex> guard(controlMutex);
            if (controlGeneration != generation) {
                return;
            }
        }
        const int leader = getCurrentLeaderID();
        const int64_t currentEpoch = getLeaderEpoch();
        const int64_t lastHeartbeat = getLastLeaderHeartbeatMs();

        if (leader != -1 && currentEpoch >= epoch && lastHeartbeat > 0) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    bool shouldPromoteSelf = false;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        shouldPromoteSelf = controlGeneration == generation &&
            electionInProgress && currentLeaderID == -1;
    }

    if (shouldPromoteSelf && isLeadershipEligible()) {
        announceCoordinator("election timeout expired after higher-node ACKs");
    }
}

void Node::recomputeAuthoritativeTrust(const std::string& reason) {
    if (!isCurrentLeader()) {
        return;
    }

    const int64_t now = nowMs();
    const std::vector<float> localTrust = getLocalTrustSnapshot();

    std::vector<float> previous;
    std::vector<std::vector<float>> matrix;
    std::vector<int64_t> freshness;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        latestTrustMatrix[nodeID] = localTrust;
        reportReceivedAtMs[nodeID] = now;
        previous = authoritativeTrustScores;
        matrix = latestTrustMatrix;
        freshness = reportReceivedAtMs;
    }

    std::vector<float> nextTrust = previous.empty() ? localTrust : previous;
    if (nextTrust.size() != kNodeCount) {
        nextTrust.assign(kNodeCount, 1.0f);
    }

    for (int subject = 0; subject < kNodeCount; ++subject) {
        struct Observation {
            float score;
            float weight;
        };

        std::vector<Observation> observations;
        int lowVotes = 0;

        for (int observer = 0; observer < kNodeCount; ++observer) {
            if (observer >= static_cast<int>(matrix.size()) ||
                static_cast<int>(matrix[observer].size()) != kNodeCount) {
                continue;
            }

            if (observer != nodeID && now - freshness[observer] > kTrustReportFreshnessMs) {
                continue;
            }

            float observerCredibility = 1.0f;
            if (observer < static_cast<int>(previous.size())) {
                observerCredibility = std::max(0.05f, previous[observer]);
            }

            if (observer != nodeID && observerCredibility < kObserverCredibilityFloor) {
                continue;
            }

            const float score = clampTrust(matrix[observer][subject]);
            const float weight = observer == nodeID ? 1.25f : observerCredibility;
            observations.push_back({score, weight});

            if (score < kQuarantineThreshold && weight >= kObserverCredibilityFloor) {
                ++lowVotes;
            }
        }

        if (observations.empty()) {
            nextTrust[subject] = subject == nodeID ? localTrust[subject] : nextTrust[subject];
            continue;
        }

        std::sort(observations.begin(), observations.end(),
                  [](const Observation& lhs, const Observation& rhs) {
                      return lhs.score < rhs.score;
                  });

        if (observations.size() >= 5) {
            observations.erase(observations.begin());
            observations.pop_back();
        }

        float totalWeight = 0.0f;
        float weightedSum = 0.0f;
        for (const Observation& observation : observations) {
            totalWeight += observation.weight;
            weightedSum += observation.score * observation.weight;
        }

        const float weightedAverage = totalWeight > 0.0f
            ? weightedSum / totalWeight
            : localTrust[subject];

        float cumulativeWeight = 0.0f;
        float weightedMedian = observations.back().score;
        for (const Observation& observation : observations) {
            cumulativeWeight += observation.weight;
            if (cumulativeWeight >= totalWeight / 2.0f) {
                weightedMedian = observation.score;
                break;
            }
        }

        float candidate = 0.55f * weightedMedian + 0.45f * localTrust[subject];
        if (lowVotes >= 2) {
            candidate = std::min(candidate, 0.5f * (weightedMedian + weightedAverage));
        }

        if (subject == nodeID) {
            candidate = localTrust[subject];
        }

        const float previousScore = subject < static_cast<int>(previous.size())
            ? previous[subject]
            : localTrust[subject];

        if (candidate < previousScore) {
            nextTrust[subject] = clampTrust(candidate);
        } else {
            nextTrust[subject] = clampTrust(previousScore + std::min(0.04f, candidate - previousScore));
        }
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        changed = hasMeaningfulDiff(authoritativeTrustScores, nextTrust);
        authoritativeTrustScores = nextTrust;
        latestTrustMatrix[nodeID] = localTrust;
        reportReceivedAtMs[nodeID] = now;
        if (changed || trustVersion == 0) {
            ++trustVersion;
            lastTrustSyncMs = now;
        }
    }

    if (changed) {
        logMessage("[Leader " + std::to_string(nodeID) + "] Reconciled trust matrix (" + reason + ")");
    }
}

void Node::broadcastTrustSync(const std::string& reason) {
    if (!isCurrentLeader()) {
        return;
    }

    int64_t epoch = 0;
    int64_t generation = 0;
    int version = 0;
    std::vector<float> scores;
    std::vector<std::vector<float>> matrix;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        epoch = leaderEpoch;
        generation = controlGeneration;
        version = trustVersion;
        scores = authoritativeTrustScores;
        matrix = latestTrustMatrix;
        lastTrustSyncMs = nowMs();
    }

    const int64_t generatedAtMs = nowMs();
    const std::string payloadToSign =
        buildTrustSyncPayload(nodeID, generation, epoch, version, generatedAtMs, scores, matrix);
    const json payload = {
        {"leaderID", nodeID},
        {"generation", generation},
        {"epoch", epoch},
        {"trustVersion", version},
        {"generatedAtMs", generatedAtMs},
        {"authoritativeTrust", scores},
        {"trustMatrix", matrix},
        {"signature", signControlPayload(payloadToSign)}
    };

    for (int target = 0; target < kNodeCount; ++target) {
        if (target == nodeID) {
            continue;
        }
        postJsonToNode(target, "/trust-sync", payload, nullptr, 1000);
    }

    logMessage("[Leader " + std::to_string(nodeID) + "] Broadcast trust sync v" +
               std::to_string(version) + " (" + reason + ")");
}

void Node::announceCoordinator(const std::string& reason) {
    if (!isLeadershipEligible()) {
        return;
    }

    const int64_t now = nowMs();
    int64_t epoch = 0;
    int64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        currentLeaderID = nodeID;
        leaderEpoch = std::max<int64_t>(leaderEpoch, 1);
        lastLeaderHeartbeatMs = now;
        electionInProgress = false;
        electionStartedAtMs = 0;
        nextHeartbeatAtMs = 0;
        nextTrustSyncAtMs = 0;
        epoch = leaderEpoch;
        generation = controlGeneration;
    }

    recomputeAuthoritativeTrust("coordinator announcement");

    std::vector<float> scores;
    std::vector<std::vector<float>> matrix;
    int version = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        scores = authoritativeTrustScores;
        matrix = latestTrustMatrix;
        version = trustVersion;
    }

    const int64_t generatedAtMs = nowMs();
    const std::string payloadToSign =
        buildTrustSyncPayload(nodeID, generation, epoch, version, generatedAtMs, scores, matrix);
    const json coordinatorPayload = {
        {"leaderID", nodeID},
        {"generation", generation},
        {"epoch", epoch},
        {"trustVersion", version},
        {"generatedAtMs", generatedAtMs},
        {"authoritativeTrust", scores},
        {"trustMatrix", matrix},
        {"signature", signControlPayload(payloadToSign)}
    };

    for (int target = 0; target < kNodeCount; ++target) {
        if (target == nodeID) {
            continue;
        }
        postJsonToNode(target, "/coordinator", coordinatorPayload, nullptr, 1000);
    }

    broadcastTrustSync(reason);
    broadcastLeaderHeartbeat();

    logMessage("[Leader Election] Leader is now Node " + std::to_string(nodeID) +
               " for epoch " + std::to_string(epoch) +
               (reason.empty() ? "" : " (" + reason + ")"));
}

void Node::acceptCoordinator(int leaderID, int64_t epoch,
                             const std::vector<float>& trustScoresFromLeader,
                             const std::vector<std::vector<float>>& trustMatrixFromLeader,
                             int incomingTrustVersion,
                             const std::string& reason) {
    const int64_t now = nowMs();
    bool leaderChanged = false;

    {
        std::lock_guard<std::mutex> guard(controlMutex);
        if (epoch < leaderEpoch) {
            return;
        }
        if (epoch == leaderEpoch && currentLeaderID != -1 && leaderID < currentLeaderID) {
            return;
        }

        leaderChanged = currentLeaderID != leaderID;
        currentLeaderID = leaderID;
        leaderEpoch = epoch;
        lastLeaderHeartbeatMs = now;
        electionInProgress = false;
        electionStartedAtMs = 0;
        nextTrustReportAtMs = now + 600 + (nodeID * 100);

        if (trustScoresFromLeader.size() == kNodeCount) {
            authoritativeTrustScores = trustScoresFromLeader;
            lastTrustSyncMs = now;
        }
        if (trustMatrixFromLeader.size() == kNodeCount) {
            latestTrustMatrix = trustMatrixFromLeader;
        }
        if (incomingTrustVersion >= trustVersion) {
            trustVersion = incomingTrustVersion;
        }
    }

    if (leaderChanged) {
        logMessage("[Node " + std::to_string(nodeID) + "] Accepted Node " +
                   std::to_string(leaderID) + " as leader (" + reason + ")");
    }
}

void Node::broadcastLeaderHeartbeat() {
    if (!isCurrentLeader() || !isLeadershipEligible()) {
        return;
    }

    int64_t epoch = 0;
    int64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        lastLeaderHeartbeatMs = nowMs();
        epoch = leaderEpoch;
        generation = controlGeneration;
    }

    const int64_t generatedAtMs = nowMs();
    const std::string payloadToSign =
        buildHeartbeatPayload(nodeID, generation, epoch, generatedAtMs);
    const json payload = {
        {"leaderID", nodeID},
        {"generation", generation},
        {"epoch", epoch},
        {"generatedAtMs", generatedAtMs},
        {"signature", signControlPayload(payloadToSign)}
    };

    for (int target = 0; target < kNodeCount; ++target) {
        if (target == nodeID) {
            continue;
        }
        postJsonToNode(target, "/leader-heartbeat", payload, nullptr, 800);
    }
}

void Node::submitTrustReport() {
    const int leaderID = getCurrentLeaderID();
    if (leaderID < 0 || leaderID == nodeID) {
        return;
    }

    const std::vector<float> scores = getLocalTrustSnapshot();
    const int64_t epoch = getLeaderEpoch();
    int64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        generation = controlGeneration;
    }
    const int64_t generatedAtMs = nowMs();
    const std::string payloadToSign =
        buildTrustReportPayload(nodeID, leaderID, generation, epoch, generatedAtMs, scores);

    {
        std::lock_guard<std::mutex> guard(controlMutex);
        latestTrustMatrix[nodeID] = scores;
        reportReceivedAtMs[nodeID] = generatedAtMs;
    }

    const json payload = {
        {"reporterID", nodeID},
        {"leaderID", leaderID},
        {"generation", generation},
        {"epoch", epoch},
        {"generatedAtMs", generatedAtMs},
        {"scores", scores},
        {"signature", signControlPayload(payloadToSign)}
    };

    postJsonToNode(leaderID, "/trust-report", payload, nullptr, 1000);
}

void Node::dispatchAutonomousProbe() {
    const ChaosMode mode = getChaosMode();
    if (mode == ChaosMode::TAMPER || mode == ChaosMode::FORGE || mode == ChaosMode::SILENT_DROP) {
        return;
    }

    int target = -1;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        for (int attempt = 0; attempt < kNodeCount; ++attempt) {
            const int candidate = (nextProbeTargetID + attempt) % kNodeCount;
            if (candidate == nodeID) {
                continue;
            }
            target = candidate;
            nextProbeTargetID = (candidate + 1) % kNodeCount;
            break;
        }
    }

    if (target < 0) {
        return;
    }

    DataPacket packet;
    packet.id = "probe_" + std::to_string(nodeID) + "_" + std::to_string(++probeSequence);
    packet.urgency = 1;
    packet.data = "PROBE_HEALTH::" + std::to_string(nodeID) + "::" + std::to_string(target) +
                  "::" + std::to_string(probeSequence);
    packet.senderID = std::to_string(nodeID);
    packet.destinationID = target;
    packet.originNodeID = nodeID;
    packet.lastHopNodeID = nodeID;
    packet.lastForwardedAtMs = nowMs();
    packet.signature = signData(packet.data, nodeKeys);

    packetStore.initPacket(packet.id, nodeID, target, packet.urgency);
    packetStore.updateStatus(packet.id, "SIGNED", nodeID,
        "Autonomous probe signature: " + std::to_string(packet.signature));

    {
        std::lock_guard<std::mutex> guard(inboxMutex);
        inbox.push(packet);
    }

    packetStore.updateStatus(packet.id, "QUEUED", nodeID,
        "Autonomous probe queued for Node " + std::to_string(target));
    logMessage("[Node " + std::to_string(nodeID) + "] Dispatched autonomous probe " + packet.id +
               " to Node " + std::to_string(target));
}

void Node::applyTrustRecoveryTick() {
    const int64_t now = nowMs();
    std::vector<int64_t> peerSuccess;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        peerSuccess = lastPeerSuccessMs;
    }

    const std::vector<float> localTrust = getLocalTrustSnapshot();
    for (int peer = 0; peer < kNodeCount; ++peer) {
        if (peer == nodeID) {
            continue;
        }

        if (peer >= static_cast<int>(peerSuccess.size()) || now - peerSuccess[peer] > 7000) {
            continue;
        }

        if (peer < static_cast<int>(localTrust.size()) && localTrust[peer] < 0.92f) {
            adjustTrust(peer, 0.01f, "Recent healthy traffic recovery");
        }
    }
}

json Node::buildNetworkSnapshot() const {
    const std::vector<float> localTrust = getLocalTrustSnapshot();
    const std::vector<float> routingTrust = getRoutingTrustVector();
    const std::vector<std::vector<float>> trustMatrix = getLatestTrustMatrix();

    int leaderID = -1;
    int64_t epoch = 0;
    int64_t generation = 0;
    int64_t heartbeatMs = 0;
    int version = 0;
    {
        std::lock_guard<std::mutex> guard(controlMutex);
        leaderID = currentLeaderID;
        epoch = leaderEpoch;
        generation = controlGeneration;
        heartbeatMs = lastLeaderHeartbeatMs;
        version = trustVersion;
    }

    json nodes = json::array();
    std::lock_guard<std::mutex> guard(registryMutex);
    for (int i = 0; i < kNodeCount; ++i) {
        auto it = registry.find(i);
        const bool online = it != registry.end();
        std::string mode = "OFFLINE";
        if (online) {
            mode = chaosModeToString(it->second->getChaosMode());
        }

        const float trust = i < static_cast<int>(routingTrust.size()) ? routingTrust[i] : 1.0f;
        nodes.push_back({
            {"id", i},
            {"port", NODE_PORTS.at(i)},
            {"online", online},
            {"mode", mode},
            {"leader", i == leaderID},
            {"trust", trust},
            {"quarantined", trust < kQuarantineThreshold}
        });
    }

    json trustRows = json::array();
    for (int observer = 0; observer < static_cast<int>(trustMatrix.size()); ++observer) {
        trustRows.push_back({
            {"observerNode", observer},
            {"scores", trustMatrix[observer]}
        });
    }

    return json{
        {"nodeID", nodeID},
        {"leaderID", leaderID},
        {"leaderPort", leaderID >= 0 ? NODE_PORTS.at(leaderID) : -1},
        {"leaderEpoch", epoch},
        {"generation", generation},
        {"lastLeaderHeartbeatMs", heartbeatMs},
        {"trustVersion", version},
        {"localTrust", localTrust},
        {"aggregatedTrust", routingTrust},
        {"quarantineThreshold", kQuarantineThreshold},
        {"nodes", nodes},
        {"trustMatrix", trustRows}
    };
}

void Node::resetRuntimeState(bool clearPacketHistory) {
    {
        std::lock_guard<std::mutex> guard(chaosMutex);
        chaosMode = ChaosMode::NORMAL;
    }

    {
        std::lock_guard<std::mutex> guard(trustMutex);
        trustScores.assign(kNodeCount, 1.0f);
    }

    {
        std::lock_guard<std::mutex> guard(inboxMutex);
        inbox.clear();
    }

    {
        std::lock_guard<std::mutex> guard(controlMutex);
        ++controlGeneration;
        currentLeaderID = -1;
        leaderEpoch = 0;
        lastLeaderHeartbeatMs = 0;
        electionInProgress = false;
        electionStartedAtMs = 0;
        authoritativeTrustScores.assign(kNodeCount, 1.0f);
        latestTrustMatrix.assign(kNodeCount, std::vector<float>(kNodeCount, 1.0f));
        latestTrustMatrix[nodeID] = std::vector<float>(kNodeCount, 1.0f);
        reportReceivedAtMs.assign(kNodeCount, 0);
        reportReceivedAtMs[nodeID] = nowMs();
        trustVersion = 0;
        lastTrustSyncMs = 0;
        lastPeerSuccessMs.assign(kNodeCount, nowMs());
        nextProbeTargetID = (nodeID + 1) % kNodeCount;
        probeSequence = 0;
        nextProbeAtMs = nowMs() + (nodeID * 350);
        nextHeartbeatAtMs = 0;
        nextTrustReportAtMs = nowMs() + 500 + (nodeID * 150);
        nextTrustSyncAtMs = 0;
        nextRecoveryAtMs = nowMs() + 1800;
    }

    lastObservedLeaderID = -1;

    for (int i = 0; i < kNodeCount; ++i) {
        packetStore.upsertTrustScore(nodeID, i, 1.0f);
    }

    if (clearPacketHistory) {
        packetStore.clearAllState();
    }
}

void Node::runServer() {
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    auto corsHandler = [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    };

    svr.Options("/inject", corsHandler);
    svr.Options("/packet", corsHandler);
    svr.Options("/packets", corsHandler);
    svr.Options("/status", corsHandler);
    svr.Options("/log", corsHandler);
    svr.Options("/check", corsHandler);
    svr.Options("/chaos", corsHandler);
    svr.Options("/network", corsHandler);
    svr.Options("/leader", corsHandler);
    svr.Options("/reset", corsHandler);
    svr.Options("/election", corsHandler);
    svr.Options("/coordinator", corsHandler);
    svr.Options("/leader-heartbeat", corsHandler);
    svr.Options("/trust-report", corsHandler);
    svr.Options("/trust-sync", corsHandler);

    svr.Post("/packet", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            DataPacket packet = json::parse(req.body);

            if (packet.originNodeID < 0 && !packet.senderID.empty()) {
                packet.originNodeID = std::stoi(packet.senderID);
            }

            if (packet.lastHopNodeID >= 0 && packet.lastHopNodeID != nodeID && packet.lastForwardedAtMs > 0) {
                const int64_t latencyMs = nowMs() - packet.lastForwardedAtMs;
                if (latencyMs > kHighLatencyThresholdMs) {
                    adjustTrust(packet.lastHopNodeID, -0.20f,
                        "High hop latency of " + std::to_string(latencyMs) + "ms");
                } else if (isProbePacket(packet)) {
                    adjustTrust(packet.lastHopNodeID, 0.05f,
                        "Probe packet arrived from Node " + std::to_string(packet.lastHopNodeID));
                } else {
                    adjustTrust(packet.lastHopNodeID, 0.02f,
                        "Healthy hop from Node " + std::to_string(packet.lastHopNodeID));
                }
            }

            packetStore.updateStatus(packet.id, "RECEIVED", nodeID,
                "Received from forwarding node");

            {
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(packet);
            }

            packetStore.updateStatus(packet.id, "QUEUED", nodeID,
                "Queued with urgency " + std::to_string(packet.urgency));

            logMessage("[Node " + std::to_string(nodeID) + "] Received packet " + packet.id +
                       " from sender " + packet.senderID);
            res.set_content("{\"status\": \"ACK\"}", "application/json");
        } catch (const std::exception&) {
            logMessage("[Node " + std::to_string(nodeID) + "] Received invalid JSON. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    svr.Post("/inject", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            DataPacket packet = json::parse(req.body);

            logMessage("[Node " + std::to_string(nodeID) + "] === INJECTION RECEIVED ===");
            logMessage("[Node " + std::to_string(nodeID) + "] New packet " + packet.id +
                       " for destination " + std::to_string(packet.destinationID));

            packetStore.initPacket(packet.id, nodeID, packet.destinationID, packet.urgency);

            packet.senderID = std::to_string(nodeID);
            packet.originNodeID = nodeID;
            packet.lastHopNodeID = nodeID;
            packet.lastForwardedAtMs = nowMs();
            packet.signature = signData(packet.data, nodeKeys);

            logMessage("[Node " + std::to_string(nodeID) + "] Packet signed by node " +
                       packet.senderID + " with signature: " + std::to_string(packet.signature));

            packetStore.updateStatus(packet.id, "SIGNED", nodeID,
                "signature: " + std::to_string(packet.signature));

            {
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(packet);
            }

            packetStore.updateStatus(packet.id, "QUEUED", nodeID,
                "Queued with urgency " + std::to_string(packet.urgency));

            res.set_content("{\"status\": \"Packet Signed and Injected\"}", "application/json");
        } catch (const std::exception&) {
            logMessage("[Node " + std::to_string(nodeID) + "] Received invalid JSON from UI. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    svr.Post("/election", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body);
            const int candidateID = body.value("candidateID", -1);
            const int64_t generation = body.value("generation", static_cast<int64_t>(0));
            const int64_t epoch = body.value("epoch", static_cast<int64_t>(0));
            const int64_t initiatedAtMs = body.value("initiatedAtMs", static_cast<int64_t>(0));
            const uint64_t signature = body.value("signature", static_cast<uint64_t>(0));

            const std::string payload =
                buildElectionPayload(candidateID, generation, epoch, initiatedAtMs);
            if (candidateID < 0 || candidateID >= kNodeCount ||
                !verifyControlPayload(candidateID, payload, signature)) {
                res.set_content("{\"error\": \"Invalid election signature\"}", 403, "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> guard(controlMutex);
                if (generation != controlGeneration) {
                    res.set_content("{\"error\": \"Stale election generation\"}", 409, "application/json");
                    return;
                }
            }

            const bool eligible = isLeadershipEligible();
            const int knownLeader = getCurrentLeaderID();
            const int64_t lastHeartbeat = getLastLeaderHeartbeatMs();
            const bool leaderFresh = knownLeader != -1 &&
                lastHeartbeat > 0 &&
                nowMs() - lastHeartbeat <= kLeaderTimeoutMs;
            const bool lowerRankLeaderKnown = knownLeader != -1 && knownLeader < nodeID;

            if (eligible && candidateID < nodeID &&
                (!leaderFresh || epoch > getLeaderEpoch() || lowerRankLeaderKnown)) {
                std::thread([this, epoch]() {
                    this->startElection("received bully election from lower node", epoch);
                }).detach();
            }

            const json response = {
                {"ack", eligible && nodeID > candidateID},
                {"eligible", eligible},
                {"responderID", nodeID},
                {"epoch", getLeaderEpoch()}
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception&) {
            res.set_content("{\"error\": \"Invalid election payload\"}", 400, "application/json");
        }
    });

    svr.Post("/coordinator", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body);
            const int leaderID = body.value("leaderID", -1);
            const int64_t generation = body.value("generation", static_cast<int64_t>(0));
            const int64_t epoch = body.value("epoch", static_cast<int64_t>(0));
            const int trustVersionValue = body.value("trustVersion", 0);
            const int64_t generatedAtMs = body.value("generatedAtMs", static_cast<int64_t>(0));
            const uint64_t signature = body.value("signature", static_cast<uint64_t>(0));
            const std::vector<float> trustScores = body.value("authoritativeTrust",
                std::vector<float>(kNodeCount, 1.0f));
            const std::vector<std::vector<float>> trustMatrix =
                body.value("trustMatrix", std::vector<std::vector<float>>{});

            const std::string payload = buildTrustSyncPayload(
                leaderID, generation, epoch, trustVersionValue, generatedAtMs, trustScores, trustMatrix);
            if (leaderID < 0 || leaderID >= kNodeCount ||
                !verifyControlPayload(leaderID, payload, signature)) {
                res.set_content("{\"error\": \"Invalid coordinator signature\"}", 403, "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> guard(controlMutex);
                if (generation != controlGeneration) {
                    res.set_content("{\"error\": \"Stale coordinator generation\"}", 409, "application/json");
                    return;
                }
            }

            acceptCoordinator(leaderID, epoch, trustScores, trustMatrix,
                              trustVersionValue, "coordinator announcement");
            res.set_content("{\"status\": \"ACK\"}", "application/json");
        } catch (const std::exception&) {
            res.set_content("{\"error\": \"Invalid coordinator payload\"}", 400, "application/json");
        }
    });

    svr.Post("/leader-heartbeat", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body);
            const int leaderID = body.value("leaderID", -1);
            const int64_t generation = body.value("generation", static_cast<int64_t>(0));
            const int64_t epoch = body.value("epoch", static_cast<int64_t>(0));
            const int64_t generatedAtMs = body.value("generatedAtMs", static_cast<int64_t>(0));
            const uint64_t signature = body.value("signature", static_cast<uint64_t>(0));

            const std::string payload =
                buildHeartbeatPayload(leaderID, generation, epoch, generatedAtMs);
            if (leaderID < 0 || leaderID >= kNodeCount ||
                !verifyControlPayload(leaderID, payload, signature)) {
                res.set_content("{\"error\": \"Invalid heartbeat signature\"}", 403, "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> guard(controlMutex);
                if (generation != controlGeneration) {
                    res.set_content("{\"error\": \"Stale heartbeat generation\"}", 409, "application/json");
                    return;
                }
                const bool acceptHeartbeat =
                    epoch > leaderEpoch ||
                    (epoch == leaderEpoch &&
                     (currentLeaderID == -1 || leaderID >= currentLeaderID));
                if (acceptHeartbeat) {
                    currentLeaderID = leaderID;
                    leaderEpoch = epoch;
                    lastLeaderHeartbeatMs = nowMs();
                    electionInProgress = false;
                    nextTrustReportAtMs = 0;
                }
            }

            res.set_content("{\"status\": \"ACK\"}", "application/json");
        } catch (const std::exception&) {
            res.set_content("{\"error\": \"Invalid heartbeat payload\"}", 400, "application/json");
        }
    });

    svr.Post("/trust-report", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            if (!isCurrentLeader()) {
                res.set_content("{\"error\": \"Not the current leader\"}", 409, "application/json");
                return;
            }

            const json body = json::parse(req.body);
            const int reporterID = body.value("reporterID", -1);
            const int leaderID = body.value("leaderID", -1);
            const int64_t generation = body.value("generation", static_cast<int64_t>(0));
            const int64_t epoch = body.value("epoch", static_cast<int64_t>(0));
            const int64_t generatedAtMs = body.value("generatedAtMs", static_cast<int64_t>(0));
            const uint64_t signature = body.value("signature", static_cast<uint64_t>(0));
            const std::vector<float> scores = body.value("scores",
                std::vector<float>(kNodeCount, 1.0f));

            const std::string payload = buildTrustReportPayload(
                reporterID, leaderID, generation, epoch, generatedAtMs, scores);
            if (reporterID < 0 || reporterID >= kNodeCount || leaderID != nodeID ||
                !verifyControlPayload(reporterID, payload, signature)) {
                res.set_content("{\"error\": \"Invalid trust report signature\"}", 403, "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> guard(controlMutex);
                if (generation != controlGeneration) {
                    res.set_content("{\"error\": \"Stale trust report generation\"}", 409, "application/json");
                    return;
                }
                latestTrustMatrix[reporterID] = scores;
                reportReceivedAtMs[reporterID] = nowMs();
                nextTrustSyncAtMs = 0;
            }

            recordPeerSuccess(reporterID);
            res.set_content("{\"status\": \"ACK\"}", "application/json");
        } catch (const std::exception&) {
            res.set_content("{\"error\": \"Invalid trust report payload\"}", 400, "application/json");
        }
    });

    svr.Post("/trust-sync", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body);
            const int leaderID = body.value("leaderID", -1);
            const int64_t generation = body.value("generation", static_cast<int64_t>(0));
            const int64_t epoch = body.value("epoch", static_cast<int64_t>(0));
            const int version = body.value("trustVersion", 0);
            const int64_t generatedAtMs = body.value("generatedAtMs", static_cast<int64_t>(0));
            const uint64_t signature = body.value("signature", static_cast<uint64_t>(0));
            const std::vector<float> trustScores = body.value("authoritativeTrust",
                std::vector<float>(kNodeCount, 1.0f));
            const std::vector<std::vector<float>> trustMatrix =
                body.value("trustMatrix", std::vector<std::vector<float>>{});

            const std::string payload = buildTrustSyncPayload(
                leaderID, generation, epoch, version, generatedAtMs, trustScores, trustMatrix);
            if (leaderID < 0 || leaderID >= kNodeCount ||
                !verifyControlPayload(leaderID, payload, signature)) {
                res.set_content("{\"error\": \"Invalid trust-sync signature\"}", 403, "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> guard(controlMutex);
                if (generation != controlGeneration) {
                    res.set_content("{\"error\": \"Stale trust-sync generation\"}", 409, "application/json");
                    return;
                }
            }

            acceptCoordinator(leaderID, epoch, trustScores, trustMatrix, version, "trust sync");
            res.set_content("{\"status\": \"ACK\"}", "application/json");
        } catch (const std::exception&) {
            res.set_content("{\"error\": \"Invalid trust-sync payload\"}", 400, "application/json");
        }
    });

    svr.Get("/log", [this](const httplib::Request&, httplib::Response& res) {
        json payload;
        {
            std::lock_guard<std::mutex> guard(logMutex);
            payload = sharedLog;
        }
        res.set_content(payload.dump(), "application/json");
    });

    svr.Get("/check", [this](const httplib::Request&, httplib::Response& res) {
        json payload = {
            {"status", "ONLINE"},
            {"nodeID", nodeID},
            {"mode", chaosModeToString(getChaosMode())}
        };
        res.set_content(payload.dump(), "application/json");
    });

    svr.Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string packetID = req.get_param_value("id");
        if (packetID.empty()) {
            res.set_content("{\"error\": \"Missing 'id' query parameter. Use /status?id=pkt_001\"}",
                            400, "application/json");
            return;
        }
        res.set_content(packetStore.getPacket(packetID).dump(2), "application/json");
    });

    svr.Get("/packets", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(packetStore.getAllPackets().dump(2), "application/json");
    });

    svr.Get("/network", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(buildNetworkSnapshot().dump(2), "application/json");
    });

    svr.Get("/leader", [this](const httplib::Request&, httplib::Response& res) {
        const int currentLeader = getCurrentLeaderID();
        int64_t generation = 0;
        {
            std::lock_guard<std::mutex> guard(controlMutex);
            generation = controlGeneration;
        }
        const json payload = {
            {"leaderID", currentLeader},
            {"leaderPort", currentLeader >= 0 ? NODE_PORTS.at(currentLeader) : -1},
            {"leaderEpoch", getLeaderEpoch()},
            {"generation", generation},
            {"lastLeaderHeartbeatMs", getLastLeaderHeartbeatMs()}
        };
        res.set_content(payload.dump(), "application/json");
    });

    svr.Post("/chaos", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string modeParam = req.get_param_value("mode");
        ChaosMode mode;
        if (!parseChaosMode(modeParam, mode)) {
            res.set_content("{\"error\": \"Invalid mode. Use NORMAL, TAMPER, SILENT_DROP, DELAY, FORGE, or EAVESDROP.\"}",
                            400, "application/json");
            return;
        }

        setChaosMode(mode);
        res.set_content(buildNetworkSnapshot().dump(2), "application/json");
    });

    svr.Post("/reset", [this](const httplib::Request&, httplib::Response& res) {
        std::vector<Node*> nodes;
        {
            std::lock_guard<std::mutex> guard(registryMutex);
            for (const auto& [id, node] : registry) {
                (void)id;
                nodes.push_back(node);
            }
        }

        packetStore.clearAllState();
        {
            std::lock_guard<std::mutex> guard(logMutex);
            sharedLog.clear();
        }

        for (Node* node : nodes) {
            node->resetRuntimeState(false);
        }

        startElection("manual reset");
        logMessage("[Cluster] Runtime state reset from Node " + std::to_string(nodeID));
        res.set_content(buildNetworkSnapshot().dump(2), "application/json");
    });

    logMessage("[Node " + std::to_string(nodeID) + "] Server listening on port " +
               std::to_string(NODE_PORTS.at(nodeID)));
    if (!svr.listen("0.0.0.0", NODE_PORTS.at(nodeID))) {
        logMessage("[Node " + std::to_string(nodeID) + "] FAILED to bind to port " +
                   std::to_string(NODE_PORTS.at(nodeID)));
    }
}

void Node::runWorker() {
    while (running) {
        std::optional<DataPacket> packet;
        {
            std::lock_guard<std::mutex> guard(inboxMutex);
            if (!inbox.empty()) {
                packet = inbox.pop();
            }
        }

        if (packet) {
            processPacket(*packet);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Node::runMonitor() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1200 + (nodeID * 120)));

    while (running) {
        const int64_t now = nowMs();

        if (isCurrentLeader() && !isLeadershipEligible()) {
            {
                std::lock_guard<std::mutex> guard(controlMutex);
                currentLeaderID = -1;
                lastLeaderHeartbeatMs = 0;
                electionInProgress = false;
            }
        }

        const int currentLeader = getCurrentLeaderID();
        const int64_t leaderHeartbeat = getLastLeaderHeartbeatMs();
        const std::vector<float> routingTrust = getRoutingTrustVector();
        const bool leaderLooksQuarantined =
            currentLeader >= 0 && currentLeader < static_cast<int>(routingTrust.size()) &&
            routingTrust[currentLeader] < kQuarantineThreshold;
        const bool lowerRankLeaderPresent =
            currentLeader != -1 && currentLeader < nodeID && isLeadershipEligible();

        if (!isCurrentLeader() &&
            (currentLeader == -1 ||
             lowerRankLeaderPresent ||
             leaderLooksQuarantined ||
             (leaderHeartbeat > 0 && now - leaderHeartbeat > kLeaderTimeoutMs))) {
            startElection(currentLeader == -1 ? "no leader observed" :
                          lowerRankLeaderPresent ? "observed lower-ranked leader" :
                          "leader heartbeat timed out",
                          getLeaderEpoch());
        }

        bool shouldHeartbeat = false;
        bool shouldSync = false;
        bool shouldReport = false;
        bool shouldProbe = false;
        bool shouldRecover = false;

        {
            std::lock_guard<std::mutex> guard(controlMutex);
            if (currentLeaderID == nodeID && now >= nextHeartbeatAtMs) {
                nextHeartbeatAtMs = now + kLeaderHeartbeatIntervalMs;
                shouldHeartbeat = true;
            }

            if (currentLeaderID == nodeID && now >= nextTrustSyncAtMs) {
                nextTrustSyncAtMs = now + kTrustSyncIntervalMs;
                shouldSync = true;
            }

            if (currentLeaderID != -1 && currentLeaderID != nodeID && now >= nextTrustReportAtMs) {
                nextTrustReportAtMs = now + kTrustReportIntervalMs;
                shouldReport = true;
            }

            if (now >= nextProbeAtMs) {
                nextProbeAtMs = now + kProbeIntervalMs;
                shouldProbe = true;
            }

            if (now >= nextRecoveryAtMs) {
                nextRecoveryAtMs = now + kTrustRecoveryIntervalMs;
                shouldRecover = true;
            }
        }

        if (shouldHeartbeat) {
            broadcastLeaderHeartbeat();
        }
        if (shouldSync) {
            recomputeAuthoritativeTrust("leader monitor tick");
            broadcastTrustSync("leader monitor tick");
        }
        if (shouldReport) {
            submitTrustReport();
        }
        if (shouldProbe) {
            dispatchAutonomousProbe();
        }
        if (shouldRecover) {
            applyTrustRecoveryTick();
        }

        const int observedLeader = getCurrentLeaderID();
        if (observedLeader != lastObservedLeaderID && observedLeader != -1) {
            logMessage("[Node " + std::to_string(nodeID) + "] Observed leader Node " +
                       std::to_string(observedLeader));
            lastObservedLeaderID = observedLeader;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kMonitorIntervalMs));
    }
}

void Node::processPacket(DataPacket packet) {
    logMessage("[Node " + std::to_string(nodeID) + "] Processing packet " + packet.id);
    packetStore.updateStatus(packet.id, "PROCESSING", nodeID,
        "Worker thread picked up packet");

    if (getChaosMode() == ChaosMode::SILENT_DROP) {
        logMessage("[Node " + std::to_string(nodeID) + "] Chaos SILENT_DROP: dropping packet " + packet.id);
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Chaos mode SILENT_DROP discarded packet at Node " + std::to_string(nodeID));
        setTrustScore(nodeID, 0.05f, "Silent drop mode active");
        return;
    }

    if (getChaosMode() == ChaosMode::EAVESDROP) {
        logMessage("[Node " + std::to_string(nodeID) + "] Chaos EAVESDROP observed payload: " + packet.data);
    }

    if (packet.originNodeID >= 0 && packet.senderID != std::to_string(packet.originNodeID)) {
        const int suspect = packet.lastHopNodeID >= 0 ? packet.lastHopNodeID : nodeID;
        logMessage("[Node " + std::to_string(nodeID) + "] Forged sender identity detected on packet " + packet.id);
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Forged sender detected: origin Node " + std::to_string(packet.originNodeID) +
            ", claimed signer Node " + packet.senderID);
        setTrustScore(suspect, 0.05f, "Detected forged sender identity");
        return;
    }

    const std::string eKey = packet.senderID + "_pub_e";
    const std::string nKey = packet.senderID + "_pub_n";
    const std::string eStr = addressBook.get(eKey);
    const std::string nStr = addressBook.get(nKey);

    if (eStr.empty() || nStr.empty()) {
        const int suspect = packet.lastHopNodeID >= 0 ? packet.lastHopNodeID : nodeID;
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. No public key for sender " +
                   packet.senderID + ". DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "No public key found for sender " + packet.senderID);
        setTrustScore(suspect, 0.05f, "Forwarded packet with unknown public key");
        return;
    }

    Keys senderPubKey;
    try {
        senderPubKey.e = std::stoull(eStr);
        senderPubKey.n = std::stoull(nStr);
    } catch (const std::exception&) {
        const int suspect = packet.lastHopNodeID >= 0 ? packet.lastHopNodeID : nodeID;
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. Invalid key format. DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Invalid key format in address book");
        setTrustScore(suspect, 0.05f, "Forwarded packet with invalid key metadata");
        return;
    }

    const bool isValid = verifySignature(packet.data, packet.signature, senderPubKey);
    if (!isValid) {
        const int suspect = packet.lastHopNodeID >= 0 ? packet.lastHopNodeID : nodeID;
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. Data/Signature mismatch. DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Signature mismatch - data may be tampered");
        setTrustScore(suspect, 0.05f, "Detected signature mismatch");
        return;
    }

    logMessage("[Node " + std::to_string(nodeID) + "] Signature VALID. Packet trusted.");
    packetStore.updateStatus(packet.id, "VERIFIED", nodeID,
        "Signature verified against sender " + packet.senderID);

    if (nodeID == packet.destinationID) {
        logMessage("[Node " + std::to_string(nodeID) + "] Packet DELIVERED to final destination.");
        packetStore.updateStatus(packet.id, "DELIVERED", nodeID,
            "Packet reached destination Node " + std::to_string(nodeID));

        if (isProbePacket(packet) && packet.originNodeID >= 0) {
            adjustTrust(packet.originNodeID, 0.08f, "Probe packet delivered successfully");
        }
        return;
    }

    const std::vector<float> trustView = getRoutingTrustVector();
    const std::vector<int> path = networkMap.findTrustedPath(
        nodeID, packet.destinationID, trustView, kQuarantineThreshold);

    if (path.size() < 2) {
        logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. No trusted path to destination " +
                   std::to_string(packet.destinationID) + ". DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "No trusted route to destination " + std::to_string(packet.destinationID));
        return;
    }

    const int nextHopID = path[1];
    const int nextHopPort = NODE_PORTS.at(nextHopID);
    logMessage("[Node " + std::to_string(nodeID) + "] Routed packet " + packet.id +
               " via path " + formatPath(path));

    packetStore.updateStatus(packet.id, "FORWARDED", nodeID,
        "Node " + std::to_string(nodeID) + " -> Node " + std::to_string(nextHopID) +
        " using path " + formatPath(path));

    packet.lastHopNodeID = nodeID;
    packet.lastForwardedAtMs = nowMs();
    applyChaosToPacket(packet);

    httplib::Client cli("127.0.0.1", nextHopPort);
    cli.set_connection_timeout(1);

    if (auto res = cli.Post("/packet", json(packet).dump(), "application/json")) {
        if (res->status == 200) {
            adjustTrust(nextHopID, isProbePacket(packet) ? 0.05f : 0.02f,
                "Accepted forwarded packet " + packet.id);
        } else {
            logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. Node " +
                       std::to_string(nextHopID) + " responded with error " +
                       std::to_string(res->status));
            packetStore.updateStatus(packet.id, "DROPPED", nodeID,
                "Next hop Node " + std::to_string(nextHopID) +
                " returned HTTP " + std::to_string(res->status));
            adjustTrust(nextHopID, -0.40f,
                "Returned HTTP " + std::to_string(res->status) + " for packet " + packet.id);
        }
    } else {
        logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. Could not connect to Node " +
                   std::to_string(nextHopID));
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Connection refused by Node " + std::to_string(nextHopID));
        adjustTrust(nextHopID, -0.50f,
            "Connection failure while forwarding packet " + packet.id);
    }
}
