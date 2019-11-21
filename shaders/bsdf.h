const char * bsdfSource = R"(#line 1
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
    return max(0.0, 1 - Cos2Theta(w));
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
float GGX_D(float alpha, const vec3 m) {
    if (m.y <= 0.0f)
        return 0.0f;
    float a2 = alpha * alpha;
    float c2 = Cos2Theta(m);
    float t2 = Tan2Theta(m);
    float at = (a2 + t2);
    return a2 / (M_PI * c2 * c2 * at * at);
}

float GGX_G1(float alpha, const vec3 v, const vec3 m) {
    if (dot(v, m) * v.y <= 0.0f) {
        return 0.0f;
    }
    return 2.0 / (1.0 + sqrt(1.0 + alpha * alpha * Tan2Theta(m)));
}
float GGX_G(float alpha,const vec3 i, const vec3 o, const vec3 m) {
    return GGX_G1(alpha,i, m) * GGX_G1(alpha,o, m);
}
vec3 GGX_SampleWh(float alpha, vec3 wo, vec2 u){
    float phi = 2.0 * M_PI * u.y;
    float t2 = alpha * alpha * u.x / (1.0 - u.x);
	float cosTheta = 1.0f / sqrt(1.0 + t2);
    float sinTheta = sqrt(max(0.0f, 1.0 - cosTheta * cosTheta));
	return vec3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}
float GGX_EvaluatePdf(float alpha, const vec3 wh) {
    return GGX_D(alpha, wh) * AbsCosTheta(wh);
}

vec3 evaluateGlossy(vec3 R, float alpha, const vec3 wo, const vec3 wi){
    if(wo.y * wi.y <= 0.0f){
        return vec3(0);
    }
    float cosThetaO = AbsCosTheta(wo);
    float cosThetaI = AbsCosTheta(wi);
    vec3 wh = (wo + wi);
    if (cosThetaI == 0 || cosThetaO == 0)return vec3(0);
    if (wh.x == 0 && wh.y == 0 && wh.z == 0)return vec3(0);
    wh = normalize(wh);
    float F = 1.0f;// SchlickWeight(abs(dot(wi, wh)));
    return max(vec3(0), R * F * GGX_D(alpha, wh) * GGX_G(alpha, wo, wi, wh)  / (4.0f * cosThetaI * cosThetaO));
}
float evaluateGlossyPdf(float alpha, const vec3 wo, const vec3 wi)  {
    if(wo.y * wi.y <= 0.0f){
        return 0.0f;
    }
    vec3 wh = normalize(wi + wo);
    return GGX_EvaluatePdf(alpha, wh) / (4.0f * dot(wo, wh));
}

void sampleGlossy(vec2 u, vec3 R, float alpha,vec3 wo, out vec3 wi){
    vec3 wh = GGX_SampleWh(alpha, wo, u);
    wi = reflect(-wo, wh);
}

)";