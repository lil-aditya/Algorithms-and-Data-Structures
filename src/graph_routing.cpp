#include "graph_routing.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

Graph::Graph(int nodes) : numNodes(nodes) {}

void Graph::addEdge(int u, int v) {
    if (u < 0 || u >= numNodes || v < 0 || v >= numNodes) {
        return;
    }

    adjList[u].push_back(v);
    adjList[v].push_back(u);
}

void Graph::BFS(int startNode) const {
    if (startNode < 0 || startNode >= numNodes) {
        return;
    }

    std::queue<int> q;
    std::map<int, bool> visited;
    q.push(startNode);
    visited[startNode] = true;

    while (!q.empty()) {
        int u = q.front();
        q.pop();
        std::cout << u << " ";

        auto it = adjList.find(u);
        if (it == adjList.end()) {
            continue;
        }

        for (int v : it->second) {
            if (!visited[v]) {
                visited[v] = true;
                q.push(v);
            }
        }
    }
}

std::vector<int> Graph::findShortestPath(int startNode, int endNode) const {
    std::vector<int> path;
    if (startNode < 0 || startNode >= numNodes || endNode < 0 || endNode >= numNodes) {
        return path;
    }

    std::queue<int> q;
    std::map<int, bool> visited;
    std::map<int, int> parent;

    q.push(startNode);
    visited[startNode] = true;
    parent[startNode] = -1;

    bool found = false;
    while (!q.empty()) {
        int u = q.front();
        q.pop();

        if (u == endNode) {
            found = true;
            break;
        }

        auto it = adjList.find(u);
        if (it == adjList.end()) {
            continue;
        }

        for (int v : it->second) {
            if (!visited[v]) {
                visited[v] = true;
                parent[v] = u;
                q.push(v);
            }
        }
    }

    if (found) {
        int current = endNode;
        while (current != -1) {
            path.push_back(current);
            current = parent[current];
        }
        std::reverse(path.begin(), path.end());
    }

    return path;
}

std::vector<int> Graph::findTrustedPath(int startNode, int endNode,
                                        const std::vector<float>& trustScores,
                                        float quarantineThreshold) const {
    std::vector<int> path;
    if (startNode < 0 || startNode >= numNodes || endNode < 0 || endNode >= numNodes) {
        return path;
    }

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(numNodes, inf);
    std::vector<int> parent(numNodes, -1);
    using QueueEntry = std::pair<double, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pq;

    dist[startNode] = 0.0;
    pq.push({0.0, startNode});

    while (!pq.empty()) {
        auto [currentDist, u] = pq.top();
        pq.pop();

        if (currentDist > dist[u]) {
            continue;
        }

        if (u == endNode) {
            break;
        }

        auto it = adjList.find(u);
        if (it == adjList.end()) {
            continue;
        }

        for (int v : it->second) {
            if (v != endNode && v != startNode &&
                v < static_cast<int>(trustScores.size()) &&
                trustScores[v] < quarantineThreshold) {
                continue;
            }

            double trust = 1.0;
            if (v < static_cast<int>(trustScores.size())) {
                trust = std::max(0.05, static_cast<double>(trustScores[v]));
            }

            const double edgeWeight = 1.0 / trust;
            const double nextDist = currentDist + edgeWeight;

            if (nextDist < dist[v]) {
                dist[v] = nextDist;
                parent[v] = u;
                pq.push({nextDist, v});
            }
        }
    }

    if (!std::isfinite(dist[endNode])) {
        return path;
    }

    for (int current = endNode; current != -1; current = parent[current]) {
        path.push_back(current);
    }
    std::reverse(path.begin(), path.end());

    return path;
}
