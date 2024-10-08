// Pallas includes
#include "pallas/pallas_storage.h"

#include "tui.h"

#include <iostream>
#include <cstring>
#include <csignal>

// Curses for TUI
#include <curses.h>

void on_abort(int sig) {
  endwin();
}

void usage(const char* prog_name) {
  std::cout << "Usage : " << prog_name << " [options] <trace file>" << std::endl;
  std::cout << "\t" << "-h" << "\t" << "Show this help and exit" << std::endl;
  std::cout << "\t" << "-v" << "\t" << "Enable verbose mode" << std::endl;
}

int main(int argc, char *argv[]) {
  signal(SIGABRT, on_abort);
  int nb_opts = 0;

  for (nb_opts = 1; nb_opts < argc; nb_opts++) {
    if (!strcmp(argv[nb_opts], "-h") || !strcmp(argv[nb_opts], "-?")) {
      usage(argv[0]);
      return EXIT_SUCCESS;
    } else if (!strcmp(argv[nb_opts], "-v")) {
      pallas_debug_level_set(pallas::DebugLevel::Debug);
    } else {
      /* Unknown parameter name. It's probably the trace's path name. We can stop
       * parsing the parameter list.
       */
      break;
    }
  }

  char* trace_name = argv[nb_opts];
  if (trace_name == nullptr) {
    std::cout << "Missing trace file" << std::endl;
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  auto trace = pallas::GlobalArchive();
  pallasReadGlobalArchive(&trace, trace_name);

  PallasExplorer explorer = PallasExplorer(trace);

  while (explorer.updateWindow()) {}

  endwin();
}
