import React from 'react';

const scoreColor = (score) => {
    if (score < 0.3) return '#f43f5e';
    if (score < 0.6) return '#f59e0b';
    if (score < 0.85) return '#06b6d4';
    return '#10b981';
};

const cellBg = (score) => {
    if (score < 0.3) return 'rgba(244,63,94,0.14)';
    if (score < 0.6) return 'rgba(245,158,11,0.14)';
    if (score < 0.85) return 'rgba(6,182,212,0.14)';
    return 'rgba(16,185,129,0.12)';
};

const TrustMatrix = ({ networkState }) => {
    if (!networkState) {
        return null;
    }

    const { aggregatedTrust = [], trustMatrix = [], leaderID, quarantineThreshold } = networkState;

    return (
        <div className="glass-card p-5">
            <div className="flex items-center justify-between mb-4">
                <div>
                    <h2 className="text-lg font-bold text-slate-100">Trust Matrix</h2>
                    <p className="text-xs text-slate-500 mt-1">
                        Leader Node {leaderID} broadcasts the most conservative trust view.
                    </p>
                </div>
                <span className="mono text-xs text-slate-400">
                    Quarantine threshold: {quarantineThreshold?.toFixed?.(2) ?? '0.30'}
                </span>
            </div>

            <div className="grid grid-cols-2 md:grid-cols-3 xl:grid-cols-6 gap-3 mb-5">
                {aggregatedTrust.map((score, idx) => (
                    <div
                        key={idx}
                        className="rounded-xl p-3"
                        style={{
                            background: 'rgba(15, 23, 42, 0.55)',
                            border: `1px solid ${scoreColor(score)}40`,
                        }}
                    >
                        <p className="text-xs uppercase tracking-wider text-slate-500">Node {idx}</p>
                        <p className="mono text-2xl font-bold mt-2" style={{ color: scoreColor(score) }}>
                            {score.toFixed(2)}
                        </p>
                        <p className="text-xs mt-1" style={{ color: score < quarantineThreshold ? '#fda4af' : '#94a3b8' }}>
                            {score < quarantineThreshold ? 'Quarantined' : 'Trusted'}
                        </p>
                    </div>
                ))}
            </div>

            <div className="overflow-x-auto">
                <table className="w-full text-sm border-separate" style={{ borderSpacing: '8px' }}>
                    <thead>
                        <tr className="text-left text-slate-500 text-xs uppercase tracking-wider">
                            <th className="px-2">Observer</th>
                            {aggregatedTrust.map((_, idx) => (
                                <th key={idx} className="px-2">N{idx}</th>
                            ))}
                        </tr>
                    </thead>
                    <tbody>
                        {trustMatrix.map((row) => (
                            <tr key={row.observerNode}>
                                <td className="px-2 py-2 mono text-slate-300">Node {row.observerNode}</td>
                                {row.scores.map((score, idx) => (
                                    <td key={`${row.observerNode}-${idx}`} className="px-1 py-1">
                                        <div
                                            className="rounded-lg px-3 py-2 mono text-xs text-center font-semibold"
                                            style={{
                                                background: cellBg(score),
                                                color: scoreColor(score),
                                                border: `1px solid ${scoreColor(score)}35`,
                                            }}
                                        >
                                            {score.toFixed(2)}
                                        </div>
                                    </td>
                                ))}
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>
        </div>
    );
};

export default TrustMatrix;
