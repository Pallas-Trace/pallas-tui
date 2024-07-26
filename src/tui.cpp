#include "tui.h"
#include "pallas/pallas.h"
#include "pallas/pallas_archive.h"
#include "pallas/pallas_read.h"
#include "pallas/pallas_timestamp.h"

#include <cstdlib>
#include <curses.h>
#include <iostream>
#include <string>
#include <vector>

#define pallas_assert(cond, errmsg) if (!(cond)) panic(errmsg)

void panic(std::string errmsg) {
  endwin();
  std::cerr << errmsg << std::endl;
  exit(1);
}

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
    this->readers[i].reserve(archive->nb_threads);
    for (size_t j = 0; j < archive->nb_threads; j++) {
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
  pallas::ThreadReader &tr = this->readers[current_archive_index][current_thread_index];

  this->renderTraceWindow(&tr);
  this->renderTokenWindow(&tr);

  pallas::Token tok = tr.pollCurToken();

  int ch = wgetch(trace_viewer);
  switch (ch) {
  case 'q':
    return false;
  case 'h':
  case KEY_LEFT:
    if (tr.current_frame > 0) tr.leaveBlock();
    break;
  case 'j':
  case KEY_DOWN:
    if (!tr.isEndOfCurrentBlock()) tr.moveToNextToken();
    break;
  case 'k':
  case KEY_UP:
    if (tr.callstack_index[tr.current_frame] > 0) tr.moveToPrevToken();
    break;
  case 'l':
  case KEY_RIGHT:
    if (tok.isIterable()) tr.enterBlock(tok);
    break;
  }
  return true;
}

void PallasExplorer::renderTraceWindow(pallas::ThreadReader *tr) {
  size_t current_callstack_index = tr->callstack_index[tr->current_frame];
  if (current_callstack_index < this->frame_begin_index) {
    this->frame_begin_index = current_callstack_index;
  } else if (current_callstack_index > this->frame_begin_index + getmaxy(this->trace_viewer)) {
    this->frame_begin_index = current_callstack_index - getmaxy(this->trace_viewer);
  }

  werase(trace_viewer);

  auto current_iterable_token = tr->getCurIterable();
  if (current_iterable_token.type == pallas::TypeSequence) {
    auto seq = tr->thread_trace->getSequence(current_iterable_token);
    for (int i = this->frame_begin_index; i - this->frame_begin_index < getmaxy(this->trace_viewer) && i < seq->tokens.size(); i++) {
      pallas::Token tok = seq->tokens[i];
      if (i == current_callstack_index) {
        attron(A_STANDOUT);
        wprintw(this->trace_viewer, "> ");
      }
      wprintwToken(this->trace_viewer, tok);
      if (i == current_callstack_index) {
        attroff(A_STANDOUT);
      }
      wprintw(this->trace_viewer, "\n");
    }
  } else if (current_iterable_token.type == pallas::TypeLoop) {
    auto loop = tr->thread_trace->getLoop(current_iterable_token);
    auto tok = loop->repeated_token;
    int nb_iterations = loop->nb_iterations.at(tr->tokenCount[current_iterable_token]);
    wprintwToken(this->trace_viewer, tok);
    wprintw(this->trace_viewer, "\t%d/%d", tr->callstack_index[tr->current_frame], nb_iterations);
  } else {
      panic("Current iterable token is not iterable");
  }

  wrefresh(this->trace_viewer);
}

void PallasExplorer::renderTokenWindow(pallas::ThreadReader *tr) {
  werase(token_viewer);
  pallas::Token current_token = tr->pollCurToken();

  pallas_duration_t current_token_duration;
  switch (current_token.type) {
  case pallas::TypeEvent:
    current_token_duration = tr->getEventSummary(current_token)->durations->at(tr->tokenCount[current_token]);
    break;
  case pallas::TypeSequence:
    current_token_duration = tr->thread_trace->getSequence(current_token)->durations->at(tr->tokenCount[current_token]);
    break;
  case pallas::TypeLoop:
    current_token_duration = tr->getLoopDuration(current_token);
    break;
  case pallas::TypeInvalid:
    panic("Encountered invalid token");
  }

  // Print token information
  wprintwToken(this->token_viewer, current_token);
  wprintw(this->token_viewer, "\n");

  wprintw(
      this->token_viewer,
      "  Beginning timestamp : %lfs\n"
      "  Duration            : %lfs\n",
      tr->referential_timestamp / 1e6,
      current_token_duration / 1e6
  );

  wrefresh(this->token_viewer);
}
