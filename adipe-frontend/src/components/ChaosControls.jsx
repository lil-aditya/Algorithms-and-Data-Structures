import React from 'react';

const MODES = ['NORMAL', 'TAMPER', 'SILENT_DROP', 'DELAY', 'FORGE', 'EAVESDROP'];

const modeColors = {
    NORMAL: '#10b981',
    TAMPER: '#f43f5e',
    SILENT_DROP: '#fb7185',
    DELAY: '#f59e0b',
    FORGE: '#a855f7',
    EAVESDROP: '#06b6d4',
    OFFLINE: '#64748b',
};

const ChaosControls = ({ networkState, onModeChange, onReset, busyNodeID, resetBusy }) => {
    const nodes = networkState?.nodes || [];

    return (
        <div className="glass-card p-5">
            <div className="flex items-center justify-between mb-4">
                <div>
                    <h2 className="text-lg font-bold text-slate-100">Chaos Controls</h2>
                    <p className="text-xs text-slate-500 mt-1">Flip nodes into rogue modes and watch routing adapt.</p>
                </div>
                <button
                    type="button"
                    onClick={onReset}
                    disabled={resetBusy}
                    className="px-3 py-2 rounded-lg text-xs font-semibold transition-colors"
                    style={{
                        background: resetBusy ? 'rgba(55,65,81,0.4)' : 'rgba(59,130,246,0.12)',
                        color: resetBusy ? '#64748b' : '#93c5fd',
                        border: '1px solid rgba(59,130,246,0.25)',
                        cursor: resetBusy ? 'not-allowed' : 'pointer',
                    }}
                >
                    {resetBusy ? 'Resetting...' : 'Reset Cluster'}
                </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-3">
                {nodes.map((node) => (
                    <div
                        key={node.id}
                        className="rounded-xl p-4"
                        style={{
                            background: 'rgba(15, 23, 42, 0.55)',
                            border: `1px solid ${node.quarantined ? 'rgba(244,63,94,0.35)' : 'rgba(55,65,81,0.3)'}`,
                        }}
                    >
                        <div className="flex items-center justify-between mb-2">
                            <div>
                                <p className="text-sm font-bold text-slate-100">
                                    Node {node.id}
                                    {node.leader ? <span className="ml-2 text-xs text-amber-300">Leader</span> : null}
                                </p>
                                <p className="mono text-xs text-slate-500">:{node.port}</p>
                            </div>
                            <span
                                className="mono text-xs font-semibold px-2 py-1 rounded-lg"
                                style={{
                                    background: 'rgba(15,23,42,0.7)',
                                    color: modeColors[node.mode] || '#cbd5e1',
                                    border: `1px solid ${(modeColors[node.mode] || '#475569')}55`,
                                }}
                            >
                                {node.mode}
                            </span>
                        </div>

                        <div className="flex items-center justify-between text-xs text-slate-400 mb-3">
                            <span>Trust: <span className="mono text-slate-200">{node.trust.toFixed(2)}</span></span>
                            <span style={{ color: node.quarantined ? '#fda4af' : '#6ee7b7' }}>
                                {node.quarantined ? 'Quarantined' : 'Eligible'}
                            </span>
                        </div>

                        <select
                            value={node.mode}
                            disabled={!node.online || busyNodeID === node.id}
                            onChange={(e) => onModeChange(node.id, e.target.value)}
                            className="w-full rounded-lg px-3 py-2 text-sm outline-none"
                            style={{
                                background: 'rgba(15, 23, 42, 0.85)',
                                color: '#e2e8f0',
                                border: '1px solid rgba(55,65,81,0.45)',
                            }}
                        >
                            {MODES.map((mode) => (
                                <option key={mode} value={mode}>{mode}</option>
                            ))}
                        </select>
                    </div>
                ))}
            </div>
        </div>
    );
};

export default ChaosControls;
