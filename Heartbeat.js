import fs from 'fs';
import path from 'path';
import readline from 'readline';

class Heartbeat {
    constructor(coreInstance, logPath = './heartbeat.log') {
        this.core = coreInstance;
        this.logPath = path.resolve(logPath);
        this.pulseInterval = null;

        // Track the dynamic state of modules independently in the cache
        this.moduleStates = new Map();

        // Track which modules are currently prompting the user to prevent prompt spam
        this.activeInterventions = new Set();

        // Initialize the clean slate protocol immediately on boot
        this.initializeLogFile();
    }

    /**
     * Wipes trailing noise from past sessions immediately upon boot.
     * Implements your total truncation strategy for file management.
     */
    initializeLogFile() {
        try {
            if (fs.existsSync(this.logPath)) {
                fs.truncateSync(this.logPath, 0);
            } else {
                fs.writeFileSync(this.logPath, '', 'utf8');
            }
            this.log('Heartbeat system initialized. Log cleared.');
        } catch (error) {
            console.error(`Failed to execute log truncation: ${error.message}`);
        }
    }

    /**
     * Writes dynamic state changes directly into the session log.
     */
    log(message) {
        const timestamp = new Date().toISOString();
        const logEntry = `[${timestamp}] ${message}\n`;
        fs.appendFileSync(this.logPath, logEntry, 'utf8');
    }

    /**
     * Launches the external metronome using absolute real-world time.
     * This keeps the heart decoupled from system or render ticks.
     */
    start() {
        if (this.pulseInterval) return;

        this.log('Heartbeat metronome started at 1000ms intervals.');

        this.pulseInterval = setInterval(() => {
            // Step 1: Strike the core to ask "Are you alive?"
            const payload = this.core.receivePing();

            if (!payload || !payload.alive) {
                this.log('CRITICAL: Core failed to respond to heartbeat pulse.');
                this.handleSystemStop('Core Unresponsive');
                return;
            }

            // Step 2: Iterate through volunteered module status data
            if (payload.statuses && payload.statuses.length > 0) {
                for (const statusBundle of payload.statuses) {
                    this.evaluateModuleState(statusBundle);
                }
            }

        }, 1000); // Absolute 1-second pulse
    }

    /**
     * Processes the fluid module statuses passed up from the core.
     */
    evaluateModuleState(statusBundle) {
        const { moduleId, port, state, errorCode } = statusBundle;

        // Ensure a tracking slot exists in the local cache
        if (!this.moduleStates.has(moduleId)) {
            this.moduleStates.set(moduleId, { missedPulses: 0 });
        }

        const track = this.moduleStates.get(moduleId);

        // Tier 1: Active reporting of a known failure
        if (state === 'FAILURE') {
            this.log(`Warning: Module ${moduleId} at Port ${port} actively reported critical error: ${errorCode}`);
            if (!this.activeInterventions.has(moduleId)) {
                this.triggerUserIntervention(moduleId, port, 'Active Logic Failure');
            }
            return;
        }

        // Tier 2: The ultimate safeguard (Silence handling)
        if (state === 'SILENT') {
            track.missedPulses += 1;

            if (track.missedPulses === 1) {
                // State 1: 1-Second Delay Warning
                this.log(`Warning: Module ${moduleId} at Port ${port} has exceeded a 1-second response time.`);
                console.warn(`[WARNING] Module ${moduleId} at Port ${port} is lagging.`);
            } else if (track.missedPulses >= 2) {
                // State 2: 2-Second Truncation Threshold
                // On double failure, intervene but DO NOT pause the metronome.
                if (track.missedPulses === 2) {
                    this.log(`CRITICAL: Module ${moduleId} at Port ${port} failed to respond within 2 seconds.`);
                }

                if (!this.activeInterventions.has(moduleId)) {
                    this.handleModuleSuspension(moduleId, port);
                }
            }
        } else if (state === 'HEALTHY') {
            // Reset tracking completely upon a successful pong sequence
            if (track.missedPulses > 0) {
                this.log(`Recovery: Module ${moduleId} at Port ${port} resynchronized successfully.`);
            }
            track.missedPulses = 0;
            this.activeInterventions.delete(moduleId); // Release intervention lock if somehow recovered
        }
    }

