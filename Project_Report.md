# Project Report: ADIPE (Algorithmic Data Integrity and Prioritization Engine)

## 1. Introduction
ADIPE is a robust, 6-node distributed systems simulation designed to test, visualize, and demonstrate various networking and security concepts. Developed in C++17 for the backend and React (via Vite) for the frontend dashboard, ADIPE acts as a secure priority-aware packet router. Over its development phases, it has evolved to include a Byzantine-style control plane featuring trust scoring, chaos injection, autonomous probes, adaptive routing, and bully-style leader election over real HTTP control messages.

## 2. Architecture and Topology
The core engine instantiates 6 unique nodes, each running its own HTTP server on sequential ports (8080 to 8085).
Each node maintains:
- An inbox priority queue for packets.
- A dedicated worker thread for packet processing.
- A monitor thread for handling control-plane operations.
- A unique RSA identity for cryptographic signing.
- A local trust vector that keeps track of the health and reliability of all nodes in the cluster.

### Network Topology
```text
Node 0 (:8080) --- Node 1 (:8081) --- Node 3 (:8083) --- Node 5 (:8085)
    |                                                          |
Node 2 (:8082) --- Node 4 (:8084) --------------------------+
```
The design creates two distinct parallel routes from the ingress point (Node 0) to the egress point (Node 5):
- Path A: `0 -> 1 -> 3 -> 5`
- Path B: `0 -> 2 -> 4 -> 5`

This topology allows for dynamic routing and fault-tolerance simulations.

## 3. Key Features

### 3.1 Trust Management and Chaos Injection
ADIPE implements a sophisticated trust management system where every node keeps a localized `trust[6]` vector. Trust is penalized under anomalous conditions such as:
- Forged sender identities.
- RSA signature mismatches.
- Connection failures or high latency.
Trust naturally regenerates through continuous, healthy traffic. 

To simulate real-world Byzantine failures, ADIPE supports several chaos modes (via `POST /chaos`):
- **NORMAL**: Standard operation.
- **TAMPER**: Maliciously alters packet data.
- **SILENT_DROP**: Discards packets without acknowledging failure.
- **DELAY**: Introduces severe latency.
- **FORGE**: Injects packets with spoofed identities.
- **EAVESDROP**: Covertly monitors traffic.

### 3.2 Adaptive Trust-Aware Routing
Instead of traditional Breadth-First Search (BFS), routing in ADIPE relies on a trust-aware implementation of Dijkstra's algorithm. Edge weights in the routing graph are inversely proportional to trust. Nodes that fall below a designated quarantine threshold are actively excluded from the routing graph, ensuring that packets navigate around unreliable or compromised nodes.

### 3.3 Distributed Control Plane and Leader Election
A resilient control plane governs the cluster:
- **Leader Election**: A bully-style election algorithm operates via explicit HTTP messages (e.g., `election`, `coordinator`, `leader-heartbeat`).
- **Trust Reconciliation**: The elected leader is responsible for aggregating signed trust reports from followers, resolving discrepancies using a weighted strategy, and broadcasting authoritative trust snapshots back to the cluster.
- **Epochs/Generations**: Control-plane generations prevent stale, pre-reset election traffic from poisoning current network states.

### 3.4 Autonomous Network Probes
Nodes are capable of autonomously injecting signed probe packets. These probes act as network diagnostics, rewarding successful deliveries (boosting trust) while exposing and penalizing high-latency or degraded network hops.

## 4. Technology Stack

### Backend Engine
- **Language**: C++17
- **Libraries**: `httplib.h` (HTTP server/client), `json.hpp` (JSON parsing), SQLite (persisting trust scores and metadata).
- **Core Modules**: Priority Engine, RSA Signatures, Graph Routing, Packet Store, Hashing.
- **Build System**: CMake

### Frontend Dashboard
- **Framework**: React.js with Vite
- **Features**: Real-time polling of the C++ engine to visualize the leader, chaos modes, quarantined nodes, authoritative trust levels, packet lifecycles, and a shared cluster log.

## 5. Development and Testing
The project implements rigorous automated testing. A JavaScript-based regression suite (`phase6_regression.js`) validates critical system behaviors:
- Leader failover upon the introduction of a rogue leader.
- Trust-aware dynamic rerouting around delayed nodes.
- Reliable forged sender detection.
- Background autonomous probe traffic health.

## 6. Conclusion and Future Work
ADIPE serves as a comprehensive educational and experimental platform for distributed systems engineering. It successfully simulates complex network behaviors, Byzantine faults, and consensus mechanisms. 
Future development will focus on:
- Adding explicit negative acknowledgements (NACKs) for dropped probes.
- Enhancing persistence to include control-plane history.
- Upgrading cryptographic primitives from educational RSA to production-grade security.

---
*Report generated automatically for ADIPE.*
