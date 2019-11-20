const char * commondDefsSource = R"(#line 1
const float M_PI = 3.1415926535;
const float M_1_PI = 1.0 / M_PI;

float saturate(float v){
    return clamp(v, 0.0,1.0);
}

vec3 saturate(vec3 v){
    return clamp(v, vec3(0.0),vec3(1.0));
}
)";