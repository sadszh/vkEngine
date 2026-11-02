//from https://github.com/AKGWSB/RealTimeAtmosphere
#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

#ifndef PI
#define PI 3.14159265359
#endif

struct AtmosphereParameter
{
    float seaLevel;
    float planetRadius;
    float atmosphereHeight;
    float sunDiskAngle;
    float sunLightIntensityFactor;
    float rayleighScatteringScale;
    float rayleighScatteringScalarHeight;
    float mieScatteringScale;
    float mieAnisotropy;
    float mieScatteringScalarHeight;
    float ozoneAbsorptionScale;
    float ozoneLevelCenterHeight;
    float ozoneLevelWidth;
    float aerialPerspectiveDistance;
    float aerialPerspectiveScale;
};

#ifndef ATMOSPHERE_DESCRIPTOR_SET
#define ATMOSPHERE_DESCRIPTOR_SET 1
#endif

layout (std430, set = ATMOSPHERE_DESCRIPTOR_SET, binding = 0) uniform UBO { AtmosphereParameter parameters; };
layout(set = ATMOSPHERE_DESCRIPTOR_SET, binding = 1) uniform sampler2D transmittanceLUT;

#ifndef ATMOSPHERE_CORE_ONLY

layout(set = ATMOSPHERE_DESCRIPTOR_SET, binding = 2) uniform sampler2D multiScatteringLUT;

#ifndef ATMOSPHERE_NO_SKYVIEW_DESCRIPTORS

layout(set = ATMOSPHERE_DESCRIPTOR_SET, binding = 3) uniform samplerCube skyViewLUT;
layout(set = ATMOSPHERE_DESCRIPTOR_SET, binding = 4, rgba16f) writeonly uniform imageCube skyViewImage;

#endif //ifndef ATMOSPHERE_NO_SKYVIEW_DESCRIPTORS
#endif //ifndef ATMOSPHERE_CORE_ONLY

float RayIntersectSphere(vec3 center, float radius, vec3 rayStart, vec3 rayDir)
{
    float OS = length(center - rayStart);
    float SH = dot(center - rayStart, rayDir);
    float OH = sqrt(OS*OS - SH*SH);
    float PH = sqrt(radius*radius - OH*OH);

    // ray miss sphere
    if(OH > radius) return -1;

    // use min distance
    float t1 = SH - PH;
    float t2 = SH + PH;
    float t = (t1 < 0) ? t2 : t1;

    return t;
}

// ------------------------------------------------------------------------- //

vec3 RayleighCoefficient(float h)
{
    const vec3 sigma = vec3(5.802, 13.558, 33.1) * 1e-6;
    float H_R = parameters.rayleighScatteringScalarHeight;
    float rho_h = exp(-(h / H_R));
    return sigma * rho_h * parameters.rayleighScatteringScale;
}

float RayleiPhase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

vec3 MieCoefficient(float h)
{
    const vec3 sigma = (3.996 * 1e-6).xxx;
    float H_M = parameters.mieScatteringScalarHeight;
    float rho_h = exp(-(h / H_M));
    return sigma * rho_h * parameters.mieScatteringScale;
}

float MiePhase(float cos_theta)
{
    float g = parameters.mieAnisotropy;

    float a = 3.0 / (8.0 * PI);
    float b = (1.0 - g*g) / (2.0 + g*g);
    float c = 1.0 + cos_theta*cos_theta;
    float d = pow(1.0 + g*g - 2*g*cos_theta, 1.5);
    
    return a * b * (c / d);
}

vec3 MieAbsorption(float h)
{
    const vec3 sigma = (4.4 * 1e-6).xxx;
    float H_M = parameters.mieScatteringScalarHeight;
    float rho_h = exp(-(h / H_M));
    return sigma * rho_h;
}

vec3 OzoneAbsorption(float h)
{
    #define sigma_lambda (vec3(0.650f, 1.881f, 0.085f)) * 1e-6
    float center = parameters.ozoneLevelCenterHeight;
    float width = parameters.ozoneLevelWidth;
    float rho = max(0, 1.0 - (abs(h - center) / width));
    return sigma_lambda * rho * parameters.ozoneAbsorptionScale;
}

// ------------------------------------------------------------------------- //

