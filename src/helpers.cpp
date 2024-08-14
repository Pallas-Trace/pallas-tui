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

pallas_duration_t getTokenDuration(pallas::ThreadReader *thread_reader, pallas::Token token) {
  if (token.type == pallas::TypeEvent) {
    return thread_reader->
      getEventSummary(token)->
      durations->
      at(thread_reader->currentState.tokenCount[token]);
  }
  if (token.type == pallas::TypeSequence) {
    return thread_reader->
      getSequenceOccurence(token, thread_reader->currentState.tokenCount[token]).duration;
  }
  if (token.type == pallas::TypeLoop) {
    return thread_reader->getLoopDuration(token);
  }
  panic("Can't get token duration");
  return 0;
}

double getLineColor(pallas::ThreadReader *thread_reader) {
  pallas::Token current_token = thread_reader->pollCurToken();
  pallas::Token current_iterable_token = thread_reader->getCurIterable();
  size_t current_iterable_size = 0;
  if (current_iterable_token.type == pallas::TypeSequence) {
    current_iterable_size = thread_reader->
      thread_trace->
      getSequence(current_iterable_token)->
      size();
  } else if (current_iterable_token.type == pallas::TypeLoop) {
    current_iterable_size = thread_reader->
      thread_trace->
      getLoop(current_iterable_token)->
      nb_iterations
      .at(thread_reader->currentState.tokenCount[current_iterable_token]);
  } else {
    panic("Current iterable is not iterable");
  }
  pallas_assert(current_iterable_size > 0, "Current iterable size is 0");

  pallas_duration_t average_duration = getTokenDuration(thread_reader, current_iterable_token) / current_iterable_size;

  double deviation_percentage = ( (double)getTokenDuration(thread_reader, current_token) - (double)average_duration ) / (double)average_duration;

  if (deviation_percentage < GREEN_MAX_DEVIATION) return 1; // Green
  else if (deviation_percentage > YELLOW_MAX_DEVIATION) return 3; // Red
  else return 2; // Yellow
}
