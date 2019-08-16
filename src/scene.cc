#include "scene.h"


namespace NanoVoxel {
	void Scene::createWorld() {
		world.resize(width() * height() * depth());
	}

	void  Scene::resetDimension(size_t w, size_t h, size_t d) {
		_width = w;
		_height = h;
		_depth = d;
		createWorld();
	}
	void Scene::createOpenCLContext() {
		device = CoreCL::CreateCPUDevice();
		context = std::make_unique<CoreCL::Context>(device.get());
		kernel = std::make_unique<CoreCL::Kernel>(context.get());
		kernel->createKernel("kernel/kernel.cc", "NanoVoxelMain", "-DOPENCL_KERNEL");
	}
	Scene::Scene(size_t w, size_t h, size_t d)
		:_width(w), _height(h), _depth(d) {
		createOpenCLContext();
		createWorld();
	}
	void Scene::commit() {
		buffers.materials = std::make_unique<CoreCL::Buffer<Material>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, materials.size(), &materials[0]);
		buffers.world = std::make_unique<CoreCL::Buffer<Voxel>>(context.get(),
			CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, world.size(), &world[0]);
	}
}