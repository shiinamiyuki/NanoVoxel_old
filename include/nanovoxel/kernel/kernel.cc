#include "kernel/kernel_def.h"

#ifdef OPENCL_KERNEL
NANOVOXEL_NS_BEGIN

static inline Ray makeRay(Float3 o, Float3 d) {
	Ray ray;
	ray.o = o;
	ray.d = d;
	ray.tnear = 0.001;
	ray.tfar = 1e10;
	return ray;
}
#define swap(x,y)do{float __temp = x;x = y;y=__temp;}while(0)

bool RayAABBIntersect(const Ray* ray, const BoundBox* box, float* _t) {
	float tmin = (box->pMin.x - ray->o.x) / ray->d.x;
	float tmax = (box->pMax.x - ray->o.x) / ray->d.x;
	if (tmin > tmax)
		swap(tmin, tmax);
	float tymin = (box->pMin.y - ray->o.y) / ray->d.y;
	float tymax = (box->pMax.y - ray->o.y) / ray->d.y;
	if (tymin > tymax)
		swap(tymin, tymax);
	if (tmin > tymax || tymin > tmax) {
		return false;
	}
	if (tymin > tmin)
		tmin = tymin;

	if (tymax < tmax)
		tmax = tymax;
	float tzmin = (box->pMin.z - ray->o.z) / ray->d.z;
	float tzmax = (box->pMax.z - ray->o.z) / ray->d.z;

	if (tzmin > tzmax) swap(tzmin, tzmax);
	if (tmin > tzmax || tzmin > tmax) {
		return false;
	}
	if (tzmin > tmin)
		tmin = tzmin;

	if (tzmax < tmax)
		tmax = tzmax;
	float t;
	if (tmin < ray->tnear)
		t = tmax;
	else if (tmax < ray->tnear)
		t = tmin;
	else
		t = min(tmax, tmin);
	if (t > ray->tnear && t < ray->tfar) {
		*_t = t;
		return true;
	}
	return false;
}

static inline bool intersect(Globals* globals, const Ray* _ray, Intersection* isct) {
	const float eps = 0.0001;
	BoundBox box;
	box.pMin = makeFloat3(0, 0, 0);
	box.pMax = makeFloat3(globals->dimension.x, globals->dimension.y, globals->dimension.z);
	float distance;
	bool result = RayAABBIntersect(_ray, &box, &distance);
	//printf("%f\n", ray->d.z);
	if (!result)return false;
	Ray ray = makeRay(_ray->o + _ray->d * distance, _ray->d);
	ray.o = max(ray.o, makeFloat3(eps, eps, eps));
	// traversal
	float3 p0 = ray.o;
	float3 p = floor(p0);
	float3 stp = sign(ray.d);
	float3 invd = makeFloat3(1, 1, 1) / ray.d;
	float3 tMax = fabs((p + max(stp, makeFloat3(0, 0, 0)) - p0) * invd);
	float3 delta = invd * stp;// min(invd * stp, makeFloat3(1, 1, 1));

	for (int i = 0; i < 128; ++i) {
		if (p.x < 0 || (int)p.x >= globals->dimension.x
			|| p.y < 0 || (int)p.y >= globals->dimension.y
			|| p.z < 0 || (int)p.z >= globals->dimension.z) {
			break;
		}
		knl_global const Voxel* voxel = getVoxel(globals, floor(p));
		if (knl_get_id(0) == 0) {
			printf("%f %f %f   %d\n", delta.x, delta.y, delta.z, voxel->materialId);
		}
		if (isVoxelValid(voxel)) {
			return true;
		}
		if (tMax.x < tMax.y) {
			if (tMax.x < tMax.z) {
				p.x += stp.x;
				tMax.x += delta.x;
			}
			else {
				p.z += stp.z;
				tMax.z += delta.z;
			}
		}
		else {
			if (tMax.y < tMax.z) {
				p.y += stp.y;
				tMax.y += delta.y;
			}
			else {
				p.z += stp.z;
				tMax.z += delta.z;
			}
		}
		
	}
	return false;
}
void trace(Globals* globals, SamplingContext* context) {
	globals->prd->done = true;
	Intersection isct;
	globals->prd->radiance = makeFloat3(0, 0, 0);
	if (intersect(globals, &context->primary, &isct)) {
		globals->prd->radiance = makeFloat3(1, 1, 1);
	}

}
knl_entry
void NanoVoxelMain(
	knl_global Globals* _globals,
	knl_global const Material* materials,
	knl_global const Voxel* voxels,
	knl_global PerRayData* _prd) {
	int id = knl_get_id(0);
	Globals globals = *_globals;
	globals.materials = materials;
	globals.prd = &_prd[id];
	globals.voxels = voxels;

	knl_global PerRayData* prd = &_prd[id];
	if (!prd->valid)return;

	Int2 pixel = prd->pixel;

	Float3 ro = makeFloat3(25, 25, -10);

	float x = pixel.x;
	float y = pixel.y;

	x /= globals.filmDimension.x;
	y /= globals.filmDimension.y;

	x = (2 * x - 1);
	y = (1 - y) * 2 - 1;

	Float3 rd = makeFloat3(x, y, 0);

	rd = normalize(rd - makeFloat3(0, 0, -1));
	SamplingContext context;
	context.primary = makeRay(ro, rd);
	trace(&globals, &context);

}

NANOVOXEL_NS_END

#endif
