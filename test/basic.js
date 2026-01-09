const { setEpochTimer } = require('../index.js');

console.log("Testing node-epoch-timer-win");

setEpochTimer("ms", Date.now() + 2000, () => {
  console.log("→ 2-second timer fired!", new Date().toISOString());
});

setEpochTimer("s", Math.floor(Date.now() / 1000) + 5, () => {
  console.log("→ 5-second epoch timer fired!", new Date().toISOString());
});

console.log("Timers armed – waiting...");