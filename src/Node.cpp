#include "Node.hpp"
#include <chrono> // For std::this_thread::sleep_for

// --- Define the static (global) log members ---
// These are shared by all Node objects
std::vector<std::string> Node::sharedLog;
std::mutex Node::logMutex;

// --- Constructor ---
// Initializes the node's state
Node::Node(int id, Graph& graph, MetadataMap& book)
    : nodeID(id), networkMap(graph), addressBook(book), running(true) {
    
    // Each node generates its own unique identity (keys)
    nodeKeys = generateKeys(id);
    
    // Add this node's *public* keys to the global address book
    // so other nodes can verify its packets in the future.
    // (We use std::to_string to convert the numbers to strings for the map)
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

// --- Thread 1: The Server (Listens for packets) ---
// This is the "Producer"
void Node::runServer() {
    
    // --- Endpoint 1: Node-to-Node communication ---
    // This endpoint is used when another node forwards a packet to this node.
    svr.Post("/packet", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            // 1. Deserialize JSON text into a C++ DataPacket struct
            DataPacket p = json::parse(req.body);
            
            // 2. Lock the inbox and push the new packet
            {
                // The lock_guard automatically locks/unlocks the mutex
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(p); // Push onto the priority queue
            }
            
            logMessage("[Node " + std::to_string(nodeID) + "] Received packet " + p.id + " from sender " + p.senderID);
            res.set_content("{\"status\": \"ACK\"}", "application/json"); // Send "OK"
        
        } catch (json::parse_error& e) {
            logMessage("[Node " + std::to_string(nodeID) + "] ❌ Received invalid JSON. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    // --- Endpoint 2: External Packet Injection ---
    // This endpoint is used by external clients (curl, frontend) to inject
    // a *new* packet into the network starting at this node.
    svr.Post("/inject", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            DataPacket p = json::parse(req.body);
            logMessage("[Node " + std::to_string(nodeID) + "] === INJECTION RECEIVED ===");
            logMessage("[Node " + std::to_string(nodeID) + "] New packet " + p.id + " from UI for " + p.senderID);

            // This node signs the data with its own private key
            // and stamps itself as the sender so verification works
            p.senderID = std::to_string(nodeID);
            p.signature = signData(p.data, nodeKeys);
            logMessage("[Node " + std::to_string(nodeID) + "] Packet signed by node " + p.senderID + " with signature: " + std::to_string(p.signature));

            // Push to our *own* inbox to start the journey
            {
                std::lock_guard<std::mutex> guard(inboxMutex);
                inbox.push(p);
            }

            res.set_content("{\"status\": \"Packet Signed and Injected\"}", "application/json");
        
        } catch (json::parse_error& e) {
            logMessage("[Node " + std::to_string(nodeID) + "] ❌ Received invalid JSON from UI. Discarding.");
            res.set_content("{\"error\": \"Invalid JSON\"}", 400, "application/json");
        }
    });

    // --- Endpoint 3: Log Retrieval ---
    // Returns the shared log for dashboard or debugging
    svr.Get("/log", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json j;
        {
            // Lock the log to safely copy it
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

    // Start the server on its unique port
    logMessage("[Node " + std::to_string(nodeID) + "] Server listening on port " + std::to_string(NODE_PORTS.at(nodeID)));
    if (!svr.listen("0.0.0.0", NODE_PORTS.at(nodeID))) {
         logMessage("[Node " + std::to_string(nodeID) + "] ❌ FAILED to bind to port " + std::to_string(NODE_PORTS.at(nodeID)));
    }
}

// --- Thread 2: The Worker (Processes inbox) ---
// This is the "Consumer"
void Node::runWorker() {
    // This loop runs forever (until 'running' is false)
    while (running) {
        std::optional<DataPacket> p_opt;

        // --- Critical Section: Check Inbox ---
        // We only lock the mutex for the *shortest possible time*
        // just to see if the queue is empty and pop one item.
        {
            std::lock_guard<std::mutex> guard(inboxMutex);
            if (!inbox.empty()) {
                p_opt = inbox.pop(); // pop() gets the highest-priority packet
            }
        } // Mutex is released here
        // --- End Critical Section ---

        if (p_opt) {
            // If we *got* a packet, we process it.
            // This happens *outside* the lock, so the server thread
            // can still receive new packets while we do this work.
            processPacket(*p_opt);
        } else {
            // No packets. Sleep for a moment to prevent
            // this thread from using 100% CPU (busy-waiting).
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// --- The Core AQoS Logic ---
// This is the "brain" from your Phase 2 simulation, now running inside the node.
void Node::processPacket(DataPacket packet) {
    logMessage("[Node " + std::to_string(nodeID) + "] Processing packet " + packet.id);

    // 1. LOOKUP sender's public key from the global address book
    std::string e_key = packet.senderID + "_pub_e";
    std::string n_key = packet.senderID + "_pub_n";
    std::string e_str = addressBook.get(e_key);
    std::string n_str = addressBook.get(n_key);

    // Check if the key even exists
    if (e_str.empty() || n_str.empty()) {
        logMessage("[Node " + std::to_string(nodeID) + "] ❌ Signature FAILED. Reason: No public key found for sender " + packet.senderID + ". DROPPING.");
        return; // Drop the packet
    }
    
    // Convert keys from string back to numbers
    Keys senderPubKey;
    try {
        senderPubKey.e = std::stoull(e_str);
        senderPubKey.n = std::stoull(n_str);
    } catch (const std::exception& e) {
        logMessage("[Node " + std::to_string(nodeID) + "] ❌ Signature FAILED. Reason: Invalid key format in address book. DROPPING.");
        return; // Drop the packet
    }


    // 2. VERIFY signature (The "Authenticated" part)
    bool isValid = verifySignature(packet.data, packet.signature, senderPubKey);

    if (!isValid) {
        logMessage("[Node " + std::to_string(nodeID) + "] ❌ Signature FAILED. Reason: Data/Signature mismatch. DROPPING.");
        return; // Drop the packet
    }
    
    // If we get here, the signature is valid.
    logMessage("[Node " + std::to_string(nodeID) + "] ✅ Signature VALID. Packet trusted.");

    // 3. DECISION (Forward or Deliver)
    if (nodeID == packet.destinationID) {
        // This is the final destination
        logMessage("[Node " + std::to_string(nodeID) + "] ✅ Packet DELIVERED to final destination.");
    
    } else {
        // This is an intermediate node. Forward it.
        // Find the next hop using the global graph
        std::vector<int> path = networkMap.findShortestPath(nodeID, packet.destinationID);
        
        if (path.size() < 2) { // path[0] is self, path[1] is next hop
            logMessage("[Node " + std::to_string(nodeID) + "] ❌ Forwarding FAILED. No path to destination " + std::to_string(packet.destinationID) + ". DROPPING.");
            return;
        }
        
        int nextHopID = path[1]; 
        int nextHopPort = NODE_PORTS.at(nextHopID);

        logMessage("[Node " + std::to_string(nodeID) + "] Forwarding packet to next hop: Node " + std::to_string(nextHopID) + " (Port " + std::to_string(nextHopPort) + ")");

        // --- Send Packet to Next Node ---
        // Create a *new* HTTP client to send the packet
        httplib::Client cli("127.0.0.1", nextHopPort);
        cli.set_connection_timeout(1); 
        
        // We re-serialize the *original* packet to forward it.
        // The signature and senderID do not change.
        if (auto res = cli.Post("/packet", json(packet).dump(), "application/json")) {
            // Check if the next node successfully received it
            if (res->status != 200) {
                logMessage("[Node " + std::to_string(nodeID) + "] ❌ Forwarding FAILED. Node " + std::to_string(nextHopID) + " responded with error " + std::to_string(res->status));
            }
        } else {
            // This error (e.g., "Connection refused") means the next node is offline
            logMessage("[Node " + std::to_string(nodeID) + "] ❌ Forwarding FAILED. Could not connect to Node " + std::to_string(nextHopID));
        }
    }
}