#include <corecl.h>
#include <ui.h>
#include <iostream>
#include <filesystem>
using namespace NanoVoxel;

struct CurrentPathSaver {
	std::filesystem::path current;
	CurrentPathSaver() :current(std::filesystem::current_path()) {}
	~CurrentPathSaver() {
		std::filesystem::current_path(current);
	}
};

int main(int argc, char** argv) {
	try {
	/*	Window window;
		window.create(argc, argv);
		window.show();*/
		Scene scene(50, 50, 50);
		scene.setFilmSize(500, 500);
		scene.commit();
		scene.doOneRenderPass([](std::shared_ptr<Film>) {});

	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}
