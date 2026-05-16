# ADIPE — Algorithmic Data Integrity and Prioritization Engine

A distributed, secure, priority-aware message routing system built in C++17.

## What It Does

ADIPE runs a network of 6 nodes. Each node is an independent HTTP server with its own thread, inbox, and RSA key pair. When a packet enters the system:

1. **Signed** — The injecting node signs the packet with its private key
2. **Queued** — The packet is pushed into a priority queue (higher urgency = processed first)
3. **Verified** — The worker thread pops the packet and verifies its signature using the sender's public key
4. **Routed** — BFS finds the shortest path to the destination; the packet is forwarded hop-by-hop

## Network Topology

```
Node 0 (:8080) --- Node 1 (:8081) --- Node 3 (:8083) --- Node 5 (:8085)
    |                                                          |
Node 2 (:8082) --- Node 4 (:8084) --------------------------+
```

Two parallel paths from Node 0 to Node 5: `0→1→3→5` and `0→2→4→5`.

## Data Structures & Algorithms Used

| Component | DS/Algorithm | Purpose |
|---|---|---|
| Address Book | Hash Map (`unordered_map`) | O(1) public key lookup |
| Node Inbox | Priority Queue (`priority_queue`) | Process urgent packets first |
| Network Map | Graph (adjacency list) | Model node connectivity |
| Path Finding | BFS | Shortest-path routing |
| Packet Signing | RSA (toy-level) | Authenticate packet origin |
| Modular Math | Fast exponentiation, Extended GCD | Core RSA operations |

## Build

**Step 1: Configure with CMake**
```bash
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
```

**Step 2: Compile the Engine**
```bash
cmake --build .
```

This produces `dsa_project.exe` inside the `build` directory.

## Run

```bash
./build/dsa_project.exe
```

All 6 nodes will start on ports 8080-8085.

## Usage

**Health check** — verify a node is online:
```bash
curl http://127.0.0.1:8080/check
# → {"status": "ONLINE"}
```

**Inject a packet** — send a message from Node 0 to Node 5:
```powershell
Invoke-RestMethod -Uri "http://127.0.0.1:8080/inject" -Method POST -ContentType "application/json" -Body '{"id":"pkt_001","urgency":15,"data":"REBOOT_SERVER","senderID":"0","signature":0,"destinationID":5}'
```

**Query packet lifecycle** — see the full journey of a packet:
```powershell
Invoke-RestMethod http://127.0.0.1:8080/status?id=pkt_001
```

**View all tracked packets** — dashboard view:
```powershell
Invoke-RestMethod http://127.0.0.1:8080/packets
```

**View logs** — raw console output:
```powershell
(Invoke-RestMethod http://127.0.0.1:8080/log) | ForEach-Object { Write-Host $_ }
```

## API Endpoints (per node)

| Method | Path | Description |
|---|---|---|
| `POST` | `/inject` | Inject a new packet (signs it, queues it) |
| `POST` | `/packet` | Node-to-node forwarding (internal) |
| `GET` | `/status?id=xxx` | Full lifecycle of a single packet |
| `GET` | `/packets` | All tracked packets and their statuses |
| `GET` | `/check` | Health check |
| `GET` | `/log` | Retrieve shared console log |

## Project Structure

```
src/
├── main.cpp             # Entry point: creates graph, nodes, runs forever
├── Node.cpp             # Per-node HTTP server + worker + packet processing
├── packet_store.cpp     # Thread-safe packet lifecycle tracker
├── graph_routing.cpp    # Graph (adjacency list) + BFS shortest path
├── priority_engine.cpp  # Priority queue wrapper
├── rsa_signature.cpp    # Toy RSA sign/verify with per-node distinct keys
├── hashing.cpp          # Simple rolling hash
├── hashmap.cpp          # unordered_map wrapper (address book)
├── number_theory.cpp    # Modular exponentiation, extended GCD
└── logger.cpp           # File + console logger

include/
├── Node.hpp             # Node class definition
├── packet_store.hpp     # PacketRecord, StatusEvent, PacketStore class
├── priority_engine.hpp  # DataPacket struct + PriorityEngine class
├── graph_routing.hpp    # Graph class
├── hashmap.hpp          # MetadataMap class
├── rsa_signature.hpp    # Keys struct + sign/verify declarations
├── hashing.hpp          # Hash function declaration
├── number_theory.hpp    # Math utility declarations
├── logger.hpp           # Logger class
├── httplib.h            # cpp-httplib (3rd-party, single-header)
└── json.hpp             # nlohmann/json (3rd-party, single-header)

adipe-frontend/         # React/Vite UI prototype (not yet connected)
```

## Technical Notes

- **Cryptography is toy-level.** Keys are small enough for `uint64_t`. For production use, use OpenSSL or libsodium.
- **Each node has a unique RSA key pair** derived from distinct primes, stored in a pre-computed pool.
- **Threading model:** Each node runs 2 threads — an HTTP server thread and a worker thread — with mutex-protected shared state.
- **Dependencies:** [cpp-httplib](https://github.com/yhirose/cpp-httplib) and [nlohmann/json](https://github.com/nlohmann/json), both single-header.


AT THIS STAGE OF PHASE 1:
Steps to verify:

1) Clone the repo and go to the project directory

2) Open terminal and write 'mkdir build -Force'

3) Run the '.\build\dsa_project.exe' (You should see all 6 nodes start and bind to ports 8080–8085.)

4) open a NEW terminal and do the below forthe health check status
and run commands:
powershell
curl http://127.0.0.1:8080/check
curl http://127.0.0.1:8083/check
curl http://127.0.0.1:8085/check

5) Run the command for injecting the packet:
Invoke-RestMethod -Uri "http://127.0.0.1:8080/inject" -Method POST -ContentType "application/json" -Body '{"id":"test_002","urgency":20,"data":"URGENT_MSG","senderID":"0","signature":0,"destinationID":5}'

6) Check the logs by '(Invoke-RestMethod http://127.0.0.1:8080/log) | ForEach-Object { Write-Host $_ }' in the powershell

7) Stop the engine by pressing 'Ctrl+C' in the terminal where the engine is running