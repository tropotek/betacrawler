import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const webApp = `${here}..`;
const appWeb = `${here}../../app/web`;

// The files this build keeps byte-identical to app/web/. Not every shared file
// belongs here: home.html and help.html differ deliberately (Web Serial has no
// port dropdown, and firmware updates happen in this build rather than a
// desktop app), and firmware.html differs too -- this one is DFU-only over
// WebUSB, with a device-grant button the backend build has no need for.
const COPIED = [
  'pages/config.html', 'pages/controller.html', 'pages/modes.html',
  'pages/terminal.html', 'pages/wiring.html',
  'vendor/bootstrap.min.css', 'vendor/bootstrap.bundle.min.js', 'vendor/alpine.min.js',
  'favicon.ico', 'tank-hero.svg',
];

for (const rel of COPIED) {
  test(`${rel} matches app/web/`, () => {
    assert.deepEqual(
      readFileSync(`${webApp}/${rel}`),
      readFileSync(`${appWeb}/${rel}`),
      `${rel} has drifted from app/web/${rel}: copy it across, or -- if the difference is`
      + ' deliberate -- drop it from COPIED here and say why in the same commit');
  });
}
