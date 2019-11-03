#include "scene.h"
#include <deque>
#include <miyuki/vec4.hpp>
#include <miyuki/math/transform.h>
#include <miyuki/utils/thread.h>
#include <PerlinNoise.hpp>
namespace NanoVoxel {
	static const int TileSize = 256;
	Float4 fromVec4f(const Miyuki::Vec4f& v) {
		return makeFloat4(v.x, v.y, v.z, v.w);
	}
	void Scene::createWorld() {
		world.resize(width() * height() * depth());
		Material mat0;
		mat0.emission.value = __makeFloat3(8, 8, 8);
		mat0.diffuse.color.value = __makeFloat3(0, 0, 0);
		materials.push_back(mat0);
		Material mat1;
		mat1.emission.value = __makeFloat3(0, 0, 0);
		mat1.diffuse.color.value = __makeFloat3(0.04, 0.5, 0.7);
		materials.push_back(mat1);
		siv::PerlinNoise perlin;
		for (int i = 0; i < width(); i++) {
			for (int j = 0; j < height(); j++) {
				for (int k = 0; k < depth(); k++) {
					double x = double(i) / width() * 5;
					double y = double(j) / width() * 5;
					double z = double(k) / width() * 5;
					auto t = perlin.noise0_1(x, y, z);
					if (t > 0.5 && t < 0.55) {
						if (int(t * 1000) % 16 == 0)
							getVoxel(i, j, k).setMat(0);
						else
							getVoxel(i, j, k).setMat(1);
					}
				}
			}
		}

	}

	void  Scene::resetDimension(size_t w, size_t h, size_t d) {
		_width = w;
		_height = h;
		_depth = d;
		createWorld();
	}
	void Scene::createOpenCLContext() {
		device = CoreCL::CreateGPUDevice();
		context = std::make_unique<CoreCL::Context>(device.get());
		kernel = std::make_unique<CoreCL::Kernel>(context.get());
		kernel->createKernel("kernel/kernel.cc", "NanoVoxelMain", "-DOPENCL_KERNEL");
	}
	Scene::Scene(size_t w, size_t h, size_t d)
		:_width(w), _height(h), _depth(d), renderContinuable(true) {
		createOpenCLContext();
		createWorld();
	}static std::random_device rd;
	static std::uniform_int_distribution<unsigned int> dist;
	void Scene::commit() {
		buffers.materials = std::make_unique<CoreCL::Buffer<Material>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, materials.size(), materials.data());
		buffers.world = std::make_unique<CoreCL::Buffer<Voxel>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, world.size(), &world[0]);
		buffers.globals = std::make_unique<CoreCL::Buffer<Globals>>(context.get(),
			CL_MEM_READ_WRITE, 1, nullptr);
		prds.clear();
		for (int j = 0; j < film->height() && renderContinuable; j += TileSize) {
			for (int i = 0; i < film->width() && renderContinuable; i += TileSize) {
				for (int x = 0; x < TileSize; x++) {
					for (int y = 0; y < TileSize; y++) {
						int pixelX = i + x;
						int pixelY = j + y;
						if (pixelX >= film->width() || pixelY >= film->height())continue;
						PerRayData prd;
						prd.pixel.x = pixelX;
						prd.pixel.y = pixelY;
						prd.done = false;
						prd.valid = true;
						prd.radiance.x = 0;
						prd.radiance.y = 0;
						prd.radiance.z = 0;
						prd.rng = dist(rd);
						prds.push_back(prd);
					}
				}
			}
		}
		//set up buffers
		buffers.prd->write(prds.size(), prds.data());

		updateCamera();

	}
	void Scene::updateCamera() {
		auto rotationMatrix = Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 0, 1), camera.direction.z);
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 1, 0), camera.direction.x));
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(1, 0, 0), -camera.direction.y));

		for (int i = 0; i < 4; i++) {
			camera.transform.m[i] = fromVec4f(rotationMatrix.m[i]);
		}
	}
	Vec3f Camera::cameraToWorld(const Vec3f& v) {
		auto rotationMatrix = Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 0, 1), direction.z);
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 1, 0), direction.x));
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(1, 0, 0), -direction.y));
		return rotationMatrix.mult(Vec4f(v, 1));
	}
	void Scene::abortRender() {
		renderContinuable = false;
	}

	PerRayData makeInvalidPRD() {
		PerRayData prd;
		prd.valid = false;
		return prd;
	}

	void Scene::doOneRenderPass(const RenderCallback& callback) {
		_isRendering = true;

		Globals globals;
		globals.dimension.x = width();
		globals.dimension.y = height();
		globals.dimension.z = depth();
		globals.tileSize = TileSize;
		globals.filmDimension.x = film->width();
		globals.filmDimension.y = film->height();
		globals.cameraT = camera.transform;
		globals.cameraPos = makeFloat3(camera.position.x, camera.position.y, camera.position.z);
		buffers.globals->write(1, &globals);

		// invoke kernel
		kernel->setWorkDimesion(prds.size(), 256);
		kernel->setArg(*buffers.globals, 0);
		kernel->setArg(*buffers.materials, 1);
		kernel->setArg(*buffers.world, 2);
		kernel->setArg(*buffers.prd, 3);
		(*kernel)();

		buffers.prd->read(prds.size(), prds.data());

		/*for (const auto& prd : prds) {
			Miyuki::Point2i raster(prd.pixel.x, prd.pixel.y);
			Miyuki::Spectrum color(prd.radiance.x, prd.radiance.y, prd.radiance.z);
			film->addSample(raster, color);
		}*/
		Miyuki::Thread::ParallelFor(0, prds.size(), [=](uint32_t i, uint32_t) {
			const auto& prd = prds[i];
			Miyuki::Point2i raster(prd.pixel.x, prd.pixel.y);
			Miyuki::Spectrum color(prd.radiance.x, prd.radiance.y, prd.radiance.z);
			film->addSample(raster, color);
		}, 4096);
		callback(film);

		fmt::print("Done pass: {}/{}\n", sampleCount + 1, spp);

	}
	void Scene::render(const RenderCallback& callback, const RenderCallback& finalCallback) {
		film->clear();
		for (sampleCount = 0; sampleCount < spp && renderContinuable; sampleCount++) {
			doOneRenderPass(callback);
		}
		if (sampleCount == spp)
			finalCallback(film);
		while (renderContinuable) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		_isRendering = false;
	}
}