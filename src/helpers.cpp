#include "helpers.h"
#include "pallas/pallas.h"
#include "pallas/pallas_linked_vector.h"
#include "pallas/pallas_read.h"
#include "pallas/pallas_timestamp.h"

#include <algorithm>
#include <cstddef>
#include <curses.h>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

void panic(const std::string& errmsg) {
  endwin();
  std::cerr << errmsg << std::endl;
  exit(1);
}

std::map<std::tuple<pallas::ThreadReader*, pallas::Token, size_t>, Histogram> memoized_histograms;

Histogram::Histogram(pallas::ThreadReader *tr, pallas::Token token, size_t nvalues) {
  pallas::LinkedVector *durations = nullptr;
  switch (token.type) {
      case pallas::TypeEvent:
          durations = tr->thread_trace->getEventSummary(token)->durations;
          break;
      case pallas::TypeSequence:
          durations = tr->thread_trace->getSequence(token)->durations;
          break;
      case pallas::TypeLoop:
          panic("Cannot get histogram for loop.");
      case pallas::TypeInvalid:
          panic("Encountered invalid token.");
  }

  if (durations == nullptr) {
      panic("Duration array wasn't loaded.");
      return;
  }

  nvalues = std::min(nvalues, durations->size);

  // Check if histogram hasn't been computed yet
  if (memoized_histograms.contains(std::tuple(tr, token, nvalues))) {
    Histogram hist = memoized_histograms.at(std::tuple(tr, token, nvalues));
    timestep = hist.timestep;
    values = hist.values;
    min_duration = hist.min_duration;
    max_duration = hist.max_duration;
    return;
  }

  min_duration = *std::min_element(durations->begin(), durations->end());
  max_duration = *std::max_element(durations->begin(), durations->end());

  timestep = (max_duration - min_duration) / nvalues;

  if (timestep == 0) {
    values = std::vector<size_t>(1, durations->size);
    return;
  }

  values = std::vector<size_t>(nvalues);
  for (pallas_duration_t dur : *durations) {
    // Since duration space is now discrete, compute the index corresponding to current duration
    size_t i = std::min((dur - min_duration) / timestep, nvalues-1);
    values[i]++;
  }
}

Histogram::~Histogram() {
}
