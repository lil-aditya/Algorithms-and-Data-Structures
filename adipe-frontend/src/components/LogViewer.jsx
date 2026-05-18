import React, { useState, useEffect, useRef } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { fetchNetworkLog } from '../api';

const LogViewer = () => {
    const [logs, setLogs] = useState([]);
    const [isOpen, setIsOpen] = useState(false);
    const [autoScroll, setAutoScroll] = useState(true);
    const scrollRef = useRef(null);

    useEffect(() => {
        if (!isOpen) return;
        const interval = setInterval(async () => {
            const data = await fetchNetworkLog();
            setLogs(data);
        }, 2000);
        // Fetch immediately on open
        fetchNetworkLog().then(setLogs);
        return () => clearInterval(interval);
    }, [isOpen]);

    useEffect(() => {
        if (autoScroll && scrollRef.current) {
            scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
        }
    }, [logs, autoScroll]);

    const getLogColor = (line) => {
        if (line.includes('DELIVERED')) return '#10b981';
        if (line.includes('DROPPED') || line.includes('FAILED')) return '#f43f5e';
        if (line.includes('INJECTION') || line.includes('Signed')) return '#8b5cf6';
        if (line.includes('VALID') || line.includes('Verified')) return '#06b6d4';
        if (line.includes('Forwarding') || line.includes('next hop')) return '#a855f7';
        if (line.includes('Starting') || line.includes('listening')) return '#3b82f6';
        return '#64748b';
    };

    return (
        <div className="fixed bottom-0 left-0 right-0 z-50">
            {/* Toggle Bar */}
            <button
                onClick={() => setIsOpen(!isOpen)}
                className="w-full flex items-center justify-between px-6 py-2.5 transition-all duration-200"
                style={{
                    background: 'rgba(15, 23, 42, 0.95)',
                    borderTop: '1px solid rgba(55, 65, 81, 0.5)',
                    backdropFilter: 'blur(12px)',
                    cursor: 'pointer',
                    border: 'none',
                    borderRadius: 0,
                    color: '#94a3b8',
                }}
            >
                <div className="flex items-center gap-2">
                    <svg className="w-4 h-4 text-slate-400" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M8 9l3 3-3 3m5 0h3M5 20h14a2 2 0 002-2V6a2 2 0 00-2-2H5a2 2 0 00-2 2v12a2 2 0 002 2z" />
                    </svg>
                    <span className="text-xs font-semibold uppercase tracking-wider">Engine Console</span>
                    <span className="mono text-xs text-slate-600">({logs.length} lines)</span>
                </div>
                <motion.svg
                    animate={{ rotate: isOpen ? 180 : 0 }}
                    className="w-4 h-4 text-slate-500"
                    fill="none" viewBox="0 0 24 24" stroke="currentColor"
                >
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M5 15l7-7 7 7" />
                </motion.svg>
            </button>

            {/* Log Panel */}
            <AnimatePresence>
                {isOpen && (
                    <motion.div
                        initial={{ height: 0 }}
                        animate={{ height: 240 }}
                        exit={{ height: 0 }}
                        transition={{ duration: 0.3, ease: 'easeInOut' }}
                        className="overflow-hidden"
                        style={{
                            background: 'rgba(10, 14, 26, 0.98)',
                            borderTop: '1px solid rgba(55, 65, 81, 0.3)',
                        }}
                    >
                        {/* Toolbar */}
                        <div className="flex items-center justify-between px-4 py-1.5 border-b" style={{ borderColor: 'rgba(55,65,81,0.3)' }}>
                            <div className="flex items-center gap-3">
                                <div className="flex gap-1.5">
                                    <div className="w-2.5 h-2.5 rounded-full bg-red-500/70"></div>
                                    <div className="w-2.5 h-2.5 rounded-full bg-yellow-500/70"></div>
                                    <div className="w-2.5 h-2.5 rounded-full bg-green-500/70"></div>
                                </div>
                                <span className="mono text-xs text-slate-600">stdout — C++ Engine</span>
                            </div>
                            <button
                                onClick={(e) => { e.stopPropagation(); setAutoScroll(!autoScroll); }}
                                className="text-xs mono px-2 py-0.5 rounded transition-colors"
                                style={{
                                    background: autoScroll ? 'rgba(16, 185, 129, 0.15)' : 'rgba(55, 65, 81, 0.3)',
                                    color: autoScroll ? '#10b981' : '#64748b',
                                    border: `1px solid ${autoScroll ? 'rgba(16,185,129,0.3)' : 'rgba(55,65,81,0.3)'}`,
                                    cursor: 'pointer',
                                }}
                            >
                                {autoScroll ? '⬇ Auto-scroll ON' : '⬇ Auto-scroll OFF'}
                            </button>
                        </div>

                        {/* Log Lines */}
                        <div ref={scrollRef} className="h-full overflow-y-auto px-4 py-2 pb-10" style={{ fontFamily: "'JetBrains Mono', monospace" }}>
                            {logs.length === 0 ? (
                                <p className="text-xs text-slate-600 italic py-4">Waiting for engine output...</p>
                            ) : (
                                logs.map((line, i) => (
                                    <div key={i} className="flex gap-3 py-0.5 text-xs leading-relaxed hover:bg-white/[0.02] rounded px-1 -mx-1">
                                        <span className="text-slate-700 select-none" style={{ minWidth: 32, textAlign: 'right' }}>{i + 1}</span>
                                        <span style={{ color: getLogColor(line) }}>{line}</span>
                                    </div>
                                ))
                            )}
                        </div>
                    </motion.div>
                )}
            </AnimatePresence>
        </div>
    );
};

export default LogViewer;
