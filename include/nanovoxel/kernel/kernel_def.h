#ifndef NANOVOXEL_KERNEL_DEF_H
#define NANOVOXEL_KERNEL_DEF_H


#include "compat.h" 

NANOVOXEL_NS_BEGIN

typedef unsigned short MaterialIdType;
#define NANOVOXEL_INVALID_MATERIAL_ID (MaterialIdType)(-1)

typedef struct knl_packed Voxel {
	MaterialIdType materialId;
#ifndef OPENCL_KERNEL
	Voxel() :materialId(NANOVOXEL_INVALID_MATERIAL_ID) {}
	void markEmpty() {
		materialId = NANOVOXEL_INVALID_MATERIAL_ID;
	}
	void setMat(MaterialIdType i) {
		materialId = i;
	}
#endif
}Voxel;
static inline bool isVoxelValid(knl_global const Voxel* voxel) {
	return voxel->materialId != NANOVOXEL_INVALID_MATERIAL_ID;
}
typedef enum MaterialType {
	EDiffuse,
	EDisney,
	ESpecular,
	EGlossy,
}MaterialType;

typedef struct knl_packed Texture {
	Spectrum value;
}Texture;

typedef struct knl_packed DiffuseMaterial {
	Texture color;
}DiffuseMaterial;


typedef struct knl_packed Material {
	Texture emission;
	MaterialType type;
	union {
		DiffuseMaterial diffuse;
	};
}Material;

typedef struct knl_packed PerRayData {
	Int2 pixel;
	bool done; bool valid;
	Spectrum radiance, beta;
#ifndef OPENCL_KERNEL
	PerRayData() :done(false), valid(false) {}
#endif
}PerRayData;



typedef struct knl_packed Globals {
	Int3 dimension;
	int tileSize;
	Int2 filmDimension;
	knl_global const Voxel* voxels;
	knl_global const Material* materials;
	knl_global PerRayData* prd;
}Globals;

static inline knl_global const Voxel* getVoxel(Globals* globals, Float3 pos) {
	return &globals->voxels[(int)pos.x 
		+ (int)pos.y * globals->dimension.x
		+ (int)pos.z * globals->dimension.x * globals->dimension.y];
}

typedef struct knl_packed Ray {
	Float3 o, d;
	float tnear, tfar;
}Ray;


typedef struct knl_packed SamplingContext {
	Ray primary;
}SamplingContext;

typedef struct knl_packed BoundBox {
	Float3 pMin, pMax;
}  BoundBox;

typedef struct knl_packed Intersection {
	Float3 hitpoint;
}Intersection;

NANOVOXEL_NS_END

#endif