import test from 'node:test';
import assert from 'node:assert/strict';

import { buildChannel } from '../js/build-channel.js';

test('the /app-dev/ deployment is the dev channel', () => {
  for (const path of ['/betacrawler/app-dev/', '/betacrawler/app-dev/index.html',
                      '/app-dev/', '/app-dev']) {
    assert.equal(buildChannel(path), 'dev', path);
  }
});

test('the released configurator says nothing', () => {
  for (const path of ['/betacrawler/app/', '/betacrawler/app/index.html', '/app/']) {
    assert.equal(buildChannel(path), 'release', path);
  }
});

test('a local server and a fork at the site root are not the dev channel', () => {
  for (const path of ['/', '', '/index.html']) {
    assert.equal(buildChannel(path), 'release', JSON.stringify(path));
  }
});

test('only a whole path segment counts', () => {
  for (const path of ['/app-development/', '/my-app-dev/', '/app-dev-old/', '/appdev/']) {
    assert.equal(buildChannel(path), 'release', path);
  }
});

test('a missing or non-string pathname is not the dev channel', () => {
  assert.equal(buildChannel(), 'release');
  assert.equal(buildChannel(undefined), 'release');
  assert.equal(buildChannel(null), 'release');
});
