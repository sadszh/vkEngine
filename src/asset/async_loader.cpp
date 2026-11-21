#include <asset/asset_manager.hpp>
#include <asset/loader.hpp>

namespace vke_common
{
    void AssetLoadTaskState::WaitForLoad() const
    {
        cpuComplete.wait(false, std::memory_order_acquire);
        if (timelineValue != 0)
            AssetManager::instance->uploadTimeline.Wait(timelineValue);
    }

    Result<void> AssetManager::AsyncLoadTexture2D(AssetHandle handle)
    {
        auto found = getTextureAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("Texture2D " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path), handle](AssetUploadContext &upload)
                         { return loadTexture2D(handle, *asset, path, &upload); });
    }

    Result<std::shared_ptr<vke_render::Texture2D>> AssetManager::WaitAsyncLoadTexture2D(AssetHandle handle)
    {
        auto found = getTextureAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Texture2D " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadTexture2D(AssetHandle handle, bool wait)
    {
        auto found = getTextureAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Texture2D " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadMesh(AssetHandle handle)
    {
        auto found = getMeshAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("Mesh " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [path = resolveAssetPath(handle, asset->path), handle](AssetUploadContext &upload)
                         { return loadMesh(handle, path, &upload); });
    }

    Result<std::shared_ptr<vke_render::Mesh>> AssetManager::WaitAsyncLoadMesh(AssetHandle handle)
    {
        auto found = getMeshAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Mesh " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadMesh(AssetHandle handle, bool wait)
    {
        auto found = getMeshAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Mesh " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadVertFragShader(AssetHandle handle)
    {
        auto found = getVFShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("VertFragShader " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [path = resolveAssetPath(handle, asset->path), fragmentPath = resolveAssetPath(handle, asset->fragPath)](AssetUploadContext &)
                         { return loadVertFragShader(path, fragmentPath); });
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::WaitAsyncLoadVertFragShader(AssetHandle handle)
    {
        auto found = getVFShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("VertFragShader " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadVertFragShader(AssetHandle handle, bool wait)
    {
        auto found = getVFShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("VertFragShader " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadComputeShader(AssetHandle handle)
    {
        auto found = getComputeShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("ComputeShader " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [path = resolveAssetPath(handle, asset->path)](AssetUploadContext &)
                         { return loadComputeShader(path); });
    }

    Result<std::shared_ptr<vke_render::ShaderModuleSet>> AssetManager::WaitAsyncLoadComputeShader(AssetHandle handle)
    {
        auto found = getComputeShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("ComputeShader " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadComputeShader(AssetHandle handle, bool wait)
    {
        auto found = getComputeShaderAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("ComputeShader " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadMaterial(AssetHandle handle)
    {
        LoadBatch batch{{ASSET_MATERIAL, {handle}}};
        return AsyncBatchLoad(batch);
    }

    Result<std::shared_ptr<vke_render::Material>> AssetManager::WaitAsyncLoadMaterial(AssetHandle handle)
    {
        auto found = getMaterialAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Material " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadMaterial(AssetHandle handle, bool wait)
    {
        auto found = getMaterialAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("material not found");
        if ((*found)->loadState != AssetLoadState::Loading)
        {
            if (wait && (*found)->loadState == AssetLoadState::Cancelled)
                (void)WaitAsyncLoadMaterial(handle);
            return {};
        }
        LoadBatch batch{{ASSET_MATERIAL, {handle}}};
        if (auto result = expandLoadBatch(batch); !result)
            return std::unexpected(result.error());
        return CancelAsyncBatchLoad(batch, wait);
    }

    Result<std::shared_ptr<vke_render::Material>> AssetManager::waitAsyncLoad(MaterialAsset &asset)
    {
        if (asset.loadState == AssetLoadState::Cancelled)
        {
            auto finishCancelled = [](auto found)
            {
                if (found && *found && (*found)->loadState == AssetLoadState::Cancelled)
                    cancelAsyncLoad(**found, true);
            };
            for (auto texture : asset.textures)
                finishCancelled(getTextureAsset(texture));
            finishCancelled(getVFShaderAsset(asset.shader));
        }
        if (asset.loadState == AssetLoadState::Loading)
        {
            auto material = std::make_shared<vke_render::Material>(asset.id);
            std::string error;
            for (auto handle : asset.textures)
            {
                auto texture = WaitAsyncLoadTexture2D(handle);
                if (texture)
                    material->textures.push_back(std::move(*texture));
                else if (error.empty())
                    error = texture.error();
            }
            auto shader = WaitAsyncLoadVertFragShader(asset.shader);
            if (shader)
                material->shader = std::move(*shader);
            else if (error.empty())
                error = shader.error();
            if (!error.empty())
                asset.loadTask->result = std::unexpected("material " + std::to_string(asset.id) + ": " + error);
            else
            {
                material->renderMode = asset.renderMode;
                material->blendMode = asset.blendMode;
                material->textureBindingInfos = asset.textureBindingInfos;
                material->pushConstantInfos = asset.pushConstantInfos;
                material->pushConstantData = asset.pushConstantData;
                asset.loadTask->result = std::move(material);
            }
        }
        return waitAsyncLoad<MaterialAsset>(asset);
    }

    Result<void> AssetManager::AsyncLoadSkeleton(AssetHandle handle)
    {
        auto found = getSkeletonAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("Skeleton " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path)](AssetUploadContext &)
                         { return loadSkeleton(*asset, path); });
    }

    Result<std::shared_ptr<Skeleton>> AssetManager::WaitAsyncLoadSkeleton(AssetHandle handle)
    {
        auto found = getSkeletonAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Skeleton " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadSkeleton(AssetHandle handle, bool wait)
    {
        auto found = getSkeletonAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Skeleton " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadAnimation(AssetHandle handle)
    {
        auto found = getAnimationAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("Animation " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path)](AssetUploadContext &)
                         { return loadAnimation(*asset, path); });
    }

    Result<std::shared_ptr<Animation>> AssetManager::WaitAsyncLoadAnimation(AssetHandle handle)
    {
        auto found = getAnimationAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Animation " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadAnimation(AssetHandle handle, bool wait)
    {
        auto found = getAnimationAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Animation " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadFont(AssetHandle handle)
    {
        auto found = getFontAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("Font " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path)](AssetUploadContext &upload)
                         { return loadFont(*asset, path, &upload); });
    }

    Result<std::shared_ptr<Font>> AssetManager::WaitAsyncLoadFont(AssetHandle handle)
    {
        auto found = getFontAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Font " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadFont(AssetHandle handle, bool wait)
    {
        auto found = getFontAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("Font " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadAudioClip(AssetHandle handle)
    {
        auto found = getAudioClipAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("AudioClip " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path)](AssetUploadContext &)
                         { return loadAudioClip(*asset, path); });
    }

    Result<std::shared_ptr<vke_audio::AudioClip>> AssetManager::WaitAsyncLoadAudioClip(AssetHandle handle)
    {
        auto found = getAudioClipAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("AudioClip " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadAudioClip(AssetHandle handle, bool wait)
    {
        auto found = getAudioClipAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("AudioClip " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadNavAgentConfig(AssetHandle handle)
    {
        auto found = getNavAgentConfigAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("NavAgentConfig " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset](AssetUploadContext &)
                         { return loadNavAgentConfig(*asset); });
    }

    Result<std::shared_ptr<vke_navmesh::NavAgentConfig>> AssetManager::WaitAsyncLoadNavAgentConfig(AssetHandle handle)
    {
        auto found = getNavAgentConfigAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("NavAgentConfig " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadNavAgentConfig(AssetHandle handle, bool wait)
    {
        auto found = getNavAgentConfigAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("NavAgentConfig " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    Result<void> AssetManager::AsyncLoadNavMeshTiles(AssetHandle handle)
    {
        auto found = getNavMeshTilesAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        auto *asset = *found;
        if (!asset)
            return std::unexpected("NavMesh " + std::to_string(handle) + ": asset not found");
        return asyncLoad(*asset, [asset, path = resolveAssetPath(handle, asset->path)](AssetUploadContext &)
                         { return loadNavMeshTiles(*asset, path); });
    }

    Result<std::shared_ptr<vke_navmesh::NavMeshTilesData>> AssetManager::WaitAsyncLoadNavMeshTiles(AssetHandle handle)
    {
        auto found = getNavMeshTilesAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("NavMesh " + std::to_string(handle) + ": asset not found");
        return waitAsyncLoad(**found);
    }

    Result<void> AssetManager::CancelAsyncLoadNavMeshTiles(AssetHandle handle, bool wait)
    {
        auto found = getNavMeshTilesAsset(handle);
        if (!found)
            return std::unexpected(found.error());
        if (!*found)
            return std::unexpected("NavMesh " + std::to_string(handle) + ": asset not found");
        cancelAsyncLoad(**found, wait);
        return {};
    }

    void AssetManager::enqueueLoad(AssetType type, LoadJob job)
    {
        if (workers.empty())
        {
            const auto count = std::max(1u, std::thread::hardware_concurrency());
            for (unsigned i = 0; i < count; ++i)
                workers.emplace_back(&AssetManager::runWorker, this);
        }
        {
            std::lock_guard lock(loadMutex);
            loadQueues[type].push_back(std::move(job));
            ++queuedLoads;
        }
        loadReady.notify_one();
    }

    AssetManager::LoadJob AssetManager::takeLoadJob()
    {
        while (loadQueues[nextLoadQueue].empty())
            nextLoadQueue = (nextLoadQueue + 1) % ASSET_CNT_FLAG;
        auto &queue = loadQueues[nextLoadQueue];
        LoadJob job = std::move(queue.front());
        queue.pop_front();
        --queuedLoads;
        nextLoadQueue = (nextLoadQueue + 1) % ASSET_CNT_FLAG;
        return job;
    }

    void AssetManager::runWorker()
    {
        AssetUploadContext upload(uploadTimeline);
        for (;;)
        {
            LoadJob job;
            {
                std::unique_lock lock(loadMutex);
                if (queuedLoads == 0 && !stopping)
                {
                    lock.unlock();
                    upload.Drain();
                    lock.lock();
                    loadReady.wait(lock, [this]
                                   { return stopping || queuedLoads != 0; });
                }
                if (queuedLoads == 0)
                    return;
                job = takeLoadJob();
            }
            std::array<std::shared_ptr<AssetLoadTaskState>, MaxJobs> uploadTasks;
            size_t uploadCount = 0;

            for (size_t i = 0; i < MaxJobs; ++i)
            {
                const auto stagedBytes = upload.GetStagedBytes();
                job.execute(upload);
                if (upload.GetStagedBytes() != stagedBytes)
                    uploadTasks[uploadCount++] = std::move(job.task);
                else
                    job.task->Complete();

                if (i + 1 == MaxJobs || upload.GetStagedBytes() >= TargetUploadBytes)
                    break;
                std::lock_guard lock(loadMutex);
                if (queuedLoads == 0)
                    break;
                job = takeLoadJob();
            }

            const auto value = upload.Submit();
            for (size_t i = 0; i < uploadCount; ++i)
                uploadTasks[i]->Complete(value);
        }
    }

    void AssetManager::stopWorkers()
    {
        {
            std::lock_guard lock(loadMutex);
            stopping = true;
        }
        loadReady.notify_all();
        for (auto &worker : workers)
            worker.join();
        workers.clear();
    }

    Result<std::vector<MaterialAsset *>> AssetManager::expandLoadBatch(LoadBatch &batch)
    {
        std::vector<MaterialAsset *> pendingMaterials;
        for (const auto &[type, handles] : batch)
            if (type < ASSET_TEXTURE || type >= ASSET_CNT_FLAG || type == ASSET_SCENE)
                return std::unexpected("unsupported resource type in load batch");
        auto materials = batch.find(ASSET_MATERIAL);
        if (materials == batch.end())
            return pendingMaterials;
        const auto &handles = materials->second;
        for (auto handle : handles)
        {
            auto found = getMaterialAsset(handle);
            if (!found)
                return std::unexpected(found.error());
            auto *asset = *found;
            if (!asset)
                return std::unexpected("material " + std::to_string(handle) + ": asset not found");
            if (asset->val)
                continue;
            pendingMaterials.push_back(asset);
            batch[ASSET_TEXTURE].insert(asset->textures.begin(), asset->textures.end());
            batch[ASSET_VF_SHADER].insert(asset->shader);
        }
        return pendingMaterials;
    }

    void AssetManager::asyncLoadMaterial(MaterialAsset &asset)
    {
        if (asset.loadState == AssetLoadState::Loading)
        {
            ++asset.loadReferences;
            return;
        }
        if (asset.loadState == AssetLoadState::Cancelled)
        {
            asset.loadState = AssetLoadState::Loading;
            asset.loadReferences = 1;
            return;
        }
        auto task = std::make_shared<AssetLoadTask<vke_render::Material>>();
        task->Complete();
        asset.loadTask = std::move(task);
        asset.loadState = AssetLoadState::Loading;
        asset.loadReferences = 1;
    }

    Result<void> AssetManager::cancelBatchEntry(AssetType type, AssetHandle handle, bool wait)
    {
        switch (type)
        {
        case ASSET_TEXTURE:
            return CancelAsyncLoadTexture2D(handle, wait);
        case ASSET_MESH:
            return CancelAsyncLoadMesh(handle, wait);
        case ASSET_VF_SHADER:
            return CancelAsyncLoadVertFragShader(handle, wait);
        case ASSET_COMPUTE_SHADER:
            return CancelAsyncLoadComputeShader(handle, wait);
        case ASSET_SKELETON:
            return CancelAsyncLoadSkeleton(handle, wait);
        case ASSET_ANIMATION:
            return CancelAsyncLoadAnimation(handle, wait);
        case ASSET_FONT:
            return CancelAsyncLoadFont(handle, wait);
        case ASSET_AUDIO_CLIP:
            return CancelAsyncLoadAudioClip(handle, wait);
        case ASSET_NAV_AGENT:
            return CancelAsyncLoadNavAgentConfig(handle, wait);
        case ASSET_NAV_MESH_TILES:
            return CancelAsyncLoadNavMeshTiles(handle, wait);
        case ASSET_MATERIAL:
        {
            auto found = getMaterialAsset(handle);
            if (!found)
                return std::unexpected(found.error());
            if (!*found)
                return std::unexpected("material not found");
            cancelAsyncLoad(**found, wait);
            return {};
        }
        default:
            return std::unexpected("unsupported resource type in load batch");
        }
    }

    Result<void> AssetManager::AsyncBatchLoad(LoadBatch &batch)
    {
        auto materials = expandLoadBatch(batch);
        if (!materials)
            return std::unexpected(materials.error());
        std::vector<std::pair<AssetType, AssetHandle>> acquired;
        for (const auto &[type, handles] : batch)
        {
            if (type == ASSET_MATERIAL)
                continue;
            for (auto handle : handles)
            {
                Result<void> requested;
                switch (type)
                {
                case ASSET_TEXTURE:
                    requested = AsyncLoadTexture2D(handle);
                    break;
                case ASSET_MESH:
                    requested = AsyncLoadMesh(handle);
                    break;
                case ASSET_VF_SHADER:
                    requested = AsyncLoadVertFragShader(handle);
                    break;
                case ASSET_COMPUTE_SHADER:
                    requested = AsyncLoadComputeShader(handle);
                    break;
                case ASSET_SKELETON:
                    requested = AsyncLoadSkeleton(handle);
                    break;
                case ASSET_ANIMATION:
                    requested = AsyncLoadAnimation(handle);
                    break;
                case ASSET_FONT:
                    requested = AsyncLoadFont(handle);
                    break;
                case ASSET_AUDIO_CLIP:
                    requested = AsyncLoadAudioClip(handle);
                    break;
                case ASSET_NAV_AGENT:
                    requested = AsyncLoadNavAgentConfig(handle);
                    break;
                case ASSET_NAV_MESH_TILES:
                    requested = AsyncLoadNavMeshTiles(handle);
                    break;
                }
                if (!requested)
                {
                    for (const auto &[acquiredType, acquiredHandle] : acquired)
                        (void)cancelBatchEntry(acquiredType, acquiredHandle, false);
                    return requested;
                }
                acquired.emplace_back(type, handle);
            }
        }
        for (auto *material : *materials)
            asyncLoadMaterial(*material);
        return {};
    }

    BatchResult AssetManager::WaitAsyncBatchLoad(const LoadBatch &batch)
    {
        BatchResult result;
        if (auto entries = batch.find(ASSET_TEXTURE); entries != batch.end())
            for (auto handle : entries->second)
                result.textures.emplace(handle, WaitAsyncLoadTexture2D(handle));
        if (auto entries = batch.find(ASSET_MESH); entries != batch.end())
            for (auto handle : entries->second)
                result.meshes.emplace(handle, WaitAsyncLoadMesh(handle));
        if (auto entries = batch.find(ASSET_VF_SHADER); entries != batch.end())
            for (auto handle : entries->second)
                result.vertFragShaders.emplace(handle, WaitAsyncLoadVertFragShader(handle));
        if (auto entries = batch.find(ASSET_COMPUTE_SHADER); entries != batch.end())
            for (auto handle : entries->second)
                result.computeShaders.emplace(handle, WaitAsyncLoadComputeShader(handle));
        if (auto entries = batch.find(ASSET_SKELETON); entries != batch.end())
            for (auto handle : entries->second)
                result.skeletons.emplace(handle, WaitAsyncLoadSkeleton(handle));
        if (auto entries = batch.find(ASSET_ANIMATION); entries != batch.end())
            for (auto handle : entries->second)
                result.animations.emplace(handle, WaitAsyncLoadAnimation(handle));
        if (auto entries = batch.find(ASSET_FONT); entries != batch.end())
            for (auto handle : entries->second)
                result.fonts.emplace(handle, WaitAsyncLoadFont(handle));
        if (auto entries = batch.find(ASSET_AUDIO_CLIP); entries != batch.end())
            for (auto handle : entries->second)
                result.audioClips.emplace(handle, WaitAsyncLoadAudioClip(handle));
        if (auto entries = batch.find(ASSET_NAV_AGENT); entries != batch.end())
            for (auto handle : entries->second)
                result.navAgentConfigs.emplace(handle, WaitAsyncLoadNavAgentConfig(handle));
        if (auto entries = batch.find(ASSET_NAV_MESH_TILES); entries != batch.end())
            for (auto handle : entries->second)
                result.navMeshes.emplace(handle, WaitAsyncLoadNavMeshTiles(handle));
        if (auto entries = batch.find(ASSET_MATERIAL); entries != batch.end())
            for (auto handle : entries->second)
                result.materials.emplace(handle, WaitAsyncLoadMaterial(handle));
        return result;
    }

    Result<void> AssetManager::CancelAsyncBatchLoad(const LoadBatch &batch, bool wait)
    {
        Result<void> result;
        for (const auto &[type, handles] : batch)
            for (auto handle : handles)
                if (auto cancelled = cancelBatchEntry(type, handle, wait); !cancelled && result)
                    result = std::unexpected(cancelled.error());
        return result;
    }
}