    /**
     * Executes the total truncation strategy, suspending the affected cascades.
     */
    handleModuleSuspension(moduleId, port) {
        this.log(`Executing total truncation cascade for Module ${moduleId}.`);

        // Command the core to hold this module and all critical dependencies
        this.core.suspendCascade(moduleId);

        this.triggerUserIntervention(moduleId, port, 'Timeout Failure');
    }

    /**
     * Hands absolute agency to the individual via the UI module interface.
     */
    triggerUserIntervention(moduleId, port, failureType) {
        this.activeInterventions.add(moduleId);

        const rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout
        });

        console.log(`\n=================================================`);
        console.log(`ALERT: Module [${moduleId}] at Port [${port}] experienced a ${failureType}.`);
        console.log(`Critical dependencies have been suspended.`);
        console.log(`Independent layers (such as Graphics/UI) remain active.`);
        console.log(`=================================================`);

        rl.question('Action required. Options: [Restart], [Shut Down], [Revert]\nEnter choice: ', (answer) => {
            const choice = answer.trim().toLowerCase();
            if (choice === 'restart') {
                rl.close();
                this.executeRecovery(moduleId, port, false);
            } else if (choice === 'revert') {
                rl.close();
                this.executeRecovery(moduleId, port, true);
            } else if (choice === 'shut down' || choice === 'shutdown') {
                this.log(`User elected Shut Down for Module ${moduleId}. Permanently quarantining.`);
                console.log(`Module ${moduleId} permanently isolated. System running on surviving modules.`);
                this.core.quarantineModule(moduleId);
                rl.close();
                this.activeInterventions.delete(moduleId); // Optional: clean up set
            } else {
                this.log(`Invalid choice. Defaulting to Shut Down for Module ${moduleId}.`);
                console.log(`Invalid input. Module ${moduleId} remains permanently isolated.`);
                this.core.quarantineModule(moduleId);
                rl.close();
                this.activeInterventions.delete(moduleId); // Optional: clean up set
            }
        });
    }

    /**
     * Reinitializes the broken cascade path and synchronizes the active cache.
     */
    executeRecovery(moduleId, port, useRollback) {
        this.log(`Initiating recovery sequence for Module ${moduleId}. Rollback enabled: ${useRollback}`);

        let targetAnchor = null;
        if (useRollback) {
            // Grab the sliding temporal anchor from 5 seconds ago out of the core cache
            targetAnchor = this.core.getTemporalAnchor(moduleId, 5);
            this.log(`Temporal anchor retrieved for Module ${moduleId}. Resetting logic stream backward 5s.`);
        }

        const restartSuccessful = this.core.restartModule(moduleId, targetAnchor);

        if (restartSuccessful) {
            this.log(`Success: Module ${moduleId} successfully restarted and resynchronized.`);
            this.moduleStates.set(moduleId, { missedPulses: 0 });
            this.activeInterventions.delete(moduleId);
            console.log(`Module ${moduleId} successfully restored. Cascade resumed.`);
        } else {
            this.log(`CRITICAL: Module ${moduleId} failed to recover during restart attempt.`);
            this.activeInterventions.delete(moduleId); // Clear lock to allow prompt again
            this.triggerUserIntervention(moduleId, port, 'Persistent Logic Failure');
        }
    }

    /**
     * Handles catastrophic base-level failures elegantly without crashing the process.
     */
    handleSystemStop(reason) {
        this.log(`System flow forced into suspension state. Reason: ${reason}`);
        console.error(`\n[SYSTEM SUSPENDED] The engine has halted core data routing: ${reason}`);
    }
}

export default Heartbeat;
