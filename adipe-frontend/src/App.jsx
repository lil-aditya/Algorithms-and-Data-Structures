import React, { useEffect, useState } from 'react';
import {
    checkAllNodes,
    fetchNetworkState,
    fetchPackets,
    injectPacket,
    resetNetwork,
    setChaosMode,
} from './api';
import ChaosControls from './components/ChaosControls';
import InjectForm from './components/InjectForm';
import LogViewer from './components/LogViewer';
import PacketTracker from './components/PacketTracker';
import StatsBar from './components/StatsBar';
import Topology from './components/Topology';
import TrustMatrix from './components/TrustMatrix';

export default function App() {
    const [packets, setPackets] = useState([]);
    const [nodeHealth, setNodeHealth] = useState({});
    const [networkState, setNetworkState] = useState(null);
    const [engineOnline, setEngineOnline] = useState(false);
    const [busyNodeID, setBusyNodeID] = useState(null);
    const [resetBusy, setResetBusy] = useState(false);

    useEffect(() => {
        const pollPackets = async () => {
            const data = await fetchPackets();
            setPackets(data);
        };

        pollPackets();
        const interval = setInterval(pollPackets, 1500);
        return () => clearInterval(interval);
    }, []);

    useEffect(() => {
        const pollNetwork = async () => {
            const [results, snapshot] = await Promise.all([
                checkAllNodes(),
                fetchNetworkState(),
            ]);

            const health = {};
            let anyOnline = false;
            results.forEach((result) => {
                health[result.id] = result.online;
                if (result.online) {
                    anyOnline = true;
                }
            });

            setNodeHealth(health);
            setNetworkState(snapshot);
            setEngineOnline(anyOnline);
        };

        pollNetwork();
        const interval = setInterval(pollNetwork, 2500);
        return () => clearInterval(interval);
    }, []);

    const refreshPacketsAndNetwork = async () => {
        const [packetData, snapshot] = await Promise.all([
            fetchPackets(),
            fetchNetworkState(),
        ]);
        setPackets(packetData);
        setNetworkState(snapshot);
    };

    const handleInject = async (packetData) => {
        await injectPacket(packetData);
        await refreshPacketsAndNetwork();
    };

    const handleModeChange = async (nodeID, mode) => {
        try {
            setBusyNodeID(nodeID);
            const snapshot = await setChaosMode(nodeID, mode);
            setNetworkState(snapshot);
            await refreshPacketsAndNetwork();
        } finally {
            setBusyNodeID(null);
        }
    };

    const handleReset = async () => {
        try {
            setResetBusy(true);
            const snapshot = await resetNetwork();
            setNetworkState(snapshot);
            await refreshPacketsAndNetwork();
        } finally {
            setResetBusy(false);
        }
    };

    return (
        <div className="min-h-screen flex flex-col pb-12">
            <header className="px-8 pt-8 pb-2">
                <div className="max-w-[1480px] mx-auto flex items-center justify-between">
                    <div>
                        <div className="flex items-center gap-3">
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
                            Autonomous adversarial network monitor with leader election and trust-aware routing
                        </p>
                    </div>

                    <div
                        className="flex items-center gap-2 px-4 py-2 rounded-xl"
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

            <main className="flex-1 px-8 py-6">
                <div className="max-w-[1480px] mx-auto space-y-6">
                    <StatsBar packets={packets} nodeHealth={nodeHealth} networkState={networkState} />

                    <div className="grid grid-cols-1 lg:grid-cols-5 gap-6">
                        <div className="lg:col-span-3 flex flex-col gap-6">
                            <section>
                                <div className="flex items-center gap-2 mb-3">
                                    <svg className="w-4 h-4 text-slate-500" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2V6zM14 6a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2V6zM4 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2H6a2 2 0 01-2-2v-2zM14 16a2 2 0 012-2h2a2 2 0 012 2v2a2 2 0 01-2 2h-2a2 2 0 01-2-2v-2z" />
                                    </svg>
                                    <h2 className="text-sm font-semibold text-slate-300 uppercase tracking-wider">Adaptive Network Topology</h2>
                                </div>
                                <Topology nodeHealth={nodeHealth} packets={packets} networkState={networkState} />
                            </section>

                            <InjectForm onInject={handleInject} />
                            <ChaosControls
                                networkState={networkState}
                                onModeChange={handleModeChange}
                                onReset={handleReset}
                                busyNodeID={busyNodeID}
                                resetBusy={resetBusy}
                            />
                        </div>

                        <div className="lg:col-span-2 flex flex-col" style={{ maxHeight: 880 }}>
                            <PacketTracker packets={packets} />
                        </div>
                    </div>

                    <TrustMatrix networkState={networkState} />
                </div>
            </main>

            <LogViewer />
        </div>
    );
}
