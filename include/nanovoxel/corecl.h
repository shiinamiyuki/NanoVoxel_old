#pragma once
#include <CL/cl.h>
#include <string>
#include <memory>
#include <stdexcept>
namespace NanoVoxel {
	namespace CoreCL {
		class ContextCreationError : public std::runtime_error {
		public:
			using std::runtime_error::runtime_error;
			template<class... Args>
			ContextCreationError(const char* fmt, Args&& ... args)
				:std::runtime_error(fmt::format(fmt, args...)) {}
		};
		class MemoryAllocationError : public std::runtime_error {
		public:
			using std::runtime_error::runtime_error;
			template<class... Args>
			MemoryAllocationError(const char* fmt, Args&& ... args)
				:std::runtime_error(fmt::format(fmt, args...)) {}
		};
		cl_platform_id findPlatformContains(const std::string& s, cl_platform_id* list, int N);
		class Device {
		public:
			virtual cl_device_id& getDevice() = 0;
			virtual cl_platform_id& getPlatform() = 0;
			virtual ~Device() = default;
		};
		std::unique_ptr<Device> CreateCPUDevice();
		std::unique_ptr<Device> CreateGPUDevice();
		class Context {
			cl_context context;
			cl_command_queue commandQueue;
			cl_platform_id platform;
			Device* device = nullptr;
		public:
			void create();

		public:
			Context(Device* device) :device(device) { create(); }
			Context(const Context& rhs) {
				clRetainCommandQueue(rhs.commandQueue);
				commandQueue = rhs.commandQueue;
				device = rhs.device;
				platform = rhs.platform;
				context = rhs.context;
				clRetainContext(context);
			}
			cl_context getContext() const { return context; }

			Device* getDevice() const { return device; }

			cl_platform_id getPlatform() const { return platform; }

			cl_command_queue getCommandQueue() const { return commandQueue; }
			~Context();
		};

		class MemObject {
		protected:
			cl_mem object;
			bool allocated;
		public:
			MemObject() { allocated = false; }

			const cl_mem* getBuffer() const { return &object; }

			virtual void release() { clReleaseMemObject(object); }

			virtual ~MemObject() {}
		};

		class _Buffer : public MemObject {

		public:
			_Buffer() : MemObject() {}

			void create(Context*, cl_mem_flags flag, size_t size, void* hostPtr = nullptr);

			void write(Context*, size_t size, void* buffer);

			void read(Context*, size_t size, void* buffer);
		};

		template<typename T>
		class Buffer : public _Buffer {
		public:
			Buffer() : _Buffer() {}

			bool create(Context* ctx, cl_mem_flags flag, size_t N, T* hostPtr = nullptr) {
				return _Buffer::create(ctx, flag, sizeof(T) * N, hostPtr);
			}

			void write(Context* ctx, size_t N, T* buffer) {
				_Buffer::write(ctx, sizeof(T) * N, buffer);
			}

			void read(Context* ctx, size_t N, T* buffer) {
				_Buffer::read(ctx, sizeof(T) * N, buffer);
			}
		};

		class Image : public MemObject {
		public:
			bool create(Context*,
				cl_mem_flags flag,
				const cl_image_format* format,
				const cl_image_desc* desc, void* hostPtr);
		};

		struct Kernel {
			cl_program program;
			cl_kernel kernel;
			bool succ;
			Context* ctx;
			unsigned int globalWorkSize, localWorkSize;

			Kernel() : ctx(nullptr) {}

			void setContext(Context* c) { ctx = c; }

			void setWorkDimesion(unsigned int g, unsigned int l) {
				globalWorkSize = g;
				localWorkSize = l;
			}

			void buildProgram(const char* src, size_t size, const char* option);

			void loadProgram(const char* filename, const char* option = nullptr);

			void createKernel(const char* file, const char* ker, const char* option = nullptr);

			void operator()();

			void setArg(const _Buffer& buffer, int i);

			void operator()(const _Buffer& buffer) { setArg(buffer, 0); }

			void setArgV(int i, const _Buffer& buffer) {
				setArg(buffer, i);
			}

			template<typename... Arg>
			void setArgV(int i, const _Buffer& buffer, Arg... arg) {
				setArg(buffer, i);
				setArgV(i + 1, arg...);
			}

			template<typename... Arg>
			void operator()(const _Buffer& buffer, Arg... arg) {
				setArg(buffer, 0);
				setArgV(1, arg...);
				(*this)();
			}
		};

		const char* getErrorString(int errorCode);
	}
}