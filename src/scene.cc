#include "scene.h"


namespace NanoVoxel {
	void Scene::createOpenCLContext() {
		device = CoreCL::CreateCPUDevice();
		context = std::make_unique<CoreCL::Context>(device.get());
	}
	Scene::Scene(size_t w, size_t h, size_t d)
		:_width(w), _height(h), _depth(d) {
		createOpenCLContext();
	}
}