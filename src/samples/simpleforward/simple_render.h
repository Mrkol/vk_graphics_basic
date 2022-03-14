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
  VkRenderPass renderpass{VK_NULL_HANDLE};
};


class SimpleRender : public IRender
{
public:
  static constexpr char const* DEFERRED_VERTEX_SHADER_PATH = "../resources/shaders/geometry/static_mesh.vert";
  static constexpr char const* DEFERRED_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/static_mesh.frag";

  static constexpr char const* DEFERRED_LANDSCAPE_VERTEX_SHADER_PATH = "../resources/shaders/geometry/landscape.vert";
  static constexpr char const* DEFERRED_LANDSCAPE_TESC_SHADER_PATH = "../resources/shaders/geometry/landscape.tesc";
  static constexpr char const* DEFERRED_LANDSCAPE_TESE_SHADER_PATH = "../resources/shaders/geometry/landscape.tese";
  static constexpr char const* DEFERRED_LANDSCAPE_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/landscape.frag";

  static constexpr char const* LIGHTING_VERTEX_SHADER_PATH = "../resources/shaders/lighting/lighting.vert";
  static constexpr char const* LIGHTING_GEOMETRY_SHADER_PATH = "../resources/shaders/lighting/lighting.geom";
  static constexpr char const* LIGHTING_FRAGMENT_SHADER_PATH = "../resources/shaders/lighting/lighting.frag";
  static constexpr char const* LIGHTING_GLOBAL_FRAGMENT_SHADER_PATH = "../resources/shaders/lighting/lighting_global.frag";

  static constexpr char const* FULLSCREEN_QUAD3_VERTEX_SHADER_PATH = "../resources/shaders/quad3_vert.vert";

  static constexpr char const* WIREFRAME_GEOMETRY_SHADER_PATH = "../resources/shaders/geometry/wireframe.geom";
  static constexpr char const* WIREFRAME_FRAGMENT_SHADER_PATH = "../resources/shaders/geometry/wireframe.frag";

  static constexpr char const* CULLING_SHADER_PATH = "../resources/shaders/culling.comp";

  SimpleRender(uint32_t a_width, uint32_t a_height);
  ~SimpleRender()  { Cleanup(); };

  inline uint32_t     GetWidth()      const override { return m_width; }
  inline uint32_t     GetHeight()     const override { return m_height; }
  inline VkInstance   GetVkInstance() const override { return m_instance; }
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

  struct
  {
    LiteMath::float4x4 projView;
    uint32_t instanceCount;
    uint32_t modelCount;
  } cullingPushConsts;

  UniformParams m_uniforms {};
  VkBuffer m_ubo = VK_NULL_HANDLE;
  VkDeviceMemory m_uboAlloc = VK_NULL_HANDLE;
  void* m_uboMappedMem = nullptr;
  
  VkBuffer m_indirectDrawBuffer = VK_NULL_HANDLE;
  VkBuffer m_instanceMappingBuffer = VK_NULL_HANDLE;

  VkDeviceMemory m_indirectRenderingMemory = VK_NULL_HANDLE;

  VkSampler m_landscapeHeightmapSampler;
  
  pipeline_data_t m_deferredPipeline {};
  pipeline_data_t m_deferredLandscapePipeline {};
  pipeline_data_t m_lightingPipeline {};
  pipeline_data_t m_globalLightingPipeline {};
  pipeline_data_t m_deferredWireframePipeline {};

  pipeline_data_t m_cullingPipeline {};

  VkDescriptorSet m_graphicsDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_graphicsDescriptorSetLayout = VK_NULL_HANDLE;

  std::vector<VkDescriptorSet> m_landscapeDescriptorSets;
  VkDescriptorSetLayout m_landscapeDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_cullingDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_cullingDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingDescriptorSetLayout = VK_NULL_HANDLE;

  VkDescriptorSet m_lightingFragmentDescriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_lightingFragmentDescriptorSetLayout = VK_NULL_HANDLE;

  std::unique_ptr<vk_utils::DescriptorMaker> m_pBindings = nullptr;
  vk_utils::DescriptorMaker& GetDescMaker();

  // *** presentation
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VulkanSwapChain m_swapchain;
  std::vector<VkFramebuffer> m_frameBuffers;
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
  float m_sunAngle = 0.5f;

  VkPhysicalDeviceFeatures m_enabledDeviceFeatures = {};
  VkPhysicalDeviceDescriptorIndexingFeatures m_enabledDeviceDescriptorIndexingFeatures = {};
  std::vector<const char*> m_deviceExtensions      = {};
  std::vector<const char*> m_instanceExtensions    = {};

  bool m_enableValidation;
  std::vector<const char*> m_validationLayers;

  std::unique_ptr<SceneManager> m_pScnMgr;

  GBuffer m_gbuffer;

  void ClearPipeline(pipeline_data_t& pipeline);
  void ClearAllPipelines();

  void DrawFrameSimple();

  void CreateInstance();
  void CreateDevice(uint32_t a_deviceId);

  void BuildCommandBufferSimple(VkCommandBuffer cmdBuff, VkFramebuffer frameBuff);

  virtual void SetupStaticMeshPipeline();
  virtual void SetupLandscapePipeline();
  virtual void SetupLightingPipeline();
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
  void CreateGBuffer();
};


#endif //SIMPLE_RENDER_H
