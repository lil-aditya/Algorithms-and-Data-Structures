import axios from 'axios';

const API_BASE = 'http://127.0.0.1:8080';
const NODE_PORTS = [8080, 8081, 8082, 8083, 8084, 8085];

const api = axios.create({
    baseURL: API_BASE,
    timeout: 3000,
});

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
        const { data } = await axios.get(`http://127.0.0.1:${port}/check`, { timeout: 1500 });
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

        throw new Error('Cannot reach the C++ engine at http://127.0.0.1:8080.');
    }
};

export const setChaosMode = async (nodeID, mode) => {
    const port = NODE_PORTS[nodeID];
    const { data } = await axios.post(`http://127.0.0.1:${port}/chaos`, null, {
        params: { mode },
        timeout: 3000,
    });
    return data;
};

export const resetNetwork = async () => {
    const { data } = await api.post('/reset');
    return data;
};
