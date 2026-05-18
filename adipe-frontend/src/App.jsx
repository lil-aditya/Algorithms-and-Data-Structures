import React, { useState, useEffect, useCallback } from 'react';
import { fetchPackets, checkAllNodes, injectPacket } from './api';
import Topology from './components/Topology';
import PacketTracker from './components/PacketTracker';
import InjectForm from './components/InjectForm';
import LogViewer from './components/LogViewer';
import StatsBar from './components/StatsBar';

export default function App() {
    const [packets, setPackets] = useState([]);
    const [nodeHealth, setNodeHealth] = useState({});
    const [engineOnline, setEngineOnline] = useState(false);

    // Poll packets every 1.5s
    useEffect(() => {
        const poll = async () => {
            const data = await fetchPackets();
            setPackets(data);
        };
        poll(); // immediate first fetch
        const interval = setInterval(poll, 1500);
        return () => clearInterval(interval);
    }, []);

    // Check node health every 4s
    useEffect(() => {
        const check = async () => {
            const results = await checkAllNodes();
            const health = {};
            let anyOnline = false;
            results.forEach(r => {
                health[r.id] = r.online;
                if (r.online) anyOnline = true;
            });
            setNodeHealth(health);
            setEngineOnline(anyOnline);
        };
        check();
        const interval = setInterval(check, 4000);
        return () => clearInterval(interval);
    }, []);

    const handleInject = useCallback(async (packetData) => {
        await injectPacket(packetData);
        // Immediate re-fetch for responsiveness
        const data = await fetchPackets();
        setPackets(data);
    }, []);

    return (
        <div className="min-h-screen flex flex-col pb-12">
            {/* ===== Header ===== */}
            <header className="px-8 pt-8 pb-2">
                <div className="max-w-[1400px] mx-auto flex items-center justify-between">
                    <div>
                        <div className="flex items-center gap-3">
                            {/* Logo mark */}
                            <div
                                className="w-10 h-10 rounded-xl flex items-center justify-center"
                                style={{
                                    background: 'linear-gradient(135deg, #3b82f6 0%, #8b5cf6 100%)',
                                    boxShadow: '0 4px 15px rgba(59, 130, 246, 0.3)',
                                }}
                            >
                                <svg className="w-5 h-5 text-white" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2.5}>
                                    <path strokeLinecap="round" strokeLinejoin="round" d="M13 10V3L4 14h7v7l9-11h-7z" />
                                </svg>
                            </div>
                            <div>
                                <h1 className="text-2xl font-extrabold tracking-tight text-slate-100">
                                    ADIPE
                                    <span className="text-sm font-medium text-slate-500 ml-2">Dashboard</span>
                                </h1>
                            </div>
                        </div>
                        <p className="text-xs text-slate-500 mt-1 ml-[52px]">
                            Algorithmic Data Integrity & Prioritization Engine
                        </p>
                    </div>

                    {/* Connection Status */}
                    <div className="flex items-center gap-2 px-4 py-2 rounded-xl"
                        style={{
                            background: engineOnline ? 'rgba(16,185,129,0.08)' : 'rgba(244,63,94,0.08)',
                            border: `1px solid ${engineOnline ? 'rgba(16,185,129,0.2)' : 'rgba(244,63,94,0.2)'}`,
                        }}
                    >
                        <div
                            className="w-2 h-2 rounded-full"
                            style={{
                                backgroundColor: engineOnline ? '#10b981' : '#f43f5e',
                                boxShadow: engineOnline ? '0 0 8px rgba(16,185,129,0.5)' : '0 0 8px rgba(244,63,94,0.5)',
                                animation: engineOnline ? 'pulse-glow 2s ease-in-out infinite' : 'none',
                            }}
                        />
                        <span className="text-xs font-semibold mono" style={{ color: engineOnline ? '#6ee7b7' : '#fda4af' }}>
                            {engineOnline ? 'ENGINE ONLINE' : 'ENGINE OFFLINE'}
                        </span>
                    </div>
                </div>
            </header>

            {/* ===== Main Content ===== */}
            <main className="flex-1 px-8 py-6">
                <div className="max-w-[1400px] mx-auto space-y-6">

                    {/* Stats Bar */}
                    <StatsBar packets={packets} nodeHealth={nodeHealth} />

                    {/* Main Grid: Topology + Inject (left) | Packets (right) */}
                    <div className="grid grid-cols-1 lg:grid-cols-5 gap-6" style={{ minHeight: 520 }}>
                        {/* Left Column — 3/5 */}
                        <div className="lg:col-span-3 flex flex-col gap-6">
                            {/* Network Topology */}
                            <section>
                                <div className="flex items-center gap-2 mb-3">
                                    <svg className="w-4 h-4 text-slate-500" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2V6zM14 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2V6zM4 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2v-2zM14 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2v-2z" />
                                    </svg>
                                    <h2 className="text-sm font-semibold text-slate-300 uppercase tracking-wider">Network Topology</h2>
                                </div>
                                <Topology nodeHealth={nodeHealth} packets={packets} />
                            </section>

                            {/* Inject Form */}
                            <InjectForm onInject={handleInject} />
                        </div>

                        {/* Right Column — 2/5 */}
                        <div className="lg:col-span-2 flex flex-col" style={{ maxHeight: 650 }}>
                            <PacketTracker packets={packets} />
                        </div>
                    </div>
                </div>
            </main>

            {/* ===== Bottom Log Panel ===== */}
            <LogViewer />
        </div>
    );
}
