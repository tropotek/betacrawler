import test from 'node:test';
import assert from 'node:assert/strict';
import { LineBuffer } from '../js/line-buffer.js';

test('a chunk with no newline stays buffered', () => {
  const lb = new LineBuffer();
  assert.deepEqual(lb.push('{"id":1'), []);
});

test('a later chunk completes the buffered line', () => {
  const lb = new LineBuffer();
  lb.push('{"id":1');
  assert.deepEqual(lb.push(',"ok":true}\n'), ['{"id":1,"ok":true}']);
});

test('one chunk can contain multiple complete lines', () => {
  const lb = new LineBuffer();
  assert.deepEqual(lb.push('{"a":1}\n{"b":2}\n'), ['{"a":1}', '{"b":2}']);
});

test('content after the final newline is buffered for next time', () => {
  const lb = new LineBuffer();
  assert.deepEqual(lb.push('{"a":1}\n{"b":2'), ['{"a":1}']);
  assert.deepEqual(lb.push('}\n'), ['{"b":2}']);
});
