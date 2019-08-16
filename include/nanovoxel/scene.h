#pragma once
#include <corecl.h>
#include <kernel/kernel_def.h>
#include <util.h>
#include <film.h>

namespace NanoVoxel {
	class Scene {
		std::unique_ptr<CoreCL::Device> device;
		std::unique_ptr<CoreCL::Context> context;
		std::unique_ptr<Film> film;
		size_t _width, _height, _depth;
		std::vector<Material> materials;
		std::vector<Voxel> world;
		void createOpenCLContext();
		std::unique_ptr < CoreCL::Kernel> kernel;
		struct Buffers {
			std::unique_ptr<CoreCL::Buffer<Voxel>> world;
			std::unique_ptr<CoreCL::Buffer<Material>> materials;
		}buffers;
		void createWorld();
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
	};
}