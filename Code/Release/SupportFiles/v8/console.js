var util = require('util');

exports.log = function() {
  system.write_stdout(util.format.apply(this, arguments) + '\n');
};


exports.info = exports.log;


exports.warn = function() {
  system.write_stderr(util.format.apply(this, arguments) + '\n');
};


exports.error = exports.warn;


exports.dir = function(object) {
  system.write_stdout(util.inspect(object) + '\n');
};


var times = {};
exports.time = function(label) {
  times[label] = Date.now();
};


exports.timeEnd = function(label) {
  var time = times[label];
  if (!time) {
    throw new Error('No such label: ' + label);
  }
  var duration = Date.now() - time;
  exports.log('%s: %dms', label, duration);
};


exports.trace = function(label) {
  // TODO probably can to do this better with V8's debug object once that is
  // exposed.
  var err = new Error;
  err.name = 'Trace';
  err.message = label || '';
  Error.captureStackTrace(err, arguments.callee);
  console.error(err.stack);
};


exports.assert = function(expression) {
  if (!expression) {
    var arr = Array.prototype.slice.call(arguments, 1);
    require('assert').ok(false, util.format.apply(this, arr));
  }
};
