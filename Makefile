all: make

%: %.cc
	g++ -std=c++20 -Wall -Wextra -o $@ $<
