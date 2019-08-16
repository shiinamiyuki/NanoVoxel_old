#pragma once

#include <util.h>
#include <miyuki/math/spectrum.h>

namespace NanoVoxel {
	struct Pixel {
		Miyuki::Spectrum color;
		Miyuki::Float weight = 0;
		Miyuki::Spectrum eval()const {
			return weight == 0 ? color : color / weight;
		}
	};
	class Film {
		std::vector<Pixel> pixelData;
		size_t _height, _width;
	public:
		Film(size_t height, size_t width) :_height(height), _width(width) {
			pixelData.resize(height * width);
		}
		size_t height()const { return _height; }
		size_t width()const { return _width; }
		const std::vector<Pixel>& getData()const { return pixelData; }
		void addSample(const Miyuki::Point2i& pos, const Miyuki::Spectrum& value, float weight = 1) {
			size_t index = pos.x + pos.y * width();
			pixelData.at(index).color += value;
			pixelData.at(index).weight += weight;
		}
		const Pixel& getPixel(int i, int j)const {
			size_t index = i + j * width();
			return pixelData.at(index);
		}
		void writeImage(const std::string& filename);
	};
}