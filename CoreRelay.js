class CoreRelay {
    constructor(systemBootTag, transmitCallback) {
        this.systemBootTag = systemBootTag; // Immutable lock for /Commands
        this.transmit = transmitCallback || (() => Promise.resolve(true));   // The external physical layer that moves the data

        this.hashBuffer = [];               // Sliding window cache
        this.MAX_BUFFER = 20;
        this.blockedPorts = new Set();      // Ports permanently blocked by Kill Switch
        this.pendingTransmissions = new Map(); // Tracks TTL for active pings
        this.txIdCounter = 0;               // Unique ID for internal tracking

        // Routing table for modules and dependencies
        this.modules = new Map();
        this.suspendedModules = new Set();
        this.moduleRotation = [];
        this.currentRotationIndex = 0;
    }

    /**
     * Registers a module and its dependencies in the routing table.
     */
    registerModule(moduleId, port, dependencies = []) {
        this.modules.set(moduleId, { id: moduleId, port, dependencies, state: 'HEALTHY' });
        if (!this.moduleRotation.includes(moduleId)) {
            this.moduleRotation.push(moduleId);
        }
    }

    /**
     * Heartbeat strike handler. Proves the core is alive and pushes data forward.
     * Volunteers any module status data independently gathered since the last beat.
     */
    receivePing() {
        this.pulse(); // Translate external strike into internal clock cycle

        const statuses = [];

        for (const [moduleId, mod] of this.modules.entries()) {
            // Do not volunteer or ping quarantined modules
            if (this.suspendedModules.has(moduleId)) continue;

            statuses.push({
                moduleId,
                port: mod.port,
                state: mod.state,
                errorCode: mod.errorCode || null
            });

            // Core's independent background check:
            // Assume silent for the next volunteered payload unless a pong is received
            // before the next heartbeat ping.
            mod.state = 'SILENT';

            // Asynchronously send a network ping to each module's port
            this.transmit(mod.port, { action: "health_ping" })
                .then(pong => {
                    if (pong) {
                        mod.state = 'HEALTHY';
                        mod.errorCode = null;
                    }
                })
                .catch(err => {
                    // Map network failures to SILENT so the double-failure logic handles them uniformly
                    mod.state = 'SILENT';
                    mod.errorCode = err.message;
                });
        }

        return {
            alive: true,
            statuses
        };
    }

    /**
     * Permanently isolates and quarantines the broken module and its dependencies.
     */
    quarantineModule(moduleId) {
        this.suspendedModules.add(moduleId);

        const moduleData = this.modules.get(moduleId);
        if (moduleData && moduleData.dependencies) {
            for (const depId of moduleData.dependencies) {
                this.quarantineModule(depId);
            }
        }
    }

    /**
     * Halts message routing strictly to the target module and any module that critically depends on it.
     */
    suspendCascade(moduleId) {
        this.suspendedModules.add(moduleId);

        // Recursively suspend anything that depends on this module
        for (const [otherId, otherData] of this.modules.entries()) {
            if (otherData.dependencies && otherData.dependencies.includes(moduleId)) {
                this.suspendCascade(otherId);
            }
        }
    }

    /**
     * Retrieves the 5-second sliding ring-buffer state for the requested module.
     */
    getTemporalAnchor(moduleId, seconds = 5) {
        // Mock returning a temporal anchor state
        return { timestamp: Date.now() - (seconds * 1000), moduleId, stateSnapshot: 'MOCKED_ANCHOR_STATE' };
    }

    /**
     * Reboots the target module, applying an optional temporal anchor.
     */
    restartModule(moduleId, anchor) {
        // Mock clearing suspension from this module and anything that depends on it
        const clearSuspension = (mId) => {
            this.suspendedModules.delete(mId);
            for (const [otherId, otherData] of this.modules.entries()) {
                if (otherData.dependencies && otherData.dependencies.includes(mId)) {
                    clearSuspension(otherId);
                }
            }
        };

        clearSuspension(moduleId);

        // Reset state
        const moduleData = this.modules.get(moduleId);
        if (moduleData) {
            moduleData.state = 'HEALTHY';
            moduleData.errorCode = null;
        }

        return true; // Assume successful restart for mock purposes
    }

    // The Kodō - The physical hardware clock cycle
    pulse() {
        for (const [txId, pending] of this.pendingTransmissions.entries()) {
            pending.pulsesSurvived++;

            // Tick 3: Pulse 2 (0 -> 1 -> 2 pulses survived) means minimum temporal lifecycle exceeded
            if (pending.pulsesSurvived >= 2) {
                // CASCADE 4: The Void Cast
                this.transmit(pending.senderPort, { error: "Receiver not responding." }); // Back to sender
                this.transmit(pending.targetPort, { error: "Time out." });                // Forward to the void

                // Sever the connection internally by removing from pending tracking
                this.pendingTransmissions.delete(txId);
            }
        }
    }

    // Core Intake - Accepts the payload and routes it through the cascade
    async receive(packet) {
        const { isCommand, hash, senderPort, targetPort, payload, bootTag } = packet;

        // Verify if port is blocked
        if (this.blockedPorts.has(senderPort)) {
            return; // Total truncation, ignore silently
        }

        // Verify if target port is mapped to a suspended module
        for (const suspendedId of this.suspendedModules) {
            const suspendedModule = this.modules.get(suspendedId);
            if (suspendedModule && suspendedModule.port === targetPort) {
                return; // Truncate messages directed to suspended cascade
            }
        }

        // CASCADE 1: The Override Intercept (Shirei)
        if (isCommand) {
            if (bootTag !== this.systemBootTag) {
                // Imposter command. Truncate and alert sender.
                this.transmit(senderPort, { error: "Invalid System Tag. Command rejected." });
                return;
            }
            // Execute absolute command logic here
            await this._executeCommand(payload, senderPort);
            return;
        }

        // CASCADE 2: Buffer Physics & Strike Triage
        const currentInstances = this.hashBuffer.filter(h => h === hash).length;

        // Push to buffer - it consumes a physical slot on the conveyor belt
        this.hashBuffer.push(hash);
        if (this.hashBuffer.length > this.MAX_BUFFER) {
            this.hashBuffer.shift(); // Violently push the oldest off the edge
        }

        const instanceNumber = currentInstances + 1;

        if (instanceNumber === 3) {
            // Instance 3: Payload is rejected
            this.transmit(senderPort, { error: "Information has already been received." });
            return;
        } else if (instanceNumber >= 4) {
            // Instance 4: The Kill Switch
            this.blockedPorts.add(senderPort);
            console.error(`Port ${senderPort} closed due to too many requests.`);
            return; // Immediate and total truncation
        }

        // Instances 1 & 2: Processed and routed normally
        // CASCADE 3: The Blind Reach (Ping/Pong)

        const txId = ++this.txIdCounter;
        this.pendingTransmissions.set(txId, {
            senderPort,
            targetPort,
            pulsesSurvived: 0
        });

        try {
            // Execute blind ping and wait for pong
            const pong = await this.transmit(targetPort, { action: "ping" });

            // If the transmission is still pending (not void-casted by pulse)
            if (this.pendingTransmissions.has(txId)) {
                if (pong) {
                    // Lock acquired. Relay data.
                    await this.transmit(targetPort, { hash, payload });
                    // Clean exit.
                    this.pendingTransmissions.delete(txId);
                }
            }
        } catch (error) {
            // If physical layer throws outside of our pulse logic, clean up
            if (this.pendingTransmissions.has(txId)) {
                this.pendingTransmissions.delete(txId);
            }
        }
    }

    // Handles the pure execution of verified Watcher/Injector commands
    async _executeCommand(commandPayload, senderPort) {
        // Example: External watcher tells core to abort a specific stuck connection
        if (commandPayload.action === "abort_connection") {
            this.transmit(commandPayload.target, { error: "Connection forcibly aborted by System Injector." });
            this.transmit(senderPort, { status: "Command executed. Target truncated." });
        }
        // Memory is instantly freed after execution.
    }
}

export default CoreRelay;
