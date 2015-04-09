// gl_timer.h
//
// https://github.com/tylerneylon/gl_timer
//
// Functions to help measure gpu speed.
//
// The callbacks may be called at time-sensitive points, so if you need
// to do something slow based on callback data, do it asynchronously.
//
// This interface is not thread-safe.
//
// This interface expects to be used with a smallish number of checkpoint
// names. There is no hard-coded limit, but it will eat memory if
// you use a continuously-growing list of names.
//
// Usage:
//
//   // Set up a callback, once per callback:
//   gl_timer__add_callback("A", "B", my_callback);
//
//   // In a gpu loop, make calls like this:
//   gl_timer__checkpoint("A");
//   // (make some gl calls)
//   gl_timer__checkpoint("B");
//

#pragma once

typedef void (*gl_timer__Callback)(const char *from, const char *to,
                                   double interval);

void gl_timer__add_callback(const char *from, const char *to,
                            gl_timer__Callback cb);

void gl_timer__checkpoint(const char *name);

