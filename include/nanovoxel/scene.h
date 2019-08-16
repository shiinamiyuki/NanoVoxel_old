#pragma once
#include <corecl.h>
#include <kernel/kernel_def.h>
#include <util.h>


namespace NanoVoxel {
	class Scene {
		std::unique_ptr<CoreCL::Device> device;
		std::unique_ptr<CoreCL::Context> context;
		size_t _width, _height, _depth;
		std::vector<Material> materials;
		void createOpenCLContext();
	public:
		Scene(size_t, size_t, size_t);
		size_t width()const { return _width; }
		size_t height()const { return _height; }
		size_t depth()const { return _depth; }
		void resetDimension(size_t, size_t, size_t);
	};
}