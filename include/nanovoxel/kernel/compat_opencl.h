#ifndef NANOVOXEL_COMPAT_OPENCL_H
#define NANOVOXEL_COMPAT_OPENCL_H
#ifdef OPENCL_KERNEL
typedef float4 Float4;
typedef float3 Float3;
typedef float2 Float2;
typedef int2 Int2;
typedef int3 Int3;
typedef float Float;
#define makeFloat4(x, y, z, w) (float4)(x,y,z, w)
#define makeFloat2(x, y) (float2)(x,y)
#define makeFloat3(x, y, z) (float3)(x,y,z)
#define knl_get_id(x)   get_global_id(x)
#define knl_global __global
#define knl_local __local
#define knl_const __constant
#define knl_entry __kernel


#define knl_packed __attribute__((packed)) 
#define NANOVOXEL_NS_BEGIN
#define NANOVOXEL_NS_END
#endif
#endif 
