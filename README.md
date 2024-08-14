# Pallas-tui
Pallas TUI is a small TUI application used to explore a pallas trace.

## Building
### Requirements
- pallas
- cmake
- pkgconfig

### Installation
```sh
mkdir build && cd build
cmake ..
make && make install
```

You can add `-DCMAKE_INSTALL_PREFIX=<installation path>` to the cmake command to define where to install pallas_tui

## Usage
### Command line
```
Usage : ./pallas_explorer [options] <trace file>
	-h	Show this help and exit
	-v	Enable verbose mode
```

### In the application
Use `hjkl` (vim style) or arrow keys to move arround the trace, right arrow to enter a block (sequence or loop) and left arrow to leave current block.
Use `<` and `>` to switch between threads and `tab` to switch around archives.
Use `t` to toggle timestamps in the trace window
Use `c` to toggle coloring depending on the duration of the token in the trace window
Use `S` (Shift + s) and `L` (Shift + l) to respectively enable sequence and loop unrolling
Press `q` at any time to leave the application.
