#pragma once
#include <map>
#include <vector>
#include <queue>
#include <iostream>

class Graph {
private:
    std::map<int, std::vector<int>> adjList;
    int numNodes;
public:
    Graph(int nodes);
    void addEdge(int u, int v);
    void BFS(int startNode);

    std::vector<int> findShortestPath(int startNode, int endNode);
};
