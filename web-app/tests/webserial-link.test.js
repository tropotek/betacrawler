import test from 'node:test';
import assert from 'node:assert/strict';
import { SerialLink, NotConnected, RequestTimeout } from '../js/webserial-link.js';

function makeFakePort() {
  let controller, readable, writable;
  const written = [];
  const signals = [];
  // A real SerialPort hands out fresh streams on every open(). Rebuilding
  // here is what lets one port object be connected, disconnected and
  // connected again -- the way the app actually uses it.
  const rebuild = () => {
    readable = new ReadableStream({ start(c) { controller = c; } });
    writable = new WritableStream({
      write(chunk) { written.push(new TextDecoder().decode(chunk)); },
    });
  };
  rebuild();
  return {
    written,
    signals,
    open: async () => { rebuild(); },
    setSignals: async (s) => { signals.push(s); },
    close: async () => {},
    get readable() { return readable; },
    get writable() { return writable; },
    pushLine(line) { controller.enqueue(new TextEncoder().encode(line)); },
    endReadable() { controller.close(); },
  };
}

test('request resolves once the matching id answers', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  port.pushLine('{"id":1,"ok":true,"proto":1}\n');
  const resp = await link.request('hello');
  assert.deepEqual(resp, { id: 1, ok: true, proto: 1 });
  assert.equal(link.state, 'connected');
});

test('connect() asserts DTR/RTS, without which the firmware writes nothing', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  assert.deepEqual(port.signals, [{ dataTerminalReady: true, requestToSend: true }]);
});

test('requestRaw also returns the exact sent and received lines', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  port.pushLine('{"id":1,"ok":true,"val":5}\n');
  const { sent, raw, response } = await link.requestRaw('get', { key: 'led.mode' });
  assert.equal(sent, '{"id":1,"op":"get","key":"led.mode"}');
  assert.equal(raw, '{"id":1,"ok":true,"val":5}');
  assert.equal(response.val, 5);
});

test('the raw line drops the CRLF the firmware terminates with', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  port.pushLine('{"id":1,"ok":true}\r\n');
  const { raw } = await link.requestRaw('hello');
  assert.equal(raw, '{"id":1,"ok":true}');
});

test('an unmatched id never resolves; a matching later request does', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  port.pushLine('{"id":999,"ok":true}\n'); // no pending request has this id
  port.pushLine('{"id":1,"ok":true,"val":1}\n');
  const resp = await link.request('get', { key: 'x' });
  assert.equal(resp.val, 1);
});

test('a request with no reply rejects with RequestTimeout', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  await assert.rejects(() => link.request('hello', {}, 50), RequestTimeout);
});

test('an unsolicited line (no id) publishes to subscribers as the device sent it', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  const received = [];
  link.subscribe((msg) => received.push(msg));
  await link.connect(port);
  port.pushLine('{"tlm":{"up":100}}\n');
  await new Promise((r) => setTimeout(r, 10));
  assert.deepEqual(received, [{ tlm: { up: 100 } }]);
});

test('an unparseable line is discarded without wedging the reader', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  await link.connect(port);
  port.pushLine('not json at all\n');
  port.pushLine('{"id":1,"ok":true}\n');
  const resp = await link.request('hello');
  assert.equal(resp.ok, true);
});

test('the readable stream ending marks the link disconnected and fails pending requests', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  const received = [];
  link.subscribe((msg) => received.push(msg));
  await link.connect(port);
  const pending = link.request('hello');
  port.endReadable();
  await assert.rejects(() => pending, NotConnected);
  await new Promise((r) => setTimeout(r, 10));
  assert.equal(link.state, 'disconnected');
  assert.deepEqual(received, [{ state: 'disconnected' }]);
});

test('a deliberate disconnect() publishes nothing and leaves the port reusable', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  const received = [];
  link.subscribe((msg) => received.push(msg));
  await link.connect(port);
  await link.disconnect();
  assert.equal(link.state, 'disconnected');
  assert.deepEqual(received, []); // the caller asked for this; it is not news
  await link.connect(port);
  assert.equal(link.state, 'connected');
  port.pushLine('{"id":1,"ok":true}\n');
  assert.equal((await link.request('hello')).ok, true);
});

test('a request before any connect() rejects with NotConnected', async () => {
  const link = new SerialLink();
  await assert.rejects(() => link.request('hello'), NotConnected);
});

test('subscribe returns a working unsubscribe function', async () => {
  const link = new SerialLink();
  const port = makeFakePort();
  const received = [];
  const unsubscribe = link.subscribe((msg) => received.push(msg));
  await link.connect(port);
  unsubscribe();
  port.pushLine('{"tlm":{"up":1}}\n');
  await new Promise((r) => setTimeout(r, 10));
  assert.deepEqual(received, []);
});
