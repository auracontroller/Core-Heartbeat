import CoreRelay from './CoreRelay.js';
import Heartbeat from './Heartbeat.js';

// The system tag
const BOOT_TAG = 'SYS_BOOT_123';

// Simulate failure state externally to prove asynchronous health checks
let simulateFailure = false;

// A mock transmit function
const transmit = async (port, data) => {
    // console.log(`[Network] Transmitting to port ${port}:`, data);

    // Simulate network latency
    await new Promise(resolve => setTimeout(resolve, 50));

    // Simulate failure specifically for PHYSICS_ENGINE
    if (simulateFailure && port === 8081 && data.action === 'health_ping') {
        throw new Error("Network timeout");
    }

    return true; // Return true for a successful pong
};

// 1. Initialize the CoreRelay
const core = new CoreRelay(BOOT_TAG, transmit);

// 2. Register mock modules
console.log('Registering modules...');
core.registerModule('UI_RENDERER', 8080, []);
core.registerModule('PHYSICS_ENGINE', 8081, []);
core.registerModule('GAME_LOGIC', 8082, ['PHYSICS_ENGINE']);

// 3. Equip the Heartbeat
console.log('Equipping the heartbeat...');
const heartbeat = new Heartbeat(core);

// 4. Start the engine flow
heartbeat.start();

console.log('Engine is running. The heartbeat is driving the metronome.');
console.log('Wait 3 seconds, then we will simulate a network failure on PHYSICS_ENGINE...');

setTimeout(() => {
    // The Core's background transmit loop will catch this asynchronously
    console.log('\n--- SIMULATING: Disconnecting PHYSICS_ENGINE from network ---');
    simulateFailure = true;
}, 3000);

setTimeout(() => {
    // Simulate user pressing enter on stdin to break the program without testing full intervention
    console.log('\n--- Test completion reached. Exiting. ---');
    process.exit(0);
}, 12000);
