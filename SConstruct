import os
import shutil

# env = Environment(tools=['mingw'])


def GetFileWithExt(dir, ext):
    ret = []
    for _, dirs, files in os.walk(dir):
        for filename in files:
            _, suf = os.path.splitext(filename)
            if suf == ext:
                ret.append(filename)
    return ret


dlls = GetFileWithExt("./libs", ".dll")
for filename in dlls:
    shutil.copy("./libs/" + filename, "./out/" + filename)

selflibs = GetFileWithExt("./libs", ".lib")

print(dlls)
print(selflibs)

DEBUG = False
ENABLE_FRAME_GRAPH_PROFILING = False

######################### CodeGen ######################

files_need_reflect = ["game_config.hpp"]
for filename in files_need_reflect:
    cmd = f"python ./tools/codegen/reflect.py ./include/{filename}"
    print(cmd)
    os.system(cmd)

generated_files = Glob("./src/generated/*.cpp")

######################### JOLT #########################
USE_AVX512 = False
USE_AVX2 = True
USE_AVX = True
USE_FMADD = False
CROSS_PLATFORM_DETERMINISTIC = True
OBJECT_LAYER_BITS = 32

jolt_ccflags = []

jolt_cppdefines = [
    # "JPH_DISABLE_CUSTOM_ALLOCATOR",
    # "JPH_ENABLE_ASSERTS",
    "JPH_DOUBLE_PRECISION",
    # "JPH_USE_STD_VECTOR",
    # "JPH_TRACK_BROADPHASE_STATS",
    # "JPH_TRACK_NARROWPHASE_STATS",
    "JPH_OBJECT_STREAM",
    "JPH_USE_SSE4_1",
    "JPH_USE_SSE4_2",
    "JPH_USE_LZCNT",
    "JPH_USE_TZCNT",
    "JPH_USE_F16C",
]

if DEBUG:
    jolt_cppdefines.append("_DEBUG")
else:
    jolt_cppdefines.append("NDEBUG")

if OBJECT_LAYER_BITS > 0:
    jolt_cppdefines.append(f"JPH_OBJECT_LAYER_BITS={OBJECT_LAYER_BITS}")

if CROSS_PLATFORM_DETERMINISTIC:
    jolt_cppdefines.append("JPH_CROSS_PLATFORM_DETERMINISTIC")
elif USE_FMADD:
    jolt_cppdefines.append("JPH_USE_FMADD")


if USE_AVX512:
    jolt_ccflags.append("/arch:AVX512")
elif USE_AVX2:
    jolt_ccflags.append("/arch:AVX2")
elif USE_AVX:
    jolt_ccflags.append("/arch:AVX")

if USE_AVX512:
    jolt_cppdefines.append("JPH_USE_AVX512")
if USE_AVX2:
    jolt_cppdefines.append("JPH_USE_AVX2")
if USE_AVX:
    jolt_cppdefines.append("JPH_USE_AVX")

#########################

env = Environment(
    CC="cl",
    CXX="cl",
    CCFLAGS=[
        "/std:c++23preview",
        "/EHs-",
        "/O2",
        "/utf-8",
        "/MDd" if DEBUG else "/MD",
    ]
    + jolt_ccflags,
)

SConscript(["csharp/Sconscript", "tests/csharp/Sconscript"])
joltObjs = SConscript(
    ["third_party/Jolt/Sconscript"], exports=["env", "jolt_cppdefines"]
)
ozzObjs = SConscript(["third_party/ozz/Sconscript"], exports=["env"])
freetypeObjs = SConscript(["third_party/freetype/Sconscript"], exports=["env"])
recastObjs = SConscript(["third_party/recast/Sconscript"], exports=["env"])
sqliteObjs = SConscript(["third_party/sqlite/Sconscript"], exports=["env"])


libs = ["Gdi32", "shell32", "user32", "vulkan-1"] + selflibs
VULKAN_PATH = "D:/VulkanSDK"
libpath = ["./libs", VULKAN_PATH + "/Lib"]
cpppath = [
    "./include",
    VULKAN_PATH + "/Include",
    "./third_party/spirv_reflect",
    "./third_party/",
    "./third_party/imgui",
    "./third_party/imgui/backends",
]
cpppath.append("./third_party/freetype/include")
cppdefines = ["JSON_NOEXCEPTION", "SPDLOG_NO_EXCEPTIONS"]
if ENABLE_FRAME_GRAPH_PROFILING:
    cppdefines.append("VKE_ENABLE_FRAME_GRAPH_PROFILING")
if DEBUG:
    cppdefines.append("VKE_DEBUG")
else:
    cppdefines.append("NDEBUG")
