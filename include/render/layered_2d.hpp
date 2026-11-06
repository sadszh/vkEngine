#ifndef LAYERED_2D_RENDER_H
#define LAYERED_2D_RENDER_H

#include <font.hpp>
#include <render/quad_manager.hpp>
#include <render/material.hpp>
#include <render/pipeline.hpp>
#include <render/subpass.hpp>
#include <ds/id_allocator.hpp>
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vke_render
{
    struct Layered2DRenderPushConstants
    {
        glm::mat4 model{1.0f};
        glm::vec2 viewportSize{1.0f};
        glm::vec2 atlasSize{1.0f};
    };

    class QuadIDVertexBufferPool
    {
    public:
        struct Allocation
        {
            uint32_t chunkIndex = INVALID_CHUNK_INDEX;
            uint32_t offset = 0;
            uint32_t capacity = 0;
            uint32_t count = 0;

            bool Valid() const { return chunkIndex != INVALID_CHUNK_INDEX; }
        };

        bool Ensure(Allocation &allocation, uint32_t count);
        void Update(const Allocation &allocation, const std::vector<QuadID> &quadIDs, uint32_t currentFrame);
        void Release(Allocation &allocation);
        VkBuffer GetBuffer(const Allocation &allocation, uint32_t currentFrame) const;
        VkDeviceSize GetOffset(const Allocation &allocation) const;

        void Reset()
        {
            freeLists.clear();
            chunks.clear();
        }

    private:
        static constexpr uint32_t INVALID_CHUNK_INDEX = std::numeric_limits<uint32_t>::max();
        static constexpr uint32_t DEFAULT_CHUNK_CAPACITY = 4096;

        struct Chunk
        {
            std::array<std::unique_ptr<HostCoherentBuffer>, MAX_FRAMES_IN_FLIGHT> buffers;
            uint32_t capacity = 0;
            uint32_t used = 0;
        };

        std::vector<Chunk> chunks;
        std::unordered_map<uint32_t, std::vector<Allocation>> freeLists;

        Allocation allocateCapacity(uint32_t capacity);
        static uint32_t capacityClass(uint32_t count);
    };

    class Layered2DRenderUnit
    {
    public:
        Layered2DRenderUnit(vke_ds::id32_t id, const glm::mat4 &modelMatrix, QuadIDVertexBufferPool *quadIDPool)
            : id(id), modelMatrix(modelMatrix), quadIDPool(quadIDPool) {}
        ~Layered2DRenderUnit();

        const vke_ds::id32_t id;
        glm::mat4 modelMatrix;
        std::vector<QuadID> quadIDs;

        void SetQuadIDs(std::vector<QuadID> ids);
        void Render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                    const glm::vec2 &viewportSize, const glm::vec2 &atlasSize, uint32_t currentFrame);

    private:
        QuadIDVertexBufferPool *quadIDPool;
        QuadIDVertexBufferPool::Allocation quadIDAllocation;
        uint32_t quadIDUpdateCnt = 0;
    };

    struct Layered2DMaterialState
    {
        ~Layered2DMaterialState()
        {
            VulkanResourceManager::FreeDescriptorSet(commonDescriptorSet);
        }

        std::shared_ptr<Material> material;
        std::unique_ptr<GraphicsPipeline> pipeline;
        VkDescriptorSet commonDescriptorSet = VK_NULL_HANDLE;
    };

    class Layered2DRenderer : public RenderPassBase
    {
    public:
        Layered2DRenderer(RenderContext *ctx, VkDescriptorSet *globalDescriptorSets,
                          QuadManager *quadManager)
            : RenderPassBase(LAYERED_2D_RENDERER, ctx, globalDescriptorSets),
              quadManager(quadManager) {}

        ~Layered2DRenderer() override;

        void Reset() override
        {
            units.clear();
            materialStates.clear();
            quadIDPool.Reset();
            unitAllocator = vke_ds::NaiveIDAllocator<vke_ds::id32_t>();
            resetImageTextures();
        }

        void Init(int subpassID, FrameGraph &frameGraph,
                  std::map<std::string, vke_ds::id32_t> &blackboard,
                  ResourceNodeIDMap &currentResourceNodeID) override;
        void Render(TaskNode &node, FrameGraph &frameGraph, VkCommandBuffer commandBuffer,
                    uint32_t currentFrame, uint32_t imageIndex) override;
        void OnWindowResize(FrameGraph &frameGraph, RenderContext *ctx) override;

        vke_ds::id32_t AllocateUnit(const std::shared_ptr<Material> &material,
                                    const glm::mat4 &modelMatrix, const std::vector<QuadID> &quadIDs);
        void DestroyUnit(vke_ds::id32_t unitID);
        bool UpdateUnitQuadIDs(vke_ds::id32_t unitID, const std::vector<QuadID> &quadIDs);

        Layered2DRenderUnit *GetUnit(vke_ds::id32_t unitID);
        std::shared_ptr<Material> GetDefaultMaterial() const { return defaultMaterial; }
        std::shared_ptr<Material> GetImageMaterial() const { return imageMaterial; }
        vke_common::Result<uint32_t> RegisterImageTexture(const std::shared_ptr<Texture2D> &texture);
        void UnregisterImageTexture(uint32_t index);
        std::shared_ptr<vke_common::Font> GetFont() const { return font; }

    private:
        struct UnitState
        {
            std::unique_ptr<Layered2DRenderUnit> unit;
            const Layered2DMaterialState *materialState;
        };

        struct ImageTextureSlot
        {
            std::shared_ptr<Texture2D> texture;
            uint32_t references = 0;
        };

        QuadManager *quadManager;
        VkDescriptorSet rendererDescriptorSets[MAX_FRAMES_IN_FLIGHT]{};
        VkDescriptorSet imageDescriptorSets[MAX_FRAMES_IN_FLIGHT]{};
        std::array<bool, MAX_FRAMES_IN_FLIGHT> imageTexturesDirty{};
        std::array<std::vector<std::shared_ptr<Texture2D>>, MAX_FRAMES_IN_FLIGHT> frameImageTextures;
        std::shared_ptr<Material> imageMaterial;
        std::shared_ptr<Texture2D> whiteTexture;
        std::vector<ImageTextureSlot> imageTextures;
        std::vector<uint32_t> freeImageTextureSlots;
        std::unordered_map<Texture2D *, uint32_t> imageTextureIndices;
        std::shared_ptr<ShaderModuleSet> textShader;
        std::shared_ptr<Material> defaultMaterial;
        std::shared_ptr<vke_common::Font> font;
        std::unique_ptr<HostCoherentBuffer> dynamicAtlasStagingBuffers[MAX_FRAMES_IN_FLIGHT];
        std::unique_ptr<Texture2D> dynamicAtlasTextures[MAX_FRAMES_IN_FLIGHT];
        QuadIDVertexBufferPool quadIDPool;
        vke_ds::NaiveIDAllocator<vke_ds::id32_t> unitAllocator;
        std::map<Material *, std::unique_ptr<Layered2DMaterialState>> materialStates;
        std::map<vke_ds::id32_t, UnitState> units;

        void constructFrameGraph(FrameGraph &frameGraph,
                                 std::map<std::string, vke_ds::id32_t> &blackboard,
                                 ResourceNodeIDMap &currentResourceNodeID);
        void createDescriptorSets();
        const Layered2DMaterialState &getOrCreateMaterialState(const std::shared_ptr<Material> &material);
        void createGraphicsPipeline(Layered2DMaterialState &state);
        void syncDynamicAtlas(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void syncImageTextures(uint32_t currentFrame);
        void resetImageTextures();
    };
}

#endif
