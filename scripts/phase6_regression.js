const http = require('http');
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const ENGINE_URL = 'http://127.0.0.1:8080';
const NODE_PORTS = [8080, 8081, 8082, 8083, 8084, 8085];
const ENGINE_PATH = path.join(
  ROOT,
  'build',
  process.platform === 'win32' ? 'dsa_project.exe' : 'dsa_project'
);

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function requestJson(method, urlString, body) {
  return new Promise((resolve, reject) => {
    const url = new URL(urlString);
    const payload = body == null ? null : JSON.stringify(body);

    const req = http.request(
      {
        hostname: url.hostname,
        port: url.port,
        path: `${url.pathname}${url.search}`,
        method,
        timeout: 3000,
        headers: payload
          ? {
              'Content-Type': 'application/json',
              'Content-Length': Buffer.byteLength(payload),
            }
          : {},
      },
      (res) => {
        let data = '';
        res.setEncoding('utf8');
        res.on('data', (chunk) => {
          data += chunk;
        });
        res.on('end', () => {
          if (res.statusCode < 200 || res.statusCode >= 300) {
            reject(new Error(`HTTP ${res.statusCode} for ${method} ${urlString}: ${data}`));
            return;
          }

          if (!data.trim()) {
            resolve({});
            return;
          }

          try {
            resolve(JSON.parse(data));
          } catch (err) {
            reject(new Error(`Invalid JSON from ${method} ${urlString}: ${err.message}`));
          }
        });
      }
    );

    req.on('timeout', () => {
      req.destroy(new Error(`Timeout for ${method} ${urlString}`));
    });
    req.on('error', reject);

    if (payload) {
      req.write(payload);
    }
    req.end();
  });
}

async function isEngineOnline() {
  try {
    const payload = await requestJson('GET', `${ENGINE_URL}/check`);
    return payload.status === 'ONLINE';
  } catch {
    return false;
  }
}

async function waitFor(fn, timeoutMs, label) {
  const deadline = Date.now() + timeoutMs;
  let lastError = null;

  while (Date.now() < deadline) {
    try {
      const result = await fn();
      if (result) {
        return result;
      }
    } catch (err) {
      lastError = err;
    }

    await sleep(250);
  }

  if (lastError) {
    throw new Error(`${label} failed: ${lastError.message}`);
  }
  throw new Error(`Timed out waiting for ${label}`);
}

async function fetchNetwork() {
  return requestJson('GET', `${ENGINE_URL}/network`);
}

async function fetchNodeNetwork(nodeID) {
  return requestJson('GET', `http://127.0.0.1:${NODE_PORTS[nodeID]}/network`);
}

async function fetchPacket(packetID) {
  return requestJson('GET', `${ENGINE_URL}/status?id=${encodeURIComponent(packetID)}`);
}

async function fetchLog() {
  return requestJson('GET', `${ENGINE_URL}/log`);
}

async function resetCluster() {
  await requestJson('POST', `${ENGINE_URL}/reset`);
  await waitFor(async () => {
    const network = await fetchNetwork();
    return network.leaderID === 5 ? network : null;
  }, 6000, 'leader election after reset');
}

async function setChaos(nodeID, mode) {
  return requestJson('POST', `http://127.0.0.1:${NODE_PORTS[nodeID]}/chaos?mode=${mode}`);
}

async function injectPacket(id, data, destinationID = 5, urgency = 20) {
  return requestJson('POST', `${ENGINE_URL}/inject`, {
    id,
    urgency,
    data,
    senderID: '0',
    signature: 0,
    destinationID,
  });
}

async function waitForPacketStatus(packetID, finalStatus) {
  return waitFor(async () => {
    const packet = await fetchPacket(packetID);
    return packet.currentStatus === finalStatus ? packet : null;
  }, 8000, `packet ${packetID} reaching ${finalStatus}`);
}

