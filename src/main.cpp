//
// Created by Shiina Miyuki on 11/18/2019.
//
#define _USE_MATH_DEFINES
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
#include <cmath>
#include <filesystem>
#include <mc.h>

namespace fs = std::filesystem;

using namespace glm;

#define ENABLE_ATMOSPHERE_SCATTERING 0x1

void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id,
                                GLenum severity, GLsizei length,
                                const GLchar *message, const void *userParam) {
    if (GL_DEBUG_SEVERITY_HIGH == severity)
        fprintf(stderr,
                "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
                (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type,
                severity, message);
}

#include "../shaders/compute-shader.h"
#include "../shaders/external-shaders.h"
#include "../shaders/bsdf.h"
#include "../shaders/common-defs.h"

void setUpDockSpace();
struct OctreeNode {
    alignas(16) ivec3 pmin;
    alignas(16) ivec3 pmax;
    std::array<int, 8> children = {-1, -1,-1, -1,-1, -1,-1, -1};
    bool isLeaf = false;
};
struct Box3i {
    ivec3 pmin;
    ivec3 pmax;
    ivec3 size() const { return pmax - pmin; }
};
struct World {
    std::vector<uint8_t> data;
    ivec3 worldDimension, alignedDimension;
    GLuint octreeBuffer;
    GLuint world;
    GLuint materialsSSBO;
    std::vector<OctreeNode> octree;
    int octreeRoot = -1;
    float sunHeight = 0.0f;
    float sunPhi = 0.0f;
    static const int octreeWidth = 8;
#define MATERIAL_COUNT 256
    struct Materials {
        vec4 MaterialEmission[MATERIAL_COUNT];
        vec4 MaterialBaseColor[MATERIAL_COUNT];
        float MaterialRoughness[MATERIAL_COUNT] = {0};
        float MaterialMetallic[MATERIAL_COUNT] = {0};
        float MaterialEmissionStrength[MATERIAL_COUNT] = {1};
    };
    std::unique_ptr<Materials>materials;
    std::vector<std::string> materialNames;

    void loadMinecraftMaterials();
    void buildOctree() {
        // auto s = ivec3(glm::ceil(
        //     glm::log(vec3(worldDimension) / vec3(octreeWidth)) / 3.0f));
        // octree.reserve(s.x * s.y * s.z);
        octreeRoot = buildOctree(Box3i{ivec3(0), worldDimension}, 0).value();
        printf("%d octree nodes; root=%d\n", (int)octree.size(), octreeRoot);
        auto box = Box3i{octree[octreeRoot].pmin, octree[octreeRoot].pmax};
        printf("box %d %d %d to %d %d %d\n", box.pmin.x, box.pmin.y, box.pmin.z,
               box.pmax.x, box.pmax.y, box.pmax.z);
    }
    std::optional<int> buildOctree(Box3i box, int level) {
        // if(level < 3)
        // printf("building octree %d %d %d to %d %d %d\n", box.pmin.x,
        // box.pmin.y, box.pmin.z , box.pmax.x, box.pmax.y, box.pmax.z);
        // printf("%d\n",level);
        // printf("= box %d %d %d to %d %d %d\n", box.pmin.x, box.pmin.y,
            //    box.pmin.z, box.pmax.x, box.pmax.y, box.pmax.z);
        if (glm::all(glm::lessThanEqual(box.size(), ivec3(octreeWidth)))) {
            // count how many ?
            size_t count = 0;
            ivec3 pmin = ivec3(std::numeric_limits<int>::max());
            ivec3 pmax = ivec3(-std::numeric_limits<int>::max());
            for (int z = box.pmin.z; z < box.pmax.z; z++) {
                for (int y = box.pmin.y; y < box.pmax.y; y++) {
                    for (int x = box.pmin.x; x < box.pmax.x; x++) {
                        if ((*this)(x, y, z) > 0) {
                            pmin = min(pmin, ivec3(x, y, z));
                            pmax = max(pmax, ivec3(x, y, z));
                            count++;
                        }
                    }
                }
            }
            if (count == 0) {
                return std::nullopt;
            }

            OctreeNode node;

            node.pmax = glm::min(box.pmax, pmax + ivec3(1));
            node.pmin = glm::max(box.pmin, pmin);
            auto box3 = Box3i{node.pmin, node.pmax};
            // printf("-> box %d %d %d to %d %d %d\n", box3.pmin.x, box3.pmin.y,
                //    box3.pmin.z, box3.pmax.x, box3.pmax.y, box3.pmax.z);
            // node.pmax = box.pmax;
            // node.pmin = box.pmax;
            node.isLeaf = true;
            int nodeIndex = (int)octree.size();
            octree.push_back(node);

            return nodeIndex;
        }
        OctreeNode node;
        ivec3 pmin = ivec3(std::numeric_limits<int>::max());
        ivec3 pmax = ivec3(-std::numeric_limits<int>::max());
        int childCount = 0;
        auto step = glm::max(ivec3(1), (box.size() / 2) + ivec3(1));

        for (int dx = 0; dx < 2; dx++) {
            for (int dy = 0; dy < 2; dy++) {
                for (int dz = 0; dz < 2; dz++) {
                    ivec3 _pmin = box.pmin + step * ivec3(dx, dy, dz);
                    ivec3 _pmax = glm::min(box.pmax, _pmin + step);
                    if (glm::all(glm::equal(_pmin, _pmax))) {
                        continue;
                    }
                    auto child = buildOctree(Box3i{_pmin, _pmax}, level + 1);

                    if (child.has_value()) {
                        if(*child < 0)
                            abort();
                        auto box2 = Box3i{_pmin, _pmax};
                        pmin = min(pmin, octree.at(child.value()).pmin);
                        pmax = max(pmax, octree.at(child.value()).pmax);
                        auto box3 = Box3i{pmin, pmax};
                        Box3i target{ivec3(0, 0, 135), ivec3(68, 16, 203)};
                        // if (glm::all(glm::equal(target.pmin, box.pmin)) &&
                        //     glm::all(glm::equal(target.pmax, box.pmax))) {
                        //     printf("box %d %d %d to %d %d %d\n", box.pmin.x,
                        //            box.pmin.y, box.pmin.z, box.pmax.x,
                        //            box.pmax.y, box.pmax.z);
                        //     printf("box2 %d %d %d to %d %d %d\n", box2.pmin.x,
                        //            box2.pmin.y, box2.pmin.z, box2.pmax.x,
                        //            box2.pmax.y, box2.pmax.z);
                        //     printf("box3 %d %d %d to %d %d %d\n", box3.pmin.x,
                        //            box3.pmin.y, box3.pmin.z, box3.pmax.x,
                        //            box3.pmax.y, box3.pmax.z);
                        // }

                        childCount++;
                        node.children[dz * 4 + dy * 2 + dx] = *child;
                    }
                }
            }
        }
        if (childCount > 0 && glm::any(glm::greaterThan(Box3i{pmin, pmax}.size(), box.size()))) {
            // printf("box %d %d %d to %d %d %d\n", box.pmin.x, box.pmin.y,
            //        box.pmin.z, box.pmax.x, box.pmax.y, box.pmax.z);
            // printf("box2 %d %d %d to %d %d %d\n", pmin.x, pmin.y, pmin.z,
            //        pmax.x, pmax.y, pmax.z);
            // for (int i = 0; i < 8; i++) {
            //     if (node.children[i] >= 0) {
            //         auto _pmin = octree[node.children[i]].pmin;
            //         auto _pmax = octree[node.children[i]].pmax;
            //         printf("box %d %d %d to %d %d %d\n", _pmin.x, _pmin.y,
            //                _pmin.z, _pmax.x, _pmax.y, _pmax.z);
            //     }
            // }
            abort();
        } else {
            node.pmin = pmin;
            node.pmax = pmax;
        }
        if (childCount == 0) {
            if (level == 0) {
                node.pmin = vec3(0);
                node.pmax = worldDimension;
                node.isLeaf = true;
                int nodeIndex = (int)octree.size();
                octree.push_back(node);
                return nodeIndex;
            }
            return std::nullopt;
        } else if (childCount == 1) {
            for (auto i : node.children) {
                if (i >= 0) {
                    // auto node = octree[i];
                    // auto box3 = Box3i{node.pmin, node.pmax};
                    // printf("-> box %d %d %d to %d %d %d\n", box3.pmin.x,
                    //        box3.pmin.y, box3.pmin.z, box3.pmax.x, box3.pmax.y,
                    //        box3.pmax.z);
                    return i;
                }
            }
        } else {
            Box3i box{pmin, pmax};
            if (glm::all(glm::lessThanEqual(box.size(), ivec3(64)))) {
                if (childCount >= 5) {
                    node.isLeaf = true;
                }
            }
            // auto box3 = Box3i{node.pmin, node.pmax};
            // printf("-> box %d %d %d to %d %d %d\n", box3.pmin.x, box3.pmin.y,
            //        box3.pmin.z, box3.pmax.x, box3.pmax.y, box3.pmax.z);
            int nodeIndex = (int)octree.size();
            octree.push_back(node);
            return nodeIndex;
        }
    }
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
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Materials), NULL,
                     GL_DYNAMIC_COPY);
        glGenBuffers(1, &octreeBuffer);
    }

    explicit World(const ivec3 &worldDimension)
        : worldDimension(worldDimension),materials (new Materials()) {
        materialNames.resize(MATERIAL_COUNT);
        alignedDimension = worldDimension;
        alignedDimension.x = (alignedDimension.x + 7U) & (-4U);
        data.resize(
            alignedDimension.x * alignedDimension.y * alignedDimension.z, 0);
        initData();
    }

    uint8_t &operator()(int x, int y, int z) {
        x = std::clamp<int>(x, 0, worldDimension.x - 1);
        y = std::clamp<int>(y, 0, worldDimension.y - 1);
        z = std::clamp<int>(z, 0, worldDimension.z - 1);

        return data.at(x + alignedDimension.x * (y + z * alignedDimension.y));
    }

    uint8_t &operator()(const ivec3 &x) { return (*this)(x.x, x.y, x.z); }

    void setUpTexture() {
        glGenTextures(1, &world);
        glBindTexture(GL_TEXTURE_3D, world);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, alignedDimension.x,
                     alignedDimension.y, alignedDimension.z, 0, GL_RED,
                     GL_UNSIGNED_BYTE, data.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, octreeBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     sizeof(OctreeNode) * octree.size(), octree.data(),
                     GL_DYNAMIC_COPY);
    }
};

