#include "Node.hpp"
#include <chrono> // For std::this_thread::sleep_for

// --- Define the static (global) log members ---
// These are shared by all Node objects
std::vector<std::string> Node::sharedLog;
std::mutex Node::logMutex;

// --- Constructor ---
// Initializes the node's state
Node::Node(int id, Graph& graph, MetadataMap& book, PacketStore& store)
    : nodeID(id), networkMap(graph), addressBook(book), packetStore(store), running(true) {
    
    // Each node generates its own unique identity (keys)
    nodeKeys = generateKeys(id);
    
    // Add this node's *public* keys to the global address book
    // so other nodes can verify its packets in the future.
    std::string id_str = std::to_string(nodeID);
    addressBook.insert(id_str + "_pub_e", std::to_string(nodeKeys.e));
    addressBook.insert(id_str + "_pub_n", std::to_string(nodeKeys.n));
}

// --- Destructor ---
// Gracefully shuts down the node
Node::~Node() {
    running = false; // Signal threads to stop
    svr.stop();      // Stop the HTTP server
    
    // Wait for threads to finish their current loop
    if (serverThread.joinable()) serverThread.join();
    if (workerThread.joinable()) workerThread.join();
}

// --- Start ---
// Launches the node's two main threads
void Node::start() {
    logMessage("[Node " + std::to_string(nodeID) + "] Starting...");
    // Launch the server thread (to listen) and the worker thread (to process)
    serverThread = std::thread(&Node::runServer, this);
    workerThread = std::thread(&Node::runWorker, this);
}

// --- logMessage ---
// A thread-safe way to log to both console and the shared UI log
void Node::logMessage(const std::string& msg) {
    // Lock the mutex to prevent multiple threads from writing at the same time
    std::lock_guard<std::mutex> guard(logMutex);
    
    // Print to the C++ console
    std::cout << msg << std::endl; 
    
    // Add to the shared log for the dashboard/API to fetch
    sharedLog.push_back(msg);
}

// ===========================================================================
// Thread 1: The Server (Listens for packets)  —  "Producer"
// ===========================================================================
void Node::runServer() {
    // Apply one consistent CORS policy to every response so the React
    // dashboard can poll and preflight cleanly from a different localhost port.
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    auto cors_handler = [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.status = 204; // No Content
    };

    // Explicitly add OPTIONS for every endpoint (since regex is disabled by default)
    svr.Options("/inject", cors_handler);
    svr.Options("/packet", cors_handler);
    svr.Options("/packets", cors_handler);
    svr.Options("/status", cors_handler);
    svr.Options("/log", cors_handler);
    svr.Options("/check", cors_handler);

    // --- Endpoint 1: Node-to-Node communication ---
    // Used when another node forwards a packet to this node.
    svr.Post("/packet", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            DataPacket p = json::parse(req.body);
            
            // Track: packet RECEIVED at this intermediate node
            packetStore.updateStatus(p.id, "RECEIVED", nodeID,
                "Received from forwarding node");

            {
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(p);
            }

            // Track: packet QUEUED in this node's priority inbox
            packetStore.updateStatus(p.id, "QUEUED", nodeID,
                "Queued with urgency " + std::to_string(p.urgency));
            
            logMessage("[Node " + std::to_string(nodeID) + "] Received packet " + p.id + " from sender " + p.senderID);
            res.set_content("{\"status\": \"ACK\"}", "application/json");
        
        } catch (json::parse_error& e) {
            logMessage("[Node " + std::to_string(nodeID) + "] Received invalid JSON. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    // --- Endpoint 2: External Packet Injection ---
    // Used by external clients (curl, frontend) to inject
    // a *new* packet into the network starting at this node.
    svr.Post("/inject", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            DataPacket p = json::parse(req.body);
            logMessage("[Node " + std::to_string(nodeID) + "] === INJECTION RECEIVED ===");
            logMessage("[Node " + std::to_string(nodeID) + "] New packet " + p.id + " for destination " + std::to_string(p.destinationID));

            // Track: initialize the packet's lifecycle record
            packetStore.initPacket(p.id, nodeID, p.destinationID, p.urgency);

            // Sign the packet with this node's private key
            p.senderID = std::to_string(nodeID);
            p.signature = signData(p.data, nodeKeys);
            logMessage("[Node " + std::to_string(nodeID) + "] Packet signed by node " + p.senderID + " with signature: " + std::to_string(p.signature));

            // Track: SIGNED
            packetStore.updateStatus(p.id, "SIGNED", nodeID,
                "signature: " + std::to_string(p.signature));

            // Push to our *own* inbox to start the journey
            {
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(p);
            }

            // Track: QUEUED
            packetStore.updateStatus(p.id, "QUEUED", nodeID,
                "Queued with urgency " + std::to_string(p.urgency));

            res.set_content("{\"status\": \"Packet Signed and Injected\"}", "application/json");
        
        } catch (json::parse_error& e) {
            logMessage("[Node " + std::to_string(nodeID) + "] Received invalid JSON from UI. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    // --- Endpoint 3: Log Retrieval ---
    // Returns the shared log for dashboard or debugging
    svr.Get("/log", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json j;
        {
            std::lock_guard<std::mutex> guard(logMutex);
            j = sharedLog; 
        }
        res.set_content(j.dump(), "application/json");
    });
    
    // --- Endpoint 4: Health Check ---
    // External clients call this to verify the node is online
    svr.Get("/check", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content("{\"status\": \"ONLINE\"}", "application/json");
    });

    // --- Endpoint 5: Single Packet Status Query ---
    // GET /status?id=pkt_001 → returns full lifecycle of that packet
    svr.Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
        std::string packetID = req.get_param_value("id");
        if (packetID.empty()) {
            res.set_content("{\"error\": \"Missing 'id' query parameter. Use /status?id=pkt_001\"}", 400, "application/json");
            return;
        }
        json result = packetStore.getPacket(packetID);
        res.set_content(result.dump(2), "application/json");
    });

    // --- Endpoint 6: All Packets Dashboard ---
    // GET /packets → returns all tracked packets and their current statuses
    svr.Get("/packets", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json result = packetStore.getAllPackets();
        res.set_content(result.dump(2), "application/json");
    });

    // Start the server on its unique port
    logMessage("[Node " + std::to_string(nodeID) + "] Server listening on port " + std::to_string(NODE_PORTS.at(nodeID)));
    if (!svr.listen("0.0.0.0", NODE_PORTS.at(nodeID))) {
         logMessage("[Node " + std::to_string(nodeID) + "] FAILED to bind to port " + std::to_string(NODE_PORTS.at(nodeID)));
    }
}

