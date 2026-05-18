import React, { useState } from 'react';
import { motion } from 'framer-motion';

const InjectForm = ({ onInject }) => {
    const [data, setData] = useState('');
    const [dest, setDest] = useState(5);
    const [urgency, setUrgency] = useState(10);
    const [loading, setLoading] = useState(false);
    const [success, setSuccess] = useState(false);
    const [error, setError] = useState('');

    const handleSubmit = async (e) => {
        e.preventDefault();
        if (!data.trim() || loading) return;

        setLoading(true);
        setError('');
        setSuccess(false);

        try {
            await onInject({
                id: `pkt_${Date.now().toString(36)}`,
                urgency,
                data: data.trim(),
                senderID: '0',
                signature: 0,
                destinationID: dest
            });
            setData('');
            setSuccess(true);
            setTimeout(() => setSuccess(false), 2500);
        } catch (err) {
            setError('Injection failed — is the C++ engine running?');
            setTimeout(() => setError(''), 4000);
        } finally {
            setLoading(false);
        }
    };

    const urgencyLabel = urgency <= 5 ? 'Low' : urgency <= 15 ? 'Medium' : urgency <= 30 ? 'High' : 'Critical';
    const urgencyColor = urgency <= 5 ? '#10b981' : urgency <= 15 ? '#f59e0b' : urgency <= 30 ? '#f97316' : '#ef4444';

    return (
        <form onSubmit={handleSubmit} className="glass-card p-5">
            {/* Header */}
            <div className="flex items-center gap-2 mb-5">
                <div className="w-8 h-8 rounded-lg flex items-center justify-center" style={{ background: 'rgba(59,130,246,0.15)' }}>
                    <svg className="w-4 h-4 text-blue-400" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                        <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 19l9 2-9-18-9 18 9-2zm0 0v-8" />
                    </svg>
                </div>
                <h2 className="text-lg font-bold text-slate-100">Inject Packet</h2>
            </div>

            {/* Payload */}
            <div className="mb-4">
                <label className="block text-xs font-semibold text-slate-400 mb-1.5 uppercase tracking-wider">Payload Data</label>
                <textarea
                    value={data}
                    onChange={(e) => setData(e.target.value)}
                    placeholder="REBOOT_SERVER, SYNC_DATA, HEALTH_PING..."
                    rows={3}
                    className="w-full rounded-lg p-3 text-sm mono resize-none outline-none transition-all duration-200"
                    style={{
                        background: 'rgba(15, 23, 42, 0.6)',
                        border: '1px solid rgba(55, 65, 81, 0.4)',
                        color: '#e2e8f0',
                    }}
                    onFocus={(e) => e.target.style.borderColor = 'rgba(59, 130, 246, 0.5)'}
                    onBlur={(e) => e.target.style.borderColor = 'rgba(55, 65, 81, 0.4)'}
                />
            </div>

            {/* Destination + Urgency Row */}
            <div className="grid grid-cols-2 gap-3 mb-4">
                <div>
                    <label className="block text-xs font-semibold text-slate-400 mb-1.5 uppercase tracking-wider">Destination</label>
                    <div className="grid grid-cols-5 gap-1">
                        {[1, 2, 3, 4, 5].map(n => (
                            <button
                                key={n}
                                type="button"
                                onClick={() => setDest(n)}
                                className="py-2 rounded-lg text-xs font-bold transition-all duration-200 mono"
                                style={{
                                    background: dest === n ? 'rgba(59, 130, 246, 0.2)' : 'rgba(15, 23, 42, 0.6)',
                                    border: `1px solid ${dest === n ? '#3b82f6' : 'rgba(55, 65, 81, 0.4)'}`,
                                    color: dest === n ? '#93c5fd' : '#64748b',
                                    boxShadow: dest === n ? '0 0 12px rgba(59,130,246,0.15)' : 'none',
                                }}
                            >
                                N{n}
                            </button>
                        ))}
                    </div>
                </div>

                <div>
                    <label className="block text-xs font-semibold text-slate-400 mb-1.5 uppercase tracking-wider">
                        Urgency
                        <span className="mono ml-2 font-bold" style={{ color: urgencyColor }}>
                            {urgency} — {urgencyLabel}
                        </span>
                    </label>
                    <input
                        type="range"
                        min={1} max={50}
                        value={urgency}
                        onChange={(e) => setUrgency(parseInt(e.target.value))}
                        className="w-full mt-2"
                        style={{
                            accentColor: urgencyColor,
                        }}
                    />
                    <div className="flex justify-between mt-1">
                        <span className="text-xs text-slate-600 mono">1</span>
                        <span className="text-xs text-slate-600 mono">50</span>
                    </div>
                </div>
            </div>

            {/* Submit Button */}
            <motion.button
                type="submit"
                disabled={loading || !data.trim()}
                whileTap={{ scale: 0.97 }}
                className="w-full py-3 rounded-xl font-bold text-sm tracking-wide transition-all duration-300"
                style={{
                    background: loading || !data.trim()
                        ? 'rgba(55, 65, 81, 0.4)'
                        : 'linear-gradient(135deg, #3b82f6 0%, #8b5cf6 100%)',
                    color: loading || !data.trim() ? '#64748b' : '#ffffff',
                    cursor: loading || !data.trim() ? 'not-allowed' : 'pointer',
                    border: 'none',
                    boxShadow: loading || !data.trim() ? 'none' : '0 4px 20px rgba(59,130,246,0.25)',
                }}
            >
                {loading ? (
                    <span className="flex items-center justify-center gap-2">
                        <svg className="w-4 h-4 animate-spin" viewBox="0 0 24 24" fill="none">
                            <circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="3" strokeDasharray="60" strokeLinecap="round" />
                        </svg>
                        Injecting...
                    </span>
                ) : 'Inject into Network →'}
            </motion.button>

            {/* Feedback messages */}
            {success && (
                <motion.div
                    initial={{ opacity: 0, y: 6 }}
                    animate={{ opacity: 1, y: 0 }}
                    className="mt-3 text-center text-xs font-semibold text-emerald-400"
                >
                    ✓ Packet injected successfully
                </motion.div>
            )}
            {error && (
                <motion.div
                    initial={{ opacity: 0, y: 6 }}
                    animate={{ opacity: 1, y: 0 }}
                    className="mt-3 text-center text-xs font-semibold text-rose-400"
                >
                    {error}
                </motion.div>
            )}
        </form>
    );
};

export default InjectForm;
