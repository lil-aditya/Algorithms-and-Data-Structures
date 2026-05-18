import React, { useMemo } from 'react';

const StatsBar = ({ packets, nodeHealth, networkState }) => {
    const stats = useMemo(() => {
        const total = packets.length;
        const delivered = packets.filter((packet) => packet.currentStatus === 'DELIVERED').length;
        const dropped = packets.filter((packet) => packet.currentStatus === 'DROPPED').length;
        const onlineNodes = Object.values(nodeHealth).filter(Boolean).length;
        const quarantined = networkState?.nodes?.filter((node) => node.quarantined).length || 0;
        const leaderID = networkState?.leaderID ?? '-';

        return { total, delivered, dropped, onlineNodes, quarantined, leaderID };
    }, [packets, nodeHealth, networkState]);

    const items = [
        { label: 'Nodes Online', value: `${stats.onlineNodes}/6`, color: stats.onlineNodes === 6 ? '#10b981' : '#f59e0b', icon: 'Nodes' },
        { label: 'Leader', value: `N${stats.leaderID}`, color: '#fbbf24', icon: 'Lead' },
        { label: 'Total Packets', value: stats.total, color: '#3b82f6', icon: 'Pkts' },
        { label: 'Delivered', value: stats.delivered, color: '#10b981', icon: 'Done' },
        { label: 'Quarantined', value: stats.quarantined, color: stats.quarantined > 0 ? '#f43f5e' : '#06b6d4', icon: 'Risk' },
        { label: 'Dropped', value: stats.dropped, color: '#f43f5e', icon: 'Drop' },
    ];

    return (
        <div className="grid grid-cols-2 lg:grid-cols-6 gap-3">
            {items.map((item, idx) => (
                <div
                    key={idx}
                    className="glass-card p-4 text-center animate-slide-up"
                    style={{ animationDelay: `${idx * 0.05}s` }}
                >
                    <div className="mono text-xs uppercase tracking-wider text-slate-600 mb-2">{item.icon}</div>
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
