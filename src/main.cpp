//
// Created by Shiina Miyuki on 11/18/2019.
//

#include <GL/gl3w.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <cstdio>
#include <vector>
#include <iostream>
#include <algorithm>
#include <PerlinNoise.hpp>
#include <imgui.h>
#include <examples/imgui_impl_glfw.h>
#include <examples/imgui_impl_opengl3.h>
#include <memory>

using namespace glm;
double glfwMouseScrollY;

struct Material {
    vec3 emission;
    vec3 baseColor;
    float roughness;
    float metalness;
    float specular;
};

void GLAPIENTRY
MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam) {
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}


#include "../shaders/compute-shader.h"

struct World {
    std::vector<uint8_t> data;
    ivec3 worldDimension, alignedDimension;
    GLuint world;

    void initData() {
#pragma  omp parallel for default(none)
        for (int x = 0; x < worldDimension.x; x++) {
            siv::PerlinNoise perlin;
            for (int y = 0; y < worldDimension.y; y++) {
                for (int z = 0; z < worldDimension.z; z++) {
                    vec3 p = vec3(x, y, z);// / vec3(worldDimension);
                    //p = 2.0f * p - vec3(1);
//                    if (length(p) < 0.4) {
//                        if(abs(p.x)<0.1)
//                            (*this)(x, y, z) = 2;
//                        else
//                            (*this)(x, y, z) = 1;
//                    } else {
//                        (*this)(x, y, z) = 0;
//                    }
                    p *= 0.1f;
                    auto n = perlin.noise0_1(p.x, p.y, p.z);
                    if (0.6 < n && n < 0.8) {
                        if (n > 0.73) {
                            (*this)(x, y, z) = 2;
                        } else {
                            (*this)(x, y, z) = 1;
                        }
                    } else {
                        (*this)(x, y, z) = 0;
                    }
                }
            }
        }

    }

    explicit World(const ivec3 &worldDimension) : worldDimension(worldDimension) {
        alignedDimension = worldDimension;
        alignedDimension.x = (alignedDimension.x + 7U) & (-4U);
        data.resize(alignedDimension.x * alignedDimension.y * alignedDimension.z, 0);
        initData();
    }

    uint8_t &operator()(int x, int y, int z) {
        x = std::clamp<int>(x, 0, worldDimension.x - 1);
        y = std::clamp<int>(y, 0, worldDimension.y - 1);
        z = std::clamp<int>(z, 0, worldDimension.z - 1);

        return data.at(x + alignedDimension.x * (y + z * alignedDimension.y));
    }

    uint8_t &operator()(const ivec3 &x) {
        return (*this)(x.x, x.y, x.z);
    }

    void setUpTexture() {
        glGenTextures(1, &world);
        glBindTexture(GL_TEXTURE_3D, world);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, alignedDimension.x, alignedDimension.y, alignedDimension.z, 0, GL_RED,
                     GL_UNSIGNED_BYTE, data.data());
    }
};

struct Renderer {
    GLint program;
    GLuint VBO;
    GLuint seed;
    World world;
    mat4 cameraDirection, cameraOrigin;
    GLuint sample; // texture for 1 spp
    GLuint accum; // accumlated sample
    GLuint composed; // post processor
    ivec2 mousePos, prevMousePos, lastFrameMousePos;
    bool prevMouseDown = false;
    int iTime = 0;
    vec2 eulerAngle = vec2(0, 0);

    explicit Renderer() : world(ivec3(50, 50, 50)) {
    }

    void compileShader() {
        std::vector<char> error(4096, 0);
        auto shader = glCreateShader(GL_COMPUTE_SHADER);
        GLint success;
        glShaderSource(shader, 1, &computeShaderSource, nullptr);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::COMPUTE::COMPILATION_FAILED\n" << error.data() << std::endl;
            exit(1);
        };

