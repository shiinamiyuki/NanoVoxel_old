//
// Created by Shiina Miyuki on 11/18/2019.
//

#include <GL/gl3w.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <cstdio>

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
    while (!glfwWindowShouldClose(window)) {
        /* Render here */
        glClear(GL_COLOR_BUFFER_BIT);

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}