// Buffers decoded text between reads of a Web Serial port, which delivers
// byte chunks with no guarantee they land on newline boundaries.
export class LineBuffer {
  constructor() {
    this._buf = '';
  }

  push(chunk) {
    this._buf += chunk;
    const lines = this._buf.split('\n');
    this._buf = lines.pop();
    return lines;
  }
}