        program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << error.data() << std::endl;
            exit(1);
        }
        glDeleteShader(shader);
        std::cout << "Shader compiled without complaint" << std::endl;

        glGenTextures(1, &sample);
        glBindTexture(GL_TEXTURE_2D, sample);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1280, 720, 0, GL_RGBA,
                     GL_FLOAT, NULL);

        glGenTextures(1, &accum);
        glBindTexture(GL_TEXTURE_2D, accum);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1280, 720, 0, GL_RGBA,
                     GL_FLOAT, NULL);

        glGenTextures(1, &composed);
        glBindTexture(GL_TEXTURE_2D, composed);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1280, 720, 0, GL_RGBA,
                     GL_FLOAT, NULL);

        std::vector<float> seeds;
        for (size_t i = 0; i < 1920 * 1080; i++) {
            seeds.emplace_back(uintBitsToFloat(rand()));
            seeds.emplace_back(uintBitsToFloat(rand()));
            seeds.emplace_back(uintBitsToFloat(rand()));
            seeds.emplace_back(uintBitsToFloat(rand()));
        }
        glGenTextures(1, &seed);
        glBindTexture(GL_TEXTURE_2D, seed);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1920, 1080, 0, GL_RGBA,
                     GL_FLOAT, seeds.data());

    }


    void setUpWorld() {
        world.setUpTexture();
        cameraOrigin = translate(vec3(20, 20, -20));
        cameraDirection = identity<mat4>();//<=inverse(M);
    }

    void render(GLFWwindow *window) {
        {
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
            if (state == GLFW_PRESS) {
                if (!prevMouseDown) {
                    mousePos = ivec2(xpos, ypos);
                    if (mousePos != prevMousePos) {
                        iTime = 0;
                    }
                }
                auto p = ivec2(xpos, ypos);
                if (lastFrameMousePos != p) {
                    iTime = 0;
                    auto rot = (vec2(p) - vec2(lastFrameMousePos)) / 300.0f * float(M_PI);
                    eulerAngle += rot;

                }

            } else {
                if (prevMouseDown) {
                    prevMousePos = lastFrameMousePos;
                }
            }
            auto M = rotate(eulerAngle.x, vec3(0, 1, 0));
            M *= rotate(eulerAngle.y, vec3(1, 0, 0));
            auto tr = vec3(world.worldDimension) * 0.5f;
            tr.z *= -1.0f;
            cameraDirection = M;
            cameraOrigin = translate(vec3(tr.x, tr.y, -tr.z)) * cameraDirection * translate(vec3(0, 0, 3.5 * tr.z));

            prevMouseDown = state == GLFW_PRESS;
            lastFrameMousePos = ivec2(xpos, ypos);
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_3D, world.world);
        glBindTexture(GL_TEXTURE_2D, accum);
        glBindImageTexture(1, accum, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, seed);
        glBindImageTexture(2, seed, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, composed);
        glBindImageTexture(3, composed, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glUniform1i(glGetUniformLocation(program, "world"), 0);
        glUniform2f(glGetUniformLocation(program, "iResolution"), 1280, 720);
        glUniform3i(glGetUniformLocation(program, "worldDimension"),
                    world.worldDimension.x,
                    world.worldDimension.y,
                    world.worldDimension.z);
        glUniform1i(glGetUniformLocation(program, "iTime"), iTime++);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraOrigin"), 1, GL_FALSE, &cameraOrigin[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraDirection"), 1, GL_FALSE, &cameraDirection[0][0]);
        glDispatchCompute(std::ceil(w / 16), std::ceil(h / 16), 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        if (iTime % 200 == 0)
            printf("pass = %d\n", iTime);
    }
};

struct Application {
    GLFWwindow *window;
    std::unique_ptr<Renderer> renderer;

    Application() {
        if (!glfwInit()) {
            fprintf(stderr, "failed to init glfw");
            exit(1);
        }
        window = glfwCreateWindow(1280, 720, "NanoVoxel", nullptr, nullptr);
        glfwMakeContextCurrent(window);
        glfwSetScrollCallback(window, [](GLFWwindow *window, double xoffset, double yoffset) {
            glfwMouseScrollY = yoffset;
        });
        glfwSwapInterval(0);
        if (0 != gl3wInit()) {
            fprintf(stderr, "failed to init gl3w");
            exit(1);
        }

        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_TEXTURE_3D);
        glDebugMessageCallback(MessageCallback, 0);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void) io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
        //io.ConfigViewportsNoAutoMerge = true;
        //io.ConfigViewportsNoTaskBarIcon = true;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsClassic();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer bindings
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 430");

        renderer = std::make_unique<Renderer>();
        renderer->compileShader();
        renderer->setUpWorld();

    }

    void displayUI() {
        if (ImGui::Begin("View", nullptr, ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::Image((void *) renderer->composed, ImVec2(1280, 720));
            ImGui::End();
        }
    }

    void show() {
        ImGuiIO &io = ImGui::GetIO();
        while (!glfwWindowShouldClose(window)) {
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer->render(window);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            displayUI();
            ImGui::Render();

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                GLFWwindow *backup_current_context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backup_current_context);
            }


            /* Swap front and back buffers */
            glfwSwapBuffers(window);

            /* Poll for and process events */
            glfwPollEvents();
        }
        glfwTerminate();
    }
};

int main(int argc, char **argv) {
    Application app;
    app.show();
    return 0;
}