import React, { useMemo } from 'react';

const StatsBar = ({ packets, nodeHealth }) => {
    const stats = useMemo(() => {
        const total = packets.length;
        const delivered = packets.filter(p => p.currentStatus === 'DELIVERED').length;
        const dropped = packets.filter(p => p.currentStatus === 'DROPPED').length;
        const inFlight = total - delivered - dropped;
        const onlineNodes = Object.values(nodeHealth).filter(Boolean).length;
        return { total, delivered, dropped, inFlight, onlineNodes };
    }, [packets, nodeHealth]);

    const items = [
        { label: 'Nodes Online', value: `${stats.onlineNodes}/6`, color: stats.onlineNodes === 6 ? '#10b981' : '#f59e0b', icon: '🖥️' },
        { label: 'Total Packets', value: stats.total, color: '#3b82f6', icon: '📦' },
        { label: 'Delivered', value: stats.delivered, color: '#10b981', icon: '✅' },
        { label: 'In Flight', value: stats.inFlight, color: '#a855f7', icon: '🚀' },
        { label: 'Dropped', value: stats.dropped, color: '#f43f5e', icon: '❌' },
    ];

    return (
        <div className="grid grid-cols-5 gap-3">
            {items.map((item, idx) => (
                <div
                    key={idx}
                    className="glass-card p-4 text-center animate-slide-up"
                    style={{ animationDelay: `${idx * 0.05}s` }}
                >
                    <div className="text-lg mb-1">{item.icon}</div>
                    <div className="mono text-2xl font-bold" style={{ color: item.color }}>
                        {item.value}
                    </div>
                    <div className="text-xs text-slate-500 mt-1 font-medium">{item.label}</div>
                </div>
            ))}
        </div>
    );
};

export default StatsBar;
