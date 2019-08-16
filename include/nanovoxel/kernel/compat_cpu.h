#ifndef NANOVOXEL_COMPAT_CPU_H
#define NANOVOXEL_COMPAT_CPU_H

#include <CL/cl.h>


typedef cl_float4 Float4;
typedef cl_float3 Float3;
typedef cl_float2 Float2;
typedef cl_int2 Int2;
typedef cl_int3 Int3;
typedef float Float;
inline Float4 __makeFloat4(Float x, Float y, Float z, Float w) {
    Float4 float4;
    float4.x = x;
    float4.y = y;
    float4.z = z;
    return float4;
}

inline Float3 __makeFloat3(Float x, Float y, Float z) {
	Float3 float3;
	float3.x = x;
	float3.y = y;
	float3.z = z;
	return float3;
}

inline Float2 __makeFloat2(Float x, Float y) {
    Float2 float2;
    float2.x = x;
    float2.y = y;
    return float2;
}

#define makeFloat4(x, y, z, w) __makeFloat4(x,y,z, w)
#define makeFloat3(x, y, z) __makeFloat3(x,y,z)
#define makeFloat2(x, y) __makeFloat2(x,y)
#define knl_get_id(x)  x
#define knl_global
#define knl_local
#define knl_const
#define knl_entry

#define knl_packed
#define NANOVOXEL_NS_BEGIN namespace NanoVoxel{__pragma(pack(push, 1))
#define NANOVOXEL_NS_END __pragma(pack(pop))  }
#endif 
