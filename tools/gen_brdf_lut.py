import math
import os
import struct
import sys


def radical_inverse_vdc(bits):
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)) & 0xFFFFFFFF
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)) & 0xFFFFFFFF
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)) & 0xFFFFFFFF
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)) & 0xFFFFFFFF
    return bits * 2.3283064365386963e-10


def hammersley(i, n):
    return i / float(n), radical_inverse_vdc(i)


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    if length <= 0.0:
        return (0.0, 0.0, 0.0)
    return tuple(c / length for c in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def importance_sample_ggx(xi, roughness, n):
    a = roughness * roughness
    phi = 2.0 * math.pi * xi[0]
    cos_theta = math.sqrt((1.0 - xi[1]) / (1.0 + (a * a - 1.0) * xi[1]))
    sin_theta = math.sqrt(max(0.0, 1.0 - cos_theta * cos_theta))

    h = (
        math.cos(phi) * sin_theta,
        math.sin(phi) * sin_theta,
        cos_theta,
    )

    up = (0.0, 0.0, 1.0) if abs(n[2]) < 0.999 else (1.0, 0.0, 0.0)
    tangent = normalize((
        up[1] * n[2] - up[2] * n[1],
        up[2] * n[0] - up[0] * n[2],
        up[0] * n[1] - up[1] * n[0],
    ))
    bitangent = (
        n[1] * tangent[2] - n[2] * tangent[1],
        n[2] * tangent[0] - n[0] * tangent[2],
        n[0] * tangent[1] - n[1] * tangent[0],
    )

    sample = (
        tangent[0] * h[0] + bitangent[0] * h[1] + n[0] * h[2],
        tangent[1] * h[0] + bitangent[1] * h[1] + n[1] * h[2],
        tangent[2] * h[0] + bitangent[2] * h[1] + n[2] * h[2],
    )
    return normalize(sample)


def geometry_schlick_ggx(n_dot_v, roughness):
    a = roughness
    k = (a * a) / 2.0
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, 1e-6)


def geometry_smith(n, v, l, roughness):
    return geometry_schlick_ggx(max(dot(n, v), 0.0), roughness) * geometry_schlick_ggx(max(dot(n, l), 0.0), roughness)


def integrate_brdf(n_dot_v, roughness, sample_count):
    v = (math.sqrt(max(0.0, 1.0 - n_dot_v * n_dot_v)), 0.0, n_dot_v)
    n = (0.0, 0.0, 1.0)
    a = 0.0
    b = 0.0

    for i in range(sample_count):
        xi = hammersley(i, sample_count)
        h = importance_sample_ggx(xi, roughness, n)
        v_dot_h = max(dot(v, h), 0.0)
        l = normalize((
            2.0 * v_dot_h * h[0] - v[0],
            2.0 * v_dot_h * h[1] - v[1],
            2.0 * v_dot_h * h[2] - v[2],
        ))

        n_dot_l = max(l[2], 0.0)
        n_dot_h = max(h[2], 0.0)

        if n_dot_l > 0.0:
            g = geometry_smith(n, v, l, roughness)
            g_vis = (g * v_dot_h) / max(n_dot_h * n_dot_v, 1e-6)
            fc = pow(1.0 - v_dot_h, 5.0)
            a += (1.0 - fc) * g_vis
            b += fc * g_vis

    inv_count = 1.0 / float(sample_count)
    return a * inv_count, b * inv_count


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "./builtin_assets/texture"
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 256
    sample_count = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
    os.makedirs(out_dir, exist_ok=True)

    pixels = bytearray(size * size * 8)
    for y in range(size):
        roughness = (y + 0.5) / size
        for x in range(size):
            n_dot_v = (x + 0.5) / size
            a, b = integrate_brdf(n_dot_v, roughness, sample_count)
            idx = (y * size + x) * 8
            struct.pack_into("<4H", pixels, idx,
                             max(0, min(65535, round(a * 65535.0))),
                             max(0, min(65535, round(b * 65535.0))), 0, 65535)

    texture_path = os.path.join(out_dir, "brdf_lut.texture")
    with open(texture_path, "wb") as output:
        output.write(struct.pack("<4s5I", b"VKET", 1, size, size, 1, 91))
        output.write(pixels)


if __name__ == "__main__":
    main()
