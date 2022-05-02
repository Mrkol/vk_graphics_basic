#ifndef SIMPLE_RENDER_H
#define SIMPLE_RENDER_H

#define VK_NO_PROTOTYPES

#include "../../render/scene_mgr.h"
#include "../../render/render_common.h"
#include "../../render/render_gui.h"
#include "../../../resources/shaders/common.h"
#include <geom/vk_mesh.h>
#include <vk_descriptor_sets.h>
#include <vk_fbuf_attachment.h>
#include <vk_images.h>
#include <vk_swapchain.h>
#include <string>
#include <iostream>


struct GBufferLayer
{
  vk_utils::VulkanImageMem image{};
};

struct GBuffer
{
  std::vector<GBufferLayer> color_layers;
  GBufferLayer depth_stencil_layer;
  // pre-postfx resolved gbuffer
  vk_utils::VulkanImageMem resolved;
  VkRenderPass renderpass{VK_NULL_HANDLE};
};

struct SceneGeometryPipeline
{
  VkPipelineLayout layout;
  VkPipeline pipeline;
  VkPipeline shadow;
  VkPipeline wireframe;
};


class SimpleRender : public IRender
{
  static constexpr char const* DEFERRED_VERTEX_SHADER_PATH = "../resources/shaders/geometry/static_mesh.vert";
  static constexpr char const* DEFERRED_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/static_mesh.frag";

  static constexpr char const* DEFERRED_LANDSCAPE_VERTEX_SHADER_PATH = "../resources/shaders/geometry/landscape.vert";
  static constexpr char const* DEFERRED_LANDSCAPE_TESC_SHADER_PATH = "../resources/shaders/geometry/landscape.tesc";
  static constexpr char const* DEFERRED_LANDSCAPE_TESE_SHADER_PATH = "../resources/shaders/geometry/landscape.tese";
  static constexpr char const* DEFERRED_LANDSCAPE_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/landscape.frag";

  static constexpr char const* DEFERRED_GRASS_VERTEX_SHADER_PATH = "../resources/shaders/geometry/grass.vert";
  static constexpr char const* DEFERRED_GRASS_TESC_SHADER_PATH = "../resources/shaders/geometry/grass.tesc";
  static constexpr char const* DEFERRED_GRASS_TESE_SHADER_PATH = "../resources/shaders/geometry/grass.tese";
  static constexpr char const* DEFERRED_GRASS_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/grass.frag";

  static constexpr char const* LIGHTING_VERTEX_SHADER_PATH = "../resources/shaders/lighting/lighting.vert";
  static constexpr char const* LIGHTING_GEOMETRY_SHADER_PATH = "../resources/shaders/lighting/lighting.geom";
  static constexpr char const* LIGHTING_FRAGMENT_SHADER_PATH = "../resources/shaders/lighting/lighting.frag";
  static constexpr char const* LIGHTING_GLOBAL_FRAGMENT_SHADER_PATH = "../resources/shaders/lighting/lighting_global.frag";
  
  static constexpr char const* FOG_FRAGMENT_SHADER_PATH = "../resources/shaders/postfx/fog.frag";
  static constexpr char const* SSAO_FRAGMENT_SHADER_PATH = "../resources/shaders/postfx/ssao.frag";
  static constexpr char const* POSTFX_FRAGMENT_SHADER_PATH = "../resources/shaders/postfx/postfx.frag";
  
  static constexpr char const* VSM_FRAGMENT_SHADER_PATH = "../resources/shaders/postfx/vsm.frag";

  static constexpr char const* FULLSCREEN_QUAD3_VERTEX_SHADER_PATH = "../resources/shaders/quad3_vert.vert";

  static constexpr char const* WIREFRAME_GEOMETRY_SHADER_PATH = "../resources/shaders/geometry/wireframe.geom";
  static constexpr char const* WIREFRAME_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/wireframe.frag";

  static constexpr char const* CULLING_SHADER_PATH = "../resources/shaders/culling.comp";
  static constexpr char const* LANDSCAPE_CULLING_SHADER_PATH = "../resources/shaders/landscape_culling.comp";

  static constexpr uint32_t POSTFX_DOWNSCALE_FACTOR = 4;

