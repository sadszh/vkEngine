//from https://github.com/AKGWSB/RealTimeAtmosphere
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable

#include "camera.glsl"
#define ATMOSPHERE_NO_SKYVIEW_DESCRIPTORS
#include "atmosphere.glsl"

layout(push_constant) uniform PushConstants {
    vec4 mainLightDir;
    vec4 mainLightColorIntensity;
};

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 5) uniform sampler2D hdrColor;
layout(set = 1, binding = 6) uniform sampler2D linearDepth;

void IntegrateAerialPerspective(vec3 eyePos, vec3 viewDir, vec3 lightDir, float sceneDistance,
                                out vec3 transmittance, out vec3 inScattering)
{
    const int sampleCount = 32;
    float atmosphereDistance = RayIntersectSphere(
        vec3(0.0), parameters.planetRadius + parameters.atmosphereHeight, eyePos, viewDir);
    float planetDistance = RayIntersectSphere(vec3(0.0), parameters.planetRadius, eyePos, viewDir);
    float physicalDistance = min(sceneDistance, atmosphereDistance);
    if (planetDistance > 0.0)
        physicalDistance = min(physicalDistance, planetDistance);

    if (atmosphereDistance < 0.0 || physicalDistance <= 0.0)
    {
        transmittance = vec3(1.0);
        inScattering = vec3(0.0);
        return;
    }

    float opticalDistance = min(physicalDistance * parameters.aerialPerspectiveScale,
                                parameters.aerialPerspectiveDistance);
    float physicalStepLength = physicalDistance / float(sampleCount);
    float opticalStepLength = opticalDistance / float(sampleCount);
    vec3 samplePos = eyePos + viewDir * physicalStepLength * 0.5;
    vec3 opticalDepth = vec3(0.0);
    vec3 sunLuminance = mainLightColorIntensity.rgb * mainLightColorIntensity.w *
                        parameters.sunLightIntensityFactor;
    inScattering = vec3(0.0);

    for (int i = 0; i < sampleCount; ++i)
    {
        float height = length(samplePos) - parameters.planetRadius;
        vec3 extinction = RayleighCoefficient(height) + MieCoefficient(height) +
                          OzoneAbsorption(height) + MieAbsorption(height);
        opticalDepth += extinction * opticalStepLength;

        vec3 viewTransmittance = exp(-opticalDepth);
        vec3 sunTransmittance = TransmittanceToAtmosphere(samplePos, lightDir);
        vec3 singleScattering = sunTransmittance *
                                Scattering(samplePos, lightDir, viewDir) *
                                viewTransmittance * opticalStepLength * sunLuminance;
        vec3 multiScattering = GetMultiScattering(samplePos, lightDir) *
                               viewTransmittance * opticalStepLength * sunLuminance;
        inScattering += singleScattering + multiScattering;
        samplePos += viewDir * physicalStepLength;
    }

    transmittance = exp(-opticalDepth);
}

void main()
{
    vec3 sceneColor = texture(hdrColor, vTexCoord).rgb;
    float depth = texture(linearDepth, vTexCoord).r;
    if (depth <= 0.0 || depth >= CameraInfo.far)
    {
        outColor = vec4(sceneColor, 1.0);
        return;
    }

    vec2 ndc = vTexCoord * 2.0 - 1.0;
    vec4 ray = CameraInfo.invProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewRay = ray.xyz / ray.w;
    vec3 viewPos = viewRay * (depth / -viewRay.z);
    vec3 viewDir = normalize(mat3(CameraInfo.invView) * normalize(viewPos));

    float cameraHeight = max(CameraInfo.viewPos.y - parameters.seaLevel, 1.0);
    vec3 eyePos = vec3(0.0, parameters.planetRadius + cameraHeight, 0.0);
    float distance = length(viewPos);
    vec3 transmittance;
    vec3 inScattering;
    IntegrateAerialPerspective(eyePos, viewDir, mainLightDir.xyz, distance,
                               transmittance, inScattering);
    outColor = vec4(sceneColor * transmittance + inScattering, 1.0);
}
