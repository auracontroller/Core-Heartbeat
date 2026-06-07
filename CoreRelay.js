class CoreRelay {
    constructor(systemBootTag, transmitCallback) {
        this.systemBootTag = systemBootTag; // Immutable lock for /Commands
        this.transmit = transmitCallback;   // The external physical layer that moves the data

        this.hashBuffer = [];               // Sliding window cache
        this.MAX_BUFFER = 20;
        this.blockedPorts = new Set();      // Ports permanently blocked by Kill Switch
        this.pendingTransmissions = new Map(); // Tracks TTL for active pings
        this.txIdCounter = 0;               // Unique ID for internal tracking
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

module.exports = CoreRelay;
