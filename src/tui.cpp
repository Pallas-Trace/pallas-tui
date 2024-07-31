#include "tui.h"
#include "helpers.h"

#include "pallas/pallas.h"
#include "pallas/pallas_archive.h"
#include "pallas/pallas_read.h"
#include "pallas/pallas_timestamp.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <curses.h>
#include <vector>

#define DESCRIPTION_BUFFER_SIZE 1024

void wprintwToken(WINDOW *win, const pallas::Token &tok) {
  switch (tok.type) {
  case pallas::TypeEvent:
    wprintw(
        win,
        "Event %d",
        tok.id
    );
    break;
  case pallas::TypeSequence:
    wprintw(
        win,
        "Sequence %d",
        tok.id
    );
    break;
  case pallas::TypeLoop:
    wprintw(
        win,
        "Loop %d",
        tok.id
    );
    break;
  case pallas::TypeInvalid:
    panic("Encountered invalid token");
  }
}

PallasExplorer::PallasExplorer(pallas::GlobalArchive global_archive) {
  // Object initialisation
  this->global_archive = global_archive;

  this->current_archive_index = 0;
  this->current_thread_index = 0;
  this->frame_begin_index = 0;

  this->readers = std::vector<std::vector<pallas::ThreadReader>>(global_archive.nb_archives);
  int reader_options = pallas::ThreadReaderOptions::None;
  pallas_assert(global_archive.nb_archives > 0, "Malformed archive");
  for (size_t i = 0; i < global_archive.nb_archives; i++) {
    auto archive = global_archive.archive_list[i];
    pallas_assert(archive->nb_threads > 0, "Malformed archive");
    this->readers[i] = std::vector<pallas::ThreadReader>();
    for (size_t j = 0; j < archive->nb_threads; j++) {
      if (archive->threads[j])
        this->readers[i].emplace_back(global_archive.archive_list[i], global_archive.archive_list[i]->threads[j]->id, reader_options);
    }
  }

  // Ncurses initialisation
  initscr();
  cbreak();
  noecho();
  curs_set(0);

  // Viewers initialisation

  int x, y;
  getmaxyx(stdscr, y, x);

  this->trace_container = newwin(y, x/2, 0, 0);
  this->token_container = newwin(y, x/2, 0, x/2);

  box(trace_container, 0, 0);
  box(token_container, 0, 0);
  wrefresh(trace_container);
  wrefresh(token_container);

  this->trace_viewer = derwin(trace_container, y - 2, x/2 - 2, 1, 1);
  this->token_viewer = derwin(token_container, y - 2, x/2 - 2, 1, 1);

  keypad(this->trace_viewer, TRUE);
}

bool PallasExplorer::updateWindow() {
  pallas::ThreadReader &thread_reader = this->readers[current_archive_index][current_thread_index];

  this->renderTraceWindow(&thread_reader);
  this->renderTokenWindow(&thread_reader);

  pallas::Token tok = thread_reader.pollCurToken();

  int ch = wgetch(trace_viewer);
  switch (ch) {
  case 'q':
    return false;
  case 'h':
  case KEY_LEFT:
    if (thread_reader.current_frame > 0) thread_reader.leaveBlock();
    break;
  case 'j':
  case KEY_DOWN:
  case KEY_STAB:
    if (!thread_reader.isEndOfCurrentBlock()) thread_reader.moveToNextToken();
    break;
  case 'k':
  case KEY_UP:
    if (thread_reader.callstack_index[thread_reader.current_frame] > 0) thread_reader.moveToPrevToken();
    break;
  case 'l':
  case KEY_RIGHT:
    if (tok.isIterable()) thread_reader.enterBlock(tok);
    break;
  case '>':
    this->current_thread_index = (this->current_thread_index + 1) % this->readers[current_archive_index].size();
    break;
  case '<':
    this->current_thread_index = (this->current_thread_index - 1) % this->readers[current_archive_index].size();
    break;
  case '\t':
    this->current_archive_index = (this->current_archive_index + 1) % this->readers.size();
  }
  return true;
}

