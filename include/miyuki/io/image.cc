#include <miyuki/io/image.h>

#define STB_IMAGE_IMPLEMENTATION

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <miyuki/utils/thread.h>
#include <iostream>
#include <lodepng/lodepng.h>

namespace Miyuki {
    namespace IO {

        Image::Image(const std::string &filename, ImageFormat format)
                : GenericImage<Spectrum>(), format(format), filename(filename) {
            if (stbi_is_hdr(filename.c_str())) {
                LoadHDR(filename, *this);
                return;
            }
            int ch;
            auto data = stbi_load(filename.c_str(), &width, &height, &ch, 3);
            if (!data) {
                throw std::runtime_error(fmt::format("Cannot load {}\n", filename).c_str());
            }
			pixelData.resize(width * height);
            std::function<Float(uint8_t)> f;
            if (format == ImageFormat::none) {
                f = [](uint8_t _x) -> Float {
                    double x = _x / 255.0f;
                    return std::pow(x, 2.2);
                };
            } else if (format == ImageFormat::raw) {
                f = [](uint8_t _x) -> Float {
                    return _x / 255.0;
                };
            }
           
            Thread::ParallelFor(0u, width * height, [&](uint32_t i, uint32_t threadId) {
                pixelData[i] = Spectrum(f(data[3 * i]),
                                        f(data[3 * i + 1]),
                                        f(data[3 * i + 2]));
            }, 4096);
            free(data);
        }

        void Image::save(const std::string &filename) {
            saveAsFormat(filename, format);
        }

        void Image::saveAsFormat(const std::string &filename, ImageFormat format) {
            switch (format) {
                case ImageFormat::none: {
                    std::vector<unsigned char> pixelBuffer;
                    for (const auto &i:pixelData) {
                        auto out = removeNaNs(i).toInt();
                        pixelBuffer.emplace_back(out.r);
                        pixelBuffer.emplace_back(out.g);
                        pixelBuffer.emplace_back(out.b);
                        pixelBuffer.emplace_back(255);
                    }
                   lodepng::encode(filename, pixelBuffer, (uint32_t) width, (uint32_t) height);
                    break;
                }

                default:
                    throw NotImplemented();
            }
        }

        void LoadHDR(const std::string &filename, Image &image) {
            int ch;
            auto data = stbi_loadf(filename.c_str(), &image.width, &image.height, &ch, 3);
            Assert(ch == 3);
			Assert(data);
            image.pixelData.resize(image.width * image.height);
           // Thread::ParallelFor(0u, image.width * image.height, [&](uint32_t i, uint32_t threadId) {
			for (int i = 0; i < image.width * image.height; i++) {
				image.pixelData[i] = Spectrum(data[3 * i],
					data[3 * i + 1],
					data[3 * i + 2]);
			}
           // }, image.width);
            free(data);

        }
    }
}