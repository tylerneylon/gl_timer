// gl_timer.c
//
// https://github.com/tylerneylon/gl_timer
//

#include "gl_timer.h"

// Local includes.
#include "cstructs/cstructs.h"
#include "dbgcheck.h"
#include "oswrap/oswrap.h"

// Standard includes.
#include <assert.h>
#include <string.h>

#include "disable_warnings.h"

#include "include_gl.h"

#define num_timer_queries 8


// Internal globals.

static GLuint timer_queries[num_timer_queries];
static    int write_query =  0;
static    int read_query  = -1;

static const char *query_names[num_timer_queries];

// This maps checkpoint_name -> timestamp when last seen.
// We'll dynamically allocate space for a double pointed to by values and won't
// release those values as we expect a small number of checkpoint names.
static Map checkpoint_times     = NULL;

// This maps to_name -> (from_name -> callback).
static Map checkpoint_callbacks = NULL;

// The total time measured, in seconds.
static double total_time = 0;


// Internal functions.

static int str_hash(void *str_void_ptr) {
  char *str = (char *)str_void_ptr;
  int h = *str;
  while (*str) {
    h *= 84207;
    h += *str++;
  }
  return h;
}

static int str_eq(void *str_void_ptr1, void *str_void_ptr2) {
  return !strcmp(str_void_ptr1, str_void_ptr2);
}

static void init_if_needed() {
  static int is_initialized = 0;
  if (is_initialized) return;

  glGenQueries(num_timer_queries, timer_queries);

  for (int i = 0; i < num_timer_queries; ++i) {
    query_names[i] = NULL;
  }

  checkpoint_times     = map__new(str_hash, str_eq);
  checkpoint_callbacks = map__new(str_hash, str_eq);
  
  is_initialized = 1;
}

static void record_time_for_name(double time, const char *name) {
  assert(name);
  
  map__key_value *name_time_pair = map__find(checkpoint_times, (void *)name);
  if (name_time_pair == NULL) {
    // time_ptr won't be freed as we expect a small number of checkpoint names.
    double *time_ptr = (double *)dbgcheck__malloc(sizeof(double),
                                                  "gl_timer double");
    name_time_pair = map__set(checkpoint_times, (void *)name, time_ptr);
  }
  *(double *)name_time_pair->value = total_time;
}

static void end_current_query(const char *name) {
  assert(name);

  // If this is the first checkpoint, we mark the name as starting at zero
  // rather than ending a query as no query is running yet.
  if (read_query == -1) {
    record_time_for_name(0, name);
    return;
  }
  
  // We tell OpenGL to end the query, record the name of this checkpoint for
  // when the value is read back out, and then increment write_query.
  glEndQuery(GL_TIME_ELAPSED);
  if (query_names[write_query]) {
    dbgcheck__free((void *)query_names[write_query], "gl_timer name");
  }
  query_names[write_query] = dbgcheck__strdup(name, "gl_timer name");
  write_query = (write_query + 1) % num_timer_queries;
}

// This expects to only be called when the read_query is ready to be read.
static void handle_ready_read_query() {
  
  // Get read_query's time delta and update total_time.
  GLuint time_elapsed_ns;
  glGetQueryObjectuiv(timer_queries[read_query],
                      GL_QUERY_RESULT,
                      &time_elapsed_ns);
  total_time += (time_elapsed_ns / 1e9f);
  
  // Find out which callbacks end at this checkpoint.
  const char *to = query_names[read_query];
  map__key_value *pair = map__find(checkpoint_callbacks, (void *)to);
  if (pair == NULL) { goto finish_up; }  // No callbacks end at this checkpoint.
  Map cbs_from_name = (Map)pair->value;

  // Call all callbacks ending at this checkpoint.
  map__for(from_cb_pair, cbs_from_name) {
    const char *       from = (const char *)      from_cb_pair->key;
    gl_timer__Callback cb   = (gl_timer__Callback)from_cb_pair->value;
    map__key_value *name_time_pair = map__find(checkpoint_times, (void *)from);
    if (name_time_pair) {
      double from_time = *(double *)name_time_pair->value;
      cb(from, to, total_time - from_time);
    }
  }

finish_up:;
  
  record_time_for_name(total_time, to);
  read_query = (read_query + 1) % num_timer_queries;
}

static void handle_read_data_if_ready() {
  // The first checkpoint has read_query set to -1.
  if (read_query == -1) {
    read_query = 0;
    return;
  }

  GLuint is_timer_ready;
  do {
    is_timer_ready = 0;
    glGetQueryObjectuiv(timer_queries[read_query],
                        GL_QUERY_RESULT_AVAILABLE,
                        &is_timer_ready);
    if (is_timer_ready) {
      handle_ready_read_query();  // This increments read_query.
    }
  } while (is_timer_ready && read_query != write_query);
}

static void start_new_query() {
  glBeginQuery(GL_TIME_ELAPSED, timer_queries[write_query]);
}


// Public functions.

void gl_timer__add_callback(const char *from, const char *to,
                            gl_timer__Callback cb) {
  assert(from);
  assert(to);
  assert(cb);

  init_if_needed();

  // Retrieve the map (from_name -> callback).
  map__key_value *pair = map__find(checkpoint_callbacks, (void *)to);
  if (pair == NULL) {
    pair = map__set(checkpoint_callbacks,
                    (void *)to, map__new(str_hash, str_eq));
  }
  Map cbs_from_name = (Map)pair->value;

  // We expect that no callback has already be set for this from/to pair.
  assert(map__find(cbs_from_name, (void *)from) == NULL);

  map__set(cbs_from_name, (void *)from, cb);  // Add the new callback.
}

void gl_timer__checkpoint(const char *name) {
  assert(name);

  init_if_needed();

  end_current_query(name);
  handle_read_data_if_ready();
  start_new_query();
}

