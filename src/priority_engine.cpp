#include "priority_engine.hpp"

void PriorityEngine::push(const DataPacket& p) {
    pq.push(p);
}

std::optional<DataPacket> PriorityEngine::pop() {
    if (pq.empty()) {
        return std::nullopt;
    }

    DataPacket highPriorityPacket = pq.top();
    pq.pop();
    return highPriorityPacket;
}

bool PriorityEngine::empty() const {
    return pq.empty();
}

size_t PriorityEngine::size() const {
    return pq.size();
}

void PriorityEngine::clear() {
    pq = std::priority_queue<DataPacket>();
}
