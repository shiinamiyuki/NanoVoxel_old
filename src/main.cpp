//
// Created by Shiina Miyuki on 11/18/2019.
//

#include <GL/gl3w.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <cstdio>
#include <vector>
#include <iostream>
#include <algorithm>

using namespace glm;

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

const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos, 1.0);
}
)";
const char *fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
uniform vec2 iResolution;

uniform mat4 cameraTransform;
uniform ivec3 worldDimension;
uniform sampler3D world;


void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    uv = 2.0 * uv - vec2(1.0);
    vec3 o = vec3(0,0,0);
    vec3 d = normalize(vec3(uv, 1) - vec3(0));
    FragColor = vec4(d, 1.0);
}
)";

struct World {
    std::vector<uint8_t> data;
    ivec3 worldDimension;
    GLuint world;

    World(const ivec3 &worldDimension) : worldDimension(worldDimension) {
        data.resize(worldDimension.x * worldDimension.y * worldDimension.z);
    }

    uint8_t &operator()(int x, int y, int z) {
        x = std::clamp(x, 0, worldDimension.x - 1);
        y = std::clamp(y, 0, worldDimension.y - 1);
        z = std::clamp(z, 0, worldDimension.z - 1);

        return data[x + y * worldDimension.y + z * worldDimension.y * worldDimension.x];
    }

    uint8_t &operator()(const ivec3 &x) {
        return (*this)(x.x, x.y, x.z);
    }

    void setUpTexture() {
        glGenTextures(1, &world);
        glBindTexture(GL_TEXTURE_3D, world);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, worldDimension.x, worldDimension.y, worldDimension.z, 0, GL_RED,
                     GL_UNSIGNED_BYTE, data.data());
    }
};

struct Renderer {
    GLint program;
    GLuint VBO;
    World world;
    mat4 cameraTransform;

    explicit Renderer() : world(ivec3(20, 20, 20)) {

    }

    void compileShader() {
        std::vector<char> error(4096, 0);
        auto vert = glCreateShader(GL_VERTEX_SHADER);
        auto frag = glCreateShader(GL_FRAGMENT_SHADER);
        GLint success;
        glShaderSource(frag, 1, &fragmentShaderSource, NULL);
        glCompileShader(frag);
        glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(frag, error.size(), NULL, error.data());
            std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << error.data() << std::endl;
        };
        glShaderSource(vert, 1, &vertexShaderSource, NULL);
        glCompileShader(vert);
        glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vert, error.size(), NULL, error.data());
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << error.data() << std::endl;
        }
        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, error.size(), NULL, error.data());
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << error.data() << std::endl;
        }
        glDeleteShader(vert);
        glDeleteShader(frag);
        std::cout << "Shader compiled without complaint" << std::endl;
    }

    void setUpVBO() {
        static float vertices[] = {
                // first triangle
                1.0f, 1.0f, 0.0f, 1.0f, 1.0f,  // top right
                1.0f, -1.0f, 0.0f, 1.0f, 0.0f,  // bottom right
                -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top left
                // second triangle
                1.0f, -1.0f, 0.0f, 1.0f, 0.0f, // bottom right
                -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // bottom left
                -1.0f, 1.0f, 0.0f, 0.0f, 1.0f  // top left
        };
        glGenBuffers(1, &VBO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    }

    void setUpWorld() {
        world.setUpTexture();
    }

    void render(GLFWwindow *window) {
        int w, h;
        glfwGetWindowSize(window, &w, &h);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) 0);
        glEnableVertexAttribArray(0);
        glUseProgram(program);
        glBindTexture(GL_TEXTURE_3D, world.world);
        glUniform2f(glGetUniformLocation(program, "iResolution"), w, h);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};

int main(int argc, char **argv) {
    if (!glfwInit()) {
        fprintf(stderr, "failed to init glfw");
        exit(1);
    }
    GLFWwindow *window = glfwCreateWindow(1280, 720, "NanoVoxel", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    if (0 != gl3wInit()) {
        fprintf(stderr, "failed to init gl3w");
        exit(1);
    }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_TEXTURE_3D);
    glDebugMessageCallback(MessageCallback, 0);
    Renderer renderer;
    renderer.setUpVBO();
    renderer.setUpWorld();
    renderer.compileShader();

    while (!glfwWindowShouldClose(window)) {
        /* Render here */
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.render(window);
        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}