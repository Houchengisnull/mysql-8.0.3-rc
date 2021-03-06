/* Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA */

/**
  @file benchmark.h

  Rudimentary microbenchmark framework. The API is generally a minimal
  subset of Google's microbenchmark framework, in order to be compatible
  if we should ever import the full one.
*/

#ifndef BENCHMARK_H_INCLUDED
#define BENCHMARK_H_INCLUDED

#include <gtest/gtest.h>

// Auto-register benchmarks as regular tests for now, piggybacking on
// Google Test's registration framework. Eventually, we should make it
// possible to run only benchmarks separate from unit tests (as well as
// making a nicely printed table), but that would necessitate actually
// making a registration framework.
#define BENCHMARK(func) TEST(Microbenchmarks, func) { \
  internal_do_microbenchmark(#func, func); \
}

void internal_do_microbenchmark(const char *name, void (*func)(size_t));

void StartBenchmarkTiming();
void StopBenchmarkTiming();
void SetBytesProcessed(size_t num_bytes);

#endif  // BENCHMARK_H_INCLUDED
