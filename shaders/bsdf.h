const char * bsdfSource = R"(#line 1
//https://schuttejoe.github.io/post/disneybsdf/

float AbsCosTheta(vec3 w){
    return abs(w.y);
}
float CosTheta(vec3 w){
    return w.y;
}
float Cos2Theta(vec3 w){
    return w.y * w.y;
}

float Sin2Theta(vec3 w){
    return min(0.0, 1 - Cos2Theta(w));
}
float SinTheta(vec3 w){
    return sqrt(SinTheta(w));
}
float Tan2Theta(vec3 w){
    return Sin2Theta(w) / Cos2Theta(w);
}

float TanTheta(vec3 w){
    return sqrt(Tan2Theta(w));
}

float CosPhi(vec3 w){
    float s = SinTheta(w);
    return s == 0.0 ? 1.0 : clamp(w.x/s, -1.0,1.0);
}
float SinPhi(vec3 w){
    float s = SinTheta(w);
    return s == 0.0 ? 0.0 : clamp(w.z/s, -1.0,1.0);
}
float Cos2Phi(vec3 w){
    float c = CosPhi(w);
    return c * c;
}
float Sin2Phi(vec3 w){
    float s = SinPhi(w);
    return s * s;
}

float SchlickWeight(float cosTheta) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    return (m * m) * (m * m) * m;
}
float Schlick(float R0, float cosTheta) {
    return mix(SchlickWeight(cosTheta), R0, 1.0);
}
vec3 evalTint(vec3 baseColor){
    float luminance = dot(vec3(0.3,0.6,0.1), baseColor);
    return luminance > 0.0 ? baseColor / luminance : vec3(1);
}

vec3 evalSheen(vec3 baseColor, float sheen, vec3 sheenTint, vec3 wo, vec3 wm, vec3 wi){
    if(sheen <= 0.0){
        return vec3(0);
    }
    float dotHL = dot(wm, wi);
    vec3 tint = evalTint(baseColor);
    return sheen * mix(vec3(1), tint, sheenTint) * SchlickWeight(dotHL);
}

float GTR1(float absDotHL, float a){
    if(a >= 1.0) {
        return M_1_PI;
    }
    float a2 = a * a;
    return (a2 - 1.0f) / (M_PI * log2(a2) * (1.0f + (a2 - 1.0f) * absDotHL * absDotHL));
}

float SeparableSmithGGXG1(vec3 w, float a){
    float a2 = a * a;
    float absDotNV = AbsCosTheta(w);

    return 2.0f / (1.0f + sqrt(a2 + (1.0 - a2) * absDotNV * absDotNV));
}

float EvaluateDisneyClearcoat(float clearcoat, float alpha, const vec3 wo, const vec3 wm,
                                     const vec3 wi, out float fPdfW, out float rPdfW){
    if(clearcoat <= 0.0f) {
        return 0.0f;
    }

    float absDotNH = AbsCosTheta(wm);
    float absDotNL = AbsCosTheta(wi);
    float absDotNV = AbsCosTheta(wo);
    float dotHL = dot(wm, wi);

    float d = GTR1(absDotNH, mix(0.1f, 0.001f, alpha));
    float f = Schlick(0.04f, dotHL);
    float gl = SeparableSmithGGXG1(wi, 0.25f);
    float gv = SeparableSmithGGXG1(wo, 0.25f);

    fPdfW = d / (4.0f * absDotNL);
    rPdfW = d / (4.0f * absDotNV);

    return 0.25f * clearcoat * d * f * gl * gv;
}

float GgxAnisotropicD(const vec3 wm, float ax, float ay){
    float dotHX2 = wm.x * wm.x;
    float dotHY2 = wm.z * wm.z;
    float cos2Theta = wm.y * wm.y;
    float ax2 = ax * ax;
    float ay2 = ay * ay;

    float d = dotHX2 / ax2 + dotHY2 / ay2 + cos2Theta;
    return 1.0f / (M_PI * ax * ay * d * d);
}


float SeparableSmithGGXG1(const vec3 w, const vec3 wm, float ax, float ay){
    float dotHW = dot(w, wm);
    if (dotHW <= 0.0f) {
        return 0.0f;
    }

    float absTanTheta = abs(TanTheta(w));
    if(isinf(absTanTheta)) {
        return 0.0f;
    }
    float a = sqrt(Cos2Phi(w) * ax * ax + Sin2Phi(w) * ay * ay);
    float a2Tan2Theta = (a * absTanTheta);
    a2Tan2Theta *= a2Tan2Theta;

    float lambda = 0.5f * (-1.0f + sqrt(1.0f + a2Tan2Theta));
    return 1.0f / (1.0f + lambda);
}

float ThinTransmissionRoughness(float ior, float roughness){
    return saturate((0.65f * ior - 0.35f) * roughness);
}

)";