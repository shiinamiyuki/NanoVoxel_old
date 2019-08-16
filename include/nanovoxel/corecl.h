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
			cl_mem object = nullptr;
			MemObject() {}
		public:
			const cl_mem* getBuffer() const { return &object; }

			virtual ~MemObject() { if(object)clReleaseMemObject(object); }
		};

		class _Buffer : public MemObject {
		protected:
			Context* context;
		public:
			_Buffer(Context*, cl_mem_flags flag, size_t size, void* hostPtr = nullptr);

			void write(size_t size, void* buffer);

			void read(size_t size, void* buffer);
		};
		class _SVM {
			void* data = nullptr;
			Context* context;
		public:
			void* getData()const { return data; }
			_SVM(Context* context, cl_svm_mem_flags flags, size_t size, cl_uint align = 0) :context(context) {
				data = clSVMAlloc(context->getContext(), flags, size, align);
				if (!data) {
					throw CoreCL::MemoryAllocationError("Cannot alloc svm");
				}
			}
			virtual ~_SVM() {
				clSVMFree(context->getContext(), data);
			}
		};
		template<class T>
		class SVM:public _SVM {
		public:
			SVM(Context* context, cl_svm_mem_flags flags, size_t size, cl_uint align = 0)
				:_SVM(context, flags, size * sizeof(T), align) {}
		};
		template<typename T>
		class Buffer : public _Buffer {
		public:
			Buffer(Context* ctx, cl_mem_flags flag, size_t N, T* hostPtr = nullptr)
				: _Buffer(ctx, flag, sizeof(T)* N, hostPtr) {}

			void write(size_t N, T* buffer) {
				_Buffer::write(sizeof(T) * N, buffer);
			}

			void read( size_t N, T* buffer) {
				_Buffer::read(sizeof(T) * N, buffer);
			}
		};

		struct Kernel {
		private:
			cl_program program;
			cl_kernel kernel;
			bool succ = false;
			Context* ctx;
		public:
			unsigned int globalWorkSize, localWorkSize;

			Kernel(Context* ctx) : ctx(ctx) {}

			void setWorkDimesion(unsigned int g, unsigned int l) {
				globalWorkSize = g;
				localWorkSize = l;
			}
		private:
			void buildProgram(const char* src, size_t size, const char* option);

			void loadProgram(const char* filename, const char* option = nullptr);
		public:
			void createKernel(const char* file, const char* ker, const char* option = "");
			void createKernelFromSource(const std::string& name,const std::string& s, const char* option = "");

			void operator()();

			void setArg(const _Buffer& buffer, int i);
			void setArg(uint32_t* data, int i);
			void setArg(const _SVM& buffer, int i);


		private:


		};

		const char* getErrorString(int errorCode);
	}
}