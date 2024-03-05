#include <papi.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace std;

namespace Papi_instrumentation {
  int event_set{ PAPI_NULL };
  const char *output_file{ "" };
  const char *current_function{ "" };
  uint64_t current_bb{ 0 };

  struct Count_state {
    long long cycles;
    int executions, pauses;
  };

  // Map of function to map of blockid to Count_state.
  // fun-id -> bb-id -> Count_state.
  unordered_map<const char *, unordered_map<uint64_t, Count_state>> counts{};

  uint64_t initialize(const char *ofname)
  {
    int retval{};
    if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT) {
      cerr <<  "Error initializing PAPI! " << PAPI_strerror(retval) << endl;
      return 1;
    }
    if ((retval = PAPI_create_eventset(&event_set)) != PAPI_OK) {
      cerr <<  "Error creating eventset! " << PAPI_strerror(retval) << endl;
      return 1;
    }
    if ((retval = PAPI_add_named_event(event_set, "PAPI_TOT_CYC")) != PAPI_OK) {
      cerr << "Error adding PAPI_TOT_CYC: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    output_file = ofname;
    return 0;
  }

  void finalize()
  {
    ofstream output{ output_file };
    for (auto &[fun_name, bb_count] : counts) {
      output << "Function [" << fun_name << "]\n";
      for (auto &[bb, count] : bb_count) {
        output << "\tBB [" << bb << "]"
               << " / RUNS = " << count.executions
               << " / PAUSES = " << count.pauses
               << " / CYCLES = " << count.cycles
               << " / AVG = " << ((double)count.cycles / count.executions) << "\n";
      }
    }
    output.close();
    PAPI_cleanup_eventset(event_set);
    PAPI_destroy_eventset(&event_set);
  }

  uint64_t resume(const char *fun_name, uint64_t bb_id)
  {
    int retval{};
    current_function = fun_name;
    current_bb = bb_id;
    PAPI_reset(event_set);
    if ((retval = PAPI_start(event_set)) != PAPI_OK) {
      cerr << "Error starting event: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    return 0;
  }

  uint64_t start(const char *fun_name, uint64_t bb_id)
  {
    auto error{ resume(fun_name, bb_id) };
    if (!error) {
      counts[current_function][current_bb].executions += 1;
    }
    return error;
  }

  uint64_t pause()
  {
    long long count{};
    int retval{};
    if ((retval = PAPI_stop(event_set, &count)) != PAPI_OK) {
      cerr << "Error stopping: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    counts[current_function][current_bb].cycles += count;
    counts[current_function][current_bb].pauses += 1;

    return 0;
  }

  long long stop()
  {
    return pause();
  }

};

extern "C" {
  uint64_t instrumentation_init(const char *output_file) {
    return Papi_instrumentation::initialize(output_file);
  }

  void instrumentation_finalize() { Papi_instrumentation::finalize(); }

  uint64_t instrumentation_start(const char *fun_name, uint64_t bb_id) {
    return Papi_instrumentation::start(fun_name, bb_id);
  }

  uint64_t instrumentation_resume(const char * fun_name, uint64_t bb_id) {
    return Papi_instrumentation::resume(fun_name, bb_id);
  }

  uint64_t instrumentation_stop() { return Papi_instrumentation::stop(); }

  uint64_t instrumentation_pause() { return Papi_instrumentation::pause(); }
}