// ===========================================================================
// Thread 2: The Worker (Processes inbox)  —  "Consumer"
// ===========================================================================
void Node::runWorker() {
    while (running) {
        std::optional<DataPacket> p_opt;

        // --- Critical Section: Check Inbox ---
        {
            std::lock_guard<std::mutex> guard(inboxMutex);
            if (!inbox.empty()) {
                p_opt = inbox.pop();
            }
        }

        if (p_opt) {
            processPacket(*p_opt);
        } else {
            // No packets — sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ===========================================================================
// The Core AQoS Logic  —  verify, route, forward/deliver
// ===========================================================================
void Node::processPacket(DataPacket packet) {
    logMessage("[Node " + std::to_string(nodeID) + "] Processing packet " + packet.id);

    // Track: PROCESSING
    packetStore.updateStatus(packet.id, "PROCESSING", nodeID,
        "Worker thread picked up packet");

    // 1. LOOKUP sender's public key from the global address book
    std::string e_key = packet.senderID + "_pub_e";
    std::string n_key = packet.senderID + "_pub_n";
    std::string e_str = addressBook.get(e_key);
    std::string n_str = addressBook.get(n_key);

    // Check if the key even exists
    if (e_str.empty() || n_str.empty()) {
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. No public key for sender " + packet.senderID + ". DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "No public key found for sender " + packet.senderID);
        return;
    }
    
    // Convert keys from string back to numbers
    Keys senderPubKey;
    try {
        senderPubKey.e = std::stoull(e_str);
        senderPubKey.n = std::stoull(n_str);
    } catch (const std::exception& e) {
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. Invalid key format. DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Invalid key format in address book");
        return;
    }

    // 2. VERIFY signature (The "Authenticated" part)
    bool isValid = verifySignature(packet.data, packet.signature, senderPubKey);

    if (!isValid) {
        logMessage("[Node " + std::to_string(nodeID) + "] Signature FAILED. Data/Signature mismatch. DROPPING.");
        packetStore.updateStatus(packet.id, "DROPPED", nodeID,
            "Signature mismatch — data may be tampered");
        return;
    }
    
    // If we get here, the signature is valid
    logMessage("[Node " + std::to_string(nodeID) + "] Signature VALID. Packet trusted.");

    // Track: VERIFIED
    packetStore.updateStatus(packet.id, "VERIFIED", nodeID,
        "Signature verified against sender " + packet.senderID);

    // 3. DECISION (Forward or Deliver)
    if (nodeID == packet.destinationID) {
        // This is the final destination
        logMessage("[Node " + std::to_string(nodeID) + "] Packet DELIVERED to final destination.");

        // Track: DELIVERED
        packetStore.updateStatus(packet.id, "DELIVERED", nodeID,
            "Packet reached destination Node " + std::to_string(nodeID));
    
    } else {
        // This is an intermediate node — forward it
        std::vector<int> path = networkMap.findShortestPath(nodeID, packet.destinationID);
        
        if (path.size() < 2) {
            logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. No path to destination " + std::to_string(packet.destinationID) + ". DROPPING.");
            packetStore.updateStatus(packet.id, "DROPPED", nodeID,
                "No route to destination " + std::to_string(packet.destinationID));
            return;
        }
        
        int nextHopID = path[1]; 
        int nextHopPort = NODE_PORTS.at(nextHopID);

        logMessage("[Node " + std::to_string(nodeID) + "] Forwarding packet to next hop: Node " + std::to_string(nextHopID) + " (Port " + std::to_string(nextHopPort) + ")");

        // Track: FORWARDED
        packetStore.updateStatus(packet.id, "FORWARDED", nodeID,
            "Node " + std::to_string(nodeID) + " -> Node " + std::to_string(nextHopID));

        // --- Send Packet to Next Node ---
        httplib::Client cli("127.0.0.1", nextHopPort);
        cli.set_connection_timeout(1); 
        
        if (auto res = cli.Post("/packet", json(packet).dump(), "application/json")) {
            if (res->status != 200) {
                logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. Node " + std::to_string(nextHopID) + " responded with error " + std::to_string(res->status));
                packetStore.updateStatus(packet.id, "DROPPED", nodeID,
                    "Next hop Node " + std::to_string(nextHopID) + " returned HTTP " + std::to_string(res->status));
            }
        } else {
            logMessage("[Node " + std::to_string(nodeID) + "] Forwarding FAILED. Could not connect to Node " + std::to_string(nextHopID));
            packetStore.updateStatus(packet.id, "DROPPED", nodeID,
                "Connection refused by Node " + std::to_string(nextHopID));
        }
    }
}
