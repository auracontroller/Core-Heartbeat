# CoreRelay: Speculative Diagnostic Analysis

This document provides a speculative failure analysis of the `CoreRelay` blueprint, focusing on asynchronous bottlenecks, race conditions within the horizontal processing lattice, and potential logical pitfalls based on the provided implementation and architectural rules.

## 1. Race Conditions & The Pong vs. Pulse Dilemma

### The Microtask Queue Vulnerability
The core relies on asynchronous JavaScript `await` to wait for a "pong" from the destination.
```javascript
const pong = await this.transmit(targetPort, { action: "ping" });
```
This suspends the execution of `receive()` by placing the continuation back into the event loop's microtask queue. The problem arises with the hardware clock cycle (`pulse()`).

If a `pulse()` fires synchronously just as the `await` is resolving but before the JavaScript engine executes the `.then()` continuation of the microtask, the `pulse()` will run first.
1. The `pulse()` iterates over `pendingTransmissions`, sees `pulsesSurvived >= 2`, and executes the Void Cast (truncation & error transmission). It then deletes `txId` from `pendingTransmissions`.
2. Immediately after the synchronous `pulse()` finishes, the microtask for `await this.transmit(...)` resumes.
3. The continuation checks `if (this.pendingTransmissions.has(txId))` and sees it's missing (because `pulse()` deleted it). It correctly ignores the payload relay.
4. **The Critical Flaw:** The destination *did* send a valid pong, and we technically received it (just microscopically late). The destination now expects the payload, but the Core executed a Void Cast sending a `"Time out"` error to the destination, which causes an incoherent state at the destination port (it said "ready" but immediately got "Time out").

### Mitigation Strategy
To fix this, the destination needs to be able to handle receiving a "Time out" Void Cast *after* sending a pong, meaning the destination must treat "Time out" as an absolute connection reset regardless of its current state.

## 2. Asynchronous Bottlenecks

### `await this.transmit(...)` Deadlock Risk
The architectural design states the `transmitCallback` represents the physical layer moving the data. If this physical layer is inherently slow, or if the `Promise` returned by `transmit` doesn't resolve or reject in a timely manner (e.g., due to an unhandled exception or hanging socket in the physical layer itself), the `await` expression in `receive()` will hang indefinitely.

While the `pulse()` correctly executes the Void Cast and severs the internal tracking `pendingTransmissions.delete(txId)`, the original async `receive()` execution context remains stuck in memory, waiting on a promise that will never resolve. Over time, in a high-throughput scenario with many hanging transmissions, this will cause a severe memory leak.

### Mitigation Strategy
The `pulse()` should not just "ignore" the pending transmission; it should actively force the pending promise in `receive()` to reject. This could be done by storing a `reject` function in the `pendingTransmissions` map when creating the Promise, allowing `pulse()` to trigger a hard abort and clear the dangling `receive()` context from memory.

## 3. Buffer Physics vs. Asynchronous Execution

### Strike 3 Rejection Race
The 20-slot physical conveyor belt (`hashBuffer`) is updated synchronously when `receive()` is called.
```javascript
this.hashBuffer.push(hash);
```
However, the payload processing takes time (awaiting the ping).

**Scenario:**
1. Packet A (Hash X) arrives. Pushed to buffer. `currentInstances = 1`. Ping is fired.
2. Before Pong returns, Packet B (Hash X) arrives. Pushed to buffer. `currentInstances = 2`. Ping is fired.
3. Before Pong returns, Packet C (Hash X) arrives. Pushed to buffer. `currentInstances = 3`.

At step 3, `currentInstances = 3`, so Packet C correctly triggers Strike 3: "Information has already been received."

**The Flaw:**
Packets A and B are still physically in flight. If the destination finally responds to Packet A, the payload is relayed. But the sender was just told "Information has already been received," implying failure.

If the sender sends a 4th packet, it triggers the Kill Switch, permanently blocking the port, *even if the first packet eventually succeeds*. The buffer tracks incoming requests synchronously, but resolutions are asynchronous, creating a desync between what the buffer tracks (intent) and what the network actually accomplished (state).

## 4. The Kill Switch Memory Leak
The `blockedPorts` Set grows indefinitely as malicious or malfunctioning ports are blocked.
```javascript
this.blockedPorts.add(senderPort);
```
In a long-running, public-facing engine, this Set will continuously grow, eventually consuming all available memory.

### Mitigation Strategy
Implement a TTL (Time-To-Live) or an administrative `/Command` to purge or reset the `blockedPorts` list, ensuring the system can recover physical memory over extreme lifespans.
