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
#include <unistd.h>
#include <vector>

#define DESCRIPTION_BUFFER_SIZE 1024

void wprintwToken(WINDOW *win, const pallas::Token &tok, const pallas::ThreadReader *thread_reader) {
  switch (tok.type) {
  case pallas::TypeEvent:
    char event_description_buffer[DESCRIPTION_BUFFER_SIZE];
    thread_reader->thread_trace->printEventToString(
        thread_reader->thread_trace->getEvent(tok),
        event_description_buffer,
        DESCRIPTION_BUFFER_SIZE
    );
    wprintw(
        win,
        "Event %d : %s",
        tok.id,
        event_description_buffer
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
        if (sscanf(sequence_first_token_desc, "Enter %d (%s\n", &enter_number, sequence_title)) {
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

  enable_timestamps = false;
  enable_duration_coloring = false;
  reader_flag = PALLAS_READ_FLAG_NO_UNROLL;

  readers = std::vector<std::vector<pallas::ThreadReader>>(global_archive.nb_archives);
  current_trace_offsets = std::vector<std::vector<size_t>>(global_archive.nb_archives);
  pallas_assert(global_archive.nb_archives > 0, "Malformed archive");
  for (size_t i = 0; i < global_archive.nb_archives; i++) {
    auto archive = global_archive.archive_list[i];
    pallas_assert(archive->nb_threads > 0, "Malformed archive");
    readers[i] = std::vector<pallas::ThreadReader>();
    current_trace_offsets[i] = std::vector<size_t>(archive->nb_threads);
    for (size_t j = 0; j < archive->nb_threads; j++) {
        auto thread = archive->getThreadAt(j);
        if (thread != nullptr)
            readers[i].emplace_back(global_archive.archive_list[i], thread->id, PALLAS_READ_FLAG_UNROLL_ALL);
    }
  }

  // Ncurses initialisation
  setlocale(LC_ALL, "");
  initscr();
  start_color();
  init_pair(1, COLOR_GREEN, COLOR_BLACK);
  init_pair(2, COLOR_YELLOW, COLOR_BLACK);
  init_pair(3, COLOR_RED, COLOR_BLACK);
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
  size_t *current_trace_offset = &current_trace_offsets[current_archive_index][current_thread_index];

  renderTraceWindow(&thread_reader);
  renderTokenWindow(&thread_reader);

  pallas::Token token = thread_reader.pollCurToken();

  int ch = wgetch(trace_viewer);
  switch (ch) {
  case 'q':
    return false;
  // Movement
  case 'h':
  case KEY_LEFT:
    if (thread_reader.currentState.current_frame_index > 1) thread_reader.leaveBlock();
    break;
  case 'j':
  case KEY_DOWN:
    thread_reader.moveToNextToken(reader_flag);
    (*current_trace_offset)++;
    break;
  case 'k':
  case KEY_UP:
    thread_reader.moveToPrevToken(reader_flag);
    (*current_trace_offset)--;
    break;
  case 'l':
  case KEY_RIGHT:
    if (token.isIterable()) thread_reader.enterBlock();
    break;
  case KEY_PPAGE:
    for (int i = 0; i < getmaxy(this->trace_viewer) - 1; i++) {
      thread_reader.moveToPrevToken(reader_flag);
    }
    break;
  case KEY_NPAGE:
    for (int i = 0; i < getmaxy(this->trace_viewer) - 1; i++) {
      thread_reader.moveToNextToken(reader_flag);
    }
    break;
  // Options
  case 't':
    enable_timestamps = !enable_timestamps;
    break;
  case 'S':
    reader_flag ^= PALLAS_READ_FLAG_UNROLL_SEQUENCE;
    break;
  case 'L':
    reader_flag ^= PALLAS_READ_FLAG_UNROLL_LOOP;
    break;
  case 'c':
    enable_duration_coloring = !enable_duration_coloring;
  // Changing traces
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

void PallasExplorer::printTraceToken(pallas::ThreadReader *thread_reader) {
  pallas::Token tok = thread_reader->pollCurToken();
  short line_color = getLineColor(thread_reader);
  if (enable_duration_coloring)
    wattron(trace_viewer, COLOR_PAIR(line_color));
  if (enable_timestamps)
    wprintw(trace_viewer, "%ld %9.9lf\t", thread_reader->currentState.currentFrame->tokenCount[tok], thread_reader->currentState.currentFrame->referential_timestamp / 1e9);
  for (int t = 0; t < thread_reader->currentState.current_frame_index; t++)
    wprintw(trace_viewer, "  ");
  wprintwToken(trace_viewer, tok, thread_reader);
  wattroff(trace_viewer, COLOR_PAIR(line_color));
}

void PallasExplorer::renderTraceWindow(pallas::ThreadReader *thread_reader) {
  size_t &current_trace_offset = current_trace_offsets.at(current_archive_index).at(current_thread_index);
  if (current_trace_offset < 1)
    current_trace_offset = 1;
  if (current_trace_offset > getmaxy(trace_viewer) - 1)
    current_trace_offset = getmaxy(trace_viewer) - 1;

  pallas::Cursor checkpoint = pallas::pallasCreateCheckpoint(thread_reader);

  werase(trace_viewer);

  wattron(trace_viewer, A_BOLD);
  wprintw(trace_viewer, "Archive %lu Thread %lu\n", current_archive_index, current_thread_index);
  wattroff(trace_viewer, A_BOLD);

  wmove(trace_viewer, current_trace_offset, 0);
  wattron(trace_viewer, A_STANDOUT);
  printTraceToken(thread_reader);
  wattroff(trace_viewer, A_STANDOUT);

  for (int i = current_trace_offset - 1; i > 0 && thread_reader->moveToPrevToken(reader_flag); i--) {
    wmove(trace_viewer, i, 0);
    printTraceToken(thread_reader);
  }
  wrefresh(trace_viewer);

  pallas::pallasLoadCheckpoint(thread_reader, &checkpoint);


  for (int i = current_trace_offset + 1; i < getmaxy(trace_viewer) && thread_reader->moveToNextToken(reader_flag); i++) {
    wmove(trace_viewer, i, 0);
    printTraceToken(thread_reader);
  }
  wrefresh(trace_viewer);

  pallas::pallasLoadCheckpoint(thread_reader, &checkpoint);

  wrefresh(trace_viewer);
}

void PallasExplorer::renderTokenWindow(pallas::ThreadReader *thread_reader) {
  werase(token_viewer);
  pallas::Token current_token = thread_reader->pollCurToken();

  pallas_duration_t current_token_duration = getTokenDuration(thread_reader, current_token);

  // Print token information
  wprintwToken(token_viewer, current_token, thread_reader);

  mvwprintw(
      token_viewer,
      2, 0,
      "  Beginning timestamp : %.9lfs\n"
      "  Duration            : %.9lfs\n",
      thread_reader->currentState.currentFrame->referential_timestamp / 1e9,
      current_token_duration / 1e9
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
