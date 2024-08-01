#include "tui.h"
#include "helpers.h"

#include "pallas/pallas.h"
#include "pallas/pallas_archive.h"
#include "pallas/pallas_read.h"
#include "pallas/pallas_timestamp.h"

#include <algorithm>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <curses.h>
#include <ncurses.h>
#include <vector>

#define DESCRIPTION_BUFFER_SIZE 1024

void wprintwToken(WINDOW *win, const pallas::Token &tok, const pallas::ThreadReader *thread_reader) {
  switch (tok.type) {
  case pallas::TypeEvent:
    wprintw(
        win,
        "Event %d",
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
  case pallas::TypeSequence:
    wprintw(
        win,
        "Sequence %d",
        tok.id
    );
    if (thread_reader) {
      pallas::Token sequence_first_token = thread_reader->thread_trace->getSequence(tok)->tokens.at(0);
      // If first token in sequence is an event, it's probably an Enter/Leave pattern,
      // then we print the corresponding instruction
      if (sequence_first_token.type == pallas::TypeEvent) {
        char sequence_first_token_desc[DESCRIPTION_BUFFER_SIZE];
        char sequence_title[DESCRIPTION_BUFFER_SIZE];
        int enter_number;
        thread_reader->thread_trace->printEventToString(
            thread_reader->thread_trace->getEvent(sequence_first_token),
            sequence_first_token_desc,
            DESCRIPTION_BUFFER_SIZE
        );
        if (sscanf(sequence_first_token_desc, "Enter %d (%s)", &enter_number, sequence_title)) {
          wprintw(win, " (%s", sequence_title);
        };
      }
    }
    break;
  }
}

PallasExplorer::PallasExplorer(const pallas::GlobalArchive& glob_arch) {
  // Object initialisation
  global_archive = glob_arch;

  current_archive_index = 0;
  current_thread_index = 0;
  frame_begin_index = 0;

  readers = std::vector<std::vector<pallas::ThreadReader>>(global_archive.nb_archives);
  int reader_options = pallas::ThreadReaderOptions::None;
  pallas_assert(global_archive.nb_archives > 0, "Malformed archive");
  for (size_t i = 0; i < global_archive.nb_archives; i++) {
    auto archive = global_archive.archive_list[i];
    pallas_assert(archive->nb_threads > 0, "Malformed archive");
    readers[i] = std::vector<pallas::ThreadReader>();
    for (size_t j = 0; j < archive->nb_threads; j++) {
        auto thread = archive->getThreadAt(j);
        if (thread != nullptr)
            readers[i].emplace_back(global_archive.archive_list[i], thread->id, reader_options);
    }
  }

  // Ncurses initialisation
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  curs_set(0);

  // Viewers initialisation

  int x, y;
  getmaxyx(stdscr, y, x);

  trace_container = newwin(y, x/2, 0, 0);
  token_container = newwin(y, x/2, 0, x/2);

  box(trace_container, 0, 0);
  box(token_container, 0, 0);
  wrefresh(trace_container);
  wrefresh(token_container);

  trace_viewer = derwin(trace_container, y - 2, x/2 - 2, 1, 1);
  token_viewer = derwin(token_container, y - 2, x/2 - 2, 1, 1);

  keypad(trace_viewer, TRUE);
}

bool PallasExplorer::updateWindow() {
  pallas::ThreadReader &thread_reader = readers[current_archive_index][current_thread_index];

  renderTraceWindow(&thread_reader);
  renderTokenWindow(&thread_reader);

  pallas::Token token = thread_reader.pollCurToken();

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
    if (!thread_reader.isEndOfCurrentBlock()) thread_reader.moveToNextToken();
    break;
  case 'k':
  case KEY_UP:
    if (thread_reader.callstack_index[thread_reader.current_frame] > 0) thread_reader.moveToPrevToken();
    break;
  case 'l':
  case KEY_RIGHT:
    if (token.isIterable()) thread_reader.enterBlock(token);
    break;
  case KEY_PPAGE:
    for (int i = 0; i < getmaxy(this->trace_viewer) - 1 && thread_reader.callstack_index[thread_reader.current_frame] > 0; i++) {
      thread_reader.moveToPrevToken();
      this->frame_begin_index--;
    }
    break;
  case KEY_NPAGE:
    for (int i = 0; i < getmaxy(this->trace_viewer) - 1 && !thread_reader.isEndOfCurrentBlock(); i++) {
      thread_reader.moveToNextToken();
      this->frame_begin_index++;
    }
    break;
  case '>':
    current_thread_index = (current_thread_index + 1) % readers[current_archive_index].size();
    break;
  case '<':
    current_thread_index = (current_thread_index - 1) % readers[current_archive_index].size();
    break;
  case '\t':
    current_archive_index = (current_archive_index + 1) % readers.size();
    break;
  default:
    break;
  }
  return true;
}

