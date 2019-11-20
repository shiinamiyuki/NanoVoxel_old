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
            if (io.MouseWheel != 0 && inside) {
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
            auto tr = vec3(world->worldDimension) * 0.5f;
            tr.z *= -1.0f;
            cameraDirection = M;
            cameraOrigin =
                    translate(vec3(tr.x, tr.y, -tr.z)) * cameraDirection * translate(vec3(0, 0, orbitDistance * tr.z));

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
        renderer->world = McLoader("../data/r.-2.0.mca");
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

void World::loadMinecraftMaterials() {
    //stone
    materialNames[1] = "stone";
    materials.MaterialBaseColor[1] = vec4(112, 112, 112, 1) / 255.0f;
    //grass
    materialNames[2] = "grass";
    materials.MaterialBaseColor[2] = vec4(127, 178, 56, 1) / 255.0f;

    //dirt
    materialNames[3] = "dir";
    materials.MaterialBaseColor[3] = vec4(151, 109, 77, 1) / 255.0f;

    //leaves
    materialNames[18] = "leaves";
    materials.MaterialBaseColor[18] = vec4(0, 124, 0, 1) / 255.0f;

    //water
    materialNames[8] = "water 1";
    materials.MaterialBaseColor[8] = vec4(64, 64, 255, 1) / 255.0f;
    materialNames[9] = "water 2";
    materials.MaterialBaseColor[9] = vec4(64, 64, 255, 1) / 255.0f;

    // wood
    materialNames[17] = "wood";
    materials.MaterialBaseColor[17] = vec4(143, 119, 72, 1) / 255.0f;
}