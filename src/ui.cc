#include <ui.h>


#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_glfw.h>
#include <miyuki/utils/thread.h>
static void Draw(const Miyuki::HW::Texture& texture) {
	ImGui::Image((void*)texture.getTexture(),
		ImVec2(texture.size()[0], texture.size()[1]));

}
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
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForOpenGL(windowHandle, true);
		ImGui_ImplOpenGL3_Init(glsl_version);
		io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/Consola.ttf", 16.0f);
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		loadDefault();

	}
	void Window::show() {

		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		while (!glfwWindowShouldClose(windowHandle)) {
		

			glfwPollEvents();

			// Start the Dear ImGui frame
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			update();
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
	static void setUpDockSpace() {
		static bool opt_fullscreen_persistant = true;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace", nullptr, window_flags);
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		// DockSpace
		ImGuiIO& io = ImGui::GetIO();
		assert(io.ConfigFlags & ImGuiConfigFlags_DockingEnable);
		ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		ImGui::End();

	}
	void Window::update() {
		setUpDockSpace();
		menu();
		viewportWindow();
		ImGui::ShowDemoWindow();
	}
	void Window::loadViewImpl() {
		viewport = std::make_unique<Miyuki::HW::Texture>(viewportUpdateFilm->width(),
			viewportUpdateFilm->height(),
			&pixelData[0]);
	}
	void Window::viewportWindow() {
		if (windowFlags.viewportUpdateAvailable) {
			std::lock_guard<std::mutex> lock(viewportMutex);
			loadViewImpl();
			windowFlags.viewportUpdateAvailable = false;
		}
		struct WindowCloser {
			~WindowCloser() {
				ImGui::End();
			}
		};
		if (ImGui::Begin("View")) {
			WindowCloser _;
			if (!viewport)return;
			Draw(*viewport);
		}
	}

	void Window::loadView(std::shared_ptr<Film> film) {
		std::lock_guard<std::mutex> lock(viewportMutex);
		size_t w = film->width(), h = film->height();
		pixelData.resize(w * h * 4ul);
		Miyuki::Thread::ParallelFor(0, h, [=](int j, int) {
			for (int i = 0; i < w; i++) {
				auto offset = i + j * w;
				auto color = film->getPixel(i,j).eval().toInt();
				pixelData[4ul * offset] = color.r;
				pixelData[4ul * offset + 1] = color.g;
				pixelData[4ul * offset + 2] = color.b;
				pixelData[4ul * offset + 3] = 255;
			}
		});
		windowFlags.viewportUpdateAvailable = true;
		viewportUpdateFilm = film;
	}

	void Window::menu() {
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New")) {
					loadDefault();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}
	void Window::loadDefault() {
		scene = std::make_unique<Scene>(50, 50, 50);
	}
}