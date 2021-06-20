# Multiplayer Client Front-End

---
CPS2008 *Operating Systems and Systems Programming 2*

Assignment for the Academic year 2020/1

Xandru Mifsud (0173498M), B.Sc. (Hons) Mathematics and Computer Science

---

This repository implements a multiplayer Tetris game front-end, with live chat support between online players, using the
shared library available [here](https://github.com/xmif1/CPS2008_Tetris_Client).

The implementation makes use of the exposed thread-safe API of the shared library, with the responsibility of spawning
and then managing threads delegated to the front-end, using however the API of the library. Moving thread-management to
the front-end in particular made debugging easier.

The game logic and graphics are a clone of Stephen Brennan's ```ncurses``` [Tetris implementation](https://github.com/brenns10/tetris).
Wherever we changed Stephen's code, we added a comment starting with ```@xandru```, for clear identification. Kindly feel
free to search for these changes. The original license for Stephen's code is included in this repository as well.

## Requirements

This program is intended to run on Linux platforms, in particular on Debian-based systems such as Ubuntu and
Lubuntu, on which we have tested the implementation with relative success.

For installation, the following are required:
1. ```cmake``` version 3.17+
2. ```ncurses```
3. The aforementioned shared library installed on the client system.

## Installation Instructions

Clone the repository, and ```cd``` into the project directory. Then run:

1. ```cmake .```
2. ```make```

## Execution Instructions

Simply ```cd``` into the directory containing the compiled executable, and run ```./CPS2008_Tetris_FrontEnd <server_ip>```,
where ```server_ip``` is a required argument specifying the IPv4 address in dot notation of the server.