CXX=g++
CXXFLAGS=-g -Wall -ansi `xapian-config --cxxflags` `xapian-config --libs`

all: main

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp

main: main.o
	$(CXX) $(CXXFLAGS) -o main main.o