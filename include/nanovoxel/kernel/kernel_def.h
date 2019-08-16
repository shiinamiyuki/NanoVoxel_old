#ifndef NANOVOXEL_KERNEL_DEF_H
#define NANOVOXEL_KERNEL_DEF_H


#include "compat.h" 

NANOVOXEL_NS_BEGIN

typedef unsigned short MaterialIdType;
#define NANOVOXEL_INVALID_MATERIAL_ID (MaterialIdType)(-1)

typedef struct knl_packed Voxel {
	MaterialIdType materialId;
}Voxel;

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

typedef struct knl_packed Globals {
	Int3 dimension;
	knl_global Voxel* voxels;
	knl_global Material * materials;
}Globals;

typedef struct knl_packed PathState {
	Int2 pixel;
}PathState;


NANOVOXEL_NS_END

#endif