commonsrc = (
    joltObjs
    + ozzObjs
    + freetypeObjs
    + recastObjs
    + sqliteObjs
    + [
        "./src/render/environment.cpp",
        "./src/asset/asset.cpp",
        "./src/asset/loader.cpp",
        "./src/asset/scene_loader.cpp",
        "./src/asset/builtin.cpp",
        "./src/asset/asset_db_json.cpp",
        "./src/asset/asset_db_sqlite.cpp",
        "./src/render/descriptor.cpp",
        "./src/render/vulkan_resource_manager.cpp",
        "./src/render/skybox.cpp",
        "./src/render/skybox_render.cpp",
        "./src/render/gbuffer_pass.cpp",
        "./src/render/shadow_manager.cpp",
        "./src/render/shadow_pass.cpp",
        "./src/render/ssao.cpp",
        "./src/render/deferred_lighting.cpp",
        "./src/render/atmosphere_pass.cpp",
        "./src/render/transparent_pass.cpp",
        "./src/render/bloom.cpp",
        "./src/render/tone_mapping.cpp",
        "./src/render/shader.cpp",
        "./src/render/pipeline.cpp",
        "./src/render/light_manager.cpp",
        "./src/render/layered_2d.cpp",
        "./src/render/render.cpp",
        "./src/render/frame_graph.cpp",
        "./src/render/profile.cpp",
        "./src/render/queue.cpp",
        "./src/editor/wireframe_collision_pass.cpp",
        "./src/component.cpp",
        "./src/scene.cpp",
        "./src/scene_data.cpp",
        "./src/ui/ui.cpp",
        "./src/ui/canvas.cpp",
        "./src/ui/text.cpp",
        "./src/ui/ui_manager.cpp",
        "./src/scene_transform_system.cpp",
        "./src/event.cpp",
        "./src/engine.cpp",
        "./src/engine_state.cpp",
        "./src/input.cpp",
        "./src/time.cpp",
        "./src/logger.cpp",
        "./src/reflect/type_info.cpp",
        "./src/reflect/type_info_json.cpp",
        "./src/reflect/value_view.cpp",
        "./src/reflect/value_editor.cpp",
        "./src/physics/physics_config.cpp",
        "./src/physics/physics.cpp",
        "./src/script.cpp",
        "./third_party/spirv_reflect/spirv_reflect.cpp",
        "./third_party/vma/vma.cpp",
        "./third_party/stb/stb_image.cpp",
        "./third_party/tinygltf/tiny_gltf.cpp",
        "./third_party/miniaudio/miniaudio.c",
        "./src/interop/native.cpp",
        "./src/interop/light.cpp",
        "./src/interop/ui.cpp",
        "./src/interop/physics.cpp",
        "./src/audio/audio_manager.cpp",
        "./src/interop/audio.cpp",
    ]
    + generated_files
)

### coreclr
NETHOST_PATH = "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/9.0.8/runtimes/win-x64/native/"
libpath.append(NETHOST_PATH)
libs.append("nethost")


### tools

toolCommonSrc = ["./src/logger.cpp"]

targetinfo = [
    [
        "tools/gltf_conv",
        [
            "./third_party/stb/stb_image.cpp",
            "./third_party/tinygltf/tiny_gltf.cpp",
            "./src/tools/gltf_conv.cpp",
        ],
    ],
    [
        "tools/gltf_anim_conv",
        [
            "./third_party/stb/stb_image.cpp",
            "./third_party/tinygltf/tiny_gltf.cpp",
            "./src/tools/gltf_anim_conv.cpp",
        ]
        + ozzObjs,
    ],
    [
        "tools/gltf_scene_conv",
        [
            "./third_party/stb/stb_image.cpp",
            "./third_party/tinygltf/tiny_gltf.cpp",
            "./src/tools/gltf_scene_conv.cpp",
        ],
    ],
    [
        "tools/gltf_log",
        [
            "./third_party/stb/stb_image.cpp",
            "./third_party/tinygltf/tiny_gltf.cpp",
            "./src/tools/gltf_log.cpp",
        ],
    ],
    ["tools/obj_conv", ["./src/tools/obj_conv.cpp"]],
]

for info in targetinfo:
    env.Program(
        info[0],
        toolCommonSrc + info[1],
        LIBS=libs,
        LIBPATH=libpath,
        CPPPATH=cpppath,
        CPPDEFINES=cppdefines + jolt_cppdefines,
    )