std::optional<std::pair<ivec3, ivec3>>
getWorldBound(const std::vector<std::string> &filenames) {
    // open the region file
    ivec3 worldMin(std::numeric_limits<int>::max()),
        worldMax(std::numeric_limits<int>::min());
    for (const auto &filename : filenames) {
        FILE *fp = fopen(filename.c_str(), "rb");
        if (!fp) {
            printf("failed to open file\n");
            return {};
        }

        // output file
        FILE *fpOutput = stdout; // fopen("output.txt", "w");
        if (!fpOutput) {
            printf("failed to open output file\n");
            return {};
            ;
        }

        enkiRegionFile regionFile = enkiRegionFileLoad(fp);

        for (int i = 0; i < ENKI_MI_REGION_CHUNKS_NUMBER; i++) {
            enkiNBTDataStream stream;
            enkiInitNBTDataStreamForChunk(regionFile, i, &stream);
            if (stream.dataLength) {
                enkiChunkBlockData aChunk = enkiNBTReadChunk(&stream);
                enkiMICoordinate chunkOriginPos =
                    enkiGetChunkOrigin(&aChunk); // y always 0
                ivec3 chunkPos =
                    ivec3(chunkOriginPos.x, chunkOriginPos.y, chunkOriginPos.z);

                //            fprintf(fpOutput, "Chunk at xyz{ %d, %d, %d }
                //            Number of sections: %d \n", chunkOriginPos.x,
                //                    chunkOriginPos.y, chunkOriginPos.z,
                //                    aChunk.countOfSections);

                // iterate through chunk and count non 0 voxels as a demo
                int64_t numVoxels = 0;
                for (int section = 0; section < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                     ++section) {
                    if (aChunk.sections[section]) {
                        enkiMICoordinate sectionOrigin =
                            enkiGetChunkSectionOrigin(&aChunk, section);

                        enkiMICoordinate sPos;
                        // note order x then z then y iteration for cache
                        // efficiency
                        for (sPos.y = 0;
                             sPos.y < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                             ++sPos.y) {
                            for (sPos.z = 0;
                                 sPos.z < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                                 ++sPos.z) {
                                for (sPos.x = 0;
                                     sPos.x < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                                     ++sPos.x) {
                                    uint8_t voxel = enkiGetChunkSectionVoxel(
                                        &aChunk, section, sPos);

                                    if (voxel) {
                                        auto p = ivec3(sPos.x, sPos.y, sPos.z) +
                                                 ivec3(sectionOrigin.x,
                                                       sectionOrigin.y,
                                                       sectionOrigin.z);
                                        worldMin = min(worldMin, p);
                                        worldMax = max(worldMax, p);
                                        ++numVoxels;
                                    }
                                }
                            }
                        }
                    }
                }
                // fprintf(fpOutput, "   Chunk has %g non zero voxels\n",
                // (float) numVoxels);

                enkiNBTRewind(&stream);
                // PrintStreamStructureToFile(&stream, fpOutput);
            }
            enkiNBTFreeAllocations(&stream);
        }

        enkiRegionFileFreeAllocations(&regionFile);

        printf("%d %d %d  to %d %d %d\n", worldMin.x, worldMin.y, worldMin.z,
               worldMax.x, worldMax.y, worldMax.z);
        fclose(fp);
    }
    return std::make_pair(worldMin, worldMax);
}

