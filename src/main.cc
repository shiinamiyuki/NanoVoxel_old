#include <corecl.h>
#include <ui.h>
#include <iostream>
#include <filesystem>
using namespace NanoVoxel;


int main(int argc, char** argv) {
	try {
		Window window;
		window.create(argc, argv);
		window.show();
		/*Scene scene(50, 50, 50);
		scene.setFilmSize(1000, 1000);
		scene.camera.position = Miyuki::Vec3f(25, 30, -30);
		scene.commit();
		for (int i = 0; i < 32; i++) {
			scene.doOneRenderPass([=](std::shared_ptr<Film>film) {
				if (i == 31) {
					film->writeImage("out.png");
				}
			});
		}*/

	}
	catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
	}
}
