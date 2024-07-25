#include "tui.h"
#include "pallas/pallas.h"
#include "pallas/pallas_archive.h"
#include "pallas/pallas_read.h"

#include <cstdlib>
#include <curses.h>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#define pallas_assert(cond, errmsg) if (!(cond)) panic(errmsg)

void panic(std::string errmsg) {
  endwin();
  std::cerr << errmsg << std::endl;
  exit(1);
}

void wprintwCurToken(WINDOW *win, const pallas::ThreadReader *tr) {
  auto current_token = tr->pollCurToken();
  switch (current_token.type) {
    case pallas::TypeEvent:
      wprintw(
          win,
          "E%d : %lu",
          current_token.id,
          tr->referential_timestamp
      );
      break;
    case pallas::TypeSequence:
      wprintw(
          win,
          "S%d : %lu",
          current_token.id,
          tr->referential_timestamp
      );
      break;
    case pallas::TypeLoop:
      wprintw(
          win,
          "L%d : %lu",
          current_token.id,
          tr->referential_timestamp
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
  keypad(stdscr, TRUE);
  noecho();

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
}

bool PallasExplorer::updateWindow() {
  pallas::ThreadReader &tr = this->readers[current_archive_index][current_thread_index];
  werase(trace_viewer);
  werase(token_viewer);

  pallas::Token tok = tr.pollCurToken();
  wprintwCurToken(token_viewer, &tr);
  wrefresh(this->trace_viewer);
  wrefresh(this->token_viewer);

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
