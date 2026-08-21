import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const webApp = `${here}..`;
const appWeb = `${here}../../app/web`;

// Copied verbatim in Task 1. help.html is edited on purpose (Task 9) and
// firmware.html is not copied at all, so neither belongs here.
const COPIED = [
  'pages/home.html', 'pages/config.html', 'pages/controller.html', 'pages/modes.html',
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
