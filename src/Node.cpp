#include "Node.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {
constexpr int kMonitorIntervalMs = 1000;
constexpr int kChaosDelayMs = 1600;
constexpr int kHighLatencyThresholdMs = 1200;

float clampTrust(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}
} // namespace

std::vector<std::string> Node::sharedLog;
std::mutex Node::logMutex;
std::map<int, Node*> Node::registry;
std::mutex Node::registryMutex;
int Node::leaderNodeID = -1;
std::mutex Node::leaderMutex;

Node::Node(int id, Graph& graph, MetadataMap& book, PacketStore& store)
    : nodeID(id),
      nodeKeys(generateKeys(id)),
      networkMap(graph),
      addressBook(book),
      packetStore(store),
      trustScores(store.getTrustVector(id, kNodeCount)),
      chaosMode(ChaosMode::NORMAL),
      lastObservedLeaderID(-1),
      running(true) {
    if (trustScores.size() != kNodeCount) {
        trustScores.assign(kNodeCount, 1.0f);
    }
    trustScores[nodeID] = 1.0f;

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
    refreshLeader("chaos mode change on node " + std::to_string(nodeID));
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

std::vector<float> Node::getBroadcastTrustVector() const {
    std::vector<float> aggregated(kNodeCount, 1.0f);
    std::vector<bool> seen(kNodeCount, false);

    std::lock_guard<std::mutex> guard(registryMutex);
    for (const auto& [id, node] : registry) {
        (void)id;
        const std::vector<float> local = node->getLocalTrustSnapshot();
        for (int subject = 0; subject < kNodeCount && subject < static_cast<int>(local.size()); ++subject) {
            if (!seen[subject]) {
                aggregated[subject] = local[subject];
                seen[subject] = true;
            } else {
                aggregated[subject] = std::min(aggregated[subject], local[subject]);
            }
        }
    }

    for (int i = 0; i < kNodeCount; ++i) {
        if (!seen[i]) {
            aggregated[i] = 1.0f;
        }
    }

    return aggregated;
}

float Node::getAggregatedTrustForNode(int subjectNode) const {
    if (subjectNode < 0 || subjectNode >= kNodeCount) {
        return 0.0f;
    }

    const std::vector<float> aggregated = getBroadcastTrustVector();
    return aggregated[subjectNode];
}

bool Node::isLeadershipEligible() const {
    const ChaosMode mode = getChaosMode();
    if (mode == ChaosMode::TAMPER || mode == ChaosMode::FORGE || mode == ChaosMode::SILENT_DROP) {
        return false;
    }

    const std::vector<float> local = getLocalTrustSnapshot();
    return nodeID < static_cast<int>(local.size()) && local[nodeID] >= kQuarantineThreshold;
}

int Node::computeLeaderCandidate() {
    std::lock_guard<std::mutex> guard(registryMutex);
    int candidate = -1;
    for (const auto& [id, node] : registry) {
        if (node->isLeadershipEligible()) {
            candidate = std::max(candidate, id);
        }
    }

    if (candidate != -1) {
        return candidate;
    }

    for (const auto& [id, node] : registry) {
        (void)node;
        candidate = std::max(candidate, id);
    }

    return candidate;
}

void Node::refreshLeader(const std::string& reason) {
    const int candidate = computeLeaderCandidate();
    int previous = -1;

    {
        std::lock_guard<std::mutex> guard(leaderMutex);
        previous = leaderNodeID;
        leaderNodeID = candidate;
    }

    if (candidate != previous && candidate != -1) {
        logMessage("[Leader Election] Leader is now Node " + std::to_string(candidate) +
                   (reason.empty() ? "" : " (" + reason + ")"));
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

json Node::buildNetworkSnapshot() const {
    const std::vector<float> localTrust = getLocalTrustSnapshot();
    const std::vector<float> aggregatedTrust = getBroadcastTrustVector();

    int currentLeader = -1;
    {
        std::lock_guard<std::mutex> guard(leaderMutex);
        currentLeader = leaderNodeID;
    }

    json nodes = json::array();
    json trustMatrix = json::array();

    std::lock_guard<std::mutex> guard(registryMutex);
    for (int i = 0; i < kNodeCount; ++i) {
        auto it = registry.find(i);
        const bool online = it != registry.end();
        std::string mode = "OFFLINE";
        if (online) {
            mode = chaosModeToString(it->second->getChaosMode());
        }

        nodes.push_back({
            {"id", i},
            {"port", NODE_PORTS.at(i)},
            {"online", online},
            {"mode", mode},
            {"leader", i == currentLeader},
            {"trust", aggregatedTrust[i]},
            {"quarantined", aggregatedTrust[i] < kQuarantineThreshold}
        });
    }

    for (const auto& [observer, node] : registry) {
        trustMatrix.push_back({
            {"observerNode", observer},
            {"scores", node->getLocalTrustSnapshot()}
        });
    }

    return json{
        {"nodeID", nodeID},
        {"leaderID", currentLeader},
        {"leaderPort", currentLeader >= 0 ? NODE_PORTS.at(currentLeader) : -1},
        {"localTrust", localTrust},
        {"aggregatedTrust", aggregatedTrust},
        {"quarantineThreshold", kQuarantineThreshold},
        {"nodes", nodes},
        {"trustMatrix", trustMatrix}
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
        refreshLeader("network snapshot requested");
        res.set_content(buildNetworkSnapshot().dump(2), "application/json");
    });

    svr.Get("/leader", [this](const httplib::Request&, httplib::Response& res) {
        refreshLeader("leader endpoint requested");
        int currentLeader = -1;
        {
            std::lock_guard<std::mutex> guard(leaderMutex);
            currentLeader = leaderNodeID;
        }
        const json payload = {
            {"leaderID", currentLeader},
            {"leaderPort", currentLeader >= 0 ? NODE_PORTS.at(currentLeader) : -1}
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

        refreshLeader("manual reset");
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
    while (running) {
        refreshLeader("monitor heartbeat");

        int currentLeader = -1;
        {
            std::lock_guard<std::mutex> guard(leaderMutex);
            currentLeader = leaderNodeID;
        }

        if (currentLeader != lastObservedLeaderID && currentLeader != -1) {
            logMessage("[Node " + std::to_string(nodeID) + "] Observed leader Node " +
                       std::to_string(currentLeader));
            lastObservedLeaderID = currentLeader;
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

    const std::vector<float> trustView = getBroadcastTrustVector();
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
