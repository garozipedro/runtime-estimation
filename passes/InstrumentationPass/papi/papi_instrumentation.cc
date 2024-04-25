#include <papi.h>

#include <cassert>
#include <cstdlib>
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
  bool counting{ false };

  struct Count_state {
    long long cycles;
    int executions, pauses;
  };

  // Map of function to map of blockid to Count_state.
  // fun-id -> bb-id -> Count_state.
  unordered_map<const char *, unordered_map<uint64_t, Count_state>> counts{};

  uint64_t resume(const char *fun_name, uint64_t bb_id)
  {
    if (counting) {
      cerr << "Trying to resume count without stoping previous!\n";
      abort();
    }
//    cout << "Counting cycles from function: " << fun_name << '\n';
    int retval{};
    current_function = fun_name;
    current_bb = bb_id;
    PAPI_reset(event_set);
    if ((retval = PAPI_start(event_set)) != PAPI_OK) {
      cerr << "Error starting event: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    counting = true;
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
    if (!counting) {
      cerr << "Trying to stop count without starting first!\n";
      abort();
    }
//    cout << "Counted cycles from function: " << current_function << '\n';
    long long count{};
    int retval{};
    if ((retval = PAPI_stop(event_set, &count)) != PAPI_OK) {
      cerr << "Error stopping: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    counts[current_function][current_bb].cycles += count;
    counts[current_function][current_bb].pauses += 1;
//    cout << "Count = " << count << '\n';

    counting = false;
    return 0;
  }

  long long stop()
  {
    return pause();
  }

  void finalize()
  {
    if (counting) stop();

//    cout << "Finalizing...";
    ofstream output{ output_file };
    output << "Runtime_data:\n"
           << "  Instrumentation: PAPI_TOT_CYC\n"
           << "  Functions:\n";
    for (auto &[fun_name, bb_count] : counts) {
      output << "    - Function:\n"
             << "        Name: " << fun_name << '\n'
             << "        BasicBlocks:\n";
      for (auto &[bb, count] : bb_count) {
        output << "          - BasicBlock:\n"
               << "              ID: " << bb << '\n'
               << "              Runs: " << count.executions << '\n'
               << "              Pauses: " << count.pauses << '\n'
               << "              Cycles: " << count.cycles << '\n'
               << "              Average: " << (static_cast<double>(count.cycles) / count.executions) << '\n';
      }
    }
    output.close();
    PAPI_cleanup_eventset(event_set);
    PAPI_destroy_eventset(&event_set);
//    cout << "Done!";
  }

  uint64_t initialize(const char *ofname)
  {
    int retval{};

    atexit(finalize);
//    cout << "Initializing papi...\n";
    if ((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT) {
      cerr <<  "Error initializing PAPI! " << PAPI_strerror(retval) << endl;
      return 1;
    }
    if ((retval = PAPI_create_eventset(&event_set)) != PAPI_OK) {
      cerr <<  "Error creating eventset! " << PAPI_strerror(retval) << endl;
      return 1;
    }
    if ((retval = PAPI_add_event(event_set, PAPI_TOT_CYC)) != PAPI_OK) {
      cerr << "Error adding PAPI_TOT_CYC: " << PAPI_strerror(retval) << endl;
      return 1;
    }
    output_file = ofname;
//    cout << "Done!\n";
    return 0;
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
