// Owns the Web Serial port: one read loop per connection, feeding a line
// buffer, with responses correlated back to their requests by id.
import { encode, decode, isResponse } from './protocol.js';
import { LineBuffer } from './line-buffer.js';

export class NotConnected extends Error {}
export class RequestTimeout extends Error {}

class Pending {
  constructor(sent) {
    this.sent = sent;
    this.promise = new Promise((resolve, reject) => {
      this._resolve = resolve;
      this._reject = reject;
    });
  }
  resolve(v) { this._resolve(v); }
  reject(e) { this._reject(e); }
}

export class SerialLink {
  constructor() {
    this._port = null;
    this._writer = null;
    this._reader = null;
    this._readDone = null;
    this._pending = new Map();
    this._nextId = 1;
    this._subs = [];
    this._state = 'disconnected';
    this._writeQueue = Promise.resolve();
    this._stopped = true;
  }

  get state() { return this._state; }

  subscribe(callback) {
    this._subs.push(callback);
    return () => { this._subs = this._subs.filter((cb) => cb !== callback); };
  }

  async connect(serialPort) {
    await this.disconnect();
    const t0 = performance.now();
    await serialPort.open({ baudRate: 115200 });
    const opened = performance.now();
    // pyserial raises DTR/RTS on open; Web Serial's open() has no such
    // option. The firmware's USB CDC write bails out the instant DTR reads
    // low, so a port opened without this connects and then answers nothing.
    await serialPort.setSignals({ dataTerminalReady: true, requestToSend: true });
    console.debug(`connect: port.open ${(opened - t0).toFixed(0)}ms,`
      + ` setSignals ${(performance.now() - opened).toFixed(0)}ms`);
    this._port = serialPort;
    this._writer = serialPort.writable.getWriter();
    this._stopped = false;
    this._state = 'connected';
    this._readDone = this._readLoop(serialPort);  // runs until disconnect() or a read error
  }

  async disconnect() {
    this._stopped = true;
    // Set before the streams come down: cancelling the reader wakes the read
    // loop, which calls handleDisconnect() -- and that must stay a no-op for
    // a disconnect the caller asked for, not publish a state frame.
    this._state = 'disconnected';
    const port = this._port;
    const writer = this._writer;
    const reader = this._reader;
    this._port = null;
    this._writer = null;
    this._reader = null;

    // close() rejects while either stream is still locked, and a port that
    // fails to close can never be reopened -- so every lock is released
    // before it, and a pending read or write is settled before its lock is.
    if (reader) {
      try { await reader.cancel(); } catch { /* already gone */ }
      try { reader.releaseLock(); } catch { /* already released */ }
    }
    if (this._readDone) {
      try { await this._readDone; } catch { /* the loop reports its own errors */ }
      this._readDone = null;
    }
    if (writer) {
      try { await this._writeQueue; } catch { /* that write already reported itself */ }
      try { writer.releaseLock(); } catch { /* already released */ }
    }
    if (port) {
      try { await port.close(); } catch { /* already gone */ }
    }
    this._failAllPending();
  }

  async request(op, fields = {}, timeoutMs = 1000) {
    return (await this._doRequest(op, fields, timeoutMs)).response;
  }

  async requestRaw(op, fields = {}, timeoutMs = 1000) {
    return this._doRequest(op, fields, timeoutMs);
  }

  async _doRequest(op, fields, timeoutMs) {
    if (!this._port || this._state !== 'connected') {
      throw new NotConnected('no serial connection');
    }
    const id = this._nextId;
    this._nextId = (this._nextId % 65535) + 1;

    const line = encode(id, op, fields);
    const sent = line.trimEnd();
    const slot = new Pending(sent);
    this._pending.set(id, slot);

    const writer = this._writer;
    this._writeQueue = this._writeQueue.then(() => writer.write(new TextEncoder().encode(line)));
    try {
      await this._writeQueue;
    } catch (exc) {
      this._pending.delete(id);
      await this.handleDisconnect();
      throw new NotConnected(`write failed: ${exc.message}`);
    }

    let timer;
    const timeoutPromise = new Promise((_, reject) => {
      timer = setTimeout(
        () => reject(new RequestTimeout(`no response to ${op} within ${timeoutMs}ms`)),
        timeoutMs);
    });
    try {
      const { raw, response } = await Promise.race([slot.promise, timeoutPromise]);
      return { sent, raw, response };
    } finally {
      clearTimeout(timer);
      this._pending.delete(id);
    }
  }

  // `afterSend` separates these from a write that never left: the request is
  // known to be on the wire, so the device may well have acted on it. `dfu` is
  // the op that turns on the difference -- a board that resets before its ack
  // flushes did exactly what was asked.
  _failAllPending() {
    const waiters = Array.from(this._pending.values());
    this._pending.clear();
    for (const slot of waiters) {
      const exc = new NotConnected('connection lost while waiting');
      exc.afterSend = true;
      slot.reject(exc);
    }
  }

  async handleDisconnect() {
    if (this._state === 'disconnected') return;
    this._state = 'disconnected';
    this._stopped = true;
    this._failAllPending();
    // link.py's own shape. api.js turns it into the {type, data} frame the UI
    // consumes, the same way main.py's Broadcaster does.
    this._publish({ state: 'disconnected' });
  }

  _publish(msg) {
    for (const cb of this._subs.slice()) {
      try { cb(msg); } catch (exc) { console.error('subscriber raised', exc); }
    }
  }

  async _readLoop(port) {
    const reader = port.readable.getReader();
    this._reader = reader;
    const decoder = new TextDecoder();
    const lineBuffer = new LineBuffer();
    while (!this._stopped) {
      let result;
      try {
        result = await reader.read();
      } catch {
        await this.handleDisconnect();
        return;
      }
      if (result.done) {
        await this.handleDisconnect();
        return;
      }
      const chunk = decoder.decode(result.value, { stream: true });
      for (const line of lineBuffer.push(chunk)) {
        const raw = line.trim();   // the firmware terminates its lines with CRLF
        let msg;
        try {
          msg = decode(raw);
        } catch {
          continue; // one bad line must never wedge the reader
        }
        if (isResponse(msg)) {
          const slot = this._pending.get(msg.id);
          if (slot) slot.resolve({ raw, response: msg });
        } else {
          this._publish(msg);
        }
      }
    }
  }
}
