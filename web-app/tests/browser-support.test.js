import test from 'node:test';
import assert from 'node:assert/strict';

import { assessBrowser } from '../js/browser-support.js';

const UA = {
  chrome: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36',
  edge: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36 Edg/141.0.0.0',
  opera: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36 OPR/127.0.0.0',
  firefox: 'Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0',
  safari: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15',
  iosSafari: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Mobile/15E148 Safari/604.1',
  iosChrome: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/141.0.0.0 Mobile/15E148 Safari/604.1',
  androidChrome: 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Mobile Safari/537.36',
};

const supported = { hasSerial: true, hasUsb: true, isSecureContext: true };

test('a Chromium browser with both APIs is waved through silently', () => {
  for (const name of ['chrome', 'edge', 'opera']) {
    const v = assessBrowser({ userAgent: UA[name], ...supported });
    assert.equal(v.ok, true, name);
    assert.equal(v.level, 'ok', name);
    assert.equal(v.message, '', `${name} must not be told anything`);
  }
});

test('Firefox and Safari are blocked and named', () => {
  for (const name of ['firefox', 'safari']) {
    const v = assessBrowser({ userAgent: UA[name], hasSerial: false, hasUsb: false });
    assert.equal(v.level, 'block', name);
    assert.equal(v.browser, name);
    assert.match(v.message, /Chrome, Edge, Brave or Opera/);
  }
});

// The message people most often get wrong: they are already in Chrome, and
// being told to install Chrome sends them in a circle.
test('mobile is told the platform is the problem, not the browser', () => {
  for (const name of ['iosSafari', 'iosChrome', 'androidChrome']) {
    const v = assessBrowser({ userAgent: UA[name], hasSerial: false, hasUsb: false });
    assert.equal(v.level, 'block', name);
    assert.match(v.message, /on a computer/, `${name} must be sent to a desktop`);
    assert.doesNotMatch(v.message, /^This browser has no/, `${name} must not blame the browser`);
  }
  assert.equal(assessBrowser({ userAgent: UA.iosChrome }).browser, 'ios',
               'Chrome on iOS is iOS, not Chrome');
  assert.equal(assessBrowser({ userAgent: UA.androidChrome }).browser, 'android',
               'Chrome on Android is Android, not Chrome');
});

// An insecure context hides Web Serial in Chrome too, so feature detection
// alone would tell a Chrome user to go and install Chrome.
test('an insecure context is reported as the cause, ahead of the browser', () => {
  const v = assessBrowser({ userAgent: UA.chrome, hasSerial: false, hasUsb: false, isSecureContext: false });
  assert.equal(v.level, 'block');
  assert.match(v.message, /https:\/\/|localhost/);
  assert.doesNotMatch(v.message, /Chrome, Edge, Brave or Opera/,
                      'the browser is not the problem here');
});

test('a secure context is assumed when the caller does not say', () => {
  assert.equal(assessBrowser({ userAgent: UA.chrome, hasSerial: true, hasUsb: true }).level, 'ok');
});

test('a Chromium browser missing the API is told it was switched off', () => {
  const v = assessBrowser({ userAgent: UA.chrome, hasSerial: false, hasUsb: false });
  assert.equal(v.level, 'block');
  assert.match(v.message, /policy or by a flag/);
});

// Web Serial but no WebUSB: everything works except the Firmware page, so this
// warns rather than blocking -- the connect button must stay alive.
test('missing WebUSB warns about flashing without blocking the app', () => {
  const v = assessBrowser({ userAgent: UA.chrome, hasSerial: true, hasUsb: false });
  assert.equal(v.level, 'warn');
  assert.equal(v.ok, false);
  assert.match(v.message, /cannot flash/);
});

test('an unrecognised browser is judged on its APIs, not its name', () => {
  assert.equal(assessBrowser({ userAgent: 'Emacs/30.1', ...supported }).level, 'ok');
  const v = assessBrowser({ userAgent: 'Emacs/30.1', hasSerial: false, hasUsb: false });
  assert.equal(v.level, 'block');
  assert.equal(v.browser, 'unknown');
});

test('no arguments at all is a block rather than a crash', () => {
  const v = assessBrowser();
  assert.equal(v.level, 'block');
  assert.ok(v.message, 'there is always something to say');
});
