#include <film.h>
#include <lodepng/lodepng.h>
namespace NanoVoxel {

	void Film::writeImage(const std::string& filename) {
		std::vector<unsigned char> pixelBuffer;
		for (const auto& i : pixelData) {
			auto out = i.eval().toInt();
			pixelBuffer.emplace_back(out.r);
			pixelBuffer.emplace_back(out.g);
			pixelBuffer.emplace_back(out.b);
			pixelBuffer.emplace_back(255);
		}
		auto error = lodepng::encode(filename, pixelBuffer, (uint32_t)width(), (uint32_t)height());
		if (error) {
			fmt::print("error saving {}: {}\n", filename, lodepng_error_text(error));
		}
		else {
			fmt::print("saved to {}\n", filename);
		}
	}
}