import React, { useState } from 'react';
import { motion, AnimatePresence } from 'framer-motion';

const STATUS_CONFIG = {
    RECEIVED:   { color: '#3b82f6', bg: 'rgba(59,130,246,0.12)',  icon: '📥' },
    SIGNED:     { color: '#8b5cf6', bg: 'rgba(139,92,246,0.12)',  icon: '🔏' },
    QUEUED:     { color: '#f59e0b', bg: 'rgba(245,158,11,0.12)',  icon: '📋' },
    PROCESSING: { color: '#f97316', bg: 'rgba(249,115,22,0.12)', icon: '⚙️' },
    VERIFIED:   { color: '#06b6d4', bg: 'rgba(6,182,212,0.12)',  icon: '✅' },
    FORWARDED:  { color: '#a855f7', bg: 'rgba(168,85,247,0.12)', icon: '➡️' },
    DELIVERED:  { color: '#10b981', bg: 'rgba(16,185,129,0.12)', icon: '🎯' },
    DROPPED:    { color: '#f43f5e', bg: 'rgba(244,63,94,0.12)',  icon: '❌' },
};

const PacketTracker = ({ packets }) => {
    const [expandedId, setExpandedId] = useState(null);

    const sorted = packets ? [...packets].reverse() : [];

    return (
        <div className="glass-card p-5 flex flex-col" style={{ maxHeight: '100%', height: '100%' }}>
            {/* Header */}
            <div className="flex items-center justify-between mb-4">
                <div className="flex items-center gap-3">
                    <div className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse-glow"></div>
                    <h2 className="text-lg font-bold text-slate-100">Live Packet Feed</h2>
                </div>
                <span className="mono text-xs text-slate-500">{packets.length} packet{packets.length !== 1 ? 's' : ''}</span>
            </div>

            {/* Packet List */}
            <div className="flex-1 overflow-y-auto space-y-2 pr-1" style={{ minHeight: 0 }}>
                <AnimatePresence mode="popLayout">
                    {sorted.length === 0 ? (
                        <motion.div
                            key="empty"
                            initial={{ opacity: 0 }}
                            animate={{ opacity: 1 }}
                            className="flex flex-col items-center justify-center py-16 text-slate-500"
                        >
                            <svg className="w-12 h-12 mb-3 text-slate-600" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1} d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4" />
                            </svg>
                            <p className="text-sm font-medium">No packets yet</p>
                            <p className="text-xs mt-1">Inject a packet to see it travel the network</p>
                        </motion.div>
                    ) : (
                        sorted.map((pkt, idx) => {
                            const cfg = STATUS_CONFIG[pkt.currentStatus] || STATUS_CONFIG.RECEIVED;
                            const isExpanded = expandedId === pkt.packetID;
                            return (
                                <motion.div
                                    key={pkt.packetID}
                                    layout
                                    initial={{ opacity: 0, y: -10 }}
                                    animate={{ opacity: 1, y: 0 }}
                                    exit={{ opacity: 0, scale: 0.95 }}
                                    transition={{ type: 'spring', stiffness: 400, damping: 30 }}
                                    onClick={() => setExpandedId(isExpanded ? null : pkt.packetID)}
                                    className="cursor-pointer rounded-xl p-4 transition-all duration-200"
                                    style={{
                                        background: isExpanded ? 'rgba(30, 41, 59, 0.8)' : 'rgba(15, 23, 42, 0.5)',
                                        border: `1px solid ${isExpanded ? cfg.color + '40' : 'rgba(55, 65, 81, 0.3)'}`,
                                    }}
                                >
                                    {/* Row: ID + Status */}
                                    <div className="flex items-center justify-between">
                                        <div className="flex items-center gap-2">
                                            <span className="text-base">{cfg.icon}</span>
                                            <span className="mono text-sm font-semibold" style={{ color: cfg.color }}>
                                                {pkt.packetID}
                                            </span>
                                        </div>
                                        <span className={`status-badge status-${pkt.currentStatus}`}>
                                            {pkt.currentStatus}
                                        </span>
                                    </div>

                                    {/* Row: Meta */}
                                    <div className="flex items-center gap-4 mt-2 text-xs text-slate-400">
                                        <span>Node {pkt.sourceNodeID} → Node {pkt.destinationNodeID}</span>
                                        <span className="mono" style={{ color: '#f59e0b' }}>⚡ {pkt.urgency}</span>
                                    </div>

                                    {/* Expandable Timeline */}
                                    <AnimatePresence>
                                        {isExpanded && pkt.events && (
                                            <motion.div
                                                initial={{ height: 0, opacity: 0 }}
                                                animate={{ height: 'auto', opacity: 1 }}
                                                exit={{ height: 0, opacity: 0 }}
                                                transition={{ duration: 0.25 }}
                                                className="overflow-hidden"
                                            >
                                                <div className="mt-4 pt-3 border-t border-slate-700/50">
                                                    <p className="text-xs font-semibold text-slate-400 mb-2 uppercase tracking-wider">Lifecycle Timeline</p>
                                                    <div className="space-y-1">
                                                        {pkt.events.map((ev, i) => {
                                                            const evCfg = STATUS_CONFIG[ev.status] || STATUS_CONFIG.RECEIVED;
                                                            return (
                                                                <div key={i} className="flex items-start gap-3 py-1">
                                                                    {/* Timeline dot + line */}
                                                                    <div className="flex flex-col items-center pt-1">
                                                                        <div
                                                                            className="w-2 h-2 rounded-full"
                                                                            style={{ backgroundColor: evCfg.color }}
                                                                        />
                                                                        {i < pkt.events.length - 1 && (
                                                                            <div className="w-px flex-1 min-h-[12px] bg-slate-700/50 mt-1" />
                                                                        )}
                                                                    </div>
                                                                    {/* Event content */}
                                                                    <div className="flex-1 min-w-0">
                                                                        <div className="flex items-center gap-2">
                                                                            <span className="mono text-xs font-semibold" style={{ color: evCfg.color }}>{ev.status}</span>
                                                                            <span className="text-slate-600 text-xs">·</span>
                                                                            <span className="mono text-xs text-slate-500">N{ev.nodeID}</span>
                                                                            <span className="text-slate-600 text-xs">·</span>
                                                                            <span className="mono text-xs text-slate-600">{ev.timestamp}</span>
                                                                        </div>
                                                                        {ev.detail && (
                                                                            <p className="text-xs text-slate-500 mt-0.5 truncate">{ev.detail}</p>
                                                                        )}
                                                                    </div>
                                                                </div>
                                                            );
                                                        })}
                                                    </div>
                                                </div>
                                            </motion.div>
                                        )}
                                    </AnimatePresence>
                                </motion.div>
                            );
                        })
                    )}
                </AnimatePresence>
            </div>
        </div>
    );
};

export default PacketTracker;
