import React, { useMemo } from 'react';
import { motion } from 'framer-motion';

const NODE_POSITIONS = [
    { id: 0, x: 80, y: 140, label: 'Node 0', port: 8080 },
    { id: 1, x: 240, y: 60, label: 'Node 1', port: 8081 },
    { id: 2, x: 240, y: 220, label: 'Node 2', port: 8082 },
    { id: 3, x: 420, y: 60, label: 'Node 3', port: 8083 },
    { id: 4, x: 420, y: 220, label: 'Node 4', port: 8084 },
    { id: 5, x: 580, y: 140, label: 'Node 5', port: 8085 },
];

const EDGES = [
    [0, 1], [0, 2], [1, 3], [2, 4], [3, 5], [4, 5],
];

const modeColor = (mode) => {
    switch (mode) {
        case 'TAMPER': return '#f43f5e';
        case 'SILENT_DROP': return '#fb7185';
        case 'DELAY': return '#f59e0b';
        case 'FORGE': return '#a855f7';
        case 'EAVESDROP': return '#06b6d4';
        default: return '#10b981';
    }
};

const Topology = ({ nodeHealth, packets, networkState }) => {
    const activeNodeSet = useMemo(() => {
        const set = new Set();
        if (packets && packets.length > 0) {
            const recent = packets.slice(-3);
            recent.forEach((packet) => {
                if (packet.events && packet.events.length > 0) {
                    const lastEvent = packet.events[packet.events.length - 1];
                    if (packet.currentStatus !== 'DELIVERED' && packet.currentStatus !== 'DROPPED') {
                        set.add(lastEvent.nodeID);
                    }
                }
            });
        }
        return set;
    }, [packets]);

    const nodePacketCounts = useMemo(() => {
        const counts = {};
        if (packets) {
            packets.forEach((packet) => {
                if (packet.events) {
                    packet.events.forEach((event) => {
                        counts[event.nodeID] = (counts[event.nodeID] || 0) + 1;
                    });
                }
            });
        }
        return counts;
    }, [packets]);

    const nodeStateMap = useMemo(() => {
        const map = {};
        (networkState?.nodes || []).forEach((node) => {
            map[node.id] = node;
        });
        return map;
    }, [networkState]);

    return (
        <div className="topology-container w-full" style={{ height: 320 }}>
            <svg
                viewBox="0 0 660 280"
                className="w-full h-full"
                style={{ filter: 'drop-shadow(0 0 1px rgba(59,130,246,0.2))' }}
            >
                <defs>
                    <filter id="edge-glow" x="-20%" y="-20%" width="140%" height="140%">
                        <feGaussianBlur in="SourceGraphic" stdDeviation="3" result="blur" />
                        <feMerge>
                            <feMergeNode in="blur" />
                            <feMergeNode in="SourceGraphic" />
                        </feMerge>
                    </filter>
                    <filter id="node-glow" x="-50%" y="-50%" width="200%" height="200%">
                        <feGaussianBlur in="SourceGraphic" stdDeviation="6" result="blur" />
                        <feMerge>
                            <feMergeNode in="blur" />
                            <feMergeNode in="SourceGraphic" />
                        </feMerge>
                    </filter>
                </defs>

                {EDGES.map(([from, to], idx) => {
                    const n1 = NODE_POSITIONS[from];
                    const n2 = NODE_POSITIONS[to];
                    const fromState = nodeStateMap[from];
                    const toState = nodeStateMap[to];
                    const isQuarantinedEdge = fromState?.quarantined || toState?.quarantined;
                    const isActive = activeNodeSet.has(from) || activeNodeSet.has(to);

                    return (
                        <motion.line
                            key={`edge-${idx}`}
                            x1={n1.x}
                            y1={n1.y}
                            x2={n2.x}
                            y2={n2.y}
                            stroke={isQuarantinedEdge ? 'rgba(244,63,94,0.3)' : isActive ? 'rgba(59,130,246,0.75)' : 'rgba(55, 65, 81, 0.4)'}
                            strokeWidth={isActive ? 2.5 : 1.5}
                            strokeDasharray={isQuarantinedEdge ? '8 6' : isActive ? 'none' : '6 4'}
                            filter={isActive ? 'url(#edge-glow)' : 'none'}
                            initial={{ pathLength: 0 }}
                            animate={{ pathLength: 1 }}
                            transition={{ duration: 1 + idx * 0.15, ease: 'easeInOut' }}
                        />
                    );
                })}

                {NODE_POSITIONS.map((node) => {
                    const state = nodeStateMap[node.id] || {};
                    const isOnline = nodeHealth[node.id] !== false;
                    const isActive = activeNodeSet.has(node.id);
                    const count = nodePacketCounts[node.id] || 0;
                    const trust = state.trust ?? 1;
                    const mode = state.mode || 'NORMAL';
                    const isLeader = Boolean(state.leader);
                    const isQuarantined = Boolean(state.quarantined);

                    let fillColor = isOnline ? '#1e293b' : '#1c1917';
                    let strokeColor = isOnline ? '#374151' : '#7f1d1d';
                    let textColor = isOnline ? '#94a3b8' : '#fca5a5';

                    if (isQuarantined) {
                        fillColor = '#31111d';
                        strokeColor = '#f43f5e';
                        textColor = '#fda4af';
                    } else if (isActive && isOnline) {
                        fillColor = '#1e3a5f';
                        strokeColor = '#3b82f6';
                        textColor = '#93c5fd';
                    } else if (isOnline && count > 0) {
                        fillColor = '#14332a';
                        strokeColor = modeColor(mode);
                        textColor = '#cbd5e1';
                    }

                    return (
                        <motion.g
                            key={node.id}
                            initial={{ opacity: 0, scale: 0.5 }}
                            animate={{ opacity: 1, scale: 1 }}
                            transition={{ delay: 0.25 + node.id * 0.08, type: 'spring', stiffness: 200 }}
                        >
                            {isActive && isOnline && !isQuarantined && (
                                <motion.circle
                                    cx={node.x}
                                    cy={node.y}
                                    r={32}
                                    fill="none"
                                    stroke="#3b82f6"
                                    strokeWidth={1}
                                    initial={{ opacity: 0.8, r: 24 }}
                                    animate={{ opacity: 0, r: 38 }}
                                    transition={{ duration: 1.5, repeat: Infinity, ease: 'easeOut' }}
                                />
                            )}

                            <circle
                                cx={node.x}
                                cy={node.y}
                                r={24}
                                fill={fillColor}
                                stroke={isLeader ? '#fbbf24' : strokeColor}
                                strokeWidth={isLeader ? 3 : isActive ? 2 : 1.5}
                                filter={isActive && !isQuarantined ? 'url(#node-glow)' : 'none'}
                                style={{ transition: 'all 0.4s ease' }}
                            />

                            <text
                                x={node.x}
                                y={node.y - 2}
                                textAnchor="middle"
                                dominantBaseline="middle"
                                fill={textColor}
                                fontSize="12"
                                fontWeight="700"
                                fontFamily="'Inter', sans-serif"
                            >
                                N{node.id}
                            </text>

                            <text
                                x={node.x}
                                y={node.y + 11}
                                textAnchor="middle"
                                dominantBaseline="middle"
                                fill="rgba(100,116,139,0.65)"
                                fontSize="7"
                                fontFamily="'JetBrains Mono', monospace"
                            >
                                :{node.port}
                            </text>

                            <g>
                                <circle
                                    cx={node.x + 19}
                                    cy={node.y - 18}
                                    r={10}
                                    fill="rgba(15,23,42,0.9)"
                                    stroke={modeColor(mode)}
                                    strokeWidth={1}
                                />
                                <text
                                    x={node.x + 19}
                                    y={node.y - 17}
                                    textAnchor="middle"
                                    dominantBaseline="middle"
                                    fill={modeColor(mode)}
                                    fontSize="7"
                                    fontWeight="700"
                                    fontFamily="'JetBrains Mono', monospace"
                                >
                                    {trust.toFixed(2)}
                                </text>
                            </g>

                            {count > 0 && (
                                <g>
                                    <circle
                                        cx={node.x - 19}
                                        cy={node.y + 20}
                                        r={8}
                                        fill="#0f172a"
                                        stroke="#3b82f6"
                                        strokeWidth={1}
                                    />
                                    <text
                                        x={node.x - 19}
                                        y={node.y + 20}
                                        textAnchor="middle"
                                        dominantBaseline="middle"
                                        fill="#93c5fd"
                                        fontSize="7"
                                        fontWeight="700"
                                        fontFamily="'JetBrains Mono', monospace"
                                    >
                                        {count}
                                    </text>
                                </g>
                            )}

                            {isLeader && (
                                <text
                                    x={node.x}
                                    y={node.y - 36}
                                    textAnchor="middle"
                                    fill="#fbbf24"
                                    fontSize="10"
                                    fontWeight="700"
                                    fontFamily="'JetBrains Mono', monospace"
                                >
                                    LEADER
                                </text>
                            )}

                            <circle
                                cx={node.x - 18}
                                cy={node.y - 18}
                                r={4}
                                fill={isOnline ? '#10b981' : '#ef4444'}
                            />
                        </motion.g>
                    );
                })}
            </svg>
        </div>
    );
};

export default Topology;
