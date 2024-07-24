#include "pallas/pallas.h"
#include "pallas/pallas_read.h"
#include <cstdlib>
#include <curses.h>
#include <iostream>
#include <ostream>
#include <string>

void panic(std::string errmsg) {
  endwin();
  std::cerr << errmsg << std::endl;
  exit(1);
}

void printwCurToken(const pallas::ThreadReader *tr, const pallas::Token &tok) {
  switch (tok.type) {
    case pallas::TypeEvent:
      printw(
          "E%d : %lu",
          tok.id,
          tr->referential_timestamp
      );
      break;
    case pallas::TypeSequence:
      printw(
          "S%d : %lu",
          tok.id,
          tr->referential_timestamp
      );
      break;
    case pallas::TypeLoop:
      printw(
          "L%d : %lu",
          tok.id,
          tr->referential_timestamp
      );
      break;
    case pallas::TypeInvalid:
      panic("Encountered invalid token");
  }
}

void initWindow() {
  initscr();
  raw();
  keypad(stdscr, TRUE);
  noecho();
}

bool updateWindow(pallas::ThreadReader *tr) {
  clear();
  pallas::Token tok = tr->pollCurToken();
  printwCurToken(tr, tok);
  refresh();
  int ch = getch();
  switch (ch) {
    case 'q':
      return false;
    case 'h':
    case KEY_LEFT:
      if (tr->current_frame > 0) tr->leaveBlock();
      break;
    case 'j':
    case KEY_DOWN:
      if (!tr->isEndOfCurrentBlock()) tr->moveToNextToken();
      break;
    case 'k':
    case KEY_UP:
      if (tr->callstack_index[tr->current_frame] > 0) tr->moveToPrevToken();
      break;
    case 'l':
    case KEY_RIGHT:
      if (tok.isIterable()) tr->enterBlock(tok);
      break;
  }
  return true;
}