std::shared_ptr<World> McLoader(const std::vector<std::string> &filenames) {
    auto bound = getWorldBound(filenames);
    if (!bound) {
        return nullptr;
    }
    auto worldMin = bound.value().first;
    auto worldMax = bound.value().second;
    auto world = std::make_shared<World>(worldMax - worldMin);
    printf("world size %d %d %d\n", world->worldDimension.x,
           world->worldDimension.y, world->worldDimension.z);
    // open the region file
    for (const auto &filename : filenames) {
        FILE *fp = fopen(filename.c_str(), "rb");
        if (!fp) {
            printf("failed to open file\n");
            return nullptr;
        }

        // output file
        FILE *fpOutput = stdout; // fopen("output.txt", "w");
        if (!fpOutput) {
            printf("failed to open output file\n");
            return nullptr;
        }

        enkiRegionFile regionFile = enkiRegionFileLoad(fp);

        for (int i = 0; i < ENKI_MI_REGION_CHUNKS_NUMBER; i++) {
            enkiNBTDataStream stream;
            enkiInitNBTDataStreamForChunk(regionFile, i, &stream);
            if (stream.dataLength) {
                enkiChunkBlockData aChunk = enkiNBTReadChunk(&stream);
                enkiMICoordinate chunkOriginPos =
                    enkiGetChunkOrigin(&aChunk); // y always 0
                ivec3 chunkPos =
                    ivec3(chunkOriginPos.x, chunkOriginPos.y, chunkOriginPos.z);

                //            fprintf(fpOutput, "Chunk at xyz{ %d, %d, %d }
                //            Number of sections: %d \n", chunkOriginPos.x,
                //                    chunkOriginPos.y, chunkOriginPos.z,
                //                    aChunk.countOfSections);

                // iterate through chunk and count non 0 voxels as a demo
                int64_t numVoxels = 0;
                for (int section = 0; section < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                     ++section) {
                    if (aChunk.sections[section]) {
                        enkiMICoordinate sectionOrigin =
                            enkiGetChunkSectionOrigin(&aChunk, section);

                        enkiMICoordinate sPos;
                        // note order x then z then y iteration for cache
                        // efficiency
                        for (sPos.y = 0;
                             sPos.y < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                             ++sPos.y) {
                            for (sPos.z = 0;
                                 sPos.z < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                                 ++sPos.z) {
                                for (sPos.x = 0;
                                     sPos.x < ENKI_MI_NUM_SECTIONS_PER_CHUNK;
                                     ++sPos.x) {
                                    uint8_t voxel = enkiGetChunkSectionVoxel(
                                        &aChunk, section, sPos);
                                    auto p =
                                        ivec3(sPos.x, sPos.y, sPos.z) +
                                        ivec3(sectionOrigin.x, sectionOrigin.y,
                                              sectionOrigin.z) -
                                        worldMin;
                                    (*world)(p) = voxel;
                                }
                            }
                        }
                    }
                }
                // fprintf(fpOutput, "   Chunk has %g non zero voxels\n",
                // (float) numVoxels);

                enkiNBTRewind(&stream);
                // PrintStreamStructureToFile(&stream, fpOutput);
            }
            enkiNBTFreeAllocations(&stream);
        }

        enkiRegionFileFreeAllocations(&regionFile);

        fclose(fp);
    }
    return world;
}

