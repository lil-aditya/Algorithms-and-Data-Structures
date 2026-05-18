#pragma once

#include <map>
#include <queue>
#include <vector>

class Graph {
private:
    std::map<int, std::vector<int>> adjList;
    int numNodes;

public:
    explicit Graph(int nodes);

    void addEdge(int u, int v);
    void BFS(int startNode) const;

    std::vector<int> findShortestPath(int startNode, int endNode) const;
    std::vector<int> findTrustedPath(int startNode, int endNode,
                                     const std::vector<float>& trustScores,
                                     float quarantineThreshold) const;
};
