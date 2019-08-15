#include "corecl.h"
#include <util.h>
namespace NanoVoxel {
	namespace CoreCL {
		cl_platform_id findPlatformContains(const std::string& s, cl_platform_id* list, int N) {
			auto name = s;
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);
			for (int i = 0; i < N; i++) {
				char* m = new char[1024];
				clGetPlatformInfo(list[i], CL_PLATFORM_VENDOR, sizeof(char) * 1024, m, nullptr);
				std::string vendor = m;
				std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::toupper);
				delete[]m;
				if (vendor.find(name) != std::string::npos)return list[i];
			}
			return nullptr;
		}

		bool _Buffer::create(Context* ctx, cl_mem_flags flag, size_t size, void* hostPtr) {
			if (allocated) { release(); }
			cl_int ret;
			object = clCreateBuffer(ctx->getContext(),
				flag,
				size,
				hostPtr, &ret);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "Failed to allocated buffer: {}\n", getErrorString(ret));
				return false;
			}
			allocated = true;
			return true;
		}

		void CoreCL::_Buffer::write(Context* ctx, size_t size, void* buffer) {
			clEnqueueWriteBuffer(ctx->getCommandQueue(), object, CL_TRUE, 0,
				size, buffer, 0, nullptr, nullptr);
		}

		void CoreCL::_Buffer::read(Context* ctx, size_t size, void* buffer) {
			clEnqueueReadBuffer(ctx->getCommandQueue(), object, CL_TRUE, 0,
				size, buffer, 0, nullptr, nullptr);
		}

		void Context::create() {
			fmt::print("Creating OpenCL Context\n");
			cl_platform_id platform_id = nullptr, platform_list[10];
			cl_device_id device_id = nullptr;
			cl_uint ret_num_devices;
			cl_uint ret_num_platforms;
			cl_int ret = clGetPlatformIDs(10, platform_list, &ret_num_platforms);


			if ((platform_id = CoreCL::findPlatformContains("NVidia", platform_list, 10)) == nullptr) {
				if ((platform_id = CoreCL::findPlatformContains("AMD", platform_list, 10)) == nullptr) {
					if ((platform_id = CoreCL::findPlatformContains("Intel", platform_list, 10)) == nullptr) {
						fmt::print(stderr, "no available platform\n", 0);
					}
				}
			}

			ret = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1,
				&device_id, &ret_num_devices);
			char* m = new char[1024];
			clGetDeviceInfo(device_id, CL_DEVICE_NAME, sizeof(char) * 1024, m, nullptr);
			fmt::print("Device: {}\n", m);
			delete[]m;
			platform = platform_id;
			device = device_id;
			context = clCreateContext(nullptr,
				1,
				&device_id,
				nullptr,
				nullptr,
				&ret);

			// Create a command queue
			commandQueue = clCreateCommandQueueWithProperties(
				context,
				device_id,
				nullptr,
				&ret);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "cannot create command queue, error: {}", getErrorString(ret));
			}
		}

		void Kernel::buildProgram(const char* src, size_t size, const char* option) {
			cl_int ret;
			program = clCreateProgramWithSource(ctx->getContext(), 1,
				(const char**)& src, (const size_t*)& size, &ret);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "cannot create program, error: {}", getErrorString(ret));
				return;
			}
			auto device = ctx->getDevice();
			std::string optStr = "-I. -cl-single-precision-constant -Werror";
			optStr += option;
			ret = clBuildProgram(program, 1, &device,
				optStr.c_str(),
				nullptr, nullptr);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "Program Build failed\n");
				size_t length;
				char buffer[40960];
				clGetProgramBuildInfo(program, ctx->getDevice(),
					CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &length);
				fmt::print(stderr, "\"--- Build log ---\n{0}\n", buffer);
			}
		}