async function ensureEngine() {
  if (await isEngineOnline()) {
    return { child: null, reused: true, logs: [] };
  }

  const logs = [];
  const child = spawn(ENGINE_PATH, {
    cwd: ROOT,
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  child.stdout.on('data', (chunk) => {
    logs.push(...chunk.toString().split(/\r?\n/).filter(Boolean));
    if (logs.length > 200) {
      logs.splice(0, logs.length - 200);
    }
  });
  child.stderr.on('data', (chunk) => {
    logs.push(...chunk.toString().split(/\r?\n/).filter(Boolean));
    if (logs.length > 200) {
      logs.splice(0, logs.length - 200);
    }
  });

  await waitFor(async () => (await isEngineOnline()) ? true : null, 12000, 'engine startup');
  return { child, reused: false, logs };
}

async function scenarioLeaderFailover() {
  await resetCluster();
  await setChaos(5, 'TAMPER');

  const network = await waitFor(async () => {
    const snapshot = await fetchNetwork();
    const node5 = snapshot.nodes.find((node) => node.id === 5);
    return snapshot.leaderID === 4 && node5 && node5.quarantined ? snapshot : null;
  }, 7000, 'leader failover from Node 5 to Node 4');

  console.log('PASS leader failover:', `leader=${network.leaderID}, node5_quarantined=true`);
}

async function scenarioDelayReroute() {
  await resetCluster();
  await setChaos(1, 'DELAY');

  await waitFor(async () => {
    const network = await fetchNetwork();
    const node1 = network.nodes.find((node) => node.id === 1);
    return node1 && node1.trust < 0.99 ? network : null;
  }, 10000, 'leader-propagated delay trust for Node 1');

  const packetID = 'pkt_delay_regression';
  await injectPacket(packetID, 'SYNC_DATA');
  const packet = await waitForPacketStatus(packetID, 'DELIVERED');

  const rerouted = packet.events.some((event) =>
    event.status === 'FORWARDED' &&
    event.nodeID === 0 &&
    typeof event.detail === 'string' &&
    event.detail.includes('0->2->4->5')
  );

  if (!rerouted) {
    throw new Error(`Packet ${packetID} never used the trust-aware reroute path`);
  }

  console.log('PASS delay reroute:', packetID);
}

async function scenarioForgedDataDetection() {
  await resetCluster();

  const packetID = 'pkt_forge_manual';
  await requestJson('POST', 'http://127.0.0.1:8083/packet', {
    id: packetID,
    urgency: 18,
    data: 'FORGED_PAYLOAD',
    senderID: '1',
    signature: 12345,
    destinationID: 5,
    originNodeID: 0,
    lastHopNodeID: 1,
    lastForwardedAtMs: 0,
  });

  await waitFor(async () => {
    const logs = await fetchLog();
    return logs.some((line) => line.includes(`Forged sender identity detected on packet ${packetID}`))
      ? logs
      : null;
  }, 5000, 'forged sender detection log');

  const logs = await waitFor(async () => {
    const entries = await fetchLog();
    return entries.some(
      (line) =>
        line.includes('[Node 3] Trust[1]') &&
        line.includes('Detected forged sender identity')
    )
      ? entries
      : null;
  }, 5000, 'local forged-sender trust penalty log on Node 3');

  console.log(
    'PASS forged sender detection:',
    logs.find(
      (line) =>
        line.includes('[Node 3] Trust[1]') &&
        line.includes('Detected forged sender identity')
    )
  );
}

async function scenarioAutonomousProbes() {
  await resetCluster();

  await waitFor(async () => {
    const logs = await fetchLog();
    return logs.some((line) => line.includes('Dispatched autonomous probe')) ? logs : null;
  }, 8000, 'autonomous probe dispatch');

  console.log('PASS autonomous probes: background probe traffic observed');
}

async function main() {
  const engine = await ensureEngine();

  try {
    await scenarioLeaderFailover();
    await scenarioDelayReroute();
    await scenarioForgedDataDetection();
    await scenarioAutonomousProbes();
    console.log('PASS phase6 regression suite');
  } catch (err) {
    console.error('FAIL phase6 regression suite');
    console.error(err.message);
    if (!engine.reused && engine.logs.length > 0) {
      console.error('Recent engine logs:');
      for (const line of engine.logs.slice(-40)) {
        console.error(line);
      }
    }
    process.exitCode = 1;
  } finally {
    if (engine.child) {
      engine.child.kill();
      await sleep(500);
    }
  }
}

main();
