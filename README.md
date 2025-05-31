
Refreshing memory with c++ and diving deeper to some networking stuff :)

Built with MSYS MinGW64

for example:\
cd echo_server\
mkdir build\
cd build\
cmake -G "MinGW Makefiles" ..\
mingw32-make

or then alias it:\
alias make=mingw32-make


1) Simple single threaded echo server
2) Simple single threaded reverse proxy
3) Multithreaded echo server
4) Multithreaded reverse proxy
5) Async multithreaded echo server using IOCP
6) Async multithreaded reverse proxy using IOCP
