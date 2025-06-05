
Goofing around with c++, refreshing memory and diving deeper to some networking stuff :)

Built with cmake via MSYS MinGW64

Build:\
cd echo_server\
mkdir build\
cd build\
cmake -G "MinGW Makefiles" ..\
mingw32-make

or then alias it for convenience:\
alias make=mingw32-make


1) Simple single threaded echo server
2) Simple single threaded reverse proxy
3) Multithreaded echo server
4) Multithreaded reverse proxy
5) Async multithreaded echo server using IOCP
6) V1 Async multithreaded reverse proxy using IOCP
7) V2 Async multithreaded reverse proxy using IOCP
8) Length-prefix framed async multithreaded echo server using IOCP
9) Minimal http server
10) Async multithreaded HTTPServer using IOCP  (WIP)
