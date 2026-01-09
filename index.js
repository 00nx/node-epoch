const addon = require('./build/Release/epoch_timer.node');

module.exports = {
  setEpochTimer: addon.setEpochTimer
};