# ADIPE

Algorithmic Data Integrity and Prioritization Engine.

ADIPE is a 6-node distributed systems simulation built in C++17 with a React dashboard. It started as a secure priority-aware packet router and now includes a Byzantine-style control plane with trust scoring, chaos injection, autonomous probes, adaptive routing, and bully-style leader election over real HTTP control messages.

## What It Does

Each node runs as its own HTTP server with:

- an inbox priority queue
- a worker thread for packet processing
- a monitor thread for control-plane work
- a unique RSA identity
- a local trust vector over all 6 nodes

Packets are signed, verified hop-by-hop, and routed across this topology:

```text
Node 0 (:8080) --- Node 1 (:8081) --- Node 3 (:8083) --- Node 5 (:8085)
    |                                                          |
Node 2 (:8082) --- Node 4 (:8084) --------------------------+
```

The network has two parallel routes from Node 0 to Node 5:

- `0 -> 1 -> 3 -> 5`
- `0 -> 2 -> 4 -> 5`

## Phase 6 Features

### Trust and Chaos

- Every node keeps `trust[6]` locally.
- Trust drops on forged sender identity, signature mismatch, connection failure, and high latency.
- Trust recovers slowly from fresh healthy traffic.
- Chaos modes are available per node through `POST /chaos`:
  - `NORMAL`
  - `TAMPER`
  - `SILENT_DROP`
  - `DELAY`
  - `FORGE`
  - `EAVESDROP`

### Adaptive Routing

- Routing uses trust-aware Dijkstra instead of plain BFS.
- Edge cost is inversely proportional to trust.
- Nodes below the quarantine threshold are excluded from routing.

### Distributed Control Plane

- Nodes elect a leader with bully-style election over explicit HTTP messages.
- Election flow uses signed `election`, `coordinator`, and `leader-heartbeat` control messages.
- The leader collects signed trust reports from followers.
- The leader reconciles conflicting trust reports with a conservative weighted strategy and broadcasts authoritative trust snapshots.
- Control-plane generations fence off stale pre-reset election traffic.

### Autonomous Probes

- Nodes periodically inject signed probe packets on their own.
- Probe traffic rewards healthy delivery and exposes high-latency or degraded hops.

## Dashboard

The React frontend is live and connected. It polls the C++ engine and shows:

- current leader
- node chaos modes
- quarantined nodes
- authoritative trust levels
- observer trust matrix
- packet lifecycles
- shared cluster log

Main frontend surfaces:

- `ChaosControls`
- `TrustMatrix`
- `Topology`
- `StatsBar`

## Build

### Backend

```powershell
mkdir build -Force
cd build
cmake ..
cmake --build .
cd ..
```

This produces `build/dsa_project.exe` on Windows.

### Frontend

```powershell
cd adipe-frontend
npm install
npm run build
cd ..
```

## Run

### Backend engine

```powershell
.\build\dsa_project.exe
```

This starts all 6 nodes on ports `8080` through `8085`.

### Frontend dashboard

```powershell
cd adipe-frontend
npm run dev
```

Open the Vite URL shown in the terminal, usually `http://localhost:5173`.

## Core API

### User-facing packet APIs

| Method | Path | Description |
|---|---|---|
| `POST` | `/inject` | Inject a new packet into the network |
| `POST` | `/packet` | Internal hop forwarding endpoint |
| `GET` | `/status?id=...` | Lifecycle for one packet |
| `GET` | `/packets` | All tracked packets |
| `GET` | `/log` | Shared cluster log |
| `GET` | `/check` | Node health and chaos mode |

### Phase 6 APIs

| Method | Path | Description |
|---|---|---|
| `GET` | `/network` | Node states, leader view, trust matrix, quarantines |
| `GET` | `/leader` | Leader ID, leader port, election epoch |
| `POST` | `/chaos?mode=...` | Flip a node into a rogue mode |
| `POST` | `/reset` | Reset trust, chaos state, logs, packet history, and control plane |

### Internal control-plane APIs

These are node-to-node protocol endpoints used by the control plane:

- `POST /election`
- `POST /coordinator`
- `POST /leader-heartbeat`
- `POST /trust-report`
- `POST /trust-sync`

## Example Usage

### Inject a packet

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:8080/inject" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"id":"pkt_001","urgency":20,"data":"SYNC_DATA","senderID":"0","signature":0,"destinationID":5}'
```

### Inspect network state

```powershell
Invoke-RestMethod http://127.0.0.1:8080/network | ConvertTo-Json -Depth 6
```

### Force a rogue node

```powershell
Invoke-RestMethod -Method POST "http://127.0.0.1:8081/chaos?mode=DELAY"
```

### Reset the cluster

```powershell
Invoke-RestMethod -Method POST http://127.0.0.1:8080/reset
```

## Regression Suite

Phase 6 now has a scripted regression pass:

```powershell
node .\scripts\phase6_regression.js
```

The suite checks:

- leader failover after a rogue leader
- trust-aware rerouting away from a delayed node
- forged sender detection and trust punishment
- background autonomous probe traffic

## Project Structure

```text
src/
├── main.cpp
├── Node.cpp
├── graph_routing.cpp
├── packet_store.cpp
├── priority_engine.cpp
├── rsa_signature.cpp
├── hashing.cpp
├── hashmap.cpp
├── number_theory.cpp
├── logger.cpp
└── sqlite3.c

include/
├── Node.hpp
├── graph_routing.hpp
├── packet_store.hpp
├── priority_engine.hpp
├── rsa_signature.hpp
├── hashing.hpp
├── hashmap.hpp
├── number_theory.hpp
├── logger.hpp
├── httplib.h
└── json.hpp

adipe-frontend/
├── src/
└── package.json

scripts/
└── phase6_regression.js
```

## Technical Notes

- RSA is toy-level and intentionally educational, not production crypto.
- Trust scores are persisted in SQLite.
- Packet metadata carries provenance and hop timing.
- The leader election is still bully-style, not full Byzantine consensus.
- Trust reconciliation is stronger than the earlier min-across-process shortcut, but it is still a simulation rather than a formally proven BFT protocol.

## Good Next Steps

- Add explicit negative acknowledgements for dropped probes.
- Persist control-plane history alongside packet history.
- Surface leader epoch, trust version, and probe activity more prominently in the dashboard.
- Add a replayable demo script for interview walkthroughs.
