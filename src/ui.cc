#include <ui.h>


#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_glfw.h>


static void glfw_error_callback(int error, const char* description)
{
	throw std::runtime_error(fmt::format("Glfw Error {}:{}\n", error, description));
}
namespace NanoVoxel {
	void Window::create(int argc, char** argv) {
		if (!glfwInit())
			std::exit(1);

		// Decide GL+GLSL versions
#if __APPLE__
	// GL 3.2 + GLSL 150
		const char* glsl_version = "#version 150";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
		//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif
		windowHandle = glfwCreateWindow(1280, 720, "NanoVoxel", NULL, NULL);
		if (windowHandle == NULL)
			std::exit(1);
		glfwMakeContextCurrent(windowHandle);
		glfwSwapInterval(1); // Enable vsync
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
		bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
		bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
		bool err = gladLoadGL() == 0;
#else
		bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
		if (err)
		{
			throw std::runtime_error("Failed to initialize OpenGL loader!\n");
		}
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io; ImGui::StyleColorsDark();
		//ImGui::StyleColorsClassic();

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForOpenGL(windowHandle, true);
		ImGui_ImplOpenGL3_Init(glsl_version);

	}
	void Window::show() {
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		while (!glfwWindowShouldClose(windowHandle)) {
			update();

			glfwPollEvents();

			// Start the Dear ImGui frame
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			ImGui::Render();
			int display_w, display_h;
			glfwGetFramebufferSize(windowHandle, &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			glfwSwapBuffers(windowHandle);

		}
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		glfwDestroyWindow(windowHandle);
		glfwTerminate();

	}

	void Window::update() {

	}
}