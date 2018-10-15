// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#define NOMINMAX
#include <inttypes.h>

#include <gtest/gtest.h>
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "glog/raw_logging.h"

#ifdef WIN32
#include "environment/environment_windows.h"
#else
#include "environment/environment_linux.h"
#endif
#include "benchmarks/benchmark.h"
#include "include/pmwcas.h"
#include "mwcas/mwcas.h"
#include "mwcas/FASASDescriptor.h"
#include "util/auto_ptr.h"

using namespace pmwcas::benchmark;

namespace pmwcas {
typedef MwcTargetField<uint64_t> CasPtr;
typedef FASASTargetField<uint64_t> FASASCasPtr;


struct BaseMwCas : public Benchmark {
  BaseMwCas()
    : Benchmark()
    , previous_dump_run_ticks_()
    , cumulative_stats_ () {
    total_success_ = 0;
  }

  uint64_t GetOperationCount() {
    MwCASMetrics metrics;
    MwCASMetrics::Sum(metrics);
    return metrics.GetUpdateAttemptCount();
  }

  uint64_t GetTotalSuccess() {
    return total_success_.load();
  }

  virtual void Dump(size_t thread_count, uint64_t run_ticks, uint64_t dump_id,
      bool final_dump) {
    MARK_UNREFERENCED(thread_count);
    uint64_t interval_ticks = 0;
    if(final_dump) {
      interval_ticks = run_ticks;
    } else {
      interval_ticks = run_ticks - previous_dump_run_ticks_;
      previous_dump_run_ticks_ = run_ticks;
    }
    Benchmark::Dump(thread_count, run_ticks, dump_id, final_dump);

    MwCASMetrics stats;
    MwCASMetrics::Sum(stats);
    if(!final_dump) {
      stats -= cumulative_stats_;
      cumulative_stats_ += stats;
    }

    stats.Print();

#ifdef WIN32
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    uint64_t ticks_per_second = frequency.QuadPart;
#else
    uint64_t ticks_per_second = 1000000;
#endif
    std::cout << "> Benchmark " << dump_id << " TicksPerSecond " <<
      ticks_per_second << std::endl;
    std::cout << "> Benchmark " << dump_id << " RunTicks " <<
      run_ticks << std::endl;

    double run_seconds = (double)run_ticks / ticks_per_second;
    std::cout << "> Benchmark " << dump_id << " RunSeconds " <<
      run_seconds << std::endl;

    std::cout << "> Benchmark " << dump_id << " IntervalTicks " <<
      interval_ticks << std::endl;
    double interval_seconds = (double)interval_ticks / ticks_per_second;
    std::cout << "> Benchmark " << dump_id << " IntervalSeconds " <<
      interval_seconds << std::endl;

    for(const auto & idOpNum : threadIdOpNumMap_) {
        std::cout << "threadId:" << idOpNum.first << ", opNum:" << idOpNum.second << std::endl;
    }

      
  }


  uint64_t previous_dump_run_ticks_;
  MwCASMetrics cumulative_stats_;
  std::atomic<uint64_t> total_success_;
  std::unordered_map<size_t, uint64_t> threadIdOpNumMap_;
};

}  // namespace pmwcas
