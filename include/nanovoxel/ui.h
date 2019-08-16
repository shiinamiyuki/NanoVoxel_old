#pragma once


#include <util.h>

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

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
	public:
		void create(int argc, char** argv);
		void show();
	};
}