import React, { useMemo } from 'react';
import { motion } from 'framer-motion';

const NODE_POSITIONS = [
    { id: 0, x: 80,  y: 140, label: 'Node 0', port: 8080 },
    { id: 1, x: 240, y: 60,  label: 'Node 1', port: 8081 },
    { id: 2, x: 240, y: 220, label: 'Node 2', port: 8082 },
    { id: 3, x: 420, y: 60,  label: 'Node 3', port: 8083 },
    { id: 4, x: 420, y: 220, label: 'Node 4', port: 8084 },
    { id: 5, x: 580, y: 140, label: 'Node 5', port: 8085 },
];

const EDGES = [
    [0, 1], [0, 2], [1, 3], [2, 4], [3, 5], [4, 5]
];

const Topology = ({ nodeHealth, packets }) => {
    // Determine which nodes are "active" based on recent packet activity
    const activeNodeSet = useMemo(() => {
        const set = new Set();
        if (packets && packets.length > 0) {
            // Look at the most recent 3 packets for activity
            const recent = packets.slice(-3);
            recent.forEach(pkt => {
                if (pkt.events && pkt.events.length > 0) {
                    const lastEvent = pkt.events[pkt.events.length - 1];
                    if (pkt.currentStatus !== 'DELIVERED' && pkt.currentStatus !== 'DROPPED') {
                        set.add(lastEvent.nodeID);
                    }
                }
            });
        }
        return set;
    }, [packets]);

    // Count packets per node
    const nodePacketCounts = useMemo(() => {
        const counts = {};
        if (packets) {
            packets.forEach(pkt => {
                if (pkt.events) {
                    pkt.events.forEach(ev => {
                        counts[ev.nodeID] = (counts[ev.nodeID] || 0) + 1;
                    });
                }
            });
        }
        return counts;
    }, [packets]);

    return (
        <div className="topology-container w-full" style={{ height: 300 }}>
            <svg
                viewBox="0 0 660 280"
                className="w-full h-full"
                style={{ filter: 'drop-shadow(0 0 1px rgba(59,130,246,0.2))' }}
            >
                <defs>
                    {/* Glow filter for active edges */}
                    <filter id="edge-glow" x="-20%" y="-20%" width="140%" height="140%">
                        <feGaussianBlur in="SourceGraphic" stdDeviation="3" result="blur" />
                        <feMerge>
                            <feMergeNode in="blur" />
                            <feMergeNode in="SourceGraphic" />
                        </feMerge>
                    </filter>
                    {/* Node glow for active nodes */}
                    <filter id="node-glow" x="-50%" y="-50%" width="200%" height="200%">
                        <feGaussianBlur in="SourceGraphic" stdDeviation="6" result="blur" />
                        <feMerge>
                            <feMergeNode in="blur" />
                            <feMergeNode in="SourceGraphic" />
                        </feMerge>
                    </filter>
                    {/* Gradient for edges */}
                    <linearGradient id="edge-gradient" x1="0%" y1="0%" x2="100%" y2="0%">
                        <stop offset="0%" stopColor="#3b82f6" stopOpacity="0.6" />
                        <stop offset="100%" stopColor="#10b981" stopOpacity="0.6" />
                    </linearGradient>
                </defs>

                {/* Edges */}
                {EDGES.map(([from, to], idx) => {
                    const n1 = NODE_POSITIONS[from];
                    const n2 = NODE_POSITIONS[to];
                    const isActive = activeNodeSet.has(from) || activeNodeSet.has(to);
                    return (
                        <g key={`edge-${idx}`}>
                            {/* Background edge */}
                            <motion.line
                                x1={n1.x} y1={n1.y}
                                x2={n2.x} y2={n2.y}
                                stroke={isActive ? "url(#edge-gradient)" : "rgba(55, 65, 81, 0.4)"}
                                strokeWidth={isActive ? 2.5 : 1.5}
                                strokeDasharray={isActive ? "none" : "6 4"}
                                filter={isActive ? "url(#edge-glow)" : "none"}
                                initial={{ pathLength: 0 }}
                                animate={{ pathLength: 1 }}
                                transition={{ duration: 1 + idx * 0.2, ease: "easeInOut" }}
                            />
                        </g>
                    );
                })}

                {/* Nodes */}
                {NODE_POSITIONS.map((node) => {
                    const isOnline = nodeHealth[node.id] !== false;
                    const isActive = activeNodeSet.has(node.id);
                    const count = nodePacketCounts[node.id] || 0;

                    let fillColor = isOnline ? '#1e293b' : '#1c1917';
                    let strokeColor = isOnline ? '#374151' : '#7f1d1d';
                    let textColor = isOnline ? '#94a3b8' : '#fca5a5';
                    let glowFilter = 'none';

                    if (isActive && isOnline) {
                        fillColor = '#1e3a5f';
                        strokeColor = '#3b82f6';
                        textColor = '#93c5fd';
                        glowFilter = 'url(#node-glow)';
                    } else if (isOnline && count > 0) {
                        fillColor = '#14332a';
                        strokeColor = '#10b981';
                        textColor = '#6ee7b7';
                    }

                    return (
                        <motion.g
                            key={node.id}
                            initial={{ opacity: 0, scale: 0.5 }}
                            animate={{ opacity: 1, scale: 1 }}
                            transition={{ delay: 0.3 + node.id * 0.08, type: 'spring', stiffness: 200 }}
                        >
                            {/* Outer pulse ring for active */}
                            {isActive && isOnline && (
                                <motion.circle
                                    cx={node.x} cy={node.y} r={32}
                                    fill="none"
                                    stroke="#3b82f6"
                                    strokeWidth={1}
                                    initial={{ opacity: 0.8, r: 24 }}
                                    animate={{ opacity: 0, r: 38 }}
                                    transition={{ duration: 1.5, repeat: Infinity, ease: "easeOut" }}
                                />
                            )}

                            {/* Main circle */}
                            <circle
                                cx={node.x} cy={node.y} r={24}
                                fill={fillColor}
                                stroke={strokeColor}
                                strokeWidth={isActive ? 2 : 1.5}
                                filter={glowFilter}
                                style={{ transition: 'all 0.4s ease' }}
                            />

                            {/* Node ID */}
                            <text
                                x={node.x} y={node.y - 2}
                                textAnchor="middle"
                                dominantBaseline="middle"
                                fill={textColor}
                                fontSize="12"
                                fontWeight="700"
                                fontFamily="'Inter', sans-serif"
                            >
                                N{node.id}
                            </text>

                            {/* Port label */}
                            <text
                                x={node.x} y={node.y + 11}
                                textAnchor="middle"
                                dominantBaseline="middle"
                                fill="rgba(100,116,139,0.6)"
                                fontSize="7"
                                fontFamily="'JetBrains Mono', monospace"
                            >
                                :{node.port}
                            </text>

                            {/* Packet count badge */}
                            {count > 0 && (
                                <g>
                                    <circle
                                        cx={node.x + 18} cy={node.y - 18} r={9}
                                        fill="#1e293b"
                                        stroke="#3b82f6"
                                        strokeWidth={1}
                                    />
                                    <text
                                        x={node.x + 18} y={node.y - 17}
                                        textAnchor="middle"
                                        dominantBaseline="middle"
                                        fill="#93c5fd"
                                        fontSize="8"
                                        fontWeight="700"
                                        fontFamily="'JetBrains Mono', monospace"
                                    >
                                        {count}
                                    </text>
                                </g>
                            )}

                            {/* Online/Offline indicator */}
                            <circle
                                cx={node.x - 18} cy={node.y - 18} r={4}
                                fill={isOnline ? '#10b981' : '#ef4444'}
                                style={{ transition: 'fill 0.3s ease' }}
                            >
                                {isOnline && (
                                    <animate
                                        attributeName="opacity"
                                        values="1;0.4;1"
                                        dur="2s"
                                        repeatCount="indefinite"
                                    />
                                )}
                            </circle>
                        </motion.g>
                    );
                })}
            </svg>
        </div>
    );
};

export default Topology;
