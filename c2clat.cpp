// © 2020 Erik Rigtorp <erik@rigtorp.se>
// SPDX-License-Identifier: MIT

// Measure inter-core one-way data latency
//
// Build:
// g++ -O3 -DNDEBUG c2clat.cpp -o c2clat -pthread
//
// Plot results using gnuplot:
// $ c2clat -p | gnuplot -p

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

void pinThread(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_setaffinity");
    exit(1);
  }
}

int main(int argc, char *argv[]) {

  int nsamples = 1000;
  bool plot = false;
  bool smt = false;
  bool use_write = false;
  bool preheat = false;
  const char *name = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "Hn:ps:tw")) != -1) {
    switch (opt) {
    case 'H':
      preheat = true;
      break;
    case 'n':
      name = optarg;
      break;
    case 'p':
      plot = true;
      break;
    case 's':
      nsamples = std::stoi(optarg);
      break;
    case 't':
      smt = true;
      break;
    case 'w':
      use_write = true;
      break;
    default:
      goto usage;
    }
  }

  if (optind != argc) {
  usage:
    std::cerr << "c2clat 1.0.0 © 2020 Erik Rigtorp <erik@rigtorp.se>\n"
                 "usage: c2clat [-Hptw] [-n name] [-s number_of_samples]\n"
                 "Use -t to interleave hardware threads with cores.\n"
                 "The name passed using -n appears in the graph's title.\n"
                 "Use write cycles instead of read cycles with -w.\n"
                 "Use -H to preheat each core for 200ms before measuring.\n"
                 "\nPlot results using gnuplot:\n"
                 "c2clat -p | gnuplot -p\n";
    exit(1);
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == -1) {
    perror("sched_getaffinity");
    exit(1);
  }

  // enumerate available CPUs
  std::vector<int> cpus;
  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (CPU_ISSET(i, &set)) {
      cpus.push_back(i);
    }
  }

  std::map<std::pair<int, int>, std::chrono::nanoseconds> data;

  for (size_t i = 0; i < cpus.size(); ++i) {
    for (size_t j = i + 1; j < cpus.size(); ++j) {

      alignas(64) std::atomic<int> seq1 = {-1};
      alignas(64) std::atomic<int> seq2 = {-1};

      auto t = std::thread([&] {
        pinThread(cpus[i]);

        if (preheat) {
          auto init = std::chrono::steady_clock::now();
          while (1) {
            auto now = std::chrono::steady_clock::now();
            if ((now - init).count() >= 200000000)
              break;
          }
        }

        for (int m = 0; m < nsamples; ++m) {
          if (!use_write) {
            for (int n = 0; n < 100; ++n) {
              while (seq1.load(std::memory_order_acquire) != n)
                ;
              seq2.store(n, std::memory_order_release);
            }
          } else {
            while (seq2.load(std::memory_order_acquire) != 0)
              ;
            seq2.store(1, std::memory_order_release);
            for (int n = 0; n < 100; ++n) {
              int cmp;
              do {
                cmp = 2 * n;
              } while (!seq1.compare_exchange_strong(cmp, cmp + 1));
            }
          }
        }
      });

      std::chrono::nanoseconds rtt = std::chrono::nanoseconds::max();

      pinThread(cpus[j]);

      if (preheat) {
        auto init = std::chrono::steady_clock::now();
        while (1) {
          auto now = std::chrono::steady_clock::now();
          if ((now - init).count() >= 200000000)
            break;
        }
      }

      for (int m = 0; m < nsamples; ++m) {
        seq1 = seq2 = -1;
        if (!use_write) {
          auto ts1 = std::chrono::steady_clock::now();
          for (int n = 0; n < 100; ++n) {
            seq1.store(n, std::memory_order_release);
            while (seq2.load(std::memory_order_acquire) != n)
              ;
          }
          auto ts2 = std::chrono::steady_clock::now();
          rtt = std::min(rtt, ts2 - ts1);
        } else {
          // wait for the other thread to be ready
          seq2.store(0, std::memory_order_release);
          while (seq2.load(std::memory_order_acquire) == 0)
            ;
          seq2.store(-1, std::memory_order_release);
          auto ts1 = std::chrono::steady_clock::now();
          for (int n = 0; n < 100; ++n) {
            int cmp;
            do {
              cmp = 2 * n - 1;
            } while (!seq1.compare_exchange_strong(cmp, cmp + 1));
          }
          // wait for the other thread to see the last value
          while (seq1.load(std::memory_order_acquire) != 199)
            ;
          auto ts2 = std::chrono::steady_clock::now();
          rtt = std::min(rtt, ts2 - ts1);
        }
      }

      t.join();

      data[{i, j}] = rtt / 2 / 100;
      data[{j, i}] = rtt / 2 / 100;
    }
  }

  if (plot) {
    std::cout
        << "set title \"" << (name ? name : "") << (name ? " : " : "")
        << "Inter-core one-way " << (use_write ? "write" : "data")
        << " latency between CPU cores\"\n"
        << "set xlabel \"CPU\"\n"
        << "set ylabel \"CPU\"\n"
        << "set cblabel \"Latency (ns)\"\n"
        << "$data << EOD\n";
  }

  std::cout << std::setw(4) << "CPU";
  for (size_t i = 0; i < cpus.size(); ++i) {
    size_t c0 = smt ? (i >> 1) + (i & 1) * cpus.size() / 2 : i;
    std::cout << " " << std::setw(4) << cpus[c0];
  }
  std::cout << std::endl;
  for (size_t i = 0; i < cpus.size(); ++i) {
    size_t c0 = smt ? (i >> 1) + (i & 1) * cpus.size() / 2 : i;
    std::cout << std::setw(4) << cpus[c0];
    for (size_t j = 0; j < cpus.size(); ++j) {
      size_t c1 = smt ? (j >> 1) + (j & 1) * cpus.size() / 2 : j;
      std::cout << " " << std::setw(4) << data[{c0, c1}].count();
    }
    std::cout << std::endl;
  }

  if (plot) {
    std::cout << "EOD\n"
              << "set palette defined (0 '#80e0e0', 1 '#54e0eb', "
                 "2 '#34d4f3', 3 '#26baf9', 4 '#40a0ff', 5 '#5888e7', "
                 "6 '#6e72d1', 7 '#845cbb', 8 '#9848a7', 9 '#ac3493', "
                 "10 '#c0207f', 11 '#d20e6d', 12 '#e60059', 13 '#f80047', "
                 "14 '#ff0035', 15 '#ff0625', 16 '#ff2113', 17 '#ff3903', "
                 "18 '#ff5400', 19 '#ff6c00', 20 '#ff8400', 21 '#ff9c00', "
                 "22 '#ffb400', 23 '#ffcc00', 24 '#ffe400', 25 '#fffc00')\n"
              << "#set tics font \",7\"\n"
              << "plot '$data' matrix rowheaders columnheaders using 2:1:3 "
                 "notitle with image, "
                 "'$data' matrix rowheaders columnheaders using "
                 "2:1:(sprintf(\"%g\",$3)) notitle with labels #font \",5\"\n";
  }

  return 0;
}
