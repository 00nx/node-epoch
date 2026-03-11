'use strict';

/**
 * node-epoch test suite
 *
 * Run with: node test/index.test.js
 *
 * Tests:
 *   1. Basic fire — timer fires within acceptable jitter window
 *   2. Past-target — fires immediately (next tick)
 *   3. Unit conversion — s / ms / us / ns all resolve to same wall-clock
 *   4. Cancellation — cleared timer does NOT fire
 *   5. Multiple concurrent timers — all fire, correct order
 *   6. getTime accuracy — within 5 ms of Date.now()
 *   7. diagnostics() shape
 *   8. Bad argument rejection — TypeError thrown for invalid inputs
 *   9. Stress test — 100 concurrent timers
 */

const assert = require('assert');
const {
  setEpochTimer,
  clearEpochTimer,
  getTime,
  diagnostics,
} = require('../index');

// ─── Helpers ────────────────────────────────────────────────────────────────

let passed = 0;
let failed = 0;

function test(name, fn) {
  const result = fn();
  const p = Promise.resolve(result);
  p.then(() => {
    console.log(`  ✓  ${name}`);
    passed++;
  }).catch((err) => {
    console.error(`  ✗  ${name}`);
    console.error(`     ${err.message || err}`);
    failed++;
  });
  return p;
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// ─── Tests ──────────────────────────────────────────────────────────────────

async function runAll() {
  console.log('\nnode-epoch test suite\n');

  // 1. Basic fire
  await test('Basic fire — fires within 30 ms of target', () =>
    new Promise((resolve, reject) => {
      const target = Date.now() + 200;
      setEpochTimer('ms', target, () => {
        const actual = Date.now();
        const jitter = Math.abs(actual - target);
        if (jitter > 30) {
          reject(new Error(`Jitter too high: ${jitter} ms (target ${target}, actual ${actual})`));
        } else {
          resolve();
        }
      });
    })
  );

  // 2. Past-target fires immediately (≤ 10 ms)
  await test('Past target — fires immediately', () =>
    new Promise((resolve, reject) => {
      const start = Date.now();
      setEpochTimer('ms', start - 1000, () => {
        const elapsed = Date.now() - start;
        if (elapsed > 10) {
          reject(new Error(`Expected immediate fire; got ${elapsed} ms delay`));
        } else {
          resolve();
        }
      });
    })
  );

  // 3. Unit conversion consistency
  await test('Unit conversion — s / ms / us / ns consistent', () => {
    const nowMs = Date.now();
    const handles = [];
    const results = {};

    return new Promise((resolve, reject) => {
      const cases = {
        s:  nowMs / 1000 + 0.1,
        ms: nowMs + 100,
        us: (nowMs + 100) * 1000,
        ns: (nowMs + 100) * 1_000_000,
      };

      let done = 0;
      for (const [unit, val] of Object.entries(cases)) {
        handles.push(
          setEpochTimer(unit, val, () => {
            results[unit] = Date.now();
            done++;
            if (done === 4) {
              // All four should have fired within 50 ms of each other
              const times = Object.values(results);
              const spread = Math.max(...times) - Math.min(...times);
              if (spread > 50) {
                reject(new Error(`Unit spread too wide: ${spread} ms`));
              } else {
                resolve();
              }
            }
          })
        );
      }
    });
  });

  // 4. Cancellation
  await test('Cancellation — cleared timer does not fire', () =>
    new Promise((resolve, reject) => {
      let fired = false;
      const handle = setEpochTimer('ms', Date.now() + 150, () => {
        fired = true;
      });
      clearEpochTimer(handle);

      setTimeout(() => {
        if (fired) {
          reject(new Error('Timer fired after clearEpochTimer'));
        } else {
          resolve();
        }
      }, 300);
    })
  );

  // 5. Multiple concurrent timers, correct order
  await test('Multiple concurrent timers — fire in order', () =>
    new Promise((resolve, reject) => {
      const order = [];
      const now = Date.now();
      setEpochTimer('ms', now + 50,  () => order.push(1));
      setEpochTimer('ms', now + 100, () => order.push(2));
      setEpochTimer('ms', now + 150, () => {
        order.push(3);
        if (order.join() !== '1,2,3') {
          reject(new Error(`Wrong order: [${order}]`));
        } else {
          resolve();
        }
      });
    })
  );

  // 6. getTime accuracy
  await test('getTime — within 5 ms of Date.now()', () => {
    const a = getTime('ms');
    const b = Date.now();
    const diff = Math.abs(a - b);
    assert.ok(diff < 5, `getTime diff ${diff} ms exceeds threshold`);
  });

  // 7. diagnostics shape
  await test('diagnostics() — correct shape', () => {
    const d = diagnostics();
    assert.strictEqual(typeof d.platform,     'string');
    assert.strictEqual(typeof d.nativeLoaded, 'boolean');
    assert.ok(d.nativeError === null || typeof d.nativeError === 'string');
  });

  // 8. Invalid argument rejection
  await test('Bad unit — throws TypeError', () => {
    assert.throws(
      () => setEpochTimer('zz', Date.now() + 100, () => {}),
      TypeError
    );
  });

  await test('Non-function callback — throws TypeError', () => {
    assert.throws(
      () => setEpochTimer('ms', Date.now() + 100, 'oops'),
      TypeError
    );
  });

  await test('NaN value — throws TypeError', () => {
    assert.throws(
      () => setEpochTimer('ms', NaN, () => {}),
      TypeError
    );
  });

  // 9. Stress test — 100 concurrent timers
  await test('Stress test — 100 concurrent timers all fire', () =>
    new Promise((resolve, reject) => {
      let count = 0;
      const total = 100;
      const now = Date.now();
      const timeout = setTimeout(() => {
        reject(new Error(`Only ${count}/${total} timers fired within 2 s`));
      }, 2000);

      for (let i = 0; i < total; i++) {
        // Spread over 0–500 ms
        setEpochTimer('ms', now + Math.random() * 500, () => {
          count++;
          if (count === total) {
            clearTimeout(timeout);
            resolve();
          }
        });
      }
    })
  );

  // ─── Summary ───────────────────────────────────────────────────────────
  // Wait for all async tests to settle
  await delay(2500);

  console.log(`\n  ${passed} passed, ${failed} failed\n`);
  if (failed > 0) process.exit(1);
}

runAll().catch((err) => {
  console.error('Unexpected test runner error:', err);
  process.exit(1);
});
