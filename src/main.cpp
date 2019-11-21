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
#include <array>
#include <sstream>
#include <chrono>
#include <enkimi.h>
#include <miniz.h>
#include <optional>

using namespace glm;

#define ENABLE_ATMOSPHERE_SCATTERING 0x1


void GLAPIENTRY
MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam) {
    if (GL_DEBUG_SEVERITY_HIGH == severity)
        fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
                (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
                type, severity, message);
}


#include "../shaders/compute-shader.h"
#include "../shaders/external-shaders.h"
#include "../shaders/bsdf.h"
#include "../shaders/common-defs.h"

void setUpDockSpace();

struct World {
    std::vector<uint8_t> data;
    ivec3 worldDimension, alignedDimension;
    GLuint world;
    GLuint materialsSSBO;
    float sunHeight = 0.0f;
#define MATERIAL_COUNT 256
    struct Materials {
        vec4 MaterialEmission[MATERIAL_COUNT];
        vec4 MaterialBaseColor[MATERIAL_COUNT];
        float MaterialRoughness[MATERIAL_COUNT] = {0};
        float MaterialBetalness[MATERIAL_COUNT] = {0};
        float MaterialSpecular[MATERIAL_COUNT] = {0};
        float MaterialEmissionStrength[MATERIAL_COUNT] = {1};
    } materials;
    std::string materialNames[MATERIAL_COUNT];

    void loadMinecraftMaterials();

    void initData() {
//#pragma  omp parallel for default(none)
//        for (int x = 0; x < worldDimension.x; x++) {
//            siv::PerlinNoise perlin;
//            for (int y = 0; y < worldDimension.y; y++) {
//                for (int z = 0; z < worldDimension.z; z++) {
//                    vec3 p = vec3(x, y, z);// / vec3(worldDimension);
//                    p *= 0.1f;
//                    auto n = perlin.noise0_1(p.x, p.y, p.z);
//                    if (0.6 < n && n < 0.8) {
//                        if (n > 0.73) {
//                            (*this)(x, y, z) = 2;
//                        } else {
//                            (*this)(x, y, z) = 1;
//                        }
//                    } else {
//                        (*this)(x, y, z) = 0;
//                    }
//                }
//            }
//        }
        for (int i = 1; i < MATERIAL_COUNT; i++) {
            std::ostringstream os;
            os << "Material " << i;
            materialNames[i] = os.str();
        }
        glGenBuffers(1, &materialsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, materialsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Materials), NULL, GL_DYNAMIC_COPY);
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

std::optional<std::pair<ivec3, ivec3>> getWorldBound(const std::string &filename) {
    // open the region file
    FILE *fp = fopen(filename.c_str(), "rb");
    if (!fp) {
        printf("failed to open file\n");
        return {};
    }

    // output file
    FILE *fpOutput = stdout;//fopen("output.txt", "w");
    if (!fpOutput) {
        printf("failed to open output file\n");
        return {};;
    }

    enkiRegionFile regionFile = enkiRegionFileLoad(fp);
    ivec3 worldMin(std::numeric_limits<int>::max()), worldMax(std::numeric_limits<int>::min());
    for (int i = 0; i < ENKI_MI_REGION_CHUNKS_NUMBER; i++) {
        enkiNBTDataStream stream;
        enkiInitNBTDataStreamForChunk(regionFile, i, &stream);
        if (stream.dataLength) {
            enkiChunkBlockData aChunk = enkiNBTReadChunk(&stream);
            enkiMICoordinate chunkOriginPos = enkiGetChunkOrigin(&aChunk); // y always 0
            ivec3 chunkPos = ivec3(chunkOriginPos.x,
                                   chunkOriginPos.y, chunkOriginPos.z);
            worldMin = min(worldMin, chunkPos);
            worldMax = max(worldMax, chunkPos);
//            fprintf(fpOutput, "Chunk at xyz{ %d, %d, %d }  Number of sections: %d \n", chunkOriginPos.x,
//                    chunkOriginPos.y, chunkOriginPos.z, aChunk.countOfSections);

            // iterate through chunk and count non 0 voxels as a demo
            int64_t numVoxels = 0;
            for (int section = 0; section < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++section) {
                if (aChunk.sections[section]) {
                    enkiMICoordinate sectionOrigin = enkiGetChunkSectionOrigin(&aChunk, section);

                    enkiMICoordinate sPos;
                    // note order x then z then y iteration for cache efficiency
                    for (sPos.y = 0; sPos.y < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.y) {
                        for (sPos.z = 0; sPos.z < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.z) {
                            for (sPos.x = 0; sPos.x < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.x) {
                                uint8_t voxel = enkiGetChunkSectionVoxel(&aChunk, section, sPos);
                                if (voxel) {
                                    ++numVoxels;
                                }
                            }
                        }
                    }
                }
            }
            //fprintf(fpOutput, "   Chunk has %g non zero voxels\n", (float) numVoxels);

            enkiNBTRewind(&stream);
            // PrintStreamStructureToFile(&stream, fpOutput);
        }
        enkiNBTFreeAllocations(&stream);
    }

    enkiRegionFileFreeAllocations(&regionFile);

    printf("%d %d %d  to %d %d %d\n", worldMin.x, worldMin.y, worldMin.z, worldMax.x, worldMax.y, worldMax.z);
    fclose(fp);
    return std::make_pair(worldMin, worldMax + ivec3(16, 256, 16));
}

std::shared_ptr<World> McLoader(const std::string &filename) {
    auto bound = getWorldBound(filename);
    if (!bound) {
        return nullptr;
    }
    // open the region file
    FILE *fp = fopen(filename.c_str(), "rb");
    if (!fp) {
        printf("failed to open file\n");
        return nullptr;
    }

    // output file
    FILE *fpOutput = stdout;//fopen("output.txt", "w");
    if (!fpOutput) {
        printf("failed to open output file\n");
        return nullptr;
    }
    auto worldMin = bound.value().first;
    auto worldMax = bound.value().second;

    auto world = std::make_shared<World>(worldMax - worldMin);
    printf("world size %d %d %d\n", world->worldDimension.x, world->worldDimension.y, world->worldDimension.z);
    enkiRegionFile regionFile = enkiRegionFileLoad(fp);

    for (int i = 0; i < ENKI_MI_REGION_CHUNKS_NUMBER; i++) {
        enkiNBTDataStream stream;
        enkiInitNBTDataStreamForChunk(regionFile, i, &stream);
        if (stream.dataLength) {
            enkiChunkBlockData aChunk = enkiNBTReadChunk(&stream);
            enkiMICoordinate chunkOriginPos = enkiGetChunkOrigin(&aChunk); // y always 0
            ivec3 chunkPos = ivec3(chunkOriginPos.x,
                                   chunkOriginPos.y, chunkOriginPos.z);

//            fprintf(fpOutput, "Chunk at xyz{ %d, %d, %d }  Number of sections: %d \n", chunkOriginPos.x,
//                    chunkOriginPos.y, chunkOriginPos.z, aChunk.countOfSections);

            // iterate through chunk and count non 0 voxels as a demo
            int64_t numVoxels = 0;
            for (int section = 0; section < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++section) {
                if (aChunk.sections[section]) {
                    enkiMICoordinate sectionOrigin = enkiGetChunkSectionOrigin(&aChunk, section);

                    enkiMICoordinate sPos;
                    // note order x then z then y iteration for cache efficiency
                    for (sPos.y = 0; sPos.y < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.y) {
                        for (sPos.z = 0; sPos.z < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.z) {
                            for (sPos.x = 0; sPos.x < ENKI_MI_NUM_SECTIONS_PER_CHUNK; ++sPos.x) {
                                uint8_t voxel = enkiGetChunkSectionVoxel(&aChunk, section, sPos);
                                auto p = ivec3(sPos.x, sPos.y, sPos.z) +
                                         ivec3(sectionOrigin.x, sectionOrigin.y, sectionOrigin.z) - worldMin;
                                (*world)(p) = voxel;

                            }
                        }
                    }
                }
            }
            //fprintf(fpOutput, "   Chunk has %g non zero voxels\n", (float) numVoxels);

            enkiNBTRewind(&stream);
            // PrintStreamStructureToFile(&stream, fpOutput);
        }
        enkiNBTFreeAllocations(&stream);
    }

    enkiRegionFileFreeAllocations(&regionFile);


    fclose(fp);
    return world;
}

struct Renderer {
    GLint program;
    GLuint VBO;
    GLuint seed;
    std::shared_ptr<World> world;
    mat4 cameraDirection, cameraOrigin;
    GLuint sample; // texture for 1 spp
    GLuint accum; // accumlated sample
    GLuint composed; // post processor
    ivec2 mousePos, prevMousePos, lastFrameMousePos;
    bool prevMouseDown = false;
    int iTime = 0;
    vec2 eulerAngle = vec2(0, 0);
    bool needRedraw = true;
    uint32_t options = ENABLE_ATMOSPHERE_SCATTERING;
    float orbitDistance = 2.5f;

