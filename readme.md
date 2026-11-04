# vkEngine 

![](./docs/imgs/sponza.jpg)

![vkEngine editor showcase](./docs/imgs/editor.jpg)

vkEngine is a toy game engine currently in development. 
It uses [Vulkan](https://www.lunarg.com/vulkan-sdk/) as the rendering backend, integrates [JoltPhysics](https://github.com/jrouwe/JoltPhysics) for real-time physics simulation, and embeds [CoreCLR](https://github.com/dotnet/runtime) as the scripting runtime for C#. 

The project is primarily intended as a platform for experimenting with graphics and game development techniques. 

## Requirements 

- Windows 10/11 
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (with C++23 support)  
- [Miniconda/Anaconda](https://docs.conda.io/) 
- [VulkanSDK](https://www.lunarg.com/vulkan-sdk/) >= 1.4 
- [.NET SDK 9.0](https://dotnet.microsoft.com/) 

## Build

``` bash
git clone --recursive https://github.com/Al0ha0e/vkEngine.git
cd vkEngine
conda create -n vkengine python=3.12
conda activate vkengine
pip install -r requirements.txt
python init_project.py
scons
```

`SConstruct` will also:

- build `csharp/EngineCore`
- build the test gameplay assembly under `tests/csharp`
- run the reflection code generator for `GameConfig`
- compile the builtin shaders

`init_project.py` creates the local build/generated directories and generates the builtin LUT textures needed by the renderer.

## Run

Run from the repository root. The engine expects a single game config json:

```
./out/engine.exe ./tests/cfg/test_sponza.json
./out/engine.exe ./tests/cfg/test_jolt.json
./out/engine.exe ./tests/cfg/test_anim.json
./out/engine.exe ./tests/cfg/test_env.json
./out/engine.exe ./tests/cfg/test_render.json
./out/engine.exe ./tests/cfg/test_audio.json
./out/engine.exe ./tests/cfg/test_prefab.json
./out/engine.exe ./tests/cfg/test_ui.json
```

Some examples, including Sponza, require external assets not included in the repository.

Use the editor launcher to create or open a project:

```
python ./tools/editor_launcher.py
```

Or launch the editor with your project's config:

```
./out/editor.exe "path/to/project/editorconfig.json"
```

## Third Party Libraries 

- [glfw](https://github.com/glfw/glfw) 
- [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) 
- [stb_image](https://github.com/nothings/stb) 
- [assimp](https://github.com/assimp/assimp) 
- [tinygltf](https://github.com/syoyo/tinygltf)
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [nlohmann/json](https://github.com/nlohmann/json)
- [spdlog](https://github.com/gabime/spdlog)  
- [JoltPhysics](https://github.com/jrouwe/JoltPhysics) 
- [ozz-animation](https://github.com/guillaumeblanc/ozz-animation)
- [entt](https://github.com/skypjack/entt)
- [freetype](https://freetype.org/)
- [recastnavigation](https://github.com/recastnavigation/recastnavigation)
- [miniaudio](https://github.com/mackron/miniaudio)
- [imgui](https://github.com/ocornut/imgui)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
- [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog)
- [sqlite](https://sqlite.org/)
- [LunaSVG](https://github.com/sammycage/lunasvg) / [PlutoVG](https://github.com/sammycage/plutovg)


## Roadmap 

- **Frame Graph**:
  - [x] support for multiple queue families 
  - [x] transient resource allocation 
  - [x] CPU/GPU profiling
  - [ ] transient resource schedule optimize
- **Rendering**:
  - [x] cluster-based deferred rendering & PBR 
  - [x] forward transparent rendering 
  - [x] directional/spot/point lights
  - [x] IBL
  - [x] SSAO
  - [x] post-processing: bloom, tone mapping
  - [x] directional light CSM with PCF
  - [x] spot light shadows
  - [ ] point light shadows
- **UI**
  - [x] SDF Font
  - [x] UI transform: anchor, pivot, size, scale
  - [x] basic widgets: text, image, panel, button
  - [x] mouse input/events: hover, press, release, click
  - [x] text layout: font size, alignment, wrapping, measurement
  - [x] automatic layout and content sizing
  - [ ] nested clipping and hit test integration
- **Scene / ECS**:
  - [x] scene loading and saving
  - [x] prefab instantiation
  - [x] native component add/remove
  - [x] unified component lifecycle and system reset
- **Physics**:
  - [x] rigidbody
  - [x] collision detection
  - [x] spatial queries
  - [x] character controller
  - [ ] constraints
- **Animation**:
  - [x] animation sampling
  - [x] GPU skinning
  - [x] blending
  - [x] root motion
  - [ ] IK
- **Scripting**:
  - [x] CoreCLR integration
  - [x] script lifecycle
  - [x] C# metadata export and binary reader/writer generation
  - [x] exported field serialization
- **Audio**:
  - [x] audio source/listener
  - [x] 3D spatial audio: attenuation, rolloff, doppler
  - [ ] audio DSP filters
  - [ ] audio bus / effect chain
- **Editor**:
  - [x] editor layout
  - [x] asset database
  - [x] script field editing
  - [x] game state snapshot
  - [x] gizmo
  - [ ] assets preivew
