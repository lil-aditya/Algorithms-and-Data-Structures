
#pragma once

// --- C++17/11 Libraries ---
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>
#include <map>

// --- Our Project Components ---
#include "priority_engine.hpp"
#include "graph_routing.hpp"
#include "hashmap.hpp"
#include "rsa_signature.hpp"
#include "packet_store.hpp"
// NOTE: We do not need logger.hpp here, only in Node.cpp

// --- New 3rd Party Dependencies ---
#include "httplib.h" 
#include "json.hpp"

// Alias for convenience
using json = nlohmann::json;

// --- Global Port Mapping ---
// This DECLARES the global port map, telling other files it exists.
// It will be DEFINED (created) in main.cpp
extern std::map<int, int> NODE_PORTS;


/**
 * @class Node
 * @brief Represents a single, independent, threaded node in the network.
 * It runs its own server to listen for packets and a worker to process them.
 */
class Node {
public:
    /**
     * @brief Constructs a new Node.
     * @param id This node's unique ID (e.g., 0, 1, 2...).
     * @param graph A reference to the global network map.
     * @param book A reference to the global address book (for public keys).
     * @param store A reference to the shared packet lifecycle store.
     */
    Node(int id, Graph& graph, MetadataMap& book, PacketStore& store);
    
    /**
     * @brief Destructor. Gracefully shuts down the node's threads.
     */
    ~Node();

    /**
     * @brief Starts the node's server and worker threads.
     */
    void start();

    // --- Static Log for Web Visualization ---
    // This log is shared by ALL nodes to send status updates to the UI.
    static std::vector<std::string> sharedLog;
    static std::mutex logMutex; // Protects the shared log from race conditions

private:
    // --- Node State ---
    int nodeID;
    Keys nodeKeys; // This node's *own* private/public key pair
    PriorityEngine inbox;
    Graph& networkMap;
    MetadataMap& addressBook;
    PacketStore& packetStore;

    // --- Concurrency Components ---
    std::thread serverThread; // Thread for listening for HTTP packets
    std::thread workerThread; // Thread for processing the inbox
    std::mutex inboxMutex;    // Protects the 'inbox' from simultaneous access
    bool running;             // Flag to signal threads to stop

    // --- Networking Component ---
    httplib::Server svr; // The embedded HTTP server for this node

    // --- Private Methods ---
    
    /**
     * @brief The function run by the serverThread. Listens for HTTP requests.
     */
    void runServer();   
    
    /**
     * @brief The function run by the workerThread. Processes the priority queue.
     */
    void runWorker();   
    
    /**
     * @brief The core AQoS logic: Verifies and forwards a single packet.
     */
    void processPacket(DataPacket packet); 
    
    /**
     * @brief Thread-safe logging. Prints to console and saves to shared log.
     */
    void logMessage(const std::string& msg);
};