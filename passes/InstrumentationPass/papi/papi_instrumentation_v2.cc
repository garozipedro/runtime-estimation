#include <papi.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace std;

namespace Papi_instrumentation {
  const char *output_file{ "" };
  const char *current_function{ "" };
  uint64_t current_bb{ 0 };

  struct Count_state {
    long long cycles;
    int executions, pauses;
    long long cyc_start;
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
    output_file = ofname;
    return 0;
  }

  void finalize()
  {
    ofstream output{ output_file };
    output << "Runtime_data:\n"
           << "  - Instrumentation: PAPI_TOT_CYC\n";
    for (auto &[fun_name, bb_count] : counts) {
      output << "  - Function:\n"
             << "      Name: " << fun_name << '\n'
             << "      BasicBlocks:\n";
      for (auto &[bb, count] : bb_count) {
        output << "        - BasicBlock:\n"
               << "            ID: " << bb << '\n'
               << "            Runs: " << count.executions << '\n'
               << "            Pauses: " << count.pauses << '\n'
               << "            Cycles: " << count.cycles << '\n'
               << "            Average: " << (static_cast<double>(count.cycles) / count.executions) << '\n';
      }
    }
    output.close();
  }

  uint64_t resume(const char *fun_name, uint64_t bb_id)
  {
    current_function = fun_name;
    current_bb = bb_id;
    counts[current_function][current_bb].cyc_start = PAPI_get_real_cyc();
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
    counts[current_function][current_bb].cycles += PAPI_get_real_cyc() - counts[current_function][current_bb].cyc_start;
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
