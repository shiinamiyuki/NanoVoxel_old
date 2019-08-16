#include <corecl.h>
#include <ui.h>
#include <iostream>
using namespace NanoVoxel;


int main(int argc, char** argv) {
	try {
		Window window;
		window.create(argc, argv);
		window.show();
	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}