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

void panic(std::string errmsg) {
  endwin();
  std::cerr << errmsg << std::endl;
  exit(1);
}

std::map<std::tuple<pallas::ThreadReader*, pallas::Token, size_t>, Histogram> memoized_histograms;

Histogram::Histogram(pallas::ThreadReader *tr, pallas::Token token, size_t nvalues) {
  pallas::LinkedVector *durations;
  if (token.type == pallas::TypeEvent) {
    durations = tr->thread_trace->getEventSummary(token)->durations;
  } else if (token.type == pallas::TypeSequence) {
    durations = tr->thread_trace->getSequence(token)->durations;
  } else if (token.type == pallas::TypeLoop) {
    panic("Cannot get histogram for loop");
  } else if (token.type == pallas::TypeInvalid) {
    panic("Encountered invalid token");
  }

  nvalues = std::min(nvalues, durations->size);

  // Check if histogram hasn't been computed yet
  if (memoized_histograms.contains(std::tuple(tr, token, nvalues))) {
    Histogram hist = memoized_histograms.at(std::tuple(tr, token, nvalues));
    this->timestep = hist.timestep;
    this->values = hist.values;
    this->min_duration = hist.min_duration;
    this->max_duration = hist.max_duration;
    return;
  }

  this->min_duration = *std::min_element(durations->begin(), durations->end());
  this->max_duration = *std::max_element(durations->begin(), durations->end());

  this->timestep = (this->max_duration - this->min_duration) / nvalues;

  if (this->timestep == 0) {
    this->values = std::vector<size_t>(1, durations->size);
    return;
  }

  this->values = std::vector<size_t>(nvalues);
  for (pallas_duration_t dur : *durations) {
    // Since duration space is now discrete, compute the index corresponding to current duration
    size_t i = std::min((dur - this->min_duration) / this->timestep, nvalues-1);
    this->values[i]++;
  }
}

Histogram::~Histogram() {
}
