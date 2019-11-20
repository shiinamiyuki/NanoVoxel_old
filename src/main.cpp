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

const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos, 1.0);
}
)";
const char *fragmentShaderSource = R"(
#version 420 core
out vec4 FragColor;
layout(binding = 0) uniform sampler3D world;
layout(binding = 1, rgba32f)  uniform image2D accumlatedImage;
layout(binding = 2, rgba32f)  uniform image2D seeds;
uniform vec2 iResolution;
uniform mat4 cameraOrigin;
uniform mat4 cameraDirection;
uniform ivec3 worldDimension;
uniform int iTime;

struct Material {
    vec3 emission;
    vec3 baseColor;
    float roughness;
    float metalness;
    float specular;
};


const float M_PI = 3.1415926535;
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
    if(t < minComp(tmax)){
    	t = max(t, 0.0);
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
    Material mat;
};
bool insideWorld(vec3 p){
    return all(lessThan(p, vec3(worldDimension)+vec3(1))) && all(greaterThanEqual(p, vec3(-2)));
}
int map(vec3 p){
    return int(texelFetch(world, ivec3(p), 0).r * 255.0);
}


bool intersect1(vec3 ro, vec3 rd, out Intersection isct)
{
    vec3 n;
    float distance = intersectBox(ro, rd, vec3(-1.5), vec3(worldDimension)+vec3(0.5), n);
    if(distance < 0.0){
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
    int maxIter = int(dot(worldDimension, ivec3(1)));
	for (int i = 0; i < maxIter; ++i) {
		if (!insideWorld(p)) {
			break;
		}
		int mat = map(p);

		if (mat > 0 && t >= RayBias) {
			isct.p = p0 + rd* t;
			// find normal
			isct.t = distance + t;
			isct.n = -sign(rd) * mask;
            isct.mat.baseColor = vec3(83, 135, 219)/255.0;
            if(mat == 2){
                isct.mat.emission = vec3(3);
            }else{
                isct.mat.emission = vec3(0);
            }
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

bool intersect(vec3 ro, vec3 rd, out Intersection isct){
    if(intersect1(ro, rd,isct)){
        return true;
    }
    float t = ro.y / -rd.y;
    if(t < RayBias)
        return false;
    isct.t = t;
    isct.p = ro + t * rd;
    isct.n = vec3(0,1,0);
    isct.mat.baseColor = vec3(1);
    return true;
}

// Returns 2D random point in [0,1]²
vec2 random2(vec2 st){
  st = vec2( dot(st,vec2(127.1,311.7)),
             dot(st,vec2(269.5,183.3)) );
  return fract(sin(st)*43758.5453123);
}
// Inputs:
//   st  3D seed
// Returns 2D random point in [0,1]²
vec2 random2(vec3 st){
  vec2 S = vec2( dot(st,vec3(127.1,311.7,783.089)),
             dot(st,vec3(269.5,183.3,173.542)) );
  return fract(sin(S)*43758.5453123);
}

struct Sampler{
    int dimension;
    uint seed;
};

float nextFloat(inout Sampler sampler){
    sampler.seed = (1103515245*(sampler.seed) + 12345);
    sampler.dimension++;
    return float(sampler.seed) * 2.3283064370807974e-10;
}

vec2 nextFloat2(inout Sampler sampler){
    return vec2(nextFloat(sampler), nextFloat(sampler));
}
struct LocalFrame{
    vec3 N, T, B;
};

void computeLocalFrame(vec3 N, out LocalFrame frame){
    frame.N = N;
    if(abs(N.x) > abs(N.y)){
        frame.T = vec3(-N.z, 0.0f, N.x) / sqrt(N.z * N.z + N.x * N.x);
    }else{
        frame.T = vec3(0.0f, -N.z, N.y) / sqrt(N.z * N.z + N.y * N.y);
    }
    frame.B = normalize(cross(N, frame.T));
}

vec3 worldToLocal(vec3 v, in LocalFrame frame){
    return vec3(dot(v, frame.T),dot(v,frame.N),dot(v, frame.B));
}

vec3 localToWorld(vec3 v, in LocalFrame frame){
    return v.x * frame.T +  v.y * frame.N + v.z * frame.B;
}

vec2 diskSampling(vec2 u){
    float r = u.x;
    float t = u.y * 2.0 * M_PI;
    r = sqrt(r);
    return vec2(r * cos(t),r * sin(t));
}

vec3 cosineHemisphereSampling(vec2 u){
    vec2 d = diskSampling(u);
    float h = 1.0 - dot(d, d);
    return vec3(d.x, sqrt(h), d.y);
}

vec3 LiBackground(vec3 o, vec3 d){
    return vec3(0);
}
#define AO
#ifdef AO
vec3 Li(vec3 o, vec3 d, inout Sampler sampler) {
    Intersection isct;
    float tmax = 100.0f;
    if(!intersect(o, d, isct)){
        return LiBackground(o, d);
    }
    LocalFrame frame;
    computeLocalFrame(isct.n, frame);
    vec3 wi = cosineHemisphereSampling(nextFloat2(sampler));
    wi = localToWorld(wi, frame);
    o = isct.p + RayBias * wi;
    d = wi;
    if(!intersect(o, d, isct) || isct.t > 30.0){
        return vec3(1);
    }
    return vec3(0);
}
#else
const int maxDepth = 5;
vec3 Li(vec3 o, vec3 d, inout Sampler sampler) {
    Intersection isct;
    float tmax = 100.0f;
    vec3 L = vec3(0);
    vec3 beta = vec3(1);
    for(int depth = 0;depth < maxDepth;depth++){
        if(!intersect(o, d, isct)){
            L += beta * LiBackground(o, d);
            break;
        }
        L += beta * isct.mat.emission;
        LocalFrame frame;
        computeLocalFrame(isct.n, frame);
        vec3 wi = cosineHemisphereSampling(nextFloat2(sampler));
        wi = localToWorld(wi, frame);
        o = isct.p + RayBias * wi;
        d = wi;
        float pdf = abs(dot(isct.n, wi)) / M_PI;
        beta *= isct.mat.baseColor * abs(dot(isct.n, wi)) / pdf;
    }
    return L;
}
#endif
vec3 removeNaN(vec3 v){
    return mix(v, vec3(0), isnan(v));
}
void main() {
    vec2 uv = gl_FragCoord.xy / iResolution;
    Sampler sampler;
    sampler.dimension = 0;

    sampler.seed = floatBitsToUint(imageLoad(seeds, ivec2(gl_FragCoord.xy)).r);

    uv = 2.0 * uv - vec2(1.0);
    uv.x *= iResolution.x / iResolution.y;
    vec4 _o = (cameraOrigin * vec4(vec3(0), 1));
    vec3 o = _o.xyz / _o.w;
    float fov = 60.0 / 180.0 * M_PI;
    float z = 1.0 / tan(fov / 2.0);
    vec3 d = normalize(mat3(cameraDirection) * normalize(vec3(uv, z) - vec3(0,0,0)));
    vec4 color = vec4(removeNaN(Li(o, d, sampler)), 1.0);
    vec4 prevColor = imageLoad(accumlatedImage,  ivec2(gl_FragCoord.xy));
    if(iTime > 0)
        color += prevColor;
    FragColor = vec4(pow(color.rgb / color.a,vec3(1.0/2.2)), 1.0);
    imageStore(accumlatedImage,  ivec2(gl_FragCoord.xy), color);
    imageStore(seeds, ivec2(gl_FragCoord.xy), vec4(uintBitsToFloat(sampler.seed)));
}
)";

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
    GLuint post; // post processor
    ivec2 mousePos, prevMousePos, lastFrameMousePos;
    bool prevMouseDown = false;
    int iTime = 0;
    vec2 eulerAngle = vec2(0, 0);

    explicit Renderer() : world(ivec3(50, 50, 50)) {
    }

    void compileShader() {
        std::vector<char> error(4096, 0);
        auto vert = glCreateShader(GL_VERTEX_SHADER);
        auto frag = glCreateShader(GL_FRAGMENT_SHADER);
        GLint success;
        glShaderSource(frag, 1, &fragmentShaderSource, nullptr);
        glCompileShader(frag);
        glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(frag, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << error.data() << std::endl;
            exit(1);
        };
        glShaderSource(vert, 1, &vertexShaderSource, nullptr);
        glCompileShader(vert);
        glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vert, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << error.data() << std::endl;
            exit(1);
        }
        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << error.data() << std::endl;
            exit(1);
        }
        glDeleteShader(vert);
        glDeleteShader(frag);
        std::cout << "Shader compiled without complaint" << std::endl;

//        GLuint FBO;
//        glGenFramebuffers(1, &FBO);
//        glGenTextures(1, &sample);
//        glBindTexture(GL_TEXTURE_2D, sample);
//        glBindFramebuffer(GL_FRAMEBUFFER, FBO);
//        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
//            std::cerr << "?" << std::endl;
//            exit(1);
//        }

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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) 0);
        glEnableVertexAttribArray(0);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_3D, world.world);
        glBindTexture(GL_TEXTURE_2D, accum);
        glBindImageTexture(1, accum, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, seed);
        glBindImageTexture(2, seed, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glUniform1i(glGetUniformLocation(program, "world"), 0);
        glUniform2f(glGetUniformLocation(program, "iResolution"), w, h);
        glUniform3i(glGetUniformLocation(program, "worldDimension"),
                    world.worldDimension.x,
                    world.worldDimension.y,
                    world.worldDimension.z);
        glUniform1i(glGetUniformLocation(program, "iTime"), iTime++);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraOrigin"), 1, GL_FALSE, &cameraOrigin[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraDirection"), 1, GL_FALSE, &cameraDirection[0][0]);
        glDrawArrays(GL_TRIANGLES, 0, 6);

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
        renderer->setUpVBO();

    }

    void show() {
        ImGuiIO& io = ImGui::GetIO();
        while (!glfwWindowShouldClose(window)) {


            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0,0,0,0);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                GLFWwindow* backup_current_context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backup_current_context);
            }
            /* Render here */
            glClear(GL_COLOR_BUFFER_BIT);
            renderer->render(window);
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