#define MAX_SOURCE_SIZE 100000

		void Kernel::loadProgram(const char* filename, const char* option) {
			FILE* fp;
			char* source_str;
			size_t source_size;

			fp = fopen(filename, "r");
			if (!fp) {
				fmt::print(stderr, "Failed to load kernel \"{}\".\n", filename);
				return;
			}
			source_str = (char*)malloc(MAX_SOURCE_SIZE);
			source_size = fread(source_str, 1, MAX_SOURCE_SIZE, fp);
			fclose(fp);
			buildProgram(source_str, source_size, option);
		}

		void Kernel::createKernel(const char* file, const char* ker, const char* option) {
			succ = false;
			fmt::print("Creating kernel {}::{}\n", file, ker);
			loadProgram(file, option);
			cl_int ret;
			kernel = clCreateKernel(program, ker, &ret);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "error creating kernel {} \n", getErrorString(ret));
				return;
			}
			succ = true;
		}

		void CoreCL::Kernel::operator()() {
			if (!this->succ)return;
			size_t local_item_size = localWorkSize;
			size_t global_item_size = globalWorkSize;
			if (global_item_size % local_item_size != 0)
				global_item_size = (global_item_size / local_item_size + 1) * local_item_size;
			cl_int ret = clEnqueueNDRangeKernel(ctx->getCommandQueue(), kernel, 1, nullptr,
				&global_item_size, &local_item_size, 0, nullptr, nullptr);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "Cannot Enqueue NDRangeKernel error: {}\n", getErrorString(ret));
			}
			clFinish(ctx->getCommandQueue());
		}

		void CoreCL::Kernel::setArg(const _Buffer& buffer, int i) {
			clSetKernelArg(kernel, i, sizeof(cl_mem), buffer.getBuffer());
		}

		bool CoreCL::Image::create(Context* ctx, cl_mem_flags flag, const cl_image_format* format, const cl_image_desc* desc,
			void* hostPtr) {
			if (allocated) { release(); }
			cl_int ret;
			object = clCreateImage(ctx->getContext(),
				flag, format, desc, hostPtr, &ret);
			if (ret != CL_SUCCESS) {
				fmt::print(stderr, "Failed to allocated buffer: {}\n", getErrorString(ret));
				return false;
			}
			allocated = true;
			return true;
		}


		const char* NanoVoxel::CoreCL::getErrorString(int errorCode) {
			switch (errorCode) {
				// run-time and JIT compiler errors
			case 0:
				return "CL_SUCCESS";
			case -1:
				return "CL_DEVICE_NOT_FOUND";
			case -2:
				return "CL_DEVICE_NOT_AVAILABLE";
			case -3:
				return "CL_COMPILER_NOT_AVAILABLE";
			case -4:
				return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
			case -5:
				return "CL_OUT_OF_RESOURCES";
			case -6:
				return "CL_OUT_OF_HOST_MEMORY";
			case -7:
				return "CL_PROFILING_INFO_NOT_AVAILABLE";
			case -8:
				return "CL_MEM_COPY_OVERLAP";
			case -9:
				return "CL_IMAGE_FORMAT_MISMATCH";
			case -10:
				return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
			case -11:
				return "CL_BUILD_PROGRAM_FAILURE";
			case -12:
				return "CL_MAP_FAILURE";
			case -13:
				return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
			case -14:
				return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
			case -15:
				return "CL_COMPILE_PROGRAM_FAILURE";
			case -16:
				return "CL_LINKER_NOT_AVAILABLE";
			case -17:
				return "CL_LINK_PROGRAM_FAILURE";
			case -18:
				return "CL_DEVICE_PARTITION_FAILED";
			case -19:
				return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";

				// compile-time errors
			case -30:
				return "CL_INVALID_VALUE";
			case -31:
				return "CL_INVALID_DEVICE_TYPE";
			case -32:
				return "CL_INVALID_PLATFORM";
			case -33:
				return "CL_INVALID_DEVICE";
			case -34:
				return "CL_INVALID_CONTEXT";
			case -35:
				return "CL_INVALID_QUEUE_PROPERTIES";
			case -36:
				return "CL_INVALID_COMMAND_QUEUE";
			case -37:
				return "CL_INVALID_HOST_PTR";
			case -38:
				return "CL_INVALID_MEM_OBJECT";
			case -39:
				return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
			case -40:
				return "CL_INVALID_IMAGE_SIZE";
			case -41:
				return "CL_INVALID_SAMPLER";
			case -42:
				return "CL_INVALID_BINARY";
			case -43:
				return "CL_INVALID_BUILD_OPTIONS";
			case -44:
				return "CL_INVALID_PROGRAM";
			case -45:
				return "CL_INVALID_PROGRAM_EXECUTABLE";
			case -46:
				return "CL_INVALID_KERNEL_NAME";
			case -47:
				return "CL_INVALID_KERNEL_DEFINITION";
			case -48:
				return "CL_INVALID_KERNEL";
			case -49:
				return "CL_INVALID_ARG_INDEX";
			case -50:
				return "CL_INVALID_ARG_VALUE";
			case -51:
				return "CL_INVALID_ARG_SIZE";
			case -52:
				return "CL_INVALID_KERNEL_ARGS";
			case -53:
				return "CL_INVALID_WORK_DIMENSION";
			case -54:
				return "CL_INVALID_WORK_GROUP_SIZE";
			case -55:
				return "CL_INVALID_WORK_ITEM_SIZE";
			case -56:
				return "CL_INVALID_GLOBAL_OFFSET";
			case -57:
				return "CL_INVALID_EVENT_WAIT_LIST";
			case -58:
				return "CL_INVALID_EVENT";
			case -59:
				return "CL_INVALID_OPERATION";
			case -60:
				return "CL_INVALID_GL_OBJECT";
			case -61:
				return "CL_INVALID_BUFFER_SIZE";
			case -62:
				return "CL_INVALID_MIP_LEVEL";
			case -63:
				return "CL_INVALID_GLOBAL_WORK_SIZE";
			case -64:
				return "CL_INVALID_PROPERTY";
			case -65:
				return "CL_INVALID_IMAGE_DESCRIPTOR";
			case -66:
				return "CL_INVALID_COMPILER_OPTIONS";
			case -67:
				return "CL_INVALID_LINKER_OPTIONS";
			case -68:
				return "CL_INVALID_DEVICE_PARTITION_COUNT";

				// extension errors
			case -1000:
				return "CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR";
			case -1001:
				return "CL_PLATFORM_NOT_FOUND_KHR";
			case -1002:
				return "CL_INVALID_D3D10_DEVICE_KHR";
			case -1003:
				return "CL_INVALID_D3D10_RESOURCE_KHR";
			case -1004:
				return "CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR";
			case -1005:
				return "CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR";
			default:
				return "Unknown OpenCL error";
			}

		}
	}
}