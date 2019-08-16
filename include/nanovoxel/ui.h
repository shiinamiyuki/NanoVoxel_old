#pragma once


#include <miyuki/miyuki.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <miyuki/hw/texture.h>
#include <film.h>
#include <scene.h>
namespace NanoVoxel {
	class AbstractWindow {
	public:
		AbstractWindow() = default;
		virtual void create(int argc, char** argv) = 0;
		virtual void show() = 0;
		~AbstractWindow() = default;
	};


	class Window : AbstractWindow {
		GLFWwindow* windowHandle = nullptr;
		void update();
		std::unique_ptr<Miyuki::HW::Texture> viewport;
		void viewportWindow();
		std::vector<uint8_t> pixelData;
		std::mutex viewportMutex;
		void loadViewImpl();
		std::shared_ptr<Film> viewportUpdateFilm;
		std::unique_ptr<Scene> scene;
		void menu();
		void loadDefault();
	public:
		struct WindowFlags {
			bool viewportUpdateAvailable;
			WindowFlags() :viewportUpdateAvailable(false) {}
		}windowFlags;
		void loadView(std::shared_ptr<Film>film);
		void create(int argc, char** argv);
		void show();
	};
}