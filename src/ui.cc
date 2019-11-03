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
		if (ImGui::Begin("View",nullptr,ImGuiWindowFlags_NoScrollbar)) {
			WindowCloser _;
			ImGui::Text("Camera Mode");
			ImGui::SameLine();
			if (ImGui::RadioButton("Free", cameraMode == EFree)) {
				cameraMode = EFree;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Perspective", cameraMode == EPerspective)) {
				cameraMode = EPerspective;
			}

			if (!viewport)return;
			Draw(*viewport);
			auto cam = &scene->camera;
			if (!cam)return;
			ImGuiIO& io = ImGui::GetIO();
			if (!scene->isRendering()) {
				return;
			}
			bool restart = false;

			Vec3f newPos = cam->position, newDir = cam->direction;
			auto pos = io.MousePos;
			if (!lastViewportMouseDown) {
				if (io.MouseDown[1]) {
					auto windowPos = ImGui::GetWindowPos();
					auto size = ImGui::GetWindowSize();
					if (pos.x >= windowPos.x && pos.y >= windowPos.y
						&& pos.x < windowPos.x + size.x && pos.y < windowPos.y + size.y) {
						lastViewportMouseDown = Point2i(pos.x, pos.y);
						cameraDir = cam->direction;
						cameraPos = cam->position;
						distance = (cameraPos - center.value()).length();

					}
				}
			}
			else if (!io.MouseDown[1]) {
				lastViewportMouseDown = {};
			}
			else {

				if (cameraMode == EPerspective) {
					if (io.MouseWheel > 0) {
						distance /= 1.1; restart = true;
					}
					else if (io.MouseWheel < 0) {
						distance *= 1.1; restart = true;
					}
				}
				else {
					Miyuki::Bound3f bound(Point3f(0, 0, 0), Point3f(scene->width(), scene->height(), scene->depth()));
					Point3f _;
					float marchDistance;
					bound.boundingSphere(&_, &marchDistance);
					Float march = marchDistance * 0.025;
					if (io.KeyShift) {
						march *= 0.1f;
					}
					if (io.MouseWheel > 0) {
						restart = true;
					}
					else if (io.MouseWheel < 0) {
						march *= -1; restart = true;
					}
					if (restart) {
						Vec3f m = cam->cameraToWorld(Vec4f(0, 0, 1, 1));
						m.normalize();
						newPos = cam->position + march * m;
						//Log::log("{}\n", march);
					}
				}
				auto delta = io.MouseDelta;
				if (delta.x != 0 || delta.y != 0 || restart) {
					restart = true;
					auto last = lastViewportMouseDown.value();
					if (cameraMode == EPerspective) {
						auto offset = Vec3f(pos.x - last.x, -(pos.y - last.y), 0.0f) / 500;
						newDir = cameraDir + Vec3f(offset.x, offset.y, 0);
						auto dir = Vec3f(
							std::sin(newDir.x) * std::cos(newDir.y),
							std::sin(newDir.y),
							std::cos(newDir.x) * std::cos(newDir.y));
						newPos = -dir * distance + center.value();
					}
					else {
						auto offset = Vec3f(pos.x - last.x, -(pos.y - last.y), 0.0f) / 500;
						newDir = cameraDir + Vec3f(offset.x, offset.y, 0);
					}
				}
			}
			if (restart) {
				scene->abortRender();
				cam->position = newPos;
				cam->direction = newDir;
				while (scene->isRendering()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				scene->resumeRender();
				scene->updateCamera();				
				std::thread th([=]() {
					scene->render([=](std::shared_ptr<Film> film) {
						loadView(film);
					}, [=](std::shared_ptr<Film> film) {
						loadView(film, true);
					});
				});
				th.detach();
			}
		}
	}

	void Window::loadView(std::shared_ptr<Film> film, bool noDiscard) {

		auto loadFunc = [=]() {
			size_t w = film->width(), h = film->height();
			pixelData.resize(w * h * 4ul);
			Miyuki::Thread::ParallelFor(0, h, [=](int j, int) {
				for (int i = 0; i < w; i++) {
					auto offset = i + j * w;
					auto color = film->getPixel(i, j).eval().toInt();
					pixelData[4ul * offset] = color.r;
					pixelData[4ul * offset + 1] = color.g;
					pixelData[4ul * offset + 2] = color.b;
					pixelData[4ul * offset + 3] = 255;
				}
			}, 128);
			windowFlags.viewportUpdateAvailable = true;
			viewportUpdateFilm = film;
		};
		if (noDiscard) {
			std::lock_guard<std::mutex> lock(viewportMutex);
			loadFunc();
		}
		else {
			if (!timer) {
				timer.reset(new Timer());
			}
			else {
				if (scene->getCurrentSamples() >= 16) {
					if (timer->elapsedSeconds() > 1) {
						timer.reset(new Timer());
					}
					else {
						return;
					}
				}
			}
			std::unique_lock<std::mutex> lock(viewportMutex, std::try_to_lock);
			if (!lock.owns_lock()) {
				fmt::print("Discarded\n");
				return;
			}
			loadFunc();
		}
	}

	void Window::menu() {
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New")) {
					loadDefault();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Render")) {
				if (ImGui::MenuItem("Start")) {
					scene->resumeRender();
					scene->commit();
					Miyuki::Bound3f bound(Point3f(0, 0, 0), Point3f(scene->width(), scene->height(), scene->depth()));
					Point3f _center;
					bound.boundingSphere(&_center, &distance);
					center = Vec3f(_center[0], _center[1], _center[2]);
					std::thread th([=]() {
						scene->setTotalSamples(4096);
						auto cb = [=](std::shared_ptr<Film> film) {
							loadView(film);
						};
						auto fcb = [=](std::shared_ptr<Film> film) {
							loadView(film, true);
							film->writeImage("out.png");
						};
						scene->render(cb, fcb);

					});
					th.detach();
				}
				if (ImGui::MenuItem("Stop")) {
					scene->abortRender();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}
	void Window::loadDefault() {
		scene = std::make_unique<Scene>(16, 16, 16);
		scene->setFilmSize(1000, 1000);
		scene->camera.position = Miyuki::Vec3f(25, 30, -30);
		//scene->camera.direction = Vec3f(3.14159 / 6, 0, 0);
	}
	Timer::Timer() {
		start = std::chrono::system_clock::now();
	}

	double Timer::elapsedSeconds() const {
		auto end = std::chrono::system_clock::now();
		std::chrono::duration<double> elapsed_seconds = end - start;
		return elapsed_seconds.count();
	}

	Timer::~Timer() {

	}
}