import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const bundle = `${here}../firmware`;
const firmware = `${here}../../firmware`;

// Mirrors fw_source_sha256() in app/tools/bundle_firmware.py: the same files,
// the same path-then-contents hashing, the same sort. The two must agree or
// the guard below is worthless.
const FW_SOURCE_DIRS = ['include', 'src'];
const FW_SOURCE_FILES = ['platformio.ini'];

function walk(dir, out) {
  for (const name of readdirSync(dir)) {
    const full = `${dir}/${name}`;
    if (statSync(full).isDirectory()) walk(full, out);
    else out.push(full);
  }
  return out;
}

function fwSourceSha256() {
  const files = [];
  for (const dir of FW_SOURCE_DIRS) walk(`${firmware}/${dir}`, files);
  for (const file of FW_SOURCE_FILES) files.push(`${firmware}/${file}`);
  const rel = (p) => p.slice(firmware.length + 1);
  files.sort((a, b) => (rel(a) < rel(b) ? -1 : rel(a) > rel(b) ? 1 : 0));
  const digest = createHash('sha256');
  for (const path of files) {
    digest.update(rel(path));
    digest.update(readFileSync(path));
  }
  return digest.digest('hex');
}

const manifest = JSON.parse(readFileSync(`${bundle}/manifest.json`, 'utf8'));

test('the committed binaries were built from the current firmware sources', () => {
  assert.equal(
    manifest.fw_source_sha256, fwSourceSha256(),
    'the firmware sources have changed since these binaries were built -- re-run'
    + ' `python3 app/tools/bundle_firmware.py blackpill_f411ce blackpill_f401ce`'
    + ' and commit web-app/firmware/');
});

test('every manifest image is present, the right size, and matches its sha256', () => {
  assert.ok(manifest.images.length > 0, 'the manifest lists no images');
  for (const img of manifest.images) {
    const blob = readFileSync(`${bundle}/${img.file}`);
    assert.equal(blob.length, img.size, `${img.file} is the wrong size`);
    assert.equal(createHash('sha256').update(blob).digest('hex'), img.sha256,
                 `${img.file} does not match its manifest sha256`);
  }
});