  static constexpr uint32_t SSAO_KERNEL_SIZE = 64;
  static constexpr uint32_t SSAO_KERNEL_SIZE_BYTES = 4*sizeof(float)*SSAO_KERNEL_SIZE;
  static constexpr uint32_t SSAO_NOISE_DIM = 8;
  static constexpr float SSAO_RADIUS = 0.5f;
  
  static constexpr size_t SHADOW_MAP_CASCADE_COUNT = 4;
  static constexpr size_t SHADOW_MAP_RESOLUTION = 2048;

  static constexpr uint32_t VSM_BLUR_RADIUS = 3;

public:
  SimpleRender(uint32_t a_width, uint32_t a_height);
  ~SimpleRender() override { Cleanup(); }

  uint32_t     GetWidth()      const override { return m_width; }
  uint32_t     GetHeight()     const override { return m_height; }
  VkInstance   GetVkInstance() const override { return m_instance; }
  void InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId) override;

  void InitPresentation(VkSurfaceKHR& a_surface) override;

  void ProcessInput(const AppInput& input) override;
  void UpdateCamera(const Camera* cams, uint32_t a_camsCount) override;
  Camera GetCurrentCamera() override {return m_cam;}
  void UpdateView();

  void LoadScene(const char *path, bool transpose_inst_matrices) override;
  void DrawFrame(float a_time, DrawMode a_mode) override;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // debugging utils
  //
  
  template<class T>
  void setObjectName(T handle, VkDebugReportObjectTypeEXT type, const char* name)
  {
    if (vkDebugMarkerSetObjectNameEXT != nullptr)
    {
		  VkDebugMarkerObjectNameInfoEXT nameInfo{
		    .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
		    .objectType = type,
		    .object = reinterpret_cast<uint64_t>(handle),
		    .pObjectName = name,
		  };
		  vkDebugMarkerSetObjectNameEXT(m_device, &nameInfo);
    }
  }

  void cmdBeginRegion(VkCommandBuffer a_cmdBuff, const char* name) const
  {
    if (vkCmdDebugMarkerBeginEXT != nullptr)
    {
      VkDebugMarkerMarkerInfoEXT info {
        .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
        .pMarkerName = name,
        .color = {0, 0.5, 0, 1},
      };
      vkCmdDebugMarkerBeginEXT(a_cmdBuff, &info);
    }
  }

  void cmdEndRegion(VkCommandBuffer a_cmdBuff) const
  {
    if (vkCmdDebugMarkerEndEXT != nullptr)
    {
      vkCmdDebugMarkerEndEXT(a_cmdBuff);
    }
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData)
  {
    (void)flags;
    (void)objectType;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    std::cout << pLayerPrefix << ": " << pMessage << std::endl;
    return VK_FALSE;
  }

  VkDebugReportCallbackEXT m_debugReportCallback = nullptr;