struct Renderer {
    GLint program;
    GLuint VBO;
    GLuint seed;
    std::shared_ptr<World> world;
    mat4 cameraDirection, cameraOrigin;
    GLuint sample;   // texture for 1 spp
    GLuint accum;    // accumlated sample
    GLuint composed; // post processor
    ivec2 mousePos, prevMousePos, lastFrameMousePos;
    float maxRayIntensity = 10.0f;
    int maxDepth = 2;
    bool prevMouseDown = false;
    int iTime = 0;
    vec2 eulerAngle = vec2(0, 0);
    bool needRedraw = true;
    uint32_t options = ENABLE_ATMOSPHERE_SCATTERING;
    float orbitDistance = 2.5f;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastRenderTime;
    enum CameraMode { Free, Orbit };

    CameraMode cameraMode = Free;

    explicit Renderer() {}

    void compileShader() {
        std::vector<char> error(4096, 0);
        auto shader = glCreateShader(GL_COMPUTE_SHADER);
        GLint success;
        const char *version = "#version 430\n";
        const char *src[] = {version, commondDefsSource, externalShaderSource,
                             bsdfSource, computeShaderSource};
        glShaderSource(shader, sizeof(src) / sizeof(src[0]), src, nullptr);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::COMPUTE::COMPILATION_FAILED\n"
                      << error.data() << std::endl;
            exit(1);
        };