    enum CameraMode {
        Free,
        Orbit
    };

    CameraMode cameraMode = Free;

    explicit Renderer() {
    }

    void compileShader() {
        std::vector<char> error(4096, 0);
        auto shader = glCreateShader(GL_COMPUTE_SHADER);
        GLint success;
        const char *version = "#version 430\n";
        const char *src[] = {
                version,
                commondDefsSource,
                externalShaderSource,
                bsdfSource,
                computeShaderSource
        };
        glShaderSource(shader, sizeof(src) / sizeof(src[0]), src, nullptr);
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
        world->setUpTexture();
        cameraOrigin = translate(vec3(20, 20, -20));
        cameraDirection = identity<mat4>();//<=inverse(M);
    }

    void render(GLFWwindow *window) {
        {
            if (needRedraw) {
                iTime = 0;
            }
            double xpos, ypos;
            auto &io = ImGui::GetIO();
            xpos = io.MousePos.x;
            ypos = io.MousePos.y;

            auto windowPos = ImGui::GetWindowPos();
            auto size = ImGui::GetWindowSize();

            auto pos = io.MousePos;
            bool pressed = io.MouseDown[1];
            bool inside = (pos.x >= windowPos.x && pos.y >= windowPos.y
                           && pos.x < windowPos.x + size.x && pos.y < windowPos.y + size.y);
            if (io.MouseWheel != 0 && inside && cameraMode == Orbit) {
                iTime = 0;
                if (io.MouseWheel < 0) {
                    orbitDistance *= 1.1;
                } else {
                    orbitDistance *= 0.9;
                }
            }
            if (pressed && inside) {
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
            if (cameraMode == Orbit) {
                auto tr = vec3(world->worldDimension) * 0.5f;
                tr.z *= -1.0f;
                cameraDirection = M;
                cameraOrigin =
                        translate(vec3(tr.x, tr.y, -tr.z)) * cameraDirection *
                        translate(vec3(0, 0, orbitDistance * tr.z));
            } else {
                // free
                float step = 1.0f;
                vec3 o = cameraOrigin * vec4(0, 0, 0, 1);
                auto R = rotate(eulerAngle.x, vec3(0, 1, 0));
                if (io.KeysDown['A']) {
                    o += vec3(step * R * vec4(-1, 0, 0, 1));
                    iTime = 0;
                }
                if (io.KeysDown['D']) {
                    o += vec3(step * R * vec4(1, 0, 0, 1));
                    iTime = 0;
                }
                if (io.KeysDown['W']) {
                    o += vec3(step * R * vec4(0, 0, 1, 1));
                    iTime = 0;
                }
                if (io.KeysDown['S']) {
                    o += vec3(step * R * vec4(0, 0, -1, 1));
                    iTime = 0;
                }
                if (io.KeyCtrl) {
                    o += vec3(step * R * vec4(0, -1, 0, 1));
                    iTime = 0;
                }
                if (io.KeyShift) {
                    o += vec3(step * R * vec4(0, 1, 0, 1));
                    iTime = 0;
                }
                cameraOrigin = translate(o);
                cameraDirection = M;
            }
            prevMouseDown = pressed;
            lastFrameMousePos = ivec2(xpos, ypos);
        }
        vec3 sunPos = vec3(0, cos(world->sunHeight) * 0.3 + 0.2, -1);
        int w = 1280, h = 720;
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_3D, world->world);
        glBindTexture(GL_TEXTURE_2D, accum);
        glBindImageTexture(1, accum, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, seed);
        glBindImageTexture(2, seed, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, composed);
        glBindImageTexture(3, composed, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glUniform1i(glGetUniformLocation(program, "world"), 0);
        glUniform2f(glGetUniformLocation(program, "iResolution"), w, h);
        glUniform3i(glGetUniformLocation(program, "worldDimension"),
                    world->worldDimension.x,
                    world->worldDimension.y,
                    world->worldDimension.z);
        glUniform1i(glGetUniformLocation(program, "iTime"), iTime++);
        glUniform1ui(glGetUniformLocation(program, "options"), options);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraOrigin"), 1, GL_FALSE, &cameraOrigin[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraDirection"), 1, GL_FALSE, &cameraDirection[0][0]);
        glUniform3fv(glGetUniformLocation(program, "sunPos"), 1, (float *) &sunPos);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, world->materialsSSBO);
        if (needRedraw) {
            //printf("redraw\n");

            GLvoid *p = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
            memcpy(p, &world->materials, sizeof(World::Materials));
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }
//        GLuint blockIndex = 0;
//        blockIndex = glGetProgramResourceIndex(program, GL_SHADER_STORAGE_BLOCK, "Materials");
//        glShaderStorageBlockBinding(program, blockIndex, 4);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, world->materialsSSBO);
        glDispatchCompute(std::ceil(w / 16), std::ceil(h / 16), 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        if (iTime % 200 == 0)
            printf("pass = %d\n", iTime);
        needRedraw = false;
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
        window = glfwCreateWindow(1920, 1080, "NanoVoxel", nullptr, nullptr);
        glfwMakeContextCurrent(window);
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
        renderer->world = McLoader("../data/r.-21.12.mca");
        renderer->world->loadMinecraftMaterials();
        renderer->compileShader();
        renderer->setUpWorld();

    }

    int selectedMaterialIndex = 0;