protected:
  VkInstance       m_instance       = VK_NULL_HANDLE;
  VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice         m_device         = VK_NULL_HANDLE;
  VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
  VkQueue          m_transferQueue  = VK_NULL_HANDLE;

  vk_utils::QueueFID_T m_queueFamilyIDXs {UINT32_MAX, UINT32_MAX, UINT32_MAX};

  struct
  {
    uint32_t    currentFrame      = 0u;
    VkQueue     queue             = VK_NULL_HANDLE;
    VkSemaphore imageAvailable    = VK_NULL_HANDLE;
    VkSemaphore renderingFinished = VK_NULL_HANDLE;
  } m_presentationResources;

  std::vector<VkFence> m_frameFences;
  std::vector<VkCommandBuffer> m_cmdBuffersDrawMain;
  
  struct
  {
    LiteMath::float4x4 proj;
    LiteMath::float4x4 view;
  } graphicsPushConsts;


  UniformParams m_uniforms {};
  VkBuffer m_ubo = VK_NULL_HANDLE;
  VkBuffer m_shadowmapUbo = VK_NULL_HANDLE;
  VkDeviceMemory m_uboAlloc = VK_NULL_HANDLE;
  void* m_uboMappedMem = nullptr;
  void* m_shadowmapUboMappedMem = nullptr;

  VkDeviceMemory m_indirectRenderingMemory = VK_NULL_HANDLE;
  
  VkSampler m_landscapeHeightmapSampler;
  VkSampler m_shadowmapSampler;
  
  SceneGeometryPipeline m_deferredPipeline {};
  SceneGeometryPipeline m_deferredLandscapePipeline {};
  SceneGeometryPipeline m_deferredGrassPipeline {};
  pipeline_data_t m_lightingPipeline {};
  pipeline_data_t m_globalLightingPipeline {};
  pipeline_data_t m_vsmPipeline {};

  pipeline_data_t m_cullingPipeline {};
  pipeline_data_t m_landscapeCullingPipeline {};

  VkDescriptorSet m_graphicsDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_graphicsDescriptorSetLayout = VK_NULL_HANDLE;

  struct CullingPushConstants
  {
    LiteMath::float4x4 projView;
    uint32_t instanceCount;
    uint32_t modelCount;
  };

  struct LandscapeCullingPushConstants
  {
    LiteMath::float4x4 projView;
  };
  
  struct VisibilityInfo
  {
    CullingPushConstants cullingPushConsts;
    LandscapeCullingPushConstants landscapeCullingPushConsts;

    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VkBuffer instanceMappingBuffer = VK_NULL_HANDLE;
    VkDescriptorSet cullingOutputDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet staticMeshVisDescSet = VK_NULL_HANDLE;

    VkBuffer landscapeIndirectDrawBuffer = VK_NULL_HANDLE;
    std::vector<VkBuffer> landscapeTileBuffers;
    std::vector<VkDescriptorSet> landscapeCullingOutputDescriptorSets;
    std::vector<VkDescriptorSet> landscapeVisDescSets;
  };

  VkDescriptorSetLayout m_landscapeVisibilityDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_graphicsVisibilityDescriptorSetLayout = VK_NULL_HANDLE;

  VisibilityInfo m_mainVisInfo;
  std::array<VisibilityInfo, SHADOW_MAP_CASCADE_COUNT> m_cascadeVisInfo;
  std::vector<VisibilityInfo*> m_visibilityInfos;

  VkDescriptorSet m_cullingSceneDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_cullingSceneDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_cullingOutputDescriptorSetLayout = VK_NULL_HANDLE;

  std::vector<VkDescriptorSet> m_landscapeCullingSceneDescriptorSets;
  VkDescriptorSetLayout m_landscapeCullingSceneDescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_landscapeCullingOutputDescriptorSetLayout = VK_NULL_HANDLE;


  std::vector<VkDescriptorSet> m_landscapeMainDescriotorSets;
  VkDescriptorSetLayout m_landscapeMainDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingFragmentDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingFragmentDescriptorSetLayout = VK_NULL_HANDLE;

  std::unique_ptr<vk_utils::DescriptorMaker> m_pBindings = nullptr;
  vk_utils::DescriptorMaker& GetDescMaker();

  // *** presentation
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VulkanSwapChain m_swapchain;
  VkFramebuffer m_mainPassFrameBuffer;
  // ***

  // *** GUI
  std::unique_ptr<IRenderGUI> m_pGUIRender;
  virtual void SetupGUIElements();
  void DrawFrameWithGUI();
  //

  Camera   m_cam;
  uint32_t m_width  = 1024u;
  uint32_t m_height = 1024u;
  uint32_t m_framesInFlight  = 2u;
  bool m_vsync = false;
  bool m_wireframe = false;
  bool m_landscapeShadows = false;
  bool m_ssao = true;
  int m_tonemappingMode = 2;
  float m_exposure = 1.0;
  float m_sunAngle = 0.5f;
  float m_cascadeSplitLambda = 0.95f;

  VkPhysicalDeviceFeatures m_enabledDeviceFeatures = {};
  VkPhysicalDeviceDescriptorIndexingFeatures m_enabledDeviceDescriptorIndexingFeatures = {};
  std::vector<const char*> m_deviceExtensions      = {};
  std::vector<const char*> m_optionalDeviceExtensions = {};
  std::vector<const char*> m_instanceExtensions    = {};

  bool m_enableValidation;
  std::vector<const char*> m_validationLayers;

  std::unique_ptr<SceneManager> m_pScnMgr;

  GBuffer m_gbuffer;

  VkRenderPass m_prePostFxRenderPass;
  vk_utils::VulkanImageMem m_fogImage;
  vk_utils::VulkanImageMem m_ssaoImage;
  vk_utils::VulkanImageMem m_ssaoNoise;
  VkSampler m_noiseSampler;
  VkBuffer m_ssaoKernel = VK_NULL_HANDLE;

  VkRenderPass m_postFxRenderPass;
  std::vector<VkFramebuffer> m_framebuffers;
  
  VkFramebuffer m_prePostFxFramebuffer;
  
  pipeline_data_t m_fogPipeline;
  pipeline_data_t m_ssaoPipeline;
  pipeline_data_t m_postFxPipeline;
  
  VkDescriptorSet m_postFxDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_postFxDescriptorSetLayout = VK_NULL_HANDLE;
  
  VkDescriptorSet m_fogDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_fogDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_ssaoDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_ssaoDescriptorSetLayout = VK_NULL_HANDLE;



  struct ShadowmapUbo
  {
    std::array<LiteMath::float4x4, SHADOW_MAP_CASCADE_COUNT> cascadeViewProjMats;
    std::array<float, SHADOW_MAP_CASCADE_COUNT> cascadeSplitDepths;
  };

  std::array<LiteMath::float4x4, SHADOW_MAP_CASCADE_COUNT> m_cascadeViewMats;
  std::array<LiteMath::float4x4, SHADOW_MAP_CASCADE_COUNT> m_cascadeProjMats;
  
  std::array<VkDescriptorSet, SHADOW_MAP_CASCADE_COUNT> m_vsmDescriptorSets;
  VkDescriptorSetLayout m_vsmDescriptorSetLayout = VK_NULL_HANDLE;

  ShadowmapUbo m_shadowmapUboData;
  
  vk_utils::VulkanImageMem m_shadowmap;
  vk_utils::VulkanImageMem m_vsm;
  std::array<VkImageView, SHADOW_MAP_CASCADE_COUNT> m_cascadeViews;
  std::array<VkImageView, SHADOW_MAP_CASCADE_COUNT> m_vsmViews;
  std::array<VkFramebuffer, SHADOW_MAP_CASCADE_COUNT> m_cascadeFramebuffers;
  std::array<VkFramebuffer, SHADOW_MAP_CASCADE_COUNT> m_vsmFramebuffers;
  VkRenderPass m_shadowmapRenderPass;
  VkRenderPass m_vsmRenderPass;


  VkPipeline pickGeometryPipeline(const SceneGeometryPipeline& pipeline, bool depthOnly) const
  {
    if (depthOnly) return pipeline.shadow;
    if (m_wireframe) return pipeline.wireframe;
    return pipeline.pipeline;
  }

  void ClearPipeline(pipeline_data_t& pipeline);
  void ClearPipeline(SceneGeometryPipeline& pipeline);
  void ClearAllPipelines();

  void DrawFrameSimple();

  void CreateInstance();
  void CreateDevice(uint32_t a_deviceId);

  void RecordShadowmapRendering(VkCommandBuffer cmdBuff);

  void RecordFrameCommandBuffer(VkCommandBuffer cmdBuff, uint32_t swapchainIdx);

  void RecordStaticMeshCulling(VkCommandBuffer cmdBuff, VisibilityInfo& visInfo);
  void RecordLandscapeCulling(VkCommandBuffer cmdBuff, VisibilityInfo& visInfo);

  void RecordStaticMeshRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly);
  void RecordLandscapeRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly);
  void RecordGrassRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly);
  void RecordLightResolve(VkCommandBuffer a_cmdBuff);

  virtual void SetupStaticMeshPipeline();
  virtual void SetupLandscapePipeline();
  virtual void SetupLightingPipeline();
  virtual void SetupPostfxPipeline();
  virtual void SetupCullingPipeline();
  void CleanupPipelineAndSwapchain();
  void RecreateSwapChain();

  void CreateUniformBuffer();
  void UpdateUniformBuffer(float a_time);

  void Cleanup();

  void SetupDeviceFeatures();
  void SetupDeviceExtensions();
  void SetupValidationLayers();

  void ClearGBuffer();
  void ClearPostFx();
  void ClearShadowmaps();

  void CreateGBuffer();
  void CreatePostFx();
  void CreateShadowmaps();
};


#endif //SIMPLE_RENDER_H