void PallasExplorer::renderTraceWindow(pallas::ThreadReader *thread_reader) {
  size_t current_callstack_index = thread_reader->callstack_index[thread_reader->current_frame];
  if (current_callstack_index < this->frame_begin_index) {
    this->frame_begin_index = current_callstack_index;
  } else if (current_callstack_index >= this->frame_begin_index + getmaxy(this->trace_viewer) - 1) {
    this->frame_begin_index = current_callstack_index - getmaxy(this->trace_viewer) + 2;
  }

  werase(trace_viewer);

  wattron(this->trace_viewer, A_BOLD);
  wprintw(this->trace_viewer, "Archive %lu Thread %lu\n", this->current_archive_index, this->current_thread_index);
  wattroff(this->trace_viewer, A_BOLD);

  auto current_iterable_token = thread_reader->getCurIterable();
  if (current_iterable_token.type == pallas::TypeSequence) {
    auto seq = thread_reader->thread_trace->getSequence(current_iterable_token);
    for (int i = this->frame_begin_index; i - this->frame_begin_index < getmaxy(this->trace_viewer) - 1 && i < seq->tokens.size(); i++) {
      pallas::Token tok = seq->tokens[i];
      if (i == current_callstack_index) {
        wattron(this->trace_viewer, A_STANDOUT);
      }
      wprintwToken(this->trace_viewer, tok);
      if (i == current_callstack_index) {
        wattroff(this->trace_viewer, A_STANDOUT);
      }
      wprintw(this->trace_viewer, "\n");
    }
  } else if (current_iterable_token.type == pallas::TypeLoop) {
    auto loop = thread_reader->thread_trace->getLoop(current_iterable_token);
    auto tok = loop->repeated_token;
    int nb_iterations = loop->nb_iterations.at(thread_reader->tokenCount[current_iterable_token]);
    wprintwToken(this->trace_viewer, tok);
    wprintw(this->trace_viewer, "\t%d/%d", thread_reader->callstack_index[thread_reader->current_frame] + 1, nb_iterations);
  } else {
      panic("Current iterable token is not iterable");
  }

  wrefresh(this->trace_viewer);
}

void PallasExplorer::renderTokenWindow(pallas::ThreadReader *thread_reader) {
  werase(token_viewer);
  pallas::Token current_token = thread_reader->pollCurToken();

  pallas_duration_t current_token_duration;
  switch (current_token.type) {
  case pallas::TypeEvent:
    current_token_duration = thread_reader->getEventSummary(current_token)->durations->at(thread_reader->tokenCount[current_token]);
    break;
  case pallas::TypeSequence:
    current_token_duration = thread_reader->thread_trace->getSequence(current_token)->durations->at(thread_reader->tokenCount[current_token]);
    break;
  case pallas::TypeLoop:
    current_token_duration = thread_reader->getLoopDuration(current_token);
    break;
  case pallas::TypeInvalid:
    panic("Encountered invalid token");
  }

  // Print token information
  wprintwToken(this->token_viewer, current_token);

  mvwprintw(
      this->token_viewer,
      2, 0,
      "  Beginning timestamp : %lfs\n"
      "  Duration            : %lfs\n",
      thread_reader->referential_timestamp / 1e6,
      current_token_duration / 1e6
  );

  if (current_token.type == pallas::TypeEvent) {
    char buffer[DESCRIPTION_BUFFER_SIZE];
    thread_reader->thread_trace->printEventToString(thread_reader->thread_trace->getEvent(current_token), buffer, DESCRIPTION_BUFFER_SIZE);
    mvwprintw(
        this->token_viewer, 
        4, 0,
        "  Description         : %s",
        buffer
    );
  }

  if (current_token.type != pallas::TypeLoop) {
    int window_size_x, window_size_y;
    getmaxyx(this->token_viewer, window_size_y, window_size_x);

    // top-left and bottom-right coordinates
    int topleftx = 3, botrightx = window_size_x - 3;
    int toplefty = (3 * 6 + window_size_y) / 4;
    int botrighty = (6 + 3 * window_size_y) / 4;

    Histogram histogram = Histogram(thread_reader, current_token, botrightx - topleftx);

    // Adjust x coordiantes to the actual size of the histogram
    size_t histogram_size = histogram.values.size();
    size_t max_value = *std::max_element(histogram.values.begin(), histogram.values.end());

    topleftx = (window_size_x - histogram_size) / 2;
    botrightx = (window_size_x + histogram_size) / 2;

    for (int x = topleftx; x < botrightx; x++) {
      for (int y = toplefty; y < botrighty; y++) {
        if (botrighty - y <= histogram.values.at(x-topleftx) * (botrighty-toplefty) / max_value)
          wattron(this->token_viewer, A_STANDOUT);
          mvwprintw(this->token_viewer, y, x, " ");
          wattroff(this->token_viewer, A_STANDOUT);
      }
    }

    if (histogram.timestep != 0) {
      size_t current_token_x = std::min((current_token_duration - histogram.min_duration) / histogram.timestep, histogram.values.size()-1);
      mvwprintw(this->token_viewer, toplefty-1, topleftx + current_token_x, "v");
      mvwprintw(this->token_viewer, toplefty-2, topleftx + current_token_x, "|");
    }
  }

  wrefresh(this->token_viewer);
}
