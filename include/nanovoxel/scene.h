#pragma once
#include <corecl.h>
#include <kernel/kernel_def.h>
#include <util.h>
#include <film.h>

namespace NanoVoxel {
	using RenderCallback = std::function<void(std::shared_ptr<Film>)>;
	struct Camera {
		Miyuki::Vec3f position, direction;
		Mat4x4 transform;
	};
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
		std::vector<PerRayData> prds;
		std::atomic<size_t> sampleCount = 0;
		size_t spp = 16;
		std::atomic<bool> _isRendering;
	public:
		void setTotalSamples(size_t spp) {
			this->spp = spp;
		}
		size_t getCurrentSamples()const {
			return sampleCount;
		}
		bool isRendering()const {
			return _isRendering;
		}
		Camera camera;
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
			buffers.prd.reset(new CoreCL::Buffer<PerRayData>(context.get(),
				CL_MEM_READ_WRITE, w * h, nullptr));
		}
		void resetFilmIfDimensionChanged(size_t w, size_t h) {
			if (w != film->width() || h != film->height()) {
				setFilmSize(w, h);
			}
		}
		void resumeRender() {
			renderContinuable = true;
		}
		void abortRender();
		void doOneRenderPass(const RenderCallback& callback);
		void render(const RenderCallback& callback, const RenderCallback& finalCallback);
	};
}