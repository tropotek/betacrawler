// JSON-lines codec. Mirrors app/backend/protocol.py and firmware/src/core/protocol.cpp.

export class ProtocolError extends Error {}

export function encode(id, op, fields = {}) {
  const msg = { id, op };
  for (const [key, val] of Object.entries(fields)) {
    if (val !== undefined && val !== null) msg[key] = val;
  }
  return JSON.stringify(msg) + '\n';
}

export function decode(line) {
  const trimmed = line.trim();
  if (!trimmed) throw new ProtocolError('empty line');
  let obj;
  try {
    obj = JSON.parse(trimmed);
  } catch (exc) {
    throw new ProtocolError(`bad json: ${exc.message}`);
  }
  if (typeof obj !== 'object' || obj === null || Array.isArray(obj)) {
    throw new ProtocolError('message is not a JSON object');
  }
  return obj;
}

// An id means it answers a request. No id means the device volunteered it --
// that distinction is what lets telemetry interleave with request/response.
export function isResponse(msg) { return 'id' in msg; }
export function isTelemetry(msg) { return !('id' in msg) && 'tlm' in msg; }
export function isLog(msg) { return !('id' in msg) && 'log' in msg; }
