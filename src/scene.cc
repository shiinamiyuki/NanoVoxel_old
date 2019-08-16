#include "scene.h"
#include <list>

namespace NanoVoxel {
	static const int TileSize = 256;
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
	}
	void Scene::commit() {
		buffers.materials = std::make_unique<CoreCL::Buffer<Material>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, materials.size(), materials.data());
		buffers.world = std::make_unique<CoreCL::Buffer<Voxel>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, world.size(), &world[0]);
		buffers.globals = std::make_unique<CoreCL::Buffer<Globals>>(context.get(),
			CL_MEM_READ_WRITE, 1, nullptr);
	}
	void Scene::abortRender() {
		renderContinuable = false;
	}

	PerRayData makeInvalidPRD() {
		PerRayData prd;
		prd.valid = false;
		return prd;
	}
	static std::random_device rd;
	static std::uniform_int_distribution<unsigned int> dist;
	void Scene::doOneRenderPass(const RenderCallback& callback) {
		fmt::print("Start\n");
		renderContinuable = true;
		Globals globals;
		globals.dimension.x = width();
		globals.dimension.y = height();
		globals.dimension.z = depth();
		globals.tileSize = TileSize;
		globals.filmDimension.x = film->width();
		globals.filmDimension.y = film->height();

		std::list<PerRayData> queue;
		buffers.prd.reset(new CoreCL::Buffer<PerRayData>(context.get(),
			CL_MEM_READ_WRITE, kernelWorkSize, nullptr));
		// Generate prds
		for (int j = 0; j < film->width() && renderContinuable; j += TileSize) {
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
						queue.push_back(prd);
					}
				}
			}
		}

		while (!queue.empty()) {
			std::vector<PerRayData> prds;
			prds.reserve(kernelWorkSize);
			for (int i = 0; i < kernelWorkSize && !queue.empty(); i++) {
				prds.push_back(queue.front());
				queue.pop_front();
			}
			while (prds.size() != kernelWorkSize) {
				prds.emplace_back(makeInvalidPRD());
			}
			//set up buffers
			buffers.prd->write(prds.size(), prds.data());
			buffers.globals->write(1, &globals);
			//buffers.

			// invoke kernel
			kernel->setWorkDimesion(kernelWorkSize, 256);
			kernel->setArg(*buffers.globals, 0);
			kernel->setArg(*buffers.materials, 1);
			kernel->setArg(*buffers.world, 2);
			kernel->setArg(*buffers.prd, 3);
			(*kernel)();

			buffers.prd->read(prds.size(), prds.data());

			// push to queue if more work needed
			for (const auto& prd : prds) {
				if (prd.valid && !prd.done) {
					queue.push_back(prd);
				}
				if (prd.done) {
					Miyuki::Point2i raster(prd.pixel.x, prd.pixel.y);
					Miyuki::Spectrum color(prd.radiance.x, prd.radiance.y, prd.radiance.z);
					film->addSample(raster, color);
				}
			}
		}

		film->writeImage("out.png");
	}
}