vec3 Scattering(vec3 p, vec3 lightDir, vec3 viewDir)
{
    float cos_theta = dot(lightDir, viewDir);

    float h = length(p) - parameters.planetRadius;
    vec3 rayleigh = RayleighCoefficient(h) * RayleiPhase(cos_theta);
    vec3 mie = MieCoefficient(h) * MiePhase(cos_theta);

    return rayleigh + mie;
}

vec2 GetTransmittanceLutUv(float bottomRadius, float topRadius, float mu, float r)
{
    float H = sqrt(max(0.0, topRadius * topRadius - bottomRadius * bottomRadius));
    float rho = sqrt(max(0.0, r * r - bottomRadius * bottomRadius));

    float discriminant = r * r * (mu * mu - 1.0) + topRadius * topRadius;
	float d = max(0.0, (-r * mu + sqrt(discriminant)));

    float d_min = topRadius - r;
    float d_max = rho + H;

    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return vec2(x_mu, x_r);
}

// 查表计算任意点 p 沿着任意方向 dir 到大气层边缘的 transmittance
vec3 TransmittanceToAtmosphere(vec3 p, vec3 dir)
{
    float bottomRadius = parameters.planetRadius;
    float topRadius = parameters.planetRadius + parameters.atmosphereHeight;

    vec3 upVector = normalize(p);
    float cos_theta = dot(upVector, dir);
    float r = length(p);

    vec2 uv = GetTransmittanceLutUv(bottomRadius, topRadius, cos_theta, r);
#ifdef ATMOSPHERE_EXPLICIT_LOD
    return textureLod(transmittanceLUT, uv, 0.0).rgb;
#else
    return texture(transmittanceLUT, uv).rgb;
#endif
}

// 读取多重散射查找表
#ifndef ATMOSPHERE_CORE_ONLY
vec3 GetMultiScattering(vec3 p, vec3 lightDir)
{
    float h = length(p) - parameters.planetRadius;
    vec3 sigma_s = RayleighCoefficient(h) + MieCoefficient(h); 
    
    float cosSunZenithAngle = dot(normalize(p), lightDir);
    vec2 uv = vec2(cosSunZenithAngle * 0.5 + 0.5, h / parameters.atmosphereHeight);
    vec3 G_ALL = texture(multiScatteringLUT, uv).rgb;
    
    return G_ALL * sigma_s;
}

// 计算天空颜色
vec3 GetSkyView(vec3 eyePos, vec3 viewDir, vec3 lightDir, vec4 lightColorIntensity, float maxDis)
{
    const int N_SAMPLE = 32;
    vec3 color = vec3(0, 0, 0);

    // 光线和大气层, 星球求交
    float dis = RayIntersectSphere(vec3(0,0,0), parameters.planetRadius + parameters.atmosphereHeight, eyePos, viewDir);
    float d = RayIntersectSphere(vec3(0,0,0), parameters.planetRadius, eyePos, viewDir);
    if(dis < 0) return color; 
    if(d > 0) dis = min(dis, d);
    if(maxDis > 0) dis = min(dis, maxDis);  // 带最长距离 maxDis 限制, 方便 aerial perspective lut 部分复用代码

    float ds = dis / float(N_SAMPLE);
    vec3 p = eyePos + (viewDir * ds) * 0.5;
    vec3 sunLuminance = lightColorIntensity.rgb * lightColorIntensity.w *
                        parameters.sunLightIntensityFactor;
    vec3 opticalDepth = vec3(0, 0, 0);

    for(int i=0; i<N_SAMPLE; i++)
    {
        // 积累沿途的湮灭系数
        float h = length(p) - parameters.planetRadius;
        vec3 extinction = RayleighCoefficient(h) + MieCoefficient(h) +  // scattering
                            OzoneAbsorption(h) + MieAbsorption(h);        // absorption
        opticalDepth += extinction * ds;

        vec3 t1 = TransmittanceToAtmosphere(p, lightDir);
        vec3 s  = Scattering(p, lightDir, viewDir);
        vec3 t2 = exp(-opticalDepth);
        
        // 单次散射
        vec3 inScattering = t1 * s * t2 * ds * sunLuminance;
        color += inScattering;

        // 多重散射
        vec3 multiScattering = GetMultiScattering(p, lightDir);
        color += multiScattering * t2 * ds * sunLuminance;

        p += viewDir * ds;
    }

    return color;
}
#endif

#endif
