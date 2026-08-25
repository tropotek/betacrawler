// Runs every tests/*.playwright.py under `node --test`, so the browser suites
// cannot rot unnoticed the way they did before -- they were run by hand, so
// nothing caught an assertion that had been failing for days.
//
// Discovers the suites rather than listing them: a new *.playwright.py is
// picked up with no edit here. Skips, visibly, when Playwright is not
// installed; never passes silently on having run nothing.
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const webApp = `${here}..`;
const PY = `${homedir()}/.pwvenv/bin/python3`;
const PORT = Number(process.env.BETACRAWLER_PORT ?? 9091);
const BASE = `http://localhost:${PORT}`;

const suites = readdirSync(here).filter((f) => f.endsWith('.playwright.py')).sort();

async function reachable() {
  try {
    const res = await fetch(BASE, { signal: AbortSignal.timeout(500) });
    return res.ok;
  } catch {
    return false;
  }
}

// Reuses a server already on the port -- a dev usually has one running -- and
// otherwise starts one for the duration, so the suites need no setup.
async function withServer(fn) {
  if (await reachable()) return fn();
  const server = spawn('python3', ['-m', 'http.server', String(PORT)], {
    cwd: webApp, stdio: 'ignore',
  });
  try {
    const deadline = Date.now() + 10_000;
    while (!(await reachable())) {
      assert.ok(Date.now() < deadline, `no server on ${BASE} after 10s`);
      await new Promise((r) => setTimeout(r, 100));
    }
    return await fn();
  } finally {
    server.kill();
  }
}

test('browser suites', { concurrency: false }, async (t) => {
  assert.ok(suites.length > 0, 'no *.playwright.py suites found');

  if (!existsSync(PY)) {
    t.skip(`Playwright not installed at ${PY} -- see CLAUDE.md`);
    return;
  }

  await withServer(async () => {
    for (const suite of suites) {
      await t.test(suite, () => {
        const r = spawnSync(PY, [`${here}${suite}`], {
          cwd: webApp,
          encoding: 'utf8',
          env: { ...process.env, BETACRAWLER_BASE: BASE },
          timeout: 120_000,
        });
        const output = `${r.stdout ?? ''}${r.stderr ?? ''}`.trim();
        assert.equal(r.status, 0, `${suite} failed:\n${output}`);
      });
    }
  });
});
