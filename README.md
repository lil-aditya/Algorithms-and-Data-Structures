# ADIPE

**Algorithmic Data Integrity and Prioritization Engine**

A 6-node network simulation written in C++17 where every node is its own HTTP server, has its own RSA keypair, maintains a local trust score for every other node, and routes packets through a priority queue. Nodes elect a leader, sign all control-plane traffic, inject chaos faults on demand, and re-route around quarantined peers — all running on 18 threads across 6 ports.

The React dashboard polls the C++ engine in real time and lets you flip nodes into rogue modes, watch trust decay, trigger elections, and trace every packet hop-by-hop.

**Live demo →** [algodsinnetworking.vercel.app](https://algodsinnetworking.vercel.app/)

---

## Topology

```
Node 0 (:8080) ── Node 1 (:8081) ── Node 3 (:8083) ── Node 5 (:8085)
     │                                                       │
Node 2 (:8082) ── Node 4 (:8084) ───────────────────────────┘
```

Two parallel paths from ingress (Node 0) to egress (Node 5):

- `0 → 1 → 3 → 5`
- `0 → 2 → 4 → 5`

When a node on one path gets quarantined, Dijkstra picks the other path automatically.

---

## How each node works

Every node (`Node.cpp`, ~1800 lines) spawns three threads:

| Thread | Job |
|---|---|
| **Server** | Listens on its port, handles all REST endpoints, receives packets and control messages |
| **Worker** | Pulls the highest-urgency packet from the inbox priority queue, verifies its RSA signature, applies chaos effects, and either delivers it or forwards it to the next hop |
| **Monitor** | Runs the control-plane loop — heartbeats, elections, trust reports, autonomous probes, trust recovery ticks |

The inbox is a `std::priority_queue` keyed on packet urgency, so high-priority traffic always gets processed first regardless of arrival order.

---

## Routing

There are two routing modes in `graph_routing.cpp`:

1. **BFS** — plain shortest path, used as a baseline.
2. **Trust-aware Dijkstra** — edge weight = `1 / trust(neighbor)`. Nodes below the quarantine threshold (0.30) are excluded entirely. This is what the engine actually uses for packet forwarding.

When you flip a node into `DELAY` mode, its trust drops, the Dijkstra cost rises, and traffic re-routes around it without anyone telling it to.

---

## Cryptography

Each node gets a unique RSA keypair at startup from a pre-computed pool of 6 key triplets (derived from distinct 6-digit primes). The crypto is intentionally toy-level — fits in `uint64_t`, no padding, no ASN.1 — but the math is real:

- **`modexp`** — modular exponentiation via repeated squaring using `__uint128_t`
- **`egcd`** — extended Euclidean algorithm, computes Bézout coefficients
- **`modInverse`** — modular multiplicative inverse via `egcd`
- **`simpleHash`** — a rolling hash (FNV-style) to map packet data to a digest before signing

Packets are signed at injection (`signData` with private key `d`) and verified at every hop (`verifySignature` with public key `e`). If the signature doesn't match, the packet is dropped and the last-hop node's trust gets crushed to 0.05.

---

## Trust system

Every node maintains `float trust[6]` locally. Trust changes happen on concrete events:

| Event | Trust delta |
|---|---|
| Healthy packet forwarded | +0.02 |
| Probe delivered successfully | +0.05 to +0.08 |
| Hop latency > 1200ms | −0.20 |
| HTTP error from next hop | −0.40 |
| Connection refused | −0.50 |
| Signature mismatch / forged sender | Set to 0.05 |

Trust recovers slowly (+0.01 per tick) from recent healthy traffic, capped at 0.92 to prevent instant full recovery.

All trust scores are persisted in SQLite (`trust_scores` table) so they survive restarts.

---

## Leader election

Bully-style election over signed HTTP messages:

1. A node notices the leader's heartbeat is missing (timeout = 2400ms) or the leader is quarantined.
2. It sends a signed `POST /election` to every node with a higher ID.
3. If no higher node ACKs as eligible, it declares itself coordinator via `POST /coordinator`.
4. If a higher node ACKs, it waits up to 1500ms for that node to announce itself.
5. The new leader immediately broadcasts a signed heartbeat and a trust sync.

Every election, coordinator, and heartbeat message carries a signature that gets verified against the sender's public key. Stale messages from a previous control-plane generation are rejected (the generation counter increments on every `/reset`).

---

## Trust reconciliation

The leader collects signed trust reports from followers and reconciles them:

1. Filter out reports older than 6500ms.
2. Filter out reporters whose own credibility is below 0.25.
3. For each subject node, collect weighted observations (leader gets 1.25x weight, followers get their credibility as weight).
4. If there are 5+ observations, trim the highest and lowest (robust to outliers).
5. Compute a weighted median and a weighted average.
6. Final score = 55% weighted median + 45% leader's local observation.
7. If 2+ credible observers rate a node below quarantine, pull the score down further.
8. Trust can drop instantly but only recovers by at most +0.04 per reconciliation cycle.

The authoritative trust vector is then broadcast to all nodes via `POST /trust-sync`.

---

## Chaos modes

Hit `POST /chaos?mode=DELAY` on any node's port to flip it into a fault mode:

| Mode | What it does |
|---|---|
| `NORMAL` | Nothing, standard operation |
| `TAMPER` | Appends `::TAMPERED_BY_NODE_X` to the packet data before forwarding — breaks the signature |
| `SILENT_DROP` | Drops every packet without responding — simulates a dead node |
| `DELAY` | Holds every packet for 1600ms before forwarding — triggers high-latency trust penalties |
| `FORGE` | Rewrites the sender ID and re-signs with its own key — simulates identity spoofing |
| `EAVESDROP` | Logs the packet payload but still forwards normally |

Nodes in `TAMPER`, `FORGE`, or `SILENT_DROP` are ineligible for leadership. If the current leader gets flipped into one of these, it steps down immediately and triggers a new election.

---

## Autonomous probes

Each node periodically injects low-urgency probe packets (`PROBE_HEALTH::...`) targeted at other nodes. These serve two purposes:

- Successful delivery rewards trust for every node on the path.
- Failed delivery (dropped, timed out) exposes degraded hops without needing real user traffic.

Probe interval is 6 seconds per node. Nodes in destructive chaos modes skip probe dispatch.

---

## Dashboard

The React frontend (Vite + Tailwind) has these components:

| Component | What it shows |
|---|---|
| `Topology` | The 6-node graph with color-coded trust levels, leader badge, quarantine indicators |
| `TrustMatrix` | 6×6 observer-vs-subject trust grid, updated live |
| `ChaosControls` | Buttons to flip any node into any chaos mode |
| `PacketTracker` | Full lifecycle of every packet — every hop, every signature check, every drop reason |
| `InjectForm` | Form to inject custom packets with arbitrary urgency and destination |
| `LogViewer` | Shared cluster log stream from all 6 nodes |
| `StatsBar` | Current leader, election epoch, active node count |

The frontend polls `GET /network` on Node 0 and renders everything. It's deployed on Vercel at [algodsinnetworking.vercel.app](https://algodsinnetworking.vercel.app/).

---

## API reference

### Packet endpoints

| Method | Path | Description |
|---|---|---|
| `POST` | `/inject` | Inject a packet into the network (signs it with the receiving node's key) |
| `POST` | `/packet` | Internal: receive a forwarded packet from another node |
| `GET` | `/status?id=...` | Get the full lifecycle of a single packet |
| `GET` | `/packets` | Get all tracked packets |
| `GET` | `/log` | Shared cluster log |
| `GET` | `/check` | Node health check (status, chaos mode) |

### Control-plane endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/network` | Full cluster snapshot — all nodes, trust matrix, leader, quarantines |
| `GET` | `/leader` | Current leader ID, port, epoch, last heartbeat |
| `POST` | `/chaos?mode=...` | Set chaos mode for this node |
| `POST` | `/reset` | Reset all trust, chaos, logs, packet history, and trigger a fresh election |

### Internal node-to-node (not for external use)

- `POST /election` — bully election message
- `POST /coordinator` — coordinator announcement with trust snapshot
- `POST /leader-heartbeat` — periodic heartbeat from leader
- `POST /trust-report` — follower sends trust vector to leader
- `POST /trust-sync` — leader broadcasts reconciled trust to all followers

---

## Build

### Backend (C++17, CMake)

```powershell
mkdir build -Force
cd build
cmake ..
cmake --build .
cd ..
```

Produces `build/dsa_project.exe` on Windows.

### Frontend (React, Vite)

```powershell
cd adipe-frontend
npm install
npm run build
cd ..
```

---

## Run

Start the backend:

```powershell
.\build\dsa_project.exe
```

All 6 nodes start on ports 8080–8085. Leader election kicks off automatically.

Start the frontend dev server:

```powershell
cd adipe-frontend
npm run dev
```

Open `http://localhost:5173` in your browser.

---

## Quick test

Inject a packet from Node 0 to Node 5:

```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:8080/inject" `
  -Method POST `
  -ContentType "application/json" `
  -Body '{"id":"pkt_001","urgency":20,"data":"SYNC_DATA","senderID":"0","signature":0,"destinationID":5}'
```

Check the full network state:

```powershell
Invoke-RestMethod http://127.0.0.1:8080/network | ConvertTo-Json -Depth 6
```

Flip Node 1 into delay mode and watch traffic re-route:

```powershell
Invoke-RestMethod -Method POST "http://127.0.0.1:8081/chaos?mode=DELAY"
```

Reset everything:

```powershell
Invoke-RestMethod -Method POST http://127.0.0.1:8080/reset
```

---

## Regression suite

```powershell
node .\scripts\phase6_regression.js
```

Tests cover:
- Leader failover when the leader goes rogue
- Trust-aware rerouting around a delayed node
- Forged sender detection and trust punishment
- Autonomous probe traffic under normal conditions

---

## Project structure

```
src/
├── main.cpp              # Launcher — creates 6 nodes on a shared graph and starts them
├── Node.cpp              # The big one — server, worker, monitor threads, all control-plane logic
├── graph_routing.cpp     # BFS and trust-aware Dijkstra
├── priority_engine.cpp   # Max-heap priority queue for DataPackets
├── rsa_signature.cpp     # Sign and verify using toy RSA
├── number_theory.cpp     # modexp, egcd, modInverse, modMul
├── hashing.cpp           # Rolling hash function
├── hashmap.cpp           # Simple key-value store for the address book
├── packet_store.cpp      # SQLite-backed packet lifecycle and trust persistence
├── logger.cpp            # Thread-safe logger
└── sqlite3.c             # SQLite amalgamation (vendored)

include/
├── Node.hpp              # Node class — 3 threads, trust vectors, chaos modes, election state
├── graph_routing.hpp     # Graph with adjacency list
├── priority_engine.hpp   # DataPacket struct + PriorityEngine class
├── rsa_signature.hpp     # Keys struct, sign/verify declarations
├── number_theory.hpp     # Number theory function declarations
├── hashing.hpp           # simpleHash declaration
├── hashmap.hpp           # MetadataMap (the address book)
├── packet_store.hpp      # PacketStore with SQLite handle
├── logger.hpp            # Logger class
├── httplib.h             # cpp-httplib (vendored, single-header HTTP library)
└── json.hpp              # nlohmann/json (vendored, single-header JSON library)

adipe-frontend/
├── src/
│   ├── App.jsx           # Main app — polling loop, layout
│   ├── api.js            # API client pointing at the C++ backend
│   ├── components/
│   │   ├── Topology.jsx
│   │   ├── TrustMatrix.jsx
│   │   ├── ChaosControls.jsx
│   │   ├── PacketTracker.jsx
│   │   ├── InjectForm.jsx
│   │   ├── LogViewer.jsx
│   │   └── StatsBar.jsx
│   └── index.css
└── package.json

scripts/
└── phase6_regression.js  # Automated regression tests
```

---

## Notes

- The RSA keys are toy-level on purpose. The point is to demonstrate the sign/verify flow, not to be production crypto.
- Trust scores persist in SQLite (`adipe.db`), so you can restart the engine and keep your trust state.
- Packet metadata tracks provenance (origin node, last hop, forwarded-at timestamp) so you can trace the full journey.
- The leader election is bully-style, not full Byzantine consensus. It handles crash faults and chaos faults, but it's a simulation, not a formally verified BFT protocol.
- `Node.cpp` is ~1800 lines. It handles everything from HTTP routing to RSA verification to Dijkstra to trust reconciliation. The rest of the modules are intentionally small and focused.