        program = glCreateProgram();
        glAttachShader(program, shader);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, error.size(), nullptr, error.data());
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
                      << error.data() << std::endl;
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1280, 720, 0, GL_RGBA, GL_FLOAT,
                     NULL);

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
        world->buildOctree();
        world->setUpTexture();

        cameraOrigin = translate(vec3(20, 20, -20));
        cameraDirection = identity<mat4>(); //<=inverse(M);
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
            bool inside =
                (pos.x >= windowPos.x && pos.y >= windowPos.y &&
                 pos.x < windowPos.x + size.x && pos.y < windowPos.y + size.y);
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
                    auto rot = (vec2(p) - vec2(lastFrameMousePos)) / 300.0f *
                               float(M_PI);
                    eulerAngle += rot;
                }

            } else {
                if (prevMouseDown) {
                    prevMousePos = lastFrameMousePos;
                }
            }
            auto M = rotate(eulerAngle.x, vec3(0, 1, 0));
            M *= rotate(eulerAngle.y, vec3(1, 0, 0));

            std::chrono::duration<double> elapsed =
                std::chrono::high_resolution_clock::now() - lastRenderTime;

            if (cameraMode == Orbit) {
                auto tr = vec3(world->worldDimension) * 0.5f;
                tr.z *= -1.0f;
                cameraDirection = M;
                cameraOrigin = translate(vec3(tr.x, tr.y, -tr.z)) *
                               cameraDirection *
                               translate(vec3(0, 0, orbitDistance * tr.z));
            } else if (elapsed.count() > 1.0 / 30.0) {
                lastRenderTime = std::chrono::high_resolution_clock::now();
                // free
                float vel = 6.0f;
                float step = vel / 30.0;
                vec3 o = cameraOrigin * vec4(0, 0, 0, 1);
                auto R = rotate(eulerAngle.x, vec3(0, 1, 0));
                if (io.KeyShift) {
                    step *= 10.0f;
                }
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
                if (io.KeysDown[' ']) {
                    o += vec3(step * R * vec4(0, 1, 0, 1));
                    iTime = 0;
                }
                cameraOrigin = translate(o);
                cameraDirection = M;
            }
            prevMouseDown = pressed;
            lastFrameMousePos = ivec2(xpos, ypos);
        }
        float theta = (world->sunHeight - M_PI_2);
        float phi = world->sunPhi;
        vec3 sunPos = normalize(
            vec3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta)));
        int w = 1280, h = 720;
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_3D, world->world);
        glBindTexture(GL_TEXTURE_2D, accum);
        glBindImageTexture(1, accum, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, seed);
        glBindImageTexture(2, seed, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindTexture(GL_TEXTURE_2D, composed);
        glBindImageTexture(3, composed, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_RGBA32F);
        glUniform1i(glGetUniformLocation(program, "octreeRoot"),
                    world->octreeRoot);
        glUniform1i(glGetUniformLocation(program, "world"), 0);
        glUniform1f(glGetUniformLocation(program, "maxRayIntensity"),
                    maxRayIntensity);
        glUniform2f(glGetUniformLocation(program, "iResolution"), w, h);
        glUniform3i(glGetUniformLocation(program, "worldDimension"),
                    world->worldDimension.x, world->worldDimension.y,
                    world->worldDimension.z);
        glUniform1i(glGetUniformLocation(program, "iTime"), iTime++);
        glUniform1ui(glGetUniformLocation(program, "options"), options);
        glUniform1i(glGetUniformLocation(program, "maxDepth"), maxDepth);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraOrigin"), 1,
                           GL_FALSE, &cameraOrigin[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program, "cameraDirection"), 1,
                           GL_FALSE, &cameraDirection[0][0]);
        glUniform3fv(glGetUniformLocation(program, "sunPos"), 1,
                     (float *)&sunPos);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, world->materialsSSBO);
        if (needRedraw) {
            // printf("redraw\n");

            GLvoid *p = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
            memcpy(p, world->materials.get(), sizeof(World::Materials));
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, world->materialsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, world->octreeBuffer);
        glDispatchCompute(std::ceil(w / 16), std::ceil(h / 16), 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glFinish();
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
        (void)io;
        io.ConfigFlags |=
            ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable
        // Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
        io.ConfigFlags |=
            ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport /
                                              // Platform Windows
        // io.ConfigViewportsNoAutoMerge = true;
        // io.ConfigViewportsNoTaskBarIcon = true;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        // ImGui::StyleColorsClassic();

        // When viewports are enabled we tweak WindowRounding/WindowBg so
        // platform windows can look identical to regular ones.
        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer bindings
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 430");

        renderer = std::make_unique<Renderer>();
        std::vector<std::string> filenames;
        for (auto &p : fs::directory_iterator("../data")) {
            filenames.emplace_back(p.path().string());
        }
        renderer->compileShader();
        renderer->world = McLoader(filenames);
        renderer->world->loadMinecraftMaterials();
        renderer->setUpWorld();
    }

    int selectedMaterialIndex = 0;

    void showEditor() {
        if (ImGui::Begin("Explorer")) {
            for (int i = 1; i < MATERIAL_COUNT; i++) {
                if (ImGui::Selectable(renderer->world->materialNames[i].c_str(),
                                      selectedMaterialIndex == i)) {
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
                        auto &emission =
                            renderer->world->materials
                                ->MaterialEmission[selectedMaterialIndex];
                        auto &emissionStrength =
                            renderer->world->materials->MaterialEmissionStrength
                                [selectedMaterialIndex];
                        auto &baseColor =
                            renderer->world->materials
                                ->MaterialBaseColor[selectedMaterialIndex];
                        auto &roughness =
                            renderer->world->materials
                                ->MaterialRoughness[selectedMaterialIndex];
                        auto &metallic =
                            renderer->world->materials
                                ->MaterialMetallic[selectedMaterialIndex];
                        if (ImGui::ColorPicker3("Emission",
                                                (float *)&emission)) {
                            needRedraw = true;
                        }
                        if (ImGui::InputFloat("Emission Strength",
                                              &emissionStrength)) {
                            needRedraw = true;
                        }
                        if (ImGui::ColorPicker3("Base Color",
                                                (float *)&baseColor)) {
                            needRedraw = true;
                        }
                        if (ImGui::SliderFloat("Metallic", (float *)&metallic,
                                               0.0f, 1.0f)) {
                            needRedraw = true;
                        }
                        if (ImGui::SliderFloat("Roughness", (float *)&roughness,
                                               0.0f, 1.0f)) {
                            needRedraw = true;
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Render")) {
                    if (ImGui::InputInt("Max Depth", &renderer->maxDepth)) {
                        needRedraw = true;
                    }
                    if (ImGui::InputFloat("Ray Clamp",
                                          &renderer->maxRayIntensity)) {
                        needRedraw = true;
                    }
                    bool enable =
                        renderer->options & ENABLE_ATMOSPHERE_SCATTERING;
                    if (ImGui::Checkbox("Atmospheric Scattering", &enable)) {
                        if (enable) {
                            renderer->options |= ENABLE_ATMOSPHERE_SCATTERING;
                        } else {
                            renderer->options &= ~ENABLE_ATMOSPHERE_SCATTERING;
                        }
                        needRedraw = true;
                    }
                    float theta = renderer->world->sunHeight / M_PI * 180.0;
                    if (ImGui::SliderFloat("Sun Height", &theta, 0.0f,
                                           180.0f)) {
                        renderer->world->sunHeight = theta / 180.0f * M_PI;
                        needRedraw = true;
                    }
                    float phi = renderer->world->sunPhi / M_PI * 180.0;
                    if (ImGui::SliderFloat("Sun Direction", &phi, 0.0f,
                                           360.0f)) {
                        renderer->world->sunPhi = phi / 180.0f * M_PI;
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

        if (ImGui::Begin("View", nullptr,
                         ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoScrollbar)) {
            renderer->render(window);
            ImGui::Image(reinterpret_cast<void *>(renderer->composed),
                         ImVec2(1280, 720));
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

    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent
    // window not dockable into, because it would be confusing to have two
    // docking targets within each others.
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen) {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus |
                        ImGuiWindowFlags_NoNavFocus;
    }

    // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will
    // render our background and handle the pass-thru hole, so we ask Begin() to
    // not render a background.
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

    // Important: note that we proceed even if Begin() returns false (aka window
    // is collapsed). This is because we want to keep our DockSpace() active. If
    // a DockSpace() is inactive, all active windows docked into it will lose
    // their parent and become undocked. We cannot preserve the docking
    // relationship between an active window and an inactive docking, otherwise
    // any change of dockspace/settings would lead to windows being stuck in
    // limbo and never being visible.
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

// clang-format off
extern BlockDefinition gBlockDefinitions[];

// clang-format on
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
        materials->MaterialBaseColor[i] =
            hexToRGB(gBlockDefinitions[i].read_color);
        materials->MaterialMetallic[i] = 0.0f;
        materials->MaterialRoughness[i] = 0.01f;
    }
    materials->MaterialRoughness[8] = 0.1;
    materials->MaterialMetallic[8] = 0.75;
    materials->MaterialRoughness[9] = 0.1;
    materials->MaterialMetallic[9] = 0.75;

    materials->MaterialRoughness[20] = 0.05;
    materials->MaterialMetallic[20] = 0.95;

    materials->MaterialRoughness[102] = 0.05;
    materials->MaterialMetallic[102] = 0.95;

    materials->MaterialRoughness[95] = 0.05;
    materials->MaterialMetallic[95] = 0.95;

    materials->MaterialRoughness[160] = 0.05;
    materials->MaterialMetallic[160] = 0.95;
}