void PallasExplorer::renderTraceWindow(pallas::ThreadReader *thread_reader) {
  size_t current_callstack_index = thread_reader->callstack_index[thread_reader->current_frame];
  if (current_callstack_index < frame_begin_index) {
    frame_begin_index = current_callstack_index;
  } else if (current_callstack_index >= frame_begin_index + getmaxy(trace_viewer) - 1) {
    frame_begin_index = current_callstack_index - getmaxy(trace_viewer) + 2;
  }

  werase(trace_viewer);

  wattron(trace_viewer, A_BOLD);
  wprintw(trace_viewer, "Archive %lu Thread %lu\n", current_archive_index, current_thread_index);
  wattroff(trace_viewer, A_BOLD);

  auto current_iterable_token = thread_reader->getCurIterable();
  if (current_iterable_token.type == pallas::TypeSequence) {
    auto seq = thread_reader->thread_trace->getSequence(current_iterable_token);
    for (int i = frame_begin_index; i - frame_begin_index < getmaxy(trace_viewer) - 1 && i < seq->tokens.size(); i++) {
      pallas::Token tok = seq->tokens[i];
      if (i == current_callstack_index) {
        wattron(trace_viewer, A_STANDOUT);
      }
      wprintwToken(trace_viewer, tok, thread_reader);
      if (i == current_callstack_index) {
        wattroff(trace_viewer, A_STANDOUT);
      }
      wprintw(trace_viewer, "\n");
    }
  } else if (current_iterable_token.type == pallas::TypeLoop) {
    auto loop = thread_reader->thread_trace->getLoop(current_iterable_token);
    auto tok = loop->repeated_token;
    int nb_iterations = loop->nb_iterations.at(thread_reader->tokenCount[current_iterable_token]);
    wprintwToken(trace_viewer, tok, thread_reader);
    wprintw(trace_viewer, "\t%d/%d", thread_reader->callstack_index[thread_reader->current_frame] + 1, nb_iterations);
  } else {
      panic("Current iterable token is not iterable");
  }

  wrefresh(trace_viewer);
}

void PallasExplorer::renderTokenWindow(pallas::ThreadReader *thread_reader) {
  werase(token_viewer);
  pallas::Token current_token = thread_reader->pollCurToken();

  pallas_duration_t current_token_duration = 0;
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
  wprintwToken(token_viewer, current_token, thread_reader);

  mvwprintw(
      token_viewer,
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
        token_viewer,
        4, 0,
        "  Description         : %s",
        buffer
    );
  }

  if (current_token.type != pallas::TypeLoop) {
    int window_size_x, window_size_y;
    getmaxyx(token_viewer, window_size_y, window_size_x);

    // top-left and bottom-right coordinates
    int top_l_x = 3, bot_r_x = window_size_x - 3;
    int top_l_y = (3 * 6 + window_size_y) / 4;
    int bot_r_y = (6 + 3 * window_size_y) / 4;

    Histogram histogram = Histogram(thread_reader, current_token, bot_r_x - top_l_x);

    if (histogram.timestep != 0) {
      size_t current_token_x = std::min((current_token_duration - histogram.min_duration) / histogram.timestep, histogram.values.size()-1);

      // Adjust x coordiantes to the actual size of the histogram
      size_t histogram_size = histogram.values.size();
      size_t max_value = *std::max_element(histogram.values.begin(), histogram.values.end());

      top_l_x = (window_size_x - histogram_size) / 2;
      bot_r_x = (window_size_x + histogram_size) / 2;

      for (int x = top_l_x; x < bot_r_x; x++) {
        for (int y = top_l_y; y < bot_r_y; y++) {
          if (
              (bot_r_y - y <= histogram.values.at(x-top_l_x) * (bot_r_y-top_l_y) / max_value) ||
              (y == bot_r_y - 1 && histogram.values.at(x-top_l_x) > 0) // Still show value if there is one
          ) {
            if (x - top_l_x != current_token_x)
              mvwaddch(this->token_viewer, y, x, ' ' | A_REVERSE | A_DIM);
            else
              mvwaddch(this->token_viewer, y, x, ' ' | A_REVERSE);
          }
        }
      }
    }
  }

  wrefresh(token_viewer);
}
