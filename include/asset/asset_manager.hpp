#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <asset/asset_db_json.hpp>
#include <filesystem>
#include <common.hpp>
#include <type_traits>
#include <unordered_set>
#include <array>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace vke_common
{
#define AM_GET_FUNC(tp)                          \
    static Result<tp *> Get##tp(AssetHandle id); \
    static Result<const tp *> Get##tp##ReadOnly(AssetHandle id);
#define AM_ITERATE_FUNC(tp) \
    static Result<void> Iterate##tp(std::function<void(const tp &)> op);

#define AM_OP_FUNCS(tp) \
    AM_GET_FUNC(tp)     \
    AM_ITERATE_FUNC(tp)

    class AssetManager
    {
    private:
        static AssetManager *instance;
        AssetManager()
            : builtinAssets(std::make_unique<AssetDBJSON>(Paths::Engine(BuiltinAssetLUTPath))) {};
        ~AssetManager()
        {
            stopWorkers();
            assetDB.reset();
            builtinAssets.reset();
        }
        AssetManager(const AssetManager &);
        AssetManager &operator=(const AssetManager);

    public:
        static AssetManager *GetInstance()
        {
            VKE_FATAL_IF(instance == nullptr, "AssetManager not initialized!")
            return instance;
        }

        static Result<AssetManager *> Init(std::unique_ptr<AssetDBBase> &&db);

        static void Dispose()
        {
            delete instance;
            instance = nullptr;
        }

        static Result<void> BulkLoad(const std::filesystem::path &pth, bool builtIn = false)
        {
            if (builtIn)
                return instance->builtinAssets->BulkLoad(pth);
            return instance->assetDB->BulkLoad(pth);
        }

        static AssetDBBase *GetAssetDB() { return instance->assetDB.get(); }
        static AssetDBBase *GetBuiltinAssets() { return instance->builtinAssets.get(); }

        AM_OP_FUNCS(TextureAsset)
        AM_OP_FUNCS(MeshAsset)
        AM_OP_FUNCS(VFShaderAsset)
        AM_OP_FUNCS(ComputeShaderAsset)
        AM_OP_FUNCS(MaterialAsset)
        AM_OP_FUNCS(SkeletonAsset)
        AM_OP_FUNCS(AnimationAsset)
        AM_OP_FUNCS(SceneAsset)
        AM_OP_FUNCS(FontAsset)
        AM_OP_FUNCS(AudioClipAsset)

        static Result<std::unique_ptr<vke_render::Texture2D>> LoadTexture2DUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_render::Mesh>> LoadMeshUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_render::ShaderModuleSet>> LoadVertFragShaderUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_render::ShaderModuleSet>> LoadComputeShaderUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_render::Material>> LoadMaterialUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_common::Skeleton>> LoadSkeletonUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_common::Animation>> LoadAnimationUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_common::Font>> LoadFontUnique(const AssetHandle hdl);
        static Result<std::unique_ptr<vke_audio::AudioClip>> LoadAudioClipUnique(const AssetHandle hdl);

        static Result<std::shared_ptr<vke_render::Texture2D>> LoadTexture2D(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_render::Mesh>> LoadMesh(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_render::ShaderModuleSet>> LoadVertFragShader(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_render::ShaderModuleSet>> LoadComputeShader(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_render::Material>> LoadMaterial(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_common::Skeleton>> LoadSkeleton(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_common::Animation>> LoadAnimation(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_common::Font>> LoadFont(const AssetHandle hdl);
        static Result<std::shared_ptr<vke_audio::AudioClip>> LoadAudioClip(const AssetHandle hdl);

        static Result<void> AsyncLoadTexture2D(AssetHandle handle);
        static Result<std::shared_ptr<vke_render::Texture2D>> WaitAsyncLoadTexture2D(AssetHandle handle);
        static Result<void> CancelAsyncLoadTexture2D(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadMesh(AssetHandle handle);
        static Result<std::shared_ptr<vke_render::Mesh>> WaitAsyncLoadMesh(AssetHandle handle);
        static Result<void> CancelAsyncLoadMesh(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadVertFragShader(AssetHandle handle);
        static Result<std::shared_ptr<vke_render::ShaderModuleSet>> WaitAsyncLoadVertFragShader(AssetHandle handle);
        static Result<void> CancelAsyncLoadVertFragShader(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadComputeShader(AssetHandle handle);
        static Result<std::shared_ptr<vke_render::ShaderModuleSet>> WaitAsyncLoadComputeShader(AssetHandle handle);
        static Result<void> CancelAsyncLoadComputeShader(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadMaterial(AssetHandle handle);
        static Result<std::shared_ptr<vke_render::Material>> WaitAsyncLoadMaterial(AssetHandle handle);
        static Result<void> CancelAsyncLoadMaterial(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadSkeleton(AssetHandle handle);
        static Result<std::shared_ptr<Skeleton>> WaitAsyncLoadSkeleton(AssetHandle handle);
        static Result<void> CancelAsyncLoadSkeleton(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadAnimation(AssetHandle handle);
        static Result<std::shared_ptr<Animation>> WaitAsyncLoadAnimation(AssetHandle handle);
        static Result<void> CancelAsyncLoadAnimation(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadFont(AssetHandle handle);
        static Result<std::shared_ptr<Font>> WaitAsyncLoadFont(AssetHandle handle);
        static Result<void> CancelAsyncLoadFont(AssetHandle handle, bool wait = false);
        static Result<void> AsyncLoadAudioClip(AssetHandle handle);
        static Result<std::shared_ptr<vke_audio::AudioClip>> WaitAsyncLoadAudioClip(AssetHandle handle);
        static Result<void> CancelAsyncLoadAudioClip(AssetHandle handle, bool wait = false);

        static Result<void> AsyncBatchLoad(LoadBatch &batch);
        static BatchResult WaitAsyncBatchLoad(const LoadBatch &batch);
        static Result<void> CancelAsyncBatchLoad(const LoadBatch &batch, bool wait = false);

        // Preflight checks may read database metadata but do not load runtime resources.
        static Result<void> ValidateTexture2D(AssetHandle handle);
        static Result<void> ValidateMesh(AssetHandle handle);
        static Result<void> ValidateVertFragShader(AssetHandle handle);
        static Result<void> ValidateComputeShader(AssetHandle handle);
        static Result<void> ValidateMaterial(AssetHandle handle);
        static Result<void> ValidateSkeleton(AssetHandle handle);
        static Result<void> ValidateAnimation(AssetHandle handle);
        static Result<void> ValidateFont(AssetHandle handle);
        static Result<void> ValidateAudioClip(AssetHandle handle);

        static Result<std::shared_ptr<const SceneData>> LoadSceneData(AssetHandle handle);
        static Result<std::shared_ptr<const SceneData>> ReloadSceneData(AssetHandle handle);
        static Result<void> InvalidateSceneData(AssetHandle handle);
        static Result<SceneData> LoadSceneFile(const std::filesystem::path &path);
        static Result<void> PrepareSceneData(SceneData &data);

    private:
        static Result<TextureAsset *> getTextureAsset(AssetHandle id);
        static Result<MeshAsset *> getMeshAsset(AssetHandle id);
        static Result<VFShaderAsset *> getVFShaderAsset(AssetHandle id);
        static Result<ComputeShaderAsset *> getComputeShaderAsset(AssetHandle id);
        static Result<MaterialAsset *> getMaterialAsset(AssetHandle id);
        static Result<SkeletonAsset *> getSkeletonAsset(AssetHandle id);
        static Result<AnimationAsset *> getAnimationAsset(AssetHandle id);
        static Result<SceneAsset *> getSceneAsset(AssetHandle id);
        static Result<FontAsset *> getFontAsset(AssetHandle id);
        static Result<AudioClipAsset *> getAudioClipAsset(AssetHandle id);

        template <typename A>
        static Result<A *> getAssetForLoad(Result<A *> found)
        {
            if (!found)
                return std::unexpected(found.error());
            if (*found && (*found)->loadTask)
                if (auto loaded = waitAsyncLoad(**found); !loaded)
                    return std::unexpected(loaded.error());
            return found;
        }

        using LoadJob = std::move_only_function<void()>;
        std::array<std::deque<LoadJob>, ASSET_CNT_FLAG> loadQueues;
        std::mutex loadMutex;
        std::condition_variable loadReady;
        std::vector<std::thread> workers;
        size_t queuedLoads = 0;
        size_t nextLoadQueue = 0;
        bool stopping = false;

        void enqueueLoad(AssetType type, LoadJob job);
        void runWorker();
        void stopWorkers();
        static Result<std::vector<MaterialAsset *>> expandLoadBatch(LoadBatch &batch);
        static void asyncLoadMaterial(MaterialAsset &asset);
        static Result<void> cancelBatchEntry(AssetType type, AssetHandle handle, bool wait);

        template <typename A, typename Load>
        static Result<decltype(A::val)> loadFromCacheOrUpdate(A &asset, Load &&load);

        template <typename A, typename Prepare>
        static Result<void> asyncLoad(A &asset, Prepare prepare)
        {
            if (asset.val)
                return {};
            if (asset.loadState == AssetLoadState::Loading)
            {
                ++asset.loadReferences;
                return {};
            }
            if (asset.loadState == AssetLoadState::Cancelled)
            {
                asset.loadState = AssetLoadState::Loading;
                asset.loadReferences = 1;
                return {};
            }
            using Task = typename decltype(asset.loadTask)::element_type;
            auto task = std::make_shared<Task>();
            asset.loadTask = task;
            asset.loadState = AssetLoadState::Loading;
            asset.loadReferences = 1;
            instance->enqueueLoad(A::type, [task, prepare = std::move(prepare)]() mutable
                                  {
                task->result = prepare();
                task->complete.store(true, std::memory_order_release);
                task->complete.notify_all(); });
            return {};
        }

        static Result<std::shared_ptr<vke_render::Material>> waitAsyncLoad(MaterialAsset &asset);

        template <typename A>
        static Result<decltype(A::val)> waitAsyncLoad(A &asset)
        {
            if (asset.val)
                return asset.val;
            if (!asset.loadTask)
                return std::unexpected("asset has no pending load");
            asset.loadTask->Wait();
            if (asset.loadState == AssetLoadState::Cancelled)
            {
                asset.loadTask.reset();
                asset.loadState = AssetLoadState::Unloaded;
                return std::unexpected("asset load was cancelled");
            }
            auto &prepared = asset.loadTask->result;
            asset.loadReferences = 0;
            if (prepared)
            {
                auto loaded = (*prepared)();
                if (loaded)
                {
                    asset.val = std::move(*loaded);
                    asset.loadTask.reset();
                    asset.loadState = AssetLoadState::Loaded;
                    return asset.val;
                }
                prepared = std::unexpected(loaded.error());
            }
            asset.loadState = AssetLoadState::Failed;
            return std::unexpected(prepared.error());
        }

        template <typename A>
        static void cancelAsyncLoad(A &asset, bool wait)
        {
            if (asset.loadState == AssetLoadState::Loading && --asset.loadReferences == 0)
                asset.loadState = AssetLoadState::Cancelled;
            if (wait && asset.loadState == AssetLoadState::Cancelled)
            {
                asset.loadTask->Wait();
                asset.loadTask.reset();
                asset.loadState = AssetLoadState::Unloaded;
            }
        }

        static std::filesystem::path resolveAssetPath(AssetHandle handle, const std::filesystem::path &path);
        std::unique_ptr<AssetDBJSON> builtinAssets;
        std::unique_ptr<AssetDBBase> assetDB;
        std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> sceneDependencies;
        std::unordered_map<AssetHandle, std::unordered_set<AssetHandle>> sceneDependents;

        Result<std::shared_ptr<const SceneData>> loadSceneData(
            AssetHandle handle, std::vector<AssetHandle> &stack);
        Result<void> expandSceneData(SceneData &data, std::vector<AssetHandle> &stack);
        Result<void> resolveSceneAssets(SceneData &data);

        Result<void> checkPath(AssetHandle handle, const std::filesystem::path &path) const;

        template <typename Get>
        static Result<void> validateAsset(AssetHandle handle, Get get)
        {
            if (!instance)
                return std::unexpected("AssetManager is not initialized");
            if (handle == 0)
                return std::unexpected("required asset is empty");
            auto asset = get(handle);
            if (!asset)
                return std::unexpected(asset.error());
            if (auto result = instance->checkAsset(*asset); !result)
                return std::unexpected("asset " + std::to_string(handle) + ": " + result.error());
            return {};
        }

        template <typename Asset>
        Result<void> checkAsset(const Asset *asset) const
        {
            if (!asset)
                return std::unexpected("asset not found");
            if (asset->val)
                return {};
            if constexpr (std::is_same_v<Asset, MeshAsset>)
                if (asset->id == BUILTIN_MESH_PLANE_ID)
                    return {};
            return checkPath(asset->id, asset->path);
        }

        Result<void> checkAsset(const MaterialAsset *material) const;
        Result<void> checkAsset(const VFShaderAsset *shader) const;

        Result<void> initBuiltinAssets();
    };
}

#undef AM_GET_FUNC
#undef AM_ITERATE_FUNC
#undef AM_OP_FUNCS

#endif
