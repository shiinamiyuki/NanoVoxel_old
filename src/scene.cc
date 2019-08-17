#include "scene.h"
#include <deque>
#include <miyuki/vec4.hpp>
#include <miyuki/math/transform.h>

namespace NanoVoxel {
	static const int TileSize = 256;
	Float4 fromVec4f(const Miyuki::Vec4f& v) {
		return makeFloat4(v.x, v.y, v.z, v.w);
	}
	void Scene::createWorld() {
		world.resize(width() * height() * depth());
		materials.push_back(Material());
		for (int i = 0; i < 50; i++) {
			for (int j = 0; j < 50; j++) {
				for (int k = 0; k < 50; k++) {
					auto x = i - 25;
					auto y = j - 25;
					auto z = k - 25;
					if (std::sqrt(x * x + y * y + z * z) <= 20)
						getVoxel(i, j, k).setMat(0);
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

		auto rotationMatrix = Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 0, 1), camera.direction.z);
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(0, 1, 0), camera.direction.x));
		rotationMatrix = rotationMatrix.mult(Miyuki::Matrix4x4::rotation(Miyuki::Vec3f(1, 0, 0), -camera.direction.y));

		for (int i = 0; i < 4; i++) {
			camera.transform.m[i] = fromVec4f(rotationMatrix.m[i]);
		}

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
		fmt::print("Start pass: {}/{}\n", sampleCount + 1, spp);		
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

		for (const auto& prd : prds) {
			Miyuki::Point2i raster(prd.pixel.x, prd.pixel.y);
			Miyuki::Spectrum color(prd.radiance.x, prd.radiance.y, prd.radiance.z);
			film->addSample(raster, color);
		}

		callback(film);

	}
	void Scene::render(const RenderCallback& callback, const RenderCallback& finalCallback) {
		film->clear();
		for (sampleCount = 0; sampleCount < spp && renderContinuable; sampleCount++) {
			doOneRenderPass(callback);
		}
		_isRendering = false;
		finalCallback(film);
	}
}