imguiObjs = SConscript(["third_party/imgui/Sconscript"], exports=["env", "VULKAN_PATH"])
editorsrc = [
    "./src/editor/editor.cpp",
    "./src/editor/camera.cpp",
    "./src/editor/render.cpp",
    "./src/editor/assets.cpp",
    "./src/editor/logger.cpp",
    "./src/editor/asset_tree.cpp",
    "./src/editor/asset_import.cpp",
    "./src/editor/component/camera.cpp",
    "./src/editor/component/light.cpp",
    "./src/editor/component/renderable_object.cpp",
    "./src/editor/component/skeleton_animator.cpp",
    "./src/editor/component/rigidbody.cpp",
    "./src/editor/component/sensor.cpp",
    "./src/editor/component/canvas.cpp",
    "./src/editor/component/audio.cpp",
    "./src/editor/component/script.cpp",
]

### tests

targetinfo = [
    ["out/test_spvrefl", ["./tests/test_spvrefl.cpp"]],
    ["out/engine", ["./src/main.cpp"]],
    ["out/editor", ["./src/editor_main.cpp"] + editorsrc + imguiObjs],
]

for info in targetinfo:
    env.Program(
        info[0],
        info[1] + commonsrc,
        LIBS=libs,
        LIBPATH=libpath,
        CPPPATH=cpppath,
        CPPDEFINES=cppdefines + jolt_cppdefines,
    )


env.Library(
    "out/vkengine",
    commonsrc,
    LIBS=libs,
    LIBPATH=libpath,
    CPPPATH=cpppath,
    CPPDEFINES=cppdefines + jolt_cppdefines,
)

### shader

glslc = "glslc --target-env=vulkan1.4"

sprefix = "./builtin_assets/shader/"
shaders = [
    ["default.frag", "default_frag.spv"],
    ["default.vert", "default_vert.spv"],
    ["default_skin.vert", "default_skin_vert.spv"],
    ["default_multi.frag", "default_multi_frag.spv"],
    ["default_multi.vert", "default_multi_vert.spv"],
    ["deferred_lighting.frag", "deferred_lighting_frag.spv"],
    ["deferred_lighting.vert", "deferred_lighting_vert.spv"],
    ["fullscreen_triangle.vert", "fullscreen_triangle_vert.spv"],
    ["ssao.frag", "ssao_frag.spv"],
    ["ssao_blur.frag", "ssao_blur_frag.spv"],
    ["atmosphere_post.frag", "atmosphere_post_frag.spv"],
    ["bloom.frag", "bloom_frag.spv"],
    ["tone_mapping.frag", "tone_mapping_frag.spv"],
    ["text.frag", "text_frag.spv"],
    ["text.vert", "text_vert.spv"],
    ["ui_image.vert", "ui_image_vert.spv"],
    ["ui_image.frag", "ui_image_frag.spv"],
    ["shadow.frag", "shadow_frag.spv"],
    ["shadow.vert", "shadow_vert.spv"],
    ["shadow_skin.vert", "shadow_skin_vert.spv"],
    ["skybox.frag", "skyfrag.spv"],
    ["skybox.vert", "skyvert.spv"],
    ["sky_lut.comp", "sky_lut.spv"],
    ["atmosphere_lut.comp", "atmosphere_lut.spv"],
    ["ibl_lut.comp", "ibl_lut.spv"],
    ["light_cull.comp", "light_cull.spv"],
]

for s in shaders:
    print(glslc + f" {sprefix+s[0]} -I {sprefix} -o {sprefix+s[1]}")
    os.system(glslc + f" {sprefix+s[0]} -I {sprefix} -o {sprefix+s[1]}")

sprefix = "./editor_assets/shader/"
shaders = [
    ["wireframe_collision.vert", "wireframe_collision_vert.spv"],
    ["wireframe_collision.frag", "wireframe_collision_frag.spv"],
]
for s in shaders:
    print(glslc + f" {sprefix+s[0]} -I {sprefix} -o {sprefix+s[1]}")
    os.system(glslc + f" {sprefix+s[0]} -I {sprefix} -o {sprefix+s[1]}")

sprefix = "./tests/shader/"
shaders = [
    ["material_constant.vert", "material_constant_vert.spv"],
    ["material_constant.frag", "material_constant_frag.spv"],
    ["transparent_pbr_constant.vert", "transparent_pbr_constant_vert.spv"],
    ["transparent_pbr_constant.frag", "transparent_pbr_constant_frag.spv"],
]
for s in shaders:
    print(glslc + f" {sprefix+s[0]} -I {sprefix} -I ./builtin_assets/shader/ -o {sprefix+s[1]}")
    os.system(glslc + f" {sprefix+s[0]} -I {sprefix} -I ./builtin_assets/shader/ -o {sprefix+s[1]}")
