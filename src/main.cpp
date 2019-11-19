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
float maxComp(vec3 o){
    return max(max(o.x,o.y),o.z);
}
float minComp(vec3 o){
	return min(min(o.x,o.y),o.z);
}
bool fleq(float x, float y){
	return abs(x - y) < 0.001;
}
const float RayBias = 1e-3f;
float intersectBox(vec3 o, vec3 d, vec3 p1, vec3 p2, out vec3 n){
	vec3 t0 = (p1 - o)/d;
    vec3 t1 = (p2 - o)/d;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float t = maxComp(tmin);
    if(t < minComp(tmax) && t >= RayBias){
    	t = max(t, RayBias);
        if(t > minComp(tmax)){
        	return -1.0;
        }
        vec3 p = o + t *d;
        if(fleq(p1.x,p.x)){
        	n = vec3(-1,0,0);
        }else if(fleq(p2.x,p.x)){
        	n = vec3(1,0,0);
        }else if(fleq(p1.y,p.y)){
           n = vec3(0,-1,0);
        }else if(fleq(p2.y,p.y)){
        	n = vec3(0,1,0);
        }else if(fleq(p1.z,p.z)){
           n = vec3(0,0,-1);
        }else {
        	n = vec3(0,0,1);
        }
        return t;
    }
    return -1.0;
}
struct Intersection{
    float t;
    vec3 n;
    vec3 p;
};
bool insideWorld(vec3 p){
    return all(lessThan(p, vec3(worldDimension))) && all(greaterThanEqual(p, vec3(-2)));
}
int map(vec3 p){
    return int(texelFetch(world, ivec3(p), 0).r * 255.0);
}


bool intersect(vec3 ro, vec3 rd, out Intersection isct)
{
    vec3 n;
    float distance = intersectBox(ro, rd, vec3(-1), vec3(worldDimension), n);
    if(distance < RayBias){
        return false;
    }

    const int maxSteps = 100;
    ro += distance * rd;
    vec3 p0 = ro;
	vec3 p = floor(p0);
	vec3 stp = sign(rd);
	vec3 invd =clamp(vec3(1, 1, 1) / rd, vec3(-1e10, -1e10, -1e10),vec3(1e10,1e10,1e10));
	vec3 tMax = abs((p + max(stp, vec3(0,0,0)) - p0) * invd);
	vec3 delta = abs(invd);
    isct.n = n;
	vec3 mask = vec3(0, 0, 0);
	float t = 0;
	for (int i = 0; i < 128; ++i) {
		if (!insideWorld(p)) {
			break;
		}
		int mat = map(p);

		if (mat > 0 && t >= RayBias) {
			isct.p = p0 + rd* t;
			// find normal
			isct.t = distance + t;
			isct.n = -sign(rd) * mask;

			return true;
		}
		if (tMax.x < tMax.y) {
			if (tMax.x < tMax.z) {
				p.x += stp.x;
				t = tMax.x;
				tMax.x += delta.x;
				mask = vec3(1, 0, 0);
			}
			else {
				p.z += stp.z; t = tMax.z;
				tMax.z += delta.z;

				mask = vec3(0, 0, 1);
			}
		}
		else {
			if (tMax.y < tMax.z) {
				p.y += stp.y; t = tMax.y;
				tMax.y += delta.y;

				mask = vec3(0, 1, 0);
			}
			else {
				p.z += stp.z; t = tMax.z;
				tMax.z += delta.z;

				mask = vec3(0, 0, 1);
			}
		}
	}
    return false;
}

struct Sampler{
    int dimension;
    vec3 seed;
};

float nextFloat(inout Sampler sampler){
    return 0.0f;
}
vec2 nextFloat2(inout Sampler sampler){
    return vec2(nextFloat(sampler), nextFloat(sampler));
}

vec3 Li(vec3 o, vec3 d){
    Intersection isct;
    float tmax = 100.0f;
    if(!intersect(o, d, isct)){
        return vec3(0);
    }
    return vec3(0.5) + 0.5 * isct.n;
}
void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    uv = 2.0 * uv - vec2(1.0);
    uv.x *= iResolution.x / iResolution.y;
    vec3 o = vec3(20,20,-10);
    vec3 d = normalize(vec3(uv, 1) - vec3(0));
    FragColor = vec4(Li(o, d), 1.0);
}
)";

struct World {
    std::vector<uint8_t> data;
    ivec3 worldDimension, alignedDimension;
    GLuint world;

    void initData() {
        for (int x = 0; x < worldDimension.x; x++) {
            for (int y = 0; y < worldDimension.y; y++) {
                for (int z = 0; z < worldDimension.z; z++) {
                    vec3 p = vec3(x, y, z) / vec3(worldDimension);
                    p = 2.0f * p - vec3(1);
                    if (length(p) < 0.4) {
                        (*this)(x, y, z) = 1;
                    } else {
                        (*this)(x, y, z) = 0;
                    }
                }
            }
        }

    }

    World(const ivec3 &worldDimension) : worldDimension(worldDimension) {
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
    World world;
    mat4 cameraTransform;

    explicit Renderer() : world(ivec3(50, 50, 50)) {

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
            exit(1);
        };
        glShaderSource(vert, 1, &vertexShaderSource, NULL);
        glCompileShader(vert);
        glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vert, error.size(), NULL, error.data());
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << error.data() << std::endl;
            exit(1);
        }
        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, error.size(), NULL, error.data());
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << error.data() << std::endl;
            exit(1);
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
        glUniform3i(glGetUniformLocation(program, "worldDimension"),
                    world.worldDimension.x,
                    world.worldDimension.y,
                    world.worldDimension.z);
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