CXX=g++-6
CXXFLAGS=-Og -g -std=c++14 -Wall -Wextra -pedantic
CXXFLAGS += $(shell pkg-config sdl2 --cflags)

viewer: view.o crc32.o
	$(CXX) -o $@ $^ $(shell pkg-config sdl2 --libs)

view.o: view.cc mario.hh crc32.h
crc32.o: crc32.cc crc32.h
