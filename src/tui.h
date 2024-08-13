#pragma once

#include "pallas/pallas_archive.h"
#include "pallas/pallas_read.h"
#include <curses.h>
#include <vector>

class PallasExplorer {
public:
  PallasExplorer(const pallas::GlobalArchive& global_archive);
  PallasExplorer(PallasExplorer &&) = default;
  PallasExplorer(const PallasExplorer &) = default;
  PallasExplorer &operator=(PallasExplorer &&) = default;
  PallasExplorer &operator=(const PallasExplorer &) = default;
  ~PallasExplorer() = default;

  bool updateWindow();

private:
  pallas::GlobalArchive global_archive;
  std::vector<std::vector<pallas::ThreadReader>> readers;
  std::vector<std::vector<size_t>> current_trace_offsets;
  size_t current_archive_index;
  size_t current_thread_index;

  WINDOW *trace_container;
  WINDOW *token_container;

  WINDOW *trace_viewer;
  WINDOW *token_viewer;

  bool enable_timestamps;
  int reader_flag;

  void renderTraceWindow(pallas::ThreadReader *tr);
  void renderTokenWindow(pallas::ThreadReader *tr);

};
