//from https://github.com/AKGWSB/RealTimeAtmosphere
#version 450 core
#extension GL_ARB_separate_shader_objects: enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#include "camera.glsl"
#include "atmosphere.glsl"

layout(push_constant) uniform PushConstants {
    vec4 mainLightDir;
    vec4 mainLightColorIntensity;
};

layout(location = 0) in vec3 Dir;
layout(location = 0) out vec4 FragColor;

vec3 GetSunDisk(vec3 eyePos, vec3 viewDir, vec3 lightDir)
{
    // 计算入射光照
    float cosine_theta = dot(viewDir, -lightDir);
    float theta = acos(cosine_theta) * (180.0 / PI);
    vec3 sunLuminance = mainLightColorIntensity.rgb * mainLightColorIntensity.w *
                        parameters.sunLightIntensityFactor;

    // 判断光线是否被星球阻挡
    float disToPlanet = RayIntersectSphere(vec3(0,0,0), parameters.planetRadius, eyePos, viewDir);
    if(disToPlanet >= 0) return vec3(0,0,0);

    // 和大气层求交
    float disToAtmosphere = RayIntersectSphere(vec3(0,0,0), parameters.planetRadius + parameters.atmosphereHeight, eyePos, viewDir);
    if(disToAtmosphere < 0) return vec3(0,0,0);

    // 计算衰减
    //vec3 hitPoint = eyePos + viewDir * disToAtmosphere;
    //sunLuminance *= Transmittance(parameters, hitPoint, eyePos);
    sunLuminance *= TransmittanceToAtmosphere(eyePos, viewDir);

    if(theta < parameters.sunDiskAngle) return sunLuminance;
    return vec3(0,0,0);
}

void main()
{    
    vec4 color = vec4(0, 0, 0, 1);
    vec3 viewDir = normalize(Dir);
    color.rgb += texture(skyViewLUT, viewDir).rgb;

    float h = max(CameraInfo.viewPos.y - parameters.seaLevel, 1) + parameters.planetRadius;
    vec3 eyePos = vec3(0, h, 0);
    
    color.rgb += GetSunDisk(eyePos, viewDir, -mainLightDir.xyz);

    FragColor = color;
}
