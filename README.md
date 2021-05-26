# Raw Display #
Raw display provides a minimal raw display buffer for doing simple interfaces.
It runs on Windows, MacOS and Linux (X11 & Framebuffer), with the goal of being
used for simple graphical interfaces to tests/simulations.

It is designed for ease of use, not performance, so the API/feature set
is deliberately minimal.

For large/complex applications it is probably better to use something like
[SDL](https://libsdl.org/).

## Features ##
 * Cross platform (Windows, macOS, Linux)
 * Clean C99
 * Single .c/.h file combo
 * Unlicense suitable for incorporation into almost all projects
 * Built in fixed-width font
 * Simple drawing routines
  * Filled/unfilled Rectangles
  * Lines
  * Filled/unfilled Circles
  * Fixed-width text

License
=======
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue.svg)](http://unlicense.org/)

If you find it useful, please drop me a line at andre@ignavus.net.

# CI Status #
![C/C++ CI](https://github.com/AndreRenaud/RawDisplay/workflows/C/C++%20CI/badge.svg)