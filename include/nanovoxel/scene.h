#pragma once
#include <corecl.h>
#include <kernel/kernel_def.h>
#include <util.h>
#include <film.h>

namespace NanoVoxel {
	using RenderCallback = std::function<void(std::shared_ptr<Film>)>;
	class Scene {
		std::unique_ptr<CoreCL::Device> device;
		std::unique_ptr<CoreCL::Context> context;
		std::shared_ptr<Film> film;
		size_t _width, _height, _depth;
		std::vector<Material> materials;
		std::vector<Voxel> world;
		void createOpenCLContext();
		std::unique_ptr < CoreCL::Kernel> kernel;
		struct Buffers {
			std::unique_ptr<CoreCL::Buffer<Voxel>> world;
			std::unique_ptr<CoreCL::Buffer<Material>> materials;
			std::unique_ptr<CoreCL::Buffer<PerRayData>> prd;
			std::unique_ptr<CoreCL::Buffer<Globals>> globals;
		}buffers;
		void createWorld();
		std::atomic<bool> renderContinuable;
		size_t kernelWorkSize = 256 * 256;
	public:
		Scene(size_t, size_t, size_t);
		size_t width()const { return _width; }
		size_t height()const { return _height; }
		size_t depth()const { return _depth; }
		void resetDimension(size_t, size_t, size_t);
		const std::vector<Material>& getMaterials()const { return materials; }
		std::vector<Material>& getMaterials() { return materials; }
		void commit();
		Voxel& getVoxel(int i, int j, int k) {
			return world[i + width() * j + width() * height() * k];
		}
		void setFilmSize(size_t w, size_t h) {
			film.reset(new Film(w, h));
		}
		void resetFilmIfDimensionChanged(size_t w, size_t h) {
			if (w != film->width() || h != film->height()) {
				setFilmSize(w, h);
			}
		}
		void abortRender();
		void doOneRenderPass(const RenderCallback& callback);
	};
}