import axios from 'axios';

const IS_PROD = window.location.hostname !== 'localhost' && window.location.hostname !== '127.0.0.1';
const API_BASE = IS_PROD ? 'https://algoandds-backend.onrender.com' : 'http://127.0.0.1:8080';
const NODE_PORTS = [8080, 8081, 8082, 8083, 8084, 8085];

const api = axios.create({
    baseURL: API_BASE,
    timeout: IS_PROD ? 10000 : 3000,
});

// In production (Render), all nodes run inside the same container
// and only Node 0 (port 8080) is exposed externally.
// Node-specific requests go through the main API_BASE.
const getNodeUrl = (port) => {
    if (IS_PROD) return API_BASE;
    return `http://127.0.0.1:${port}`;
};

export const fetchPackets = async () => {
    try {
        const { data } = await api.get('/packets');
        return data;
    } catch (err) {
        console.error('[ADIPE] Packet fetch failed:', err.message);
        return [];
    }
};

export const fetchPacketStatus = async (packetID) => {
    try {
        const { data } = await api.get('/status', { params: { id: packetID } });
        return data;
    } catch (err) {
        console.error('[ADIPE] Status fetch failed:', err.message);
        return null;
    }
};

export const fetchNetworkLog = async () => {
    try {
        const { data } = await api.get('/log');
        return data;
    } catch (err) {
        console.error('[ADIPE] Log fetch failed:', err.message);
        return [];
    }
};

export const fetchNetworkState = async () => {
    try {
        const { data } = await api.get('/network');
        return data;
    } catch (err) {
        console.error('[ADIPE] Network state fetch failed:', err.message);
        return null;
    }
};

export const fetchNodeHealth = async (port) => {
    try {
        const { data } = await axios.get(`${getNodeUrl(port)}/check`, { timeout: IS_PROD ? 8000 : 1500 });
        return data.status === 'ONLINE';
    } catch {
        return false;
    }
};

export const checkAllNodes = async () => {
    const results = await Promise.allSettled(
        NODE_PORTS.map((port, idx) =>
            fetchNodeHealth(port).then((online) => ({ id: idx, port, online }))
        )
    );

    return results.map((result, idx) =>
        result.status === 'fulfilled'
            ? result.value
            : { id: idx, port: NODE_PORTS[idx], online: false }
    );
};

export const injectPacket = async (packetData) => {
    try {
        const { data } = await api.post('/inject', packetData);
        return data;
    } catch (err) {
        if (err.response) {
            throw new Error(`Injection failed with HTTP ${err.response.status}.`);
        }

        if (err.code === 'ECONNABORTED') {
            throw new Error('Injection timed out while waiting for the C++ engine.');
        }

        throw new Error(`Cannot reach the C++ engine at ${API_BASE}.`);
    }
};

export const setChaosMode = async (nodeID, mode) => {
    const port = NODE_PORTS[nodeID];
    const { data } = await axios.post(`${getNodeUrl(port)}/chaos`, null, {
        params: { mode },
        timeout: IS_PROD ? 10000 : 3000,
    });
    return data;
};

export const resetNetwork = async () => {
    const { data } = await api.post('/reset');
    return data;
};
