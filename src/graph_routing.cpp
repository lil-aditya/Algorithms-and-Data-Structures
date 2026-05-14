
#include "graph_routing.hpp"
#include <algorithm>
using namespace std;

Graph::Graph(int nodes): numNodes(nodes){}

void Graph::addEdge(int u, int v) {
    if (u<0 || u>=numNodes || v<0 || v>=numNodes) return;
    adjList[u].push_back(v);
    adjList[v].push_back(u);
}

void Graph::BFS(int startNode){
    if (startNode < 0 || startNode >= numNodes) return;
    queue<int> q;
    map<int,bool> vis;
    q.push(startNode);
    vis[startNode] = true;
    while(!q.empty()) {
        int u=q.front(); q.pop();
        cout<<u<<" ";
        for (int v: adjList[u]) if(!vis[v]) { vis[v]=true; q.push(v); }
    }
}

vector<int> Graph::findShortestPath(int startNode, int endNode) {
    vector<int> path;
    if (startNode < 0 || startNode >= numNodes || endNode < 0 || endNode >= numNodes) {
        return path; // Return empty path on invalid input
    }

    queue<int> q;
    map<int, bool> visited; // Tracks visited nodes
    map<int, int> parent;   // Tracks the path: parent[child] = parent

    q.push(startNode);
    visited[startNode] = true;
    parent[startNode] = -1; // -1 indicates no parent (the start)

    bool found = false;
    while (!q.empty()) {
        int u = q.front();
        q.pop();

        if (u == endNode) {
            found = true;
            break; // Found the destination
        }

        // Iterate over neighbors
        for (int v : adjList[u]) {
            if (!visited[v]) {
                visited[v] = true;
                parent[v] = u; // Set the parent
                q.push(v);
            }
        }
    }

    // Now, backtrack from endNode to startNode to build the path
    if (found) {
        int current = endNode;
        while (current != -1) {
            path.push_back(current);
            current = parent[current]; // Move to the parent
        }
        // The path is currently [end, ..., start], so we must reverse it
        reverse(path.begin(), path.end());
    }

    return path; // Returns [start, ..., end] or an empty vector if no path
}
