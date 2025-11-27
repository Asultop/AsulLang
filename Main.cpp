// Main.cpp - small launcher that delegates to console::runConsole
#include <iostream>

namespace console { int runConsole(int argc, char* argv[]); }

int main(int argc, char* argv[]) {
	return console::runConsole(argc, argv);
}