
/**
 * main.cpp (Phase 3 - The Launcher)
 *
 * This program's ONLY job is to:
 * 1. Set up the shared network resources (Graph, Address Book).
 * 2. Create and launch all 6 independent Node servers.
 * 3. Keep the main thread alive so the node threads can run.
 */

// --- C++ Libraries ---
#include <iostream>
#include <vector>
#include <string>
#include <chrono> // For std::this_thread::sleep_for
#include <thread> // For std::this_thread::sleep_for
#include <map>
#include <memory> // For std::unique_ptr and std::make_unique

// --- Our Project Headers ---
#include "Node.hpp"            // The new "Brain" class
#include "graph_routing.hpp" // The global network map
#include "hashmap.hpp"       // The global address book
#include "logger.hpp"        // Your logger
#include "rsa_signature.hpp" // For generateKeys()

// --- DEFINE THE GLOBAL LOGGER MACRO ---
#define PRINT(x) logger.Log(x)

// --- DEFINE THE GLOBAL PORT MAP ---
// This DEFINES the map that Node.hpp declared as 'extern'.
// This is the *only* place it is created.
std::map<int, int> NODE_PORTS = {
    {0, 8080},
    {1, 8081},
    {2, 8082},
    {3, 8083},
    {4, 8084},
    {5, 8085}
};


int main() {
    // --- 1. Simulation Setup ---
    PRINT("--- ADIPE Network Engine ---\n");
    PRINT("[Setup] Setting up world...\n");

    // Create the global, shared network map
    Graph network(6); // 6 nodes, 0-5
    network.addEdge(0, 1);
    network.addEdge(1, 3);
    network.addEdge(3, 5);
    network.addEdge(0, 2);
    network.addEdge(2, 4);
    network.addEdge(4, 5);
    PRINT("[Setup] Network graph created.\n");

    // Create the global, shared address book
    MetadataMap addressBook;
    PRINT("[Setup] Global address book created.\n");

    // --- 2. Create and Start all Nodes ---
    
    // Store nodes as unique_ptrs on the heap (Node is non-copyable)
    std::vector<std::unique_ptr<Node>> nodes;

    for (int i = 0; i < 6; ++i) {
        nodes.push_back(std::make_unique<Node>(i, network, addressBook));
    }
    PRINT("[Setup] All 6 nodes created.\n");

    // Start all node threads (server + worker)
    for (auto& node_ptr : nodes) {
        node_ptr->start();
    }
    
    PRINT("\n--- C++ Network Engine is LIVE ---\n");
    PRINT("All nodes running. Inject packets via POST http://127.0.0.1:" + std::to_string(NODE_PORTS.at(0)) + "/inject\n");

    // --- 3. Keep the Main Thread Alive ---
    // If main() exits, the entire program (and all node threads)
    // will be terminated. We must keep it running forever.
    while (true) {
        // Sleep for a long time to prevent this loop from
        // using any CPU power.
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    return 0; // This line will never be reached
}