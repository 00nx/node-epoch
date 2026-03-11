'use strict';

/**
 * node-epoch benchmark
 *
 * Measures timer jitter distribution over N samples.
 * Run: node test/bench.js [samples=200] [spread_ms=1000]
 *
 * Output: min/mean/max/p95/p99 jitter in milliseconds.
 */

const { setEpochTimer, getTime } = require('../index');

const SAMPLES   = parseInt(process.argv[2] ?? '200', 10);
const SPREAD_MS = parseInt(process.argv[3] ?? '1000', 10);

console.log(`\nnode-epoch benchmark — ${SAMPLES} samples over ${SPREAD_MS} ms spread\n`);

const jitters = [];

function runBench() {
  return new Promise((resolve) => {
    let done = 0;
    const now = Date.now();

    for (let i = 0; i < SAMPLES; i++) {
      const targetMs = now + 200 + (i / SAMPLES) * SPREAD_MS;
      setEpochTimer('ms', targetMs, () => {
        const actual = getTime('ms');
        jitters.push(Math.abs(actual - targetMs));
        done++;
        if (done === SAMPLES) resolve();
      });
    }
  });
}

function percentile(sorted, p) {
  const idx = Math.ceil((p / 100) * sorted.length) - 1;
  return sorted[Math.max(0, idx)].toFixed(3);
}

runBench().then(() => {
  jitters.sort((a, b) => a - b);
  const sum = jitters.reduce((a, b) => a + b, 0);

  console.log(`  samples : ${SAMPLES}`);
  console.log(`  min     : ${jitters[0].toFixed(3)} ms`);
  console.log(`  mean    : ${(sum / SAMPLES).toFixed(3)} ms`);
  console.log(`  p50     : ${percentile(jitters, 50)} ms`);
  console.log(`  p95     : ${percentile(jitters, 95)} ms`);
  console.log(`  p99     : ${percentile(jitters, 99)} ms`);
  console.log(`  max     : ${jitters[jitters.length - 1].toFixed(3)} ms`);
  console.log();
});
