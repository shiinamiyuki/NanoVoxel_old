#include "kernel/kernel_def.h"

#ifdef OPENCL_KERNEL
NANOVOXEL_NS_BEGIN
knl_const const float PI = 3.1415926535;
knl_const float INVPI = 1.0f / PI;
typedef struct CoordinateSystem {
	Float3 localX, localY, normal;
}CoordinateSystem;


void CreateCoordinateSystem(CoordinateSystem* frame, Float3 N) {
	frame->normal = N;
	frame->localX = normalize(cross((fabs(N.x) > 0.1) ? makeFloat3(0, 1, 0) : makeFloat3(1, 0, 0), N));
	frame->localY = normalize(cross(N, frame->localX));
}
Float3 WorldToLocal(CoordinateSystem* frame, const Float3 v)  {
	return normalize(
		makeFloat3(dot(frame->localX, v),
			dot(frame->localY, v),
			dot(frame->normal, v)));
}

Float3 LocalToWorld(CoordinateSystem* frame, const Float3 v)  {
	return normalize(v.x * frame->localX + v.y * frame->localY + v.z * frame->normal);
}

float3 cosine_hemisphere_sampling(float2 u) {
	float theta = u.x * 2 * PI;
	float r = sqrt(u.y);
	return makeFloat3(sin(theta) * r, cos(theta) * r, sqrt(1 - r * r));
}


float cosine_hemiphsere_pdf(float3 v) {
	return fabs(v.z) * INVPI;
}

static inline Ray makeRay(Float3 o, Float3 d) {
	Ray ray;
	ray.o = o;
	ray.d = d;
	ray.tnear = 0.0001;
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
	if (tmin > tmax) {
		swap(tmin, tmax);
	}
	if (tmin < 0 && tmax > 0) {
		*_t = 0;
		return true;
	}
	if (tmin > 0 && tmax > 0) {
		*_t = tmin;
		return true;
	}
	return false;
}

static inline bool intersect(Globals* globals, const Ray* _ray, Intersection* isct) {
	const float eps = 0.001;
	BoundBox box;
	box.pMin = makeFloat3(0, 0, 0);
	box.pMax = makeFloat3(globals->dimension.x, globals->dimension.y, globals->dimension.z);
	float distance;
	bool result = RayAABBIntersect(_ray, &box, &distance);
	/*if (globals->prd->pixel.x == 200 && globals->prd->pixel.y == 238) {
		printf("AABB hit = %d\n", result);
	}*/
	distance = max(distance, 0.0f);
	//printf("%f\n", ray->d.z);
	if (!result)return false;
	Ray ray = makeRay(_ray->o + _ray->d * distance, _ray->d);
	ray.o = max(ray.o, makeFloat3(eps, eps, eps));
	// traversal
	float3 p0 = ray.o;
	float3 p = floor(p0);
	float3 stp = sign(ray.d);
	float3 invd = makeFloat3(1, 1, 1) / ray.d;
	float3 tMax = fabs((p + max(stp, makeFloat3(0,0,0)) - p0) * invd);
	float3 delta = fabs(invd);

	float3 mask = makeFloat3(0, 0, 0);
	float t = 0;
	for (int i = 0; i < 128; ++i) {
		if (p.x < 0 || (int)p.x >= globals->dimension.x
			|| p.y < 0 || (int)p.y >= globals->dimension.y
			|| p.z < 0 || (int)p.z >= globals->dimension.z) {
			break;
		}
		knl_global const Voxel* voxel = getVoxel(globals, floor(p));
		/*if (globals->prd->pixel.x == 200 && globals->prd->pixel.y == 238) {
			printf("p0: %f %f %f  %d\n", p.x, p.y, p.z, isVoxelValid(voxel));
			Float3 p1 = p0 + t * ray.d;
			printf("p1: %f %f %f  %d\n", p1.x, p1.y, p1.z, isVoxelValid(voxel));
			printf("t : %f %f %f  %f\n", tMax.x, tMax.y, tMax.z, distance + t);
		}
		*/
		if (isVoxelValid(voxel) && distance + t > _ray->tnear ) {
			isct->hitpoint = p0 + ray.d * t;
			// find normal
			isct->distance = distance + t;
			isct->normal = -sign(ray.d) * mask;

			//printf("%f %f %f\n", p.x, p.y, p.z);
			return true;
		}
		if (tMax.x < tMax.y) {
			if (tMax.x < tMax.z) {
				p.x += stp.x;
				t = tMax.x;
				tMax.x += delta.x;				
				mask = makeFloat3(1, 0, 0);
			}
			else {
				p.z += stp.z; t = tMax.z;
				tMax.z += delta.z;
				
				mask = makeFloat3(0, 0, 1);
			}
		}
		else {
			if (tMax.y < tMax.z) {
				p.y += stp.y; t = tMax.y;
				tMax.y += delta.y;
				
				mask = makeFloat3(0, 1, 0);
			}
			else {
				p.z += stp.z; t = tMax.z;
				tMax.z += delta.z;
				
				mask = makeFloat3(0, 0, 1);
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
		float distance = length(context->primary.o - isct.hitpoint);
		Float2 u;
		u.x = lcg_rng(&globals->prd->rng);
		u.y = lcg_rng(&globals->prd->rng);
		Float3 dir = cosine_hemisphere_sampling(u);
		CoordinateSystem frame;
		CreateCoordinateSystem(&frame, isct.normal);
		dir = LocalToWorld(&frame, dir);
		Spectrum color = makeFloat3(1, 1, 1);
		Ray ray = makeRay(isct.hitpoint, dir);
		ray.o += 0.001 * ray.d;
		if (!intersect(globals, &ray, &isct)) {
			
		}
		else {
			color *= 0.0f;
		}
		globals->prd->radiance = color;
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

	Float3 ro = makeFloat3(30, 25, -10);

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