    void showEditor() {
        if (ImGui::Begin("Explorer")) {
            for (int i = 1; i < MATERIAL_COUNT; i++) {
                if (ImGui::Selectable(renderer->world->materialNames[i].c_str(), selectedMaterialIndex == i)) {
                    selectedMaterialIndex = i;
                }
            }
            ImGui::End();
        }
        if (ImGui::Begin("Inspector")) {
            auto &needRedraw = renderer->needRedraw;
            if (ImGui::BeginTabBar("Tab##Inspector")) {
                if (ImGui::BeginTabItem("Material")) {
                    if (selectedMaterialIndex != 0) {
                        auto &emission = renderer->world->materials.MaterialEmission[selectedMaterialIndex];
                        auto &emissionStrength = renderer->world->materials.MaterialEmissionStrength[selectedMaterialIndex];
                        auto &baseColor = renderer->world->materials.MaterialBaseColor[selectedMaterialIndex];
                        if (ImGui::ColorPicker3("Emission", (float *) &emission)) {
                            needRedraw = true;
                        }
                        if (ImGui::InputFloat("Emission Strength", &emissionStrength)) {
                            needRedraw = true;
                        }
                        if (ImGui::ColorPicker3("Base Color", (float *) &baseColor)) {
                            needRedraw = true;
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Render")) {
                    bool enable = renderer->options & ENABLE_ATMOSPHERE_SCATTERING;
                    if (ImGui::Checkbox("Atmospheric Scattering", &enable)) {
                        if (enable) {
                            renderer->options |= ENABLE_ATMOSPHERE_SCATTERING;
                        } else {
                            renderer->options &= ~ENABLE_ATMOSPHERE_SCATTERING;
                        }
                        needRedraw = true;
                    }
                    float theta = renderer->world->sunHeight / M_PI * 180.0;
                    if (ImGui::SliderFloat("Sun Height", &theta, 0.0f, 180.0f)) {
                        renderer->world->sunHeight = theta / 180.0f * M_PI;
                        needRedraw = true;
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }
    }

    void displayUI() {
        setUpDockSpace();

        if (ImGui::Begin("View", nullptr, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
            renderer->render(window);
            ImGui::Image(reinterpret_cast<void *> (renderer->composed), ImVec2(1280, 720));
            ImGui::End();
        }
        showEditor();
    }

    void show() {
        ImGuiIO &io = ImGui::GetIO();
        while (!glfwWindowShouldClose(window)) {
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

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

void setUpDockSpace() {
    static bool opt_fullscreen_persistant = true;
    bool opt_fullscreen = opt_fullscreen_persistant;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    // because it would be confusing to have two docking targets within each others.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen) {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove;
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
    ImGuiIO &io = ImGui::GetIO();

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);


    ImGui::End();
}

// https://github.com/erich666/Mineways/blob/master/Win/blockInfo.h


// fills whole block
#define BLF_WHOLE            0x0001
// almost a whole block
#define BLF_ALMOST_WHOLE    0x0002
// stairs
#define BLF_STAIRS            0x0004
// half block
#define BLF_HALF            0x0008
// fair-sized, worth rendering, has geometry
#define BLF_MIDDLER            0x0010
// larger billboard object worth rendering
#define BLF_BILLBOARD        0x0020
// billboard flat through middle, usually transparent (portal, glass pane)
#define BLF_PANE            0x0040
// sits on top of a block below it
#define BLF_FLATTEN            0x0080
// flat on a wall: sign, ladder, etc. - normally not shown on the map; to make something visible on map, use BLF_FLATTEN instead, which otherwise is identical
#define BLF_FLATTEN_SMALL        0x0100
// small, not as worth rendering (will disappear if not flattened, etc. when exporting for a 3D print), has geometry - normally not shown on the map
#define BLF_SMALL_MIDDLER    0x0200
// small thing: lever, flower - normally culled out
#define BLF_SMALL_BILLBOARD    0x0400

// has an alpha for the whole block (vs. glass, which often has a frame that's solid)
#define BLF_TRANSPARENT        0x0800
// has cutout parts to its texture, on or off (no semitransparent alpha)
#define BLF_CUTOUTS            0x1000
// trunk
#define BLF_TRUNK_PART      0x2000
// leaf
#define BLF_LEAF_PART       0x4000
// is related to trees - if something is floating and is a tree, delete it for printing
#define BLF_TREE_PART       (BLF_TRUNK_PART|BLF_LEAF_PART)
// is an entrance of some sort, for sealing off building interiors
#define BLF_ENTRANCE        0x8000
// export image texture for this object, as it makes sense - almost everything has this property (i.e. has a texture tile)
// actually, now everything has this property, so it's eliminated
//#define BLF_IMAGE_TEXTURE   0x10000

// this object emits light - affects output material
#define BLF_EMITTER         0x10000
// this object attaches to fences; note that fences do not have this property themselves, so that nether & regular fence won't attach
#define BLF_FENCE_NEIGHBOR    0x20000
// this object outputs its true geometry (not just a block) for rendering
#define BLF_TRUE_GEOMETRY    0x40000
// this object outputs its special non-full-block geometry for 3D printing, if the printer can glue together the bits.
// Slightly different than TRUE_GEOMETRY in that things that are just too thin don't have this bit set.
#define BLF_3D_BIT          0x80000
// this object is a 3D bit, and this bit is set if it can actually glue horizontal neighboring blocks together
// - not really used. TODO - may want to use this to decide whether objects should be grouped together or whatever.
#define BLF_3D_BIT_GLUE     0x100000
// set if the block does not affect fluid height. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_DNE_FLUID        0x200000
// set if the block connects to redstone - do only if there's no orientation to the block, e.g. repeaters attach only on two sides, so don't have this flag
#define BLF_CONNECTS_REDSTONE        0x400000
// has no geometry, on purpose
#define BLF_NONE            0x800000
// is an offset tile, rendered separately: rails, vines, lily pad, redstone, ladder (someday, tripwire? TODO)
#define BLF_OFFSET            0x1000000
// is a billboard or similar that is always underwater, such as seagrass and kelp. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_WATERLOG        0x2000000
// is a billboard or similar that may waterlog, such as coral fans; bit 0x100 is set if waterlogged. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_MAYWATERLOG        0x4000000
// this object is a gate that attachs to fences if oriented properly - like BLF_FENCE_NEIGHBOR, but needs orientation to connect
#define BLF_FENCE_GATE        0x8000000
// this object is a fence that attachs to fences if of the right type - like BLF_FENCE_NEIGHBOR, but needs for types (nether, wood) to match to connect
#define BLF_FENCE            0x10000000

#define BLF_CLASS_SE

typedef struct BlockDefinition {
    const char *name;
    unsigned int read_color;    // r,g,b, locked in place, never written to: used for initial setting of color
    float read_alpha;
    unsigned int color;    // r,g,b, NOT multiplied by alpha - input by the user, result of color scheme application
    unsigned int pcolor;    // r,g,b, premultiplied by alpha (basically, unmultColor * alpha) - used (only) in mapping
    float alpha;
    int txrX;   // column and row, from upper left, of 16x16 tiles in terrainExt.png, for TOP view of block
    int txrY;
    unsigned char subtype_mask;    // bits that are used in the data value to determine whether this is a separate material
    unsigned int flags;
} BlockDefinition;

BlockDefinition gBlockDefinitions[] = {
        // Ignore the premultiplied colors and alphas - these really are just placeholders, it's color * alpha that sets them when the program starts up.
        // name                               		read_color ralpha color     prem-clr  alpha,     txX,Y   mtl, flags
        { /*   0 */ "Air",                              0x000000, 0.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 14, 0x00, BLF_NONE},    //00
        { /*   1 */ "Stone",                            0x7C7C7C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  0,  0x07, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //01
        // Grass block color is from Plains biome color (default terrain in a flat world). Grass and Sunflower should also be changed if this is changed.
        { /*   2 */ "Grass Block", /*output!*/          0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  0,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //02 3,0 side gets turned into 6,2
        { /*   3 */ "Dirt",                             0x8c6344, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  0,  0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //03
        { /*   4 */ "Cobblestone",                      0x828282, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //04
        { /*   5 */ "Oak Planks",                       0x9C8149, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x07, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //05
        { /*   6 */ "Sapling",                          0x7b9a29, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 0,  0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //06
        { /*   7 */ "Bedrock",                          0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //07
        { /*   8 */ "Water",                            0x295dfe, 0.535f, 0xff7711, 0xff7711, 0.12345f,                       15, 13, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRANSPARENT},    //08
        { /*   9 */ "Stationary Water",                 0x295dfe, 0.535f, 0xff7711, 0xff7711, 0.12345f,                       15, 13, 0x10, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRANSPARENT},    //09
        { /*  10 */ "Lava",                             0xf56d00, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 15, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_EMITTER},    //0a
        { /*  11 */ "Stationary Lava",                  0xf56d00, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 15, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_EMITTER},    //0b
        { /*  12 */ "Sand",                             0xDCD0A6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  1,  0x01, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //0c/12
        { /*  13 */ "Gravel",                           0x857b7b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //0d
        { /*  14 */ "Gold Ore",                         0xfcee4b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  2,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //0e
        { /*  15 */ "Iron Ore",                         0xbc9980, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  2,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //0f
        { /*  16 */ "Coal Ore",                         0x343434, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  2,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //10
        { /*  17 */ "Oak Log",                          0x695333, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  1,  0x03, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //11/17
        // Leaves block color is from Plains biome color (default terrain in a flat world). Acacia Leaves should also be changed if this is changed.
        { /*  18 */ "Oak Leaves",                       0x6fac2c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  3,  0x03, BLF_WHOLE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_LEAF_PART},    //12
        { /*  19 */ "Sponge",                           0xD1D24E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  3,  0x01, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //13
        { /*  20 */ "Glass",                            0xc0f6fe, 0.500f, 0xff7711, 0xff7711, 0.12345f,                       1,  3,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_CUTOUTS},    //14 - note that BLF_TRANSPARENT is not flagged, 0x00FF, Because glass is either fully on or off, not blended
        { /*  21 */ "Lapis Lazuli Ore",                 0x143880, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  10, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //15
        { /*  22 */ "Lapis Lazuli Block",               0x1b4ebb, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  9,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //16
        { /*  23 */ "Dispenser",                        0x6f6f6f, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 3,  0x00, BLF_WHOLE},    //17 14,2 front, 13,2 sides
        { /*  24 */ "Sandstone",                        0xe0d8a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  11, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //18 0,12 side, 0,13 bottom
        { /*  25 */ "Note Block",                       0x342017, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 8,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //19 10,4 side
        { /*  26 */ "Bed",                              0xff3333, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  8,  0x00, BLF_HALF |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //1a
        { /*  27 */ "Powered Rail",                     0xAB0301, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  11, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET},    //1b/27
        { /*  28 */ "Detector Rail",                    0xCD5E58, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  12, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE |
                                                                                                                                            BLF_OFFSET},    //1c
        { /*  29 */ "Sticky Piston",                    0x719e60, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 6,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //1d
        { /*  30 */ "Cobweb",                           0xeeeeee, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 0,  0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //1e
        { /*  31 */ "Grass",       /*output!*/          0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  2,  0x03, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //1f/31
        { /*  32 */ "Dead Bush",                        0x946428, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  3,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //20/32
        { /*  33 */ "Piston",                           0x95774b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 6,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //21
        { /*  34 */ "Piston Head",                      0x95774b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 6,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //22/34
        { /*  35 */ "Wool",                             0xEEEEEE, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  4,  0x0f, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //23 - gets converted to colors at end
        { /*  36 */ "Block moved by Piston",            0x000000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 6,  0x00, BLF_NONE},    //24 (36) - really, nothing...
        { /*  37 */ "Dandelion",                        0xD3DD05, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 0,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //25
        { /*  38 */ "Poppy",                            0xCE1A05, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 0,  0x0f, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //26 - 38
        { /*  39 */ "Brown Mushroom",                   0xc19171, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 1,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //27
        { /*  40 */ "Red Mushroom",                     0xfc5c5d, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 1,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //28
        { /*  41 */ "Block of Gold",                    0xfef74e, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //29
        { /*  42 */ "Block of Iron",                    0xeeeeee, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //2a
        { /*  43 */ "Double Stone Slab",                0xa6a6a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 23, 0x07, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //2b/43 - was 6,0, and 5,0 side
        { /*  44 */ "Stone Slab",                       0xa5a5a5, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 23, 0x07, BLF_HALF |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //2c/44 - was 6,0, and 5,0 side
        { /*  45 */ "Bricks",                           0x985542, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  0,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //2d
        { /*  46 */ "TNT",                              0xdb441a, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  0,  0x00, BLF_WHOLE},    //2e 7,0 side, 9,0 under
        { /*  47 */ "Bookshelf",                        0x795a39, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //2f 3,2
        { /*  48 */ "Mossy Cobblestone",                0x627162, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  2,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //30
        { /*  49 */ "Obsidian",                         0x1b1729, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  2,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //31
        { /*  50 */ "Torch",                            0xfcfc00, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  5,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_DNE_FLUID},    //32/50 - should be BLF_EMITTER, flatten torches only if sides get flattened, too
        { /*  51 */ "Fire",                             0xfca100, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* somewhat bogus */ 15, 1,  0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_DNE_FLUID},    //33/51 - no billboard, sadly BLF_CUTOUTS
        { /*  52 */ "Monster Spawner",                  0x254254, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  4,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_CUTOUTS},    //34
        { /*  53 */ "Oak Stairs",                       0x9e804f, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //35
        { /*  54 */ "Chest",                            0xa06f23, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  1,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //36 (10,1) side; (11,1) front
        { /*  55 */ "Redstone Wire",/*"Dust"*/          0xd60000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  10, 0x0F, BLF_FLATTEN |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_CONNECTS_REDSTONE |
                                                                                                                                            BLF_OFFSET},    //37 - really, 0xfd3200 is lit, we use a "neutral" red here
        { /*  56 */ "Diamond Ore",                      0x5decf5, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  3,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //38
        { /*  57 */ "Block of Diamond",                 0x7fe3df, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //39
        { /*  58 */ "Crafting Table",                   0x825432, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 2,  0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //3a - and cartography, fletching, and smithing
        { /*  59 */ "Wheat",                            0x766615, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 5,  0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //3b
        { /*  60 */ "Farmland",                         0x40220b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  5,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},    //3c - 7,5 dry
        { /*  61 */ "Furnace",                          0x767677, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 3,  0x30, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //3d 13,2 side, 12,2 front
        { /*  62 */ "Burning Furnace",                  0x777676, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 3,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //3e 13,2 side, 13,3 front
        { /*  63 */ "Standing Sign",                    0x9f814f, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          4,  0,  0x30, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},    //3f (63)
        { /*  64 */ "Oak Door",                         0x7e5d2d, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  5,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT}, // 40 1,6 bottom	//40 TODO: BLF_FLATSIDE?
        { /*  65 */ "Ladder",                           0xaa8651, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  5,  0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET |
                                                                                                                                            BLF_MAYWATERLOG},    //41
        { /*  66 */ "Rail",                             0x686868, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  8,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET},    //42 - TODO: doesn't do angled pieces, top to bottom edge
        { /*  67 */ "Cobblestone Stairs",               0x818181, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  1,  0x30, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //43 (67)
        { /*  68 */ "Wall Sign",                        0xa68a46, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          4,  0,  0x38, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},    //44
        { /*  69 */ "Lever",                            0x8a6a3d, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  6,  0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //45
        { /*  70 */ "Stone Pressure Plate",             0xa4a4a4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  0,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //46 (70)
        { /*  71 */ "Iron Door",                        0xb2b2b2, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  5,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //47 (71) 2,6 bottom TODO BLF_FLATSIDE?
        { /*  72 */ "Oak Pressure Plate",               0x9d7f4e, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /*  73 */ "Redstone Ore",                     0x8f0303, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  3,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //49
        { /*  74 */ "Glowing Redstone Ore",             0x900303, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  3,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //4a (74)
        { /*  75 */ "Redstone Torch (inactive)",        0x560000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  7,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //4b
        { /*  76 */ "Redstone Torch (active)",          0xfd0000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  6,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //4c should be BLF_EMITTER, 0x00FF, But it makes the whole block glow
        { /*  77 */ "Stone Button",                     0xacacac, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  0,  0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //4d
        { /*  78 */ "Snow",                             0xf0fafa, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  4,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //4e
        { /*  79 */ "Ice",                              0x7dacfe, 0.613f, 0xff7711, 0xff7711, 0.12345f,                       3,  4,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_TRANSPARENT},    //4f
        { /*  80 */ "Snow Block",                       0xf1fafa, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  4,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //50 4,4 side
        { /*  81 */ "Cactus",                           0x0D6118, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  4,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //51 6,4 side - note: the cutouts are not used when "lesser" is off for rendering, 0x00FF, But so it goes.
        { /*  82 */ "Clay",                             0xa2a7b4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  4,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //52
        { /*  83 */ "Sugar Cane",                       0x72944e, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  4,  0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //53
        { /*  84 */ "Jukebox",                          0x8a5a40, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 4,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //54 11,3 side
        { /*  85 */ "Fence",                            0x9f814e, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //55
        { /*  86 */ "Pumpkin",                          0xc07615, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  6,  0x04, BLF_WHOLE},    //56 6,7 side, 7,7 face
        { /*  87 */ "Netherrack",                       0x723a38, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  6,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //57
        { /*  88 */ "Soul Sand",                        0x554134, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  6,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //58
        { /*  89 */ "Glowstone",                        0xf9d49c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  6,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_EMITTER},   //59
        { /*  90 */ "Nether Portal",                    0x472272, 0.800f, 0xff7711, 0xff7711, 0.12345f,                       14, 0,  0x00, BLF_PANE |
                                                                                                                                            BLF_TRANSPARENT |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_DNE_FLUID},   //5a/90 - 0xd67fff unpremultiplied
        { /*  91 */ "Jack o'Lantern",                   0xe9b416, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  6,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_EMITTER},   //5b 6,7 side, 8,7 lit face
        { /*  92 */ "Cake",                             0xfffdfd, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  7,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},   //5c 10,7 side, 11,7 inside, 12,7 under - TODO: not really whole
        { /*  93 */ "Redstone Repeater (inactive)",     0x560000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  8,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},   //5d
        { /*  94 */ "Redstone Repeater (active)",       0xee5555, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  9,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},   //5e
        // in 1.7 locked chest was replaced by stained glass block
        //{"Locked Chest",           0xa06f23, 1.000f, 0xa06f23,  9, 1, 0x0F, BLF_WHOLE},   //5f/95 (10,1) side; (11,1) front
        { /*  95 */ "Stained Glass",                    0xEFEFEF, 0.500f, 0xff7711, 0xff7711, 0.12345f,                       0,  20, 0x0f, BLF_WHOLE |
                                                                                                                                            BLF_TRANSPARENT},    //5f/95 - note BLF_CUTOUTS is off, since all pixels are semitransparent
        { /*  96 */ "Oak Trapdoor",                     0x886634, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  5,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   //60/96 - tricky case: could be a flattop, or a flatside. For now, render it
        { /*  97 */ "Infested Stone",                   0x787878, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  0,  0x07, BLF_WHOLE},   //61 - was "Monster Egg"
        { /*  98 */ "Stone Bricks",                     0x797979, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  3,  0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //62
        { /*  99 */ "Brown Mushroom Block",             0x654b39, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 7,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //63
        { /* 100 */ "Red Mushroom Block",               0xa91b19, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 7,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //64
        { /* 101 */ "Iron Bars",                        0xa3a4a4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  5,  0x00, BLF_PANE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_MAYWATERLOG},   //65
        { /* 102 */ "Glass Pane",                       0xc0f6fe, 0.500f, 0xff7711, 0xff7711, 0.12345f,                       1,  3,  0x00, BLF_PANE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_MAYWATERLOG},   //66
        { /* 103 */ "Melon",                            0xaead27, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  8,  0x00, BLF_WHOLE},   //67 (8,8) side
        { /* 104 */ "Pumpkin Stem",                     0xE1C71C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 11, 0x07, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},   //68/104 15,11 connected TODO
        { /* 105 */ "Melon Stem",                       0xE1C71C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 6,  0x07, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},   //69/105 15,7 connected TODO
        { /* 106 */ "Vines",                            0x76AB2F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 8,  0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_PANE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET},   //6a
        { /* 107 */ "Fence Gate",                       0xa88754, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          4,  0,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},   // oddly, fence gates do not waterlog
        { /* 108 */ "Brick Stairs",                     0xa0807b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  0,  0x30, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},   //6c
        { /* 109 */ "Stone Brick Stairs",               0x797979, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  3,  0x30, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},   //6d
        { /* 110 */ "Mycelium",                         0x685d69, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 4,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //6e 13,4 side, 2,0 bottom
        { /* 111 */ "Lily Pad",                         0x217F30, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 4,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET},   //6f
        { /* 112 */ "Nether Brick",                     0x32171c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  14, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //70/112
        { /* 113 */ "Nether Brick Fence",               0x241316, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  14, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},   //71
        { /* 114 */ "Nether Brick Stairs",              0x32171c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  14, 0x30, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},   //72
        { /* 115 */ "Nether Wart",                      0x81080a, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  14, 0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},   //73
        { /* 116 */ "Enchanting Table",                 0xa6701a, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  10, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},   //74 6,11 side, 7,11 under
        { /* 117 */ "Brewing Stand",                    0x77692e, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          13, 9,  0x00, BLF_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_CUTOUTS},   //75 13,8 base - no BLF_IMAGE_TEXTURE
        { /* 118 */ "Cauldron",                         0x3b3b3b, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 8,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},   //76 - 10,8 top (inside's better), 10,9 side, 11,9 feet TODO: not really whole
        { /* 119 */ "End Portal",                       0x0c0b0b, 0.7f,   0xff7711, 0xff7711, 0.12345f,                       8,  11, 0x00, BLF_PANE |
                                                                                                                                            BLF_TRANSPARENT},   //77 - not really whole, no real texture, make it a portal
        { /* 120 */ "End Portal Frame",                 0x3e6c60, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 9,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},   //78 15,9 side, 15,10 bottom
        { /* 121 */ "End Stone",                        0xdadca6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 10, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //79
        { /* 122 */ "Dragon Egg",                       0x1b1729, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  10, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    //7A - not really whole
        { /* 123 */ "Redstone Lamp (inactive)",         0x9F6D4D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  13, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},   //7b
        { /* 124 */ "Redstone Lamp (active)",           0xf9d49c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  13, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR |
                                                                                                                                            BLF_EMITTER},    //7c
        { /* 125 */ "Double Oak Slab",                  0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //7d
        { /* 126 */ "Oak Slab",                         0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x07, BLF_HALF |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //7e
        { /* 127 */ "Cocoa",                            0xBE742D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  10, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //7f/127
        { /* 128 */ "Sandstone Stairs",                 0xe0d8a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  11, 0x30, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //80/128
        { /* 129 */ "Emerald Ore",                      0x7D8E81, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 10, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //81
        { /* 130 */ "Ender Chest",                      0x293A3C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 13, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},    //82 - don't really have tiles for this one, added to terrainExt.png
        { /* 131 */ "Tripwire Hook",                    0xC79F63, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 10, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_DNE_FLUID},    //83 - decal
        { /* 132 */ "Tripwire",                         0x000000, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 10, 0x00, BLF_NONE |
                                                                                                                                            BLF_DNE_FLUID},    //84 - sorta redwire decal, 0x00FF, But really it should be invisible, so BLF_NONE. Color 0x8F8F8F
        // alternate { /* 130 */ "Tripwire",               0x8F8F8F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,10, 0x0F, BLF_FLATTOP|BLF_CUTOUTS},    //84 - sorta redwire decal, but really it should be invisible, so BLF_NONE. Color 0x8F8F8F
        { /* 133 */ "Block of Emerald",                 0x53D778, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 14, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //85
        { /* 134 */ "Spruce Stairs",                    0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  12, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //86
        { /* 135 */ "Birch Stairs",                     0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  13, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //87
        { /* 136 */ "Jungle Stairs",                    0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  12, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //88
        { /* 137 */ "Command Block",                    0xB28B79, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //89
        { /* 138 */ "Beacon",                           0x9CF2ED, 0.800f, 0xff7711, 0xff7711, 0.12345f,                       11, 14, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_EMITTER},    //8A/138 - it's a whole block sorta, it doesn't attach to fences or block wires
        { /* 139 */ "Cobblestone Wall",                 0x828282, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  1,  0x0f, BLF_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},   //8B
        { /* 140 */ "Flower Pot",                       0x7C4536, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 11, 0xff, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},   //8C
        { /* 141 */ "Carrot",                           0x056B05, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 12, 0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //8d/141
        { /* 142 */ "Potato",                           0x00C01B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 12, 0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    //8e/142
        { /* 143 */ "Oak Button",                       0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  0,  0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //8f/143
        { /* 144 */ "Mob Head",                         0xcacaca, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  6,  0xF0, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    //90/144 - TODO; we use a pumpkin for now...
        { /* 145 */ "Anvil",                            0x404040, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  13, 0x0c, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    // 91/145 - NOTE: the top swatch is not used, the generic side swatch is
        // 1.6
        { /* 146 */ "Trapped Chest",                    0xa06f23, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  1,  0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE |
                                                                                                                                            BLF_MAYWATERLOG},    // 92/146
        { /* 147 */ "Light Weighted Pressure Plate",    0xEFE140, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  1,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    // 93/147 gold
        { /* 148 */ "Heavy Weighted Pressure Plate",    0xD7D7D7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  1,  0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    // 94/148 iron
        { /* 149 */ "Redstone Comparator",              0xC5BAAD, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 14, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE},   // 95/149 TODO from repeater off
        { /* 150 */ "Redstone Comparator (deprecated)", 0xD1B5AA, 1.0f,   0xff7711, 0xff7711, 0.12345f,                       15, 14, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_CONNECTS_REDSTONE},   // 96/150 TODO from repeater on
        { /* 151 */ "Daylight Sensor",                  0xBBA890, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  15, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},   // 97/151 TODO from trapdoor
        { /* 152 */ "Block of Redstone",                0xA81E09, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 15, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_EMITTER |
                                                                                                                                            BLF_FENCE_NEIGHBOR |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    // 98/152
        { /* 153 */ "Nether Quartz Ore",                0x7A5B57, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  17, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR}, // 99/153
        { /* 154 */ "Hopper",                           0x363636, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 15, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    // 9A/154 - note that 3d print version is simpler, no indentation, so it's thick enough
        { /* 155 */ "Block of Quartz",                  0xE0DDD7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  17, 0x01, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // 9B/155
        { /* 156 */ "Quartz Stairs",                    0xE1DCD1, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  17, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    // 9C/156
        { /* 157 */ "Activator Rail",                   0x880300, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 17, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_OFFSET},    // 9D/157
        { /* 158 */ "Dropper",                          0x6E6E6E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 3,  0x00, BLF_WHOLE},    // 9E/158
        { /* 159 */ "Colored Terracotta",               0xCEAE9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  16, 0x0f, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // 9F/159
        { /* 160 */ "Stained Glass Pane",               0xEFEFEF, 0.500f, 0xff7711, 0xff7711, 0.12345f,                       0,  20, 0x0f, BLF_PANE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_TRANSPARENT |
                                                                                                                                            BLF_MAYWATERLOG},    // A0/160 - semitransparent, not a cutout like glass panes are
        { /* 161 */ "Acacia Leaves",                    0x6fac2c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 19, 0x01, BLF_WHOLE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_LEAF_PART},    //A1/161
        { /* 162 */ "Acacia Log",                       0x766F64, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 19, 0x01, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //A2/162
        { /* 163 */ "Acacia Stairs",                    0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  22, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //A3
        { /* 164 */ "Dark Oak Stairs",                  0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  22, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //A4/164
        { /* 165 */ "Slime Block",                      0x787878, 0.500f, 0xff7711, 0xff7711, 0.12345f,                       3,  22, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_TRANSPARENT |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // A5/165 - 1.8
        { /* 166 */ "Barrier",                          0xE30000, 0.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 25, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_CUTOUTS},    // A6/166 - 1.8 - to make visible, set alpha to 1.0
        { /* 167 */ "Iron Trapdoor",                    0xC0C0C0, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},    // A7/167 - 1.8
        { /* 168 */ "Prismarine",                       0x66ADA1, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 22, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // 1.8 add
        { /* 169 */ "Sea Lantern",                      0xD3DBD3, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 22, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_EMITTER},   //59
        { /* 170 */ "Hay Bale",                         0xB5970C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 15, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // AA/170
        { /* 171 */ "Carpet",                           0xEAEDED, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  4,  0x0f, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // AB/171
        { /* 172 */ "Terracotta",                       0x945A41, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  17, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // same as 159, except that it doesn't have names
        { /* 173 */ "Block of Coal",                    0x191919, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 14, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // AD/173
        // 1.7
        { /* 174 */ "Packed Ice",                       0x7dacfe, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 17, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // AE/174 - like ice, but not transparent, and a fence neighbor
        { /* 175 */ "Sunflower",                        0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  18, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},    // AF/175 - note color is used to multiply grayscale textures, so don't change it
        { /* 176 */ "Banner",                           0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 177 */ "Wall Banner",                      0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 178 */ "Inverted Daylight Sensor",         0xBBA890, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},
        { /* 179 */ "Red Sandstone",                    0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 19, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 180 */ "Red Sandstone Stairs",             0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 19, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 181 */ "Double Red Sandstone Slab",        0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 19, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    //2b/43
        { /* 182 */ "Red Sandstone Slab",               0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 19, 0x07, BLF_HALF |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},    //2c/44
        { /* 183 */ "Spruce Fence Gate",                0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          6,  12, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 184 */ "Birch Fence Gate",                 0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          6,  13, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 185 */ "Jungle Fence Gate",                0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          7,  12, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 186 */ "Dark Oak Fence Gate",              0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          1,  22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 187 */ "Acacia Fence Gate",                0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */          0,  22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FENCE_GATE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 188 */ "Spruce Fence",                     0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  12, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 189 */ "Birch Fence",                      0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  13, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 190 */ "Jungle Fence",                     0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  12, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 191 */ "Dark Oak Fence",                   0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 192 */ "Acacia Fence",                     0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  22, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_FENCE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 193 */ "Spruce Door",                      0x7A5A36, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  23, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 194 */ "Birch Door",                       0xD6CA8C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  23, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 195 */ "Jungle Door",                      0xB2825E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  23, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 196 */ "Acacia Door",                      0xB16640, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  23, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 197 */ "Dark Oak Door",                    0x51341A, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  23, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},    // yes, for this one dark oak really does go after acacia
        { /* 198 */ "End Rod",                          0xE0CFD0, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 23, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 199 */ "Chorus Plant",                     0x654765, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 23, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 200 */ "Chorus Flower",                    0x937E93, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 23, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 201 */ "Purpur Block",                     0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 202 */ "Purpur Pillar",                    0xAC82AC, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 203 */ "Purpur Stairs",                    0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  24, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 204 */ "Double Purpur Slab",               0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 205 */ "Purpur Slab",                      0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  24, 0x07, BLF_HALF |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 206 */ "End Stone Bricks",                 0xE2E8AC, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 207 */ "Beetroot Seeds",                   0x6D7F44, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  24, 0x00, BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 208 */ "Grass Path",                       0x977E48, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 209 */ "End Gateway",                      0x1A1828, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  11, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 210 */ "Repeating Command Block",          0x8577B2, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 24, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 211 */ "Chain Command Block",              0x8AA59A, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  25, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 212 */ "Frosted Ice",                      0x81AFFF, 0.613f, 0xff7711, 0xff7711, 0.12345f,                       6,  25, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_TRANSPARENT},
        { /* 213 */ "Magma Block",                      0x9D4E1D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  26, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 214 */ "Nether Wart Block",                0x770C0D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  26, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 215 */ "Red Nether Bricks",                0x470709, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  26, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 216 */ "Bone Block",                       0xE1DDC9, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  26, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR}, // top location; side is previous tile
        { /* 217 */ "Structure Void",                   0xff0000, 0.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  8,  0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // uses red wool TODOTODO
        { /* 218 */ "Observer",                         0x6E6E6E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  33, 0x00, BLF_WHOLE},
        { /* 219 */ "White Shulker Box",                0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  27, 0x00, BLF_WHOLE},
        { /* 220 */ "Orange Shulker Box",               0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  27, 0x00, BLF_WHOLE},
        { /* 221 */ "Magenta Shulker Box",              0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  27, 0x00, BLF_WHOLE},
        { /* 222 */ "Light Blue Shulker Box",           0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  27, 0x00, BLF_WHOLE},
        { /* 223 */ "Yellow Shulker Box",               0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  27, 0x00, BLF_WHOLE},
        { /* 224 */ "Lime Shulker Box",                 0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  27, 0x00, BLF_WHOLE},
        { /* 225 */ "Pink Shulker Box",                 0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  27, 0x00, BLF_WHOLE},
        { /* 226 */ "Gray Shulker Box",                 0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  27, 0x00, BLF_WHOLE},
        { /* 227 */ "Light Grey Shulker Box",           0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  27, 0x00, BLF_WHOLE},
        { /* 228 */ "Cyan Shulker Box",                 0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  27, 0x00, BLF_WHOLE},
        { /* 229 */ "Purple Shulker Box",               0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 27, 0x00, BLF_WHOLE},
        { /* 230 */ "Blue Shulker Box",                 0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 27, 0x00, BLF_WHOLE},
        { /* 231 */ "Brown Shulker Box",                0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 27, 0x00, BLF_WHOLE},
        { /* 232 */ "Green Shulker Box",                0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 27, 0x00, BLF_WHOLE},
        { /* 233 */ "Red Shulker Box",                  0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 27, 0x00, BLF_WHOLE},
        { /* 234 */ "Black Shulker Box",                0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 27, 0x00, BLF_WHOLE},
        { /* 235 */ "White Glazed Terracotta",          0xD4DBD7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  28, 0x00, BLF_WHOLE},
        { /* 236 */ "Orange Glazed Terracotta",         0xC09A7F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  28, 0x00, BLF_WHOLE},
        { /* 237 */ "Magenta Glazed Terracotta",        0xD26FC1, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  28, 0x00, BLF_WHOLE},
        { /* 238 */ "Light Blue Glazed Terracotta",     0x80B3D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  28, 0x00, BLF_WHOLE},
        { /* 239 */ "Yellow Glazed Terracotta",         0xEDC671, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  28, 0x00, BLF_WHOLE},
        { /* 240 */ "Lime Glazed Terracotta",           0xB0C84F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  28, 0x00, BLF_WHOLE},
        { /* 241 */ "Pink Glazed Terracotta",           0xEC9EB7, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  28, 0x00, BLF_WHOLE},
        { /* 242 */ "Gray Glazed Terracotta",           0x5B6164, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  28, 0x00, BLF_WHOLE},
        { /* 243 */ "Light Grey Glazed Terracotta",     0x9FACAD, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  28, 0x00, BLF_WHOLE},
        { /* 244 */ "Cyan Glazed Terracotta",           0x4F8288, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  28, 0x00, BLF_WHOLE},
        { /* 245 */ "Purple Glazed Terracotta",         0x7633A5, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 28, 0x00, BLF_WHOLE},
        { /* 246 */ "Blue Glazed Terracotta",           0x324D98, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 28, 0x00, BLF_WHOLE},
        { /* 247 */ "Brown Glazed Terracotta",          0x896E60, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 28, 0x00, BLF_WHOLE},
        { /* 248 */ "Green Glazed Terracotta",          0x7F9563, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 28, 0x00, BLF_WHOLE},
        { /* 249 */ "Red Glazed Terracotta",            0xB8433B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 28, 0x00, BLF_WHOLE},
        { /* 250 */ "Black Glazed Terracotta",          0x592225, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 28, 0x00, BLF_WHOLE},
        { /* 251 */ "Concrete",                         0xCFD5D6, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  29, 0x0f, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 252 */ "Concrete Powder",                  0xE2E4E4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  30, 0x0f, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 253 */ "Unknown Block",                    0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // same as bedrock
        { /* 254 */ "Unknown Block",                    0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  1,  0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // same as bedrock - BLOCK_FAKE is used here
        { /* 255 */ "Structure Block",                  0x665E5F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 25, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},

        // just to be safe, we don't use 256 and consider it AIR
        // name                               read_color ralpha color     prem-clr  alpha,   txX,  Y,  mtl, flags
        { /* 256 */ "Air",                              0x000000, 0.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 14, 0x00, BLF_NONE},
        { /* 257 */ "Prismarine Stairs",                0x66ADA1, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 22, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 258 */ "Prismarine Brick Stairs",          0x68A495, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 22, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 259 */ "Dark Prismarine Stairs",           0x355F4E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 22, 0x00, BLF_STAIRS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 260 */ "Spruce Trapdoor",                  0x674E34, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 34, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   // tricky case: could be a flattop, or a flatside. For now, render it
        { /* 261 */ "Birch Trapdoor",                   0xD3C8A8, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 34, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   // tricky case: could be a flattop, or a flatside. For now, render it
        { /* 262 */ "Jungle Trapdoor",                  0x9D7250, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 34, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   // tricky case: could be a flattop, or a flatside. For now, render it
        { /* 263 */ "Acacia Trapdoor",                  0xA05936, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 34, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   // tricky case: could be a flattop, or a flatside. For now, render it
        { /* 264 */ "Dark Oak Trapdoor",                0x4E3318, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  35, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_ENTRANCE |
                                                                                                                                            BLF_FLATTEN |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},   // tricky case: could be a flattop, or a flatside. For now, render it
        { /* 265 */ "Spruce Button",                    0x6B5030, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  12, 0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 266 */ "Birch Button",                     0x9E7250, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  13, 0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 267 */ "Jungle Button",                    0xC5B57C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  12, 0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 268 */ "Acacia Button",                    0xAB5D34, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  22, 0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 269 */ "Dark Oak Button",                  0x3F2813, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  22, 0x00, BLF_FLATTEN_SMALL |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},
        { /* 270 */ "Spruce Pressure Plate",            0x6B5030, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  12, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /* 271 */ "Birch Pressure Plate",             0x9E7250, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  13, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /* 272 */ "Jungle Pressure Plate",            0xC5B57C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  12, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /* 273 */ "Acacia Pressure Plate",            0xAB5D34, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  22, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /* 274 */ "Dark Oak Pressure Plate",          0x3F2813, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  22, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_CONNECTS_REDSTONE},    //48
        { /* 275 */ "Stripped Oak Log",                 0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  34, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 276 */ "Stripped Acacia Log",              0xA85C3B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 34, 0x01, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 277 */ "Stripped Oak Wood",                0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  34, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 278 */ "Stripped Acacia Wood",             0xAF5D3C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  34, 0x01, BLF_WHOLE |
                                                                                                                                            BLF_TRUNK_PART |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 279 */ "Orange Banner",                    0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 280 */ "Magenta Banner",                   0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 281 */ "Light Blue Banner",                0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 282 */ "Yellow Banner",                    0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 283 */ "Lime Banner",                      0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 284 */ "Pink Banner",                      0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 285 */ "Gray Banner",                      0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 286 */ "Light Gray Banner",                0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 287 */ "Cyan Banner",                      0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 288 */ "Purple Banner",                    0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 289 */ "Blue Banner",                      0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 290 */ "Brown Banner",                     0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 291 */ "Green Banner",                     0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 292 */ "Red Banner",                       0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 293 */ "Black Banner",                     0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY},    // assumed to be like signs in properties, but cannot 3D print (too darn thin)
        { /* 294 */ "Orange Wall Banner",               0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 295 */ "Magenta Wall Banner",              0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       2,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 296 */ "Light Blue Wall Banner",           0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       3,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 297 */ "Yellow Wall Banner",               0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 298 */ "Lime Wall Banner",                 0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 299 */ "Pink Wall Banner",                 0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 300 */ "Gray Wall Banner",                 0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 301 */ "Light Gray Wall Banner",           0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 302 */ "Cyan Wall Banner",                 0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       9,  29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 303 */ "Purple Wall Banner",               0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 304 */ "Blue Wall Banner",                 0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 305 */ "Brown Wall Banner",                0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 306 */ "Green Wall Banner",                0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 307 */ "Red Wall Banner",                  0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 308 */ "Black Wall Banner",                0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 29, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID},    // BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
        { /* 309 */ "Tall Seagrass",                    0x30790E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 33, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_WATERLOG},
        { /* 310 */ "Seagrass",                         0x3D8B17, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       15, 33, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_WATERLOG},
        { /* 311 */ "Smooth Stone",                     0xA0A0A0, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 23, 0x03, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 312 */ "Blue Ice",                         0x75A8FD, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 33, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},    // like ice, but not transparent, and a fence neighbor
        { /* 313 */ "Dried Kelp Block",                 0x414534, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       7,  33, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 314 */ "Kelp", /* plant == 0 */            0x58912E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 33, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_WATERLOG},
        { /* 315 */ "Coral Block",                      0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       6,  35, 0x07, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 316 */ "Dead Coral Block",                 0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       1,  35, 0x07, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 317 */ "Coral",                            0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 35, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 318 */ "Coral Fan",                        0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 35, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 319 */ "Dead Coral Fan",                   0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  36, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 320 */ "Coral Wall Fan",                   0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 35, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 321 */ "Dead Coral Wall Fan",              0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       5,  36, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 322 */ "Conduit",                          0xA5927A, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 36, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 323 */ "Sea Pickle",                       0x616B3B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 33, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 324 */ "Turtle Egg",                       0xEAE4C2, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 36, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_3D_BIT},
        // 1.14
        { /* 325 */ "Dead Coral",                       0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       14, 36, 0x07, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 326 */ "Standing Sign",                    0x9f814f, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  22, 0x10, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_MAYWATERLOG},    // acacia and dark oak, sigh
        { /* 327 */ "Sweet Berry Bush",                 0x32613c, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       12, 37, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_SMALL_BILLBOARD |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_DNE_FLUID}, // does not stop fluid
        { /* 328 */ "Bamboo",                           0x619324, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 37, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY},
        { /* 329 */ "Double Andesite Slab",             0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  22, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 330 */ "Andesite Slab",                    0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  22, 0x07, BLF_HALF |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE |
                                                                                                                                            BLF_MAYWATERLOG},
        { /* 331 */ "Jigsaw",                           0x665E5F, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       13, 39, 0x00, BLF_WHOLE |
                                                                                                                                            BLF_FENCE_NEIGHBOR},
        { /* 332 */ "Composter",                        0x774C27, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 38, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 333 */ "Barrel",                           0x86643B, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  38, 0x00, BLF_WHOLE},
        { /* 334 */ "Stone Cutter",                     0x7D7975, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  41, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT |
                                                                                                                                            BLF_3D_BIT_GLUE},
        { /* 335 */ "Grindstone",                       0x8E8E8E, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       10, 39, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 336 */ "Lectern",                          0xAF8B55, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  40, 0x00, BLF_ALMOST_WHOLE |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 337 */ "Bell",                             0xC69E36, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       4,  38, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_3D_BIT},
        { /* 338 */ "Lantern",                          0x846C5A, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       11, 37, 0x00, BLF_SMALL_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_EMITTER},
        { /* 339 */ "Campfire",                         0xE0B263, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       0,  39, 0x00, BLF_FLATTEN |
                                                                                                                                            BLF_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY |
                                                                                                                                            BLF_EMITTER},
        { /* 340 */ "Scaffolding",                      0xB38D54, 1.000f, 0xff7711, 0xff7711, 0.12345f,                       8,  40, 0x00, BLF_MIDDLER |
                                                                                                                                            BLF_CUTOUTS |
                                                                                                                                            BLF_TRUE_GEOMETRY},

        // Important note: skip 396 if we get there, it's the BLOCK_FLOWER_POT, also skip 400, BLOCK_HEAD. Nicer still would be to redo the code for those two blocks (and redo IDBlock() method) so that we don't use up all 8 bits
};

auto hexToRGB(uint32_t x) {
    auto r = (x & 0xff0000) >> 16;
    auto g = (x & 0xff00) >> 8;
    auto b = x & 0xff;
    return vec4(vec3(r, g, b) / 255.0f, 1.0);
}

extern float minecraftTexture[];

void World::loadMinecraftMaterials() {
    for (int i = 1; i < MATERIAL_COUNT; i++) {
        materialNames[i] = gBlockDefinitions[i].name;
        materials.MaterialBaseColor[i] = hexToRGB(gBlockDefinitions[i].read_color);
    }
}