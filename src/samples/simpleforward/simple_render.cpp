#include "simple_render.h"

#include <random>
#include <tuple>
#include <span>
#include <type_traits>

#include "../../utils/input_definitions.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>

#ifdef WIN32
#undef max
#undef min
#endif


static std::default_random_engine rndEngine;
static std::uniform_real_distribution<float> randUNorm(0.0f, 1.0f);


template<class... Ts>
void makeSpecMap(const std::tuple<Ts...>& data,
  std::span<VkSpecializationMapEntry, sizeof...(Ts)> map,
  VkSpecializationInfo& info)
{
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    (...,
      (map[Is] = VkSpecializationMapEntry{
        Is,
        static_cast<uint32_t>(
          reinterpret_cast<const std::byte*>(std::addressof(std::get<Is>(data)))
            - reinterpret_cast<const std::byte*>(std::addressof(data))),
        sizeof(std::get<Is>(data))
      }));
  }(std::make_index_sequence<sizeof...(Ts)>{});
  info = VkSpecializationInfo{
    .mapEntryCount = static_cast<uint32_t>(map.size()),
    .pMapEntries = map.data(),
    .dataSize = sizeof(data),
    .pData = std::addressof(data),
  };
}

SimpleRender::SimpleRender(uint32_t a_width, uint32_t a_height) : m_width(a_width), m_height(a_height)
{
#ifdef NDEBUG
  m_enableValidation = false;
#else
  m_enableValidation = true;
#endif

  m_visibilityInfos.reserve(SHADOW_MAP_CASCADE_COUNT + 1);
  m_visibilityInfos.emplace_back(&m_mainVisInfo);
  for (auto& vi : m_cascadeVisInfo)
  {
    m_visibilityInfos.emplace_back(&vi);
  }
}

void SimpleRender::SetupDeviceFeatures()
{
  // m_enabledDeviceFeatures.fillModeNonSolid = VK_TRUE;
  m_enabledDeviceFeatures.multiDrawIndirect = true;
  m_enabledDeviceFeatures.drawIndirectFirstInstance = true;
  m_enabledDeviceFeatures.geometryShader = true;
  m_enabledDeviceFeatures.tessellationShader = true;

  
  m_enabledDeviceDescriptorIndexingFeatures = VkPhysicalDeviceDescriptorIndexingFeatures{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
    .descriptorBindingPartiallyBound = true,
    .runtimeDescriptorArray = true,
  };
}

void SimpleRender::SetupDeviceExtensions()
{
  m_deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  m_deviceExtensions.emplace_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
  m_deviceExtensions.emplace_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
  m_optionalDeviceExtensions.emplace_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
}

void SimpleRender::SetupValidationLayers()
{
  m_validationLayers.emplace_back("VK_LAYER_KHRONOS_validation");
  m_validationLayers.emplace_back("VK_LAYER_LUNARG_monitor");
}

void SimpleRender::InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId)
{
  for(size_t i = 0; i < a_instanceExtensionsCount; ++i)
  {
    m_instanceExtensions.emplace_back(a_instanceExtensions[i]);
  }

  SetupValidationLayers();
  VK_CHECK_RESULT(volkInitialize());
  CreateInstance();
  volkLoadInstance(m_instance);

  CreateDevice(a_deviceId);
  volkLoadDevice(m_device);

  m_commandPool = vk_utils::createCommandPool(m_device, m_queueFamilyIDXs.graphics,
                                              VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  m_cmdBuffersDrawMain.reserve(m_framesInFlight);
  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_landscapeHeightmapSampler = vk_utils::createSampler(
    m_device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
  m_shadowmapSampler = vk_utils::createSampler(
    m_device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
  m_noiseSampler = vk_utils::createSampler(
    m_device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);

  m_pScnMgr = std::make_unique<SceneManager>(m_device, m_physicalDevice, m_queueFamilyIDXs.transfer,
                                             m_queueFamilyIDXs.graphics, false);

  m_pScnMgr->AddLandscape();

}

void SimpleRender::InitPresentation(VkSurfaceKHR &a_surface)
{
  m_surface = a_surface;

  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface,
                                                              m_width, m_height, m_framesInFlight, m_vsync);
  m_presentationResources.currentFrame = 0;

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.imageAvailable));
  VK_CHECK_RESULT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_presentationResources.renderingFinished));

  CreateGBuffer();
  CreatePostFx();
  CreateShadowmaps();

  m_pGUIRender = std::make_unique<ImGuiRender>(m_instance, m_device, m_physicalDevice, m_queueFamilyIDXs.graphics, m_graphicsQueue, m_swapchain);
}

void SimpleRender::CreateInstance()
{
  VkApplicationInfo appInfo = {};
  appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext              = nullptr;
  appInfo.pApplicationName   = "VkRender";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName        = "SimpleForward";
  appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion         = VK_MAKE_VERSION(1, 2, 0);

  m_instance = vk_utils::createInstance(m_enableValidation, m_validationLayers, m_instanceExtensions, &appInfo);

  if (m_enableValidation)
    vk_utils::initDebugReportCallback(m_instance, &debugReportCallbackFn, &m_debugReportCallback);
}

void SimpleRender::CreateDevice(uint32_t a_deviceId)
{
  SetupDeviceExtensions();
  m_physicalDevice = vk_utils::findPhysicalDevice(m_instance, true, a_deviceId, m_deviceExtensions);

  std::vector<const char*> extensions = m_deviceExtensions;
  {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    for (auto optExt : m_optionalDeviceExtensions)
    {
      bool found = false;
      for (auto ext : availableExtensions)
      {
        if (std::strcmp(optExt, ext.extensionName) == 0)
        {
          found = true;
          break;
        }
      }
      if (found)
      {
        std::cout << "Enabling optional extension " << optExt << std::endl;
        extensions.emplace_back(optExt);
      }
    }

  }

  SetupDeviceFeatures();
  m_device = vk_utils::createLogicalDevice(m_physicalDevice, m_validationLayers, extensions,
                                           m_enabledDeviceFeatures, m_queueFamilyIDXs,
                                           VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT,
                                           &m_enabledDeviceDescriptorIndexingFeatures);

  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.graphics, 0, &m_graphicsQueue);
  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.transfer, 0, &m_transferQueue);
}

vk_utils::DescriptorMaker& SimpleRender::GetDescMaker()
{
  if(m_pBindings == nullptr)
  {
    std::vector<std::pair<VkDescriptorType, uint32_t> > dtypes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100}
    };
    m_pBindings = std::make_unique<vk_utils::DescriptorMaker>(m_device, dtypes, 100);
  }

  return *m_pBindings;
}


void SimpleRender::SetupStaticMeshPipeline()
{
  auto& bindings = GetDescMaker();

  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
  bindings.BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  bindings.BindBuffer(1, m_pScnMgr->GetInstanceMatricesBuffer());
  bindings.BindEnd(&m_graphicsDescriptorSet, &m_graphicsDescriptorSetLayout);

  for (auto* visInfo : m_visibilityInfos)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
    bindings.BindBuffer(0, visInfo->instanceMappingBuffer);
    bindings.BindEnd(&visInfo->staticMeshVisDescSet, &m_graphicsVisibilityDescriptorSetLayout);
  }


  
  auto make_deferred_pipeline = [this](std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths)
    {
      vk_utils::GraphicsPipelineMaker maker;

      maker.LoadShaders(m_device, shader_paths);

      SceneGeometryPipeline result;
    
      result.layout = maker.MakeLayout(m_device,
        {m_graphicsDescriptorSetLayout, m_graphicsVisibilityDescriptorSetLayout}, sizeof(graphicsPushConsts));

      maker.SetDefaultState(m_width, m_height);
      
      std::array<VkPipelineColorBlendAttachmentState, 3> cba_state;

      cba_state.fill(VkPipelineColorBlendAttachmentState {
          .blendEnable    = VK_FALSE,
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        });

      maker.colorBlending.attachmentCount = static_cast<uint32_t>(cba_state.size());
      maker.colorBlending.pAttachments = cba_state.data();

      auto vertexInputStateCreateInfo = m_pScnMgr->GetPipelineVertexInputStateCreateInfo();
      
      result.pipeline = maker.MakePipeline(m_device, vertexInputStateCreateInfo,
        m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

      shader_paths.erase(VK_SHADER_STAGE_FRAGMENT_BIT);
      maker.LoadShaders(m_device, shader_paths);

      result.shadow = maker.MakePipeline(m_device, vertexInputStateCreateInfo,
        m_shadowmapRenderPass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});
      
      shader_paths.emplace(VK_SHADER_STAGE_FRAGMENT_BIT, std::string{WIREFRAME_FRAGMENT_SHADER_PATH} + ".spv");
      shader_paths.emplace(VK_SHADER_STAGE_GEOMETRY_BIT, std::string{WIREFRAME_GEOMETRY_SHADER_PATH} + ".spv");
      maker.LoadShaders(m_device, shader_paths);

      result.wireframe = maker.MakePipeline(m_device, vertexInputStateCreateInfo,
        m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});

      return result;
    };
  
  m_deferredPipeline = make_deferred_pipeline(
    std::unordered_map<VkShaderStageFlagBits, std::string> {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{WRITE_GBUF_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{STATIC_MESH_VERTEX_SHADER_PATH} + ".spv"}
    });
}

void SimpleRender::SetupLandscapePipeline()
{
  auto& bindings = GetDescMaker();

  auto heightmaps = m_pScnMgr->GetLandscapeHeightmaps();
  m_landscapeMainDescriotorSets.clear();
  for (size_t i = 0; i < heightmaps.size(); ++i)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
      | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    auto textures = m_pScnMgr->GetLandscapeHeightmaps();
    bindings.BindBuffer(0, m_ubo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    bindings.BindImage(1, textures[i], m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindBuffer(2, m_pScnMgr->GetLandscapeInfos(), nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    bindings.BindEnd(&m_landscapeMainDescriotorSets.emplace_back(), &m_landscapeMainDescriptorSetLayout);
  }
  
  for (auto* visInfo : m_visibilityInfos)
  {
    visInfo->landscapeVisDescSets.clear();
    for (size_t i = 0; i < heightmaps.size(); ++i)
    {
      bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
      | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
      bindings.BindBuffer(0, visInfo->landscapeTileBuffers[i], nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      bindings.BindEnd(&visInfo->landscapeVisDescSets.emplace_back(), &m_landscapeVisibilityDescriptorSetLayout);
    }
  }

  auto makeTessellationPipeline =
    [this](std::unordered_map<VkShaderStageFlagBits, std::string> shader_paths,
      uint32_t controlPoints)
    {
      SceneGeometryPipeline result;

      vk_utils::GraphicsPipelineMaker maker;

      maker.LoadShaders(m_device, shader_paths);

      result.layout = maker.MakeLayout(m_device,
        {m_landscapeMainDescriptorSetLayout, m_landscapeVisibilityDescriptorSetLayout}, sizeof(graphicsPushConsts));
      
      maker.SetDefaultState(m_width, m_height);
      
      std::array<VkPipelineColorBlendAttachmentState, 3> cba_state;

      cba_state.fill(VkPipelineColorBlendAttachmentState {
          .blendEnable    = VK_FALSE,
          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        });

      maker.colorBlending.attachmentCount = static_cast<uint32_t>(cba_state.size());
      maker.colorBlending.pAttachments = cba_state.data();

      std::array dynStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

      VkPipelineDynamicStateCreateInfo dynamicState {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
        .pDynamicStates    = dynStates.data(),
      };

      VkPipelineVertexInputStateCreateInfo vertexLayout{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
      };

      VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
        .primitiveRestartEnable = false,
      };

      VkPipelineTessellationStateCreateInfo tessState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = controlPoints,
      };

      VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shader_paths.size()),
        .pStages             = maker.shaderStageInfos,
        .pVertexInputState   = &vertexLayout,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState  = &tessState,
        .pViewportState      = &maker.viewportState,
        .pRasterizationState = &maker.rasterizer,
        .pMultisampleState   = &maker.multisampling,
        .pDepthStencilState  = &maker.depthStencilTest,
        .pColorBlendState    = &maker.colorBlending,
        .pDynamicState       = &dynamicState,
        .layout              = result.layout,
        .renderPass          = m_gbuffer.renderpass,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
      };

      auto clearModules = [&maker, this]()
        {
          for (size_t i = 0; i < std::size(maker.shaderModules); ++i)
          {
            if(maker.shaderModules[i] != VK_NULL_HANDLE)
              vkDestroyShaderModule(m_device, maker.shaderModules[i], VK_NULL_HANDLE);
            maker.shaderModules[i] = VK_NULL_HANDLE;
          }
        };

      VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
        nullptr, &result.pipeline))
      clearModules();


      shader_paths.erase(VK_SHADER_STAGE_FRAGMENT_BIT);
      maker.LoadShaders(m_device, shader_paths);
      pipelineInfo.stageCount = static_cast<uint32_t>(shader_paths.size());
      pipelineInfo.renderPass = m_shadowmapRenderPass;

      VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
        nullptr, &result.shadow))
      clearModules();


      shader_paths.emplace(VK_SHADER_STAGE_FRAGMENT_BIT, std::string{WIREFRAME_FRAGMENT_SHADER_PATH} + ".spv");
      shader_paths.emplace(VK_SHADER_STAGE_GEOMETRY_BIT, std::string{WIREFRAME_GEOMETRY_SHADER_PATH} + ".spv");
      maker.LoadShaders(m_device, shader_paths);
      pipelineInfo.stageCount = static_cast<uint32_t>(shader_paths.size());
      pipelineInfo.renderPass = m_gbuffer.renderpass;

      VK_CHECK_RESULT(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
        nullptr, &result.wireframe))
      clearModules();

      return result;
    };

  m_deferredLandscapePipeline = makeTessellationPipeline(
    {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{WRITE_GBUF_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, std::string{LANDSCAPE_TESC_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, std::string{LANDSCAPE_TESE_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{LANDSCAPE_VERTEX_SHADER_PATH} + ".spv"}
    }, 4);

  m_deferredGrassPipeline = makeTessellationPipeline(
    {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{WRITE_GBUF_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, std::string{GRASS_TESC_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, std::string{GRASS_TESE_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{GRASS_VERTEX_SHADER_PATH} + ".spv"}
    }, 3);
}

void SimpleRender::SetupLightingPipeline()
{
  auto& bindings = GetDescMaker();
  
  bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_VERTEX_BIT);
  bindings.BindBuffer(0, m_ubo, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  bindings.BindBuffer(1, m_pScnMgr->GetLightsBuffer());
  bindings.BindEnd(&m_lightingDescriptorSet, &m_lightingDescriptorSetLayout);

  if (m_lightingFragmentDescriptorSetLayout == nullptr)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindImage(0, m_gbuffer.color_layers[0].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(1, m_gbuffer.color_layers[1].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(2, m_gbuffer.color_layers[2].image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(3, m_gbuffer.depth_stencil_layer.image.view,
      nullptr, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    bindings.BindImage(4, m_vsm.view, m_shadowmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindBuffer(5, m_shadowmapUbo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    bindings.BindEnd(&m_lightingFragmentDescriptorSet, &m_lightingFragmentDescriptorSetLayout);
  }
  else
  {
    std::array image_infos {
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[0].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[1].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.color_layers[2].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = nullptr,
        .imageView = m_gbuffer.depth_stencil_layer.image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_shadowmapSampler,
        .imageView = m_vsm.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    std::array<VkWriteDescriptorSet, image_infos.size()> writes;

    for (std::size_t i = 0; i < image_infos.size(); ++i)
    {
      writes[i] = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_lightingFragmentDescriptorSet,
        .dstBinding = static_cast<uint32_t>(i),
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
        .pImageInfo = image_infos.data() + i
      };
    }
    writes.back().descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }
  
  vk_utils::GraphicsPipelineMaker maker;

  maker.LoadShaders(m_device, std::unordered_map<VkShaderStageFlagBits, std::string> {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{LIGHTING_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_GEOMETRY_BIT, std::string{LIGHTING_GEOMETRY_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{LIGHTING_VERTEX_SHADER_PATH} + ".spv"}
    });

  m_lightingPipeline.layout = maker.MakeLayout(m_device,
    {m_lightingDescriptorSetLayout, m_lightingFragmentDescriptorSetLayout}, sizeof(graphicsPushConsts));

  maker.SetDefaultState(m_width, m_height);

  maker.rasterizer.cullMode = VK_CULL_MODE_NONE;
  maker.depthStencilTest.depthTestEnable = false;
  
  maker.colorBlendAttachments = {VkPipelineColorBlendAttachmentState {
    .blendEnable = true,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
  }};

  VkPipelineVertexInputStateCreateInfo emptyVertexInput{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0,
    .vertexAttributeDescriptionCount = 0,
  };

  m_lightingPipeline.pipeline = maker.MakePipeline(m_device, emptyVertexInput,
    m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, vk_utils::IA_PList(), 1);

  
  maker.LoadShaders(m_device, std::unordered_map<VkShaderStageFlagBits, std::string> {
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{LIGHTING_GLOBAL_FRAGMENT_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"}
    });

  
  auto specData = std::tuple<uint32_t>(SHADOW_MAP_CASCADE_COUNT);
  std::array<VkSpecializationMapEntry, 1> specMap;
  VkSpecializationInfo specInfo;
  makeSpecMap(specData, specMap, specInfo);

  for (auto& info : maker.shaderStageInfos)
  {
    if (info.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
    {
      info.pSpecializationInfo = &specInfo;
      break;
    }
  }

  m_globalLightingPipeline.layout = maker.MakeLayout(m_device,
    {m_lightingDescriptorSetLayout, m_lightingFragmentDescriptorSetLayout}, sizeof(graphicsPushConsts));
  
  m_globalLightingPipeline.pipeline = maker.MakePipeline(m_device, emptyVertexInput,
    m_gbuffer.renderpass, {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}, vk_utils::IA_TList(), 1);

  
  maker.SetDefaultState(SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION);
  
  maker.LoadShaders(m_device,
      {{VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{VSM_FRAGMENT_SHADER_PATH} + ".spv"}});


  auto specData2 = std::tuple<uint32_t>(VSM_BLUR_RADIUS);
  std::array<VkSpecializationMapEntry, 1> specMap2;
  VkSpecializationInfo specInfo2;
  makeSpecMap(specData2, specMap2, specInfo2);
  
  for (auto& info : maker.shaderStageInfos)
  {
    if (info.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
    {
      info.pSpecializationInfo = &specInfo;
      break;
    }
  }
  
  for (size_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindImage(0, m_cascadeViews[i], m_shadowmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindEnd(&m_vsmDescriptorSets[i], &m_vsmDescriptorSetLayout);
  }

  m_vsmPipeline.layout = maker.MakeLayout(m_device, {m_vsmDescriptorSetLayout}, sizeof(uint32_t));
  m_vsmPipeline.pipeline = maker.MakePipeline(m_device, emptyVertexInput, m_vsmRenderPass,
      {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR});
}

void SimpleRender::SetupPostfxPipeline()
{
  auto& bindings = GetDescMaker();
  
  if (m_postFxDescriptorSetLayout == nullptr)
  {
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindBuffer(0, m_ubo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    // TODO: Sampler should be different here
    bindings.BindImage(1, m_gbuffer.resolved.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindImage(2, m_fogImage.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindImage(3, m_ssaoImage.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindEnd(&m_postFxDescriptorSet, &m_postFxDescriptorSetLayout);

    
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindBuffer(0, m_ubo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    // TODO: Sampler should be different here
    bindings.BindImage(1, m_gbuffer.depth_stencil_layer.image.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindEnd(&m_fogDescriptorSet, &m_fogDescriptorSetLayout);

    
    bindings.BindBegin(VK_SHADER_STAGE_FRAGMENT_BIT);
    bindings.BindBuffer(0, m_ubo,
      nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    // TODO: Sampler should be different here
    bindings.BindImage(1, m_gbuffer.depth_stencil_layer.image.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindImage(2, m_gbuffer.color_layers[0].image.view,
      m_landscapeHeightmapSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindImage(3, m_ssaoNoise.view,
      m_noiseSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindings.BindBuffer(4, m_ssaoKernel,
      nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    bindings.BindEnd(&m_ssaoDescriptorSet, &m_ssaoDescriptorSetLayout);
  }
  else
  {
    // THIS CODE IS UTTER SHIT
    std::array image_infos {
      VkDescriptorImageInfo {
        .sampler = m_landscapeHeightmapSampler,
        .imageView = m_gbuffer.resolved.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_landscapeHeightmapSampler,
        .imageView = m_fogImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_landscapeHeightmapSampler,
        .imageView = m_ssaoImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_landscapeHeightmapSampler,
        .imageView = m_gbuffer.depth_stencil_layer.image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_landscapeHeightmapSampler,
        .imageView = m_gbuffer.color_layers[0].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkDescriptorImageInfo {
        .sampler = m_noiseSampler,
        .imageView = m_ssaoNoise.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    std::array writes{
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_postFxDescriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[0],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_postFxDescriptorSet,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[1],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_postFxDescriptorSet,
        .dstBinding = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[2],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_fogDescriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[3],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_ssaoDescriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[3],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_ssaoDescriptorSet,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[4],
      },
      VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_ssaoDescriptorSet,
        .dstBinding = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_infos[5],
      },
    };

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  
  vk_utils::GraphicsPipelineMaker maker;

  {
    maker.LoadShaders(m_device, {
      {VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{POSTFX_FRAGMENT_SHADER_PATH} + ".spv"},
    });

    m_postFxPipeline.layout = maker.MakeLayout(m_device,
      {m_postFxDescriptorSetLayout}, sizeof(graphicsPushConsts));

    maker.SetDefaultState(m_width, m_height);

    m_postFxPipeline.pipeline = maker.MakePipeline(m_device,
      VkPipelineVertexInputStateCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      },
      m_postFxRenderPass, {}, vk_utils::IA_TList(), 0);
  }

  {
    maker.LoadShaders(m_device, {
        {VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"},
      {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{FOG_FRAGMENT_SHADER_PATH} + ".spv"},
    });

    m_fogPipeline.layout = maker.MakeLayout(m_device,
      {m_fogDescriptorSetLayout, m_lightingFragmentDescriptorSetLayout}, sizeof(graphicsPushConsts));
    
    maker.SetDefaultState(m_width, m_height, 2);

    m_fogPipeline.pipeline = maker.MakePipeline(m_device,
      VkPipelineVertexInputStateCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      },
      m_prePostFxRenderPass, {}, vk_utils::IA_TList(), 0);
  }

  {
    maker.LoadShaders(m_device, {
        {VK_SHADER_STAGE_VERTEX_BIT, std::string{FULLSCREEN_QUAD3_VERTEX_SHADER_PATH} + ".spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, std::string{SSAO_FRAGMENT_SHADER_PATH} + ".spv"},
      });
    
    auto specData = std::tuple<uint32_t, float>(SSAO_KERNEL_SIZE, SSAO_RADIUS);
    std::array<VkSpecializationMapEntry, 2> specMap;
    VkSpecializationInfo specInfo;
    makeSpecMap(specData, specMap, specInfo);

    for (auto& info : maker.shaderStageInfos)
    {
      if (info.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
      {
        info.pSpecializationInfo = &specInfo;
        break;
      }
    }

    m_ssaoPipeline.layout = maker.MakeLayout(m_device,
      {m_ssaoDescriptorSetLayout}, sizeof(graphicsPushConsts));
    
    maker.SetDefaultState(m_width, m_height, 2);

    m_ssaoPipeline.pipeline = maker.MakePipeline(m_device,
      VkPipelineVertexInputStateCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      },
      m_prePostFxRenderPass, {}, vk_utils::IA_TList(), 0);
  }
}

void SimpleRender::SetupCullingPipeline()
{
  auto& bindings = GetDescMaker();
  
  bindings.BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
  bindings.BindBuffer(0, m_pScnMgr->GetInstanceInfosBuffer());
  bindings.BindBuffer(1, m_pScnMgr->GetInstanceMatricesBuffer());
  bindings.BindBuffer(2, m_pScnMgr->GetModelInfosBuffer());
  bindings.BindEnd(&m_cullingSceneDescriptorSet, &m_cullingSceneDescriptorSetLayout);
  
  for (auto* visInfo : m_visibilityInfos)
  {
    bindings.BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
    bindings.BindBuffer(0, visInfo->indirectDrawBuffer);
    bindings.BindBuffer(1, visInfo->instanceMappingBuffer);
    bindings.BindEnd(&visInfo->cullingOutputDescriptorSet, &m_cullingOutputDescriptorSetLayout);
  }

  
  vk_utils::ComputePipelineMaker maker;
  maker.LoadShader(m_device, std::string{CULLING_SHADER_PATH} + ".spv");

  m_cullingPipeline.layout = maker.MakeLayout(m_device,
    {m_cullingSceneDescriptorSetLayout, m_cullingOutputDescriptorSetLayout}, sizeof(CullingPushConstants));
  m_cullingPipeline.pipeline = maker.MakePipeline(m_device);


  auto minMaxHeights = m_pScnMgr->GetLandscapeMinMaxHeights();
  m_landscapeCullingSceneDescriptorSets.clear();
  for (size_t i = 0; i < minMaxHeights.size(); ++i)
  {
    bindings.BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
    bindings.BindBuffer(0, minMaxHeights[i]);
    bindings.BindBuffer(1, m_pScnMgr->GetLandscapeInfos(), nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    bindings.BindEnd(&m_landscapeCullingSceneDescriptorSets.emplace_back(),
      &m_landscapeCullingSceneDescriptorSetLayout);
  }
  
  for (auto* visInfo : m_visibilityInfos)
  {
    visInfo->landscapeCullingOutputDescriptorSets.clear();
    for (size_t i = 0; i < minMaxHeights.size(); ++i)
    {
      bindings.BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
      bindings.BindBuffer(0, visInfo->landscapeIndirectDrawBuffer, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
      bindings.BindBuffer(1, visInfo->landscapeTileBuffers[i], nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      bindings.BindEnd(&visInfo->landscapeCullingOutputDescriptorSets.emplace_back(),
        &m_landscapeCullingOutputDescriptorSetLayout);
    }
  }


  maker.LoadShader(m_device, std::string{LANDSCAPE_CULLING_SHADER_PATH} + ".spv");
  
  m_landscapeCullingPipeline.layout = maker.MakeLayout(m_device,
    {m_landscapeCullingSceneDescriptorSetLayout, m_landscapeCullingOutputDescriptorSetLayout},
      sizeof(LandscapeCullingPushConstants));
  m_landscapeCullingPipeline.pipeline = maker.MakePipeline(m_device);
}

void SimpleRender::CreateUniformBuffer()
{
  VkMemoryRequirements memReq1;
  VkMemoryRequirements memReq2;
  m_ubo = vk_utils::createBuffer(m_device, sizeof(UniformParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &memReq1);
  m_shadowmapUbo = vk_utils::createBuffer(m_device, sizeof(ShadowmapUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &memReq2);

  if (memReq1.memoryTypeBits != memReq2.memoryTypeBits)
  {
    vk_utils::logWarning("UBOs have different mem reqs!");
  }

  auto offsets = vk_utils::calculateMemOffsets({memReq1, memReq2});

  VkMemoryAllocateInfo allocateInfo{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = offsets.back(),
    .memoryTypeIndex =
      vk_utils::findMemoryType(memReq1.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_physicalDevice)
  };

  VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_uboAlloc));

  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_ubo, m_uboAlloc, offsets[0]))
  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_shadowmapUbo, m_uboAlloc, offsets[1]))

  void* mappedMem;
  vkMapMemory(m_device, m_uboAlloc, 0, offsets.back(), 0, &mappedMem);

  m_uboMappedMem = static_cast<std::byte*>(mappedMem) + offsets[0];
  m_shadowmapUboMappedMem = static_cast<std::byte*>(mappedMem) + offsets[1];

  m_uniforms.baseColor = LiteMath::float3(0.9f, 0.92f, 1.0f);
  m_uniforms.animateLightColor = false;
  m_uniforms.postFxDownscaleFactor = POSTFX_DOWNSCALE_FACTOR;

  UpdateUniformBuffer(0.0f);

  std::vector<VkBuffer> allBuffers;

  m_ssaoKernel = vk_utils::createBuffer(m_device,
      SSAO_KERNEL_SIZE_BYTES,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  allBuffers.emplace_back(m_ssaoKernel);

  for (auto* visInfo : m_visibilityInfos)
  {
    // worst case we'll see all instances
    visInfo->instanceMappingBuffer = vk_utils::createBuffer(m_device,
      sizeof(uint32_t)*(m_pScnMgr->InstancesNum() + 1),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    // worst case we'll have to draw all model types
    visInfo->indirectDrawBuffer = vk_utils::createBuffer(m_device,
      sizeof(VkDrawIndexedIndirectCommand) * m_pScnMgr->MeshesNum(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);


    visInfo->landscapeIndirectDrawBuffer = vk_utils::createBuffer(m_device,
      2*sizeof(VkDrawIndirectCommand)*m_pScnMgr->LandscapeNum(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

    allBuffers.emplace_back(visInfo->instanceMappingBuffer);
    allBuffers.emplace_back(visInfo->indirectDrawBuffer);
    allBuffers.emplace_back(visInfo->landscapeIndirectDrawBuffer);

    for (auto tiles : m_pScnMgr->LandscapeTileCounts())
    {
      allBuffers.emplace_back(visInfo->landscapeTileBuffers.emplace_back(vk_utils::createBuffer(m_device,
        (1 + tiles) * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)));
    }
  }

  m_indirectRenderingMemory = vk_utils::allocateAndBindWithPadding(m_device, m_physicalDevice,
      allBuffers, VkMemoryAllocateFlags{});



  std::vector<LiteMath::float4> ssaoKernel(SSAO_KERNEL_SIZE);
  for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
  {
    auto sample =
        normalize(float3(randUNorm(rndEngine) * 2.f - 1.f, randUNorm(rndEngine) * 2.f - 1.f, randUNorm(rndEngine)));
    const float scale = static_cast<float>(i) / static_cast<float>(SSAO_KERNEL_SIZE);
    sample *= lerp(0.1f, 1.0f, randUNorm(rndEngine) * scale * scale);
    ssaoKernel[i] = LiteMath::float4(sample.x, sample.y, sample.z, 0.0f);
  }
  m_pScnMgr->GetCopyHelper()->UpdateBuffer(m_ssaoKernel, 0, ssaoKernel.data(), ssaoKernel.size()*sizeof(ssaoKernel[0]));
}

void SimpleRender::UpdateUniformBuffer(float a_time)
{
// most uniforms are updated in GUI -> SetupGUIElements()
  m_uniforms.time = a_time;
  m_uniforms.screenWidth = static_cast<float>(m_width);
  m_uniforms.screenHeight = static_cast<float>(m_height);
  m_uniforms.lightPos = float3(0, std::sin(m_sunAngle), std::cos(m_sunAngle))*10000;
  m_uniforms.enableLandscapeShadows = m_landscapeShadows;
  m_uniforms.enableSsao = m_ssao;
  m_uniforms.tonemappingMode = static_cast<uint32_t>(m_tonemappingMode);
  m_uniforms.exposure = m_exposure;
  std::memcpy(m_uboMappedMem, &m_uniforms, sizeof(m_uniforms));
  std::memcpy(m_shadowmapUboMappedMem, &m_shadowmapUboData, sizeof(m_shadowmapUboData));
}

void SimpleRender::RecordStaticMeshCulling(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo)
{
  cmdBeginRegion(a_cmdBuff, "Static mesh culling");
  vkCmdFillBuffer(a_cmdBuff, visInfo.instanceMappingBuffer, 0, sizeof(uint), 0);

  {
    std::array bufferMemBarriers
    {
      VkBufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .buffer = visInfo.instanceMappingBuffer,
        .offset = 0,
        .size = sizeof(uint)
      }
    };

    vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {},
        0, nullptr,
        static_cast<uint32_t>(bufferMemBarriers.size()), bufferMemBarriers.data(),
        0, nullptr);
  }

  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullingPipeline.pipeline);

  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE,
    m_cullingPipeline.layout, 0, 1, &m_cullingSceneDescriptorSet, 0, nullptr);
  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE,
    m_cullingPipeline.layout, 1, 1, &visInfo.cullingOutputDescriptorSet, 0, nullptr);

  vkCmdPushConstants(a_cmdBuff, m_cullingPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(visInfo.cullingPushConsts), &visInfo.cullingPushConsts);

  vkCmdDispatch(a_cmdBuff, m_pScnMgr->MeshesNum(), 1, 1);

  {
    std::array bufferMemBarriers
    {
      VkBufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        .buffer = visInfo.indirectDrawBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
      },
      VkBufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = visInfo.instanceMappingBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
      }
    };

    vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {},
        0, nullptr,
        static_cast<uint32_t>(bufferMemBarriers.size()), bufferMemBarriers.data(),
        0, nullptr);
  }

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordLandscapeCulling(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo)
{
  cmdBeginRegion(a_cmdBuff, "Landscape culling");

  for (auto& buf : visInfo.landscapeTileBuffers)
  {
    vkCmdFillBuffer(a_cmdBuff, buf, 0, sizeof(uint), 0);
  }

  {
    std::vector<VkBufferMemoryBarrier> bufferMemBarriers;
    bufferMemBarriers.reserve(visInfo.landscapeTileBuffers.size());

    for (auto& buf : visInfo.landscapeTileBuffers)
    {
      bufferMemBarriers.emplace_back(VkBufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .buffer = buf,
        .offset = 0,
        .size = sizeof(uint)
      });
    }

    vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {},
        0, nullptr,
        static_cast<uint32_t>(bufferMemBarriers.size()), bufferMemBarriers.data(),
        0, nullptr);
  }


  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_landscapeCullingPipeline.pipeline);
  vkCmdPushConstants(a_cmdBuff, m_landscapeCullingPipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(visInfo.landscapeCullingPushConsts), &visInfo.landscapeCullingPushConsts);


  for (std::size_t i = 0; i < m_landscapeCullingSceneDescriptorSets.size(); ++i)
  {
    uint32_t drawIndirectOffset = static_cast<uint32_t>(i*2*sizeof(VkDrawIndirectCommand));
    uint32_t landscapeInfoOffset = static_cast<uint32_t>(i*sizeof(LandscapeGpuInfo));
    
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE,
      m_landscapeCullingPipeline.layout, 0, 1, &m_landscapeCullingSceneDescriptorSets[i],
      1, &landscapeInfoOffset);
    
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE,
      m_landscapeCullingPipeline.layout, 1, 1, &visInfo.landscapeCullingOutputDescriptorSets[i],
      1, &drawIndirectOffset);

    vkCmdDispatch(a_cmdBuff, 1, 1, 1);
  }

  {
    std::vector bufferMemBarriers
    {
      VkBufferMemoryBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        .buffer = visInfo.landscapeIndirectDrawBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
      }
    };
    bufferMemBarriers.reserve(visInfo.landscapeTileBuffers.size() + 1);

    for (auto& buf : visInfo.landscapeTileBuffers)
    {
      bufferMemBarriers.emplace_back(
        VkBufferMemoryBarrier {
          .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .buffer = buf,
          .offset = 0,
          .size = VK_WHOLE_SIZE
        });
    }

    vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
          | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT,
        {},
        0, nullptr,
        static_cast<uint32_t>(bufferMemBarriers.size()), bufferMemBarriers.data(),
        0, nullptr);
  }

  cmdEndRegion(a_cmdBuff);
}


void SimpleRender::RecordStaticMeshRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly)
{
  cmdBeginRegion(a_cmdBuff, "Static meshes");

  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS,
    pickGeometryPipeline(m_deferredPipeline, depthOnly));

  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline.layout, 0, 1,
    &m_graphicsDescriptorSet, 0, VK_NULL_HANDLE);

  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline.layout, 1, 1,
    &visInfo.staticMeshVisDescSet, 0, VK_NULL_HANDLE);

  const VkShaderStageFlags stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDeviceSize zero_offset = 0u;
  VkBuffer vertexBuf = m_pScnMgr->GetVertexBuffer();
  VkBuffer indexBuf = m_pScnMgr->GetIndexBuffer();

  vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
  vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

  vkCmdDrawIndexedIndirect(a_cmdBuff, visInfo.indirectDrawBuffer, 0, m_pScnMgr->MeshesNum(), sizeof(VkDrawIndexedIndirectCommand));

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordLandscapeRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly)
{
  cmdBeginRegion(a_cmdBuff, "Landscapes");

  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, pickGeometryPipeline(m_deferredLandscapePipeline, depthOnly));

  for (size_t i = 0; i < m_landscapeMainDescriotorSets.size(); ++i)
  {
    std::vector<uint32_t> dynOffset{static_cast<uint32_t>(i*sizeof(LandscapeGpuInfo))};
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredLandscapePipeline.layout, 0, 1,
      &m_landscapeMainDescriotorSets[i], static_cast<uint32_t>(dynOffset.size()), dynOffset.data());

    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredLandscapePipeline.layout, 1, 1,
      &visInfo.landscapeVisDescSets[i], 0, nullptr);

    vkCmdDrawIndirect(a_cmdBuff, visInfo.landscapeIndirectDrawBuffer, 0, 1, 0);
  }

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordGrassRendering(VkCommandBuffer a_cmdBuff, VisibilityInfo& visInfo, bool depthOnly)
{
  cmdBeginRegion(a_cmdBuff, "Grass");

  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS,
    pickGeometryPipeline(m_deferredGrassPipeline, depthOnly));
  
  for (size_t i = 0; i < m_landscapeMainDescriotorSets.size(); ++i)
  {
    std::vector<uint32_t> dynOffset{static_cast<uint32_t>(i*sizeof(LandscapeGpuInfo))};
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredLandscapePipeline.layout, 0, 1,
      &m_landscapeMainDescriotorSets[i], static_cast<uint32_t>(dynOffset.size()), dynOffset.data());

    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredLandscapePipeline.layout, 1, 1,
      &visInfo.landscapeVisDescSets[i], 0, nullptr);

    vkCmdDrawIndirect(a_cmdBuff, visInfo.landscapeIndirectDrawBuffer, sizeof(VkDrawIndirectCommand), 1, 0);
  }

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordLightResolve(VkCommandBuffer a_cmdBuff)
{
  cmdBeginRegion(a_cmdBuff, "Light resolve");

  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline.pipeline);
        
  std::array dsets {m_lightingDescriptorSet, m_lightingFragmentDescriptorSet};
  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline.layout, 0,
    static_cast<uint32_t>(dsets.size()), dsets.data(), 0, VK_NULL_HANDLE);

  VkShaderStageFlags stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
  vkCmdPushConstants(a_cmdBuff, m_lightingPipeline.layout, stageFlags, 0,
    sizeof(graphicsPushConsts), &graphicsPushConsts);
        
  vkCmdDraw(a_cmdBuff, 1, m_pScnMgr->LightsNum(), 0, 0);

        
  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_globalLightingPipeline.pipeline);
  vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_globalLightingPipeline.layout, 0,
    static_cast<uint32_t>(dsets.size()), dsets.data(), 0, VK_NULL_HANDLE);

  vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordShadowmapRendering(VkCommandBuffer a_cmdBuff)
{
  cmdBeginRegion(a_cmdBuff, "Shadowmaps");

  vk_utils::setDefaultViewport(a_cmdBuff,
      static_cast<float>(SHADOW_MAP_RESOLUTION), static_cast<float>(SHADOW_MAP_RESOLUTION));
  vk_utils::setDefaultScissor(a_cmdBuff, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION);

  for (size_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
  {
    std::string regName = "Cascade ";
    regName += std::to_string(i);
    cmdBeginRegion(a_cmdBuff, regName.c_str());

    
    RecordStaticMeshCulling(a_cmdBuff, m_cascadeVisInfo[i]);
    RecordLandscapeCulling(a_cmdBuff, m_cascadeVisInfo[i]);

    VkClearValue depthClear {
      .depthStencil = {1.0f, 0}
    };
    VkRenderPassBeginInfo shadowPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_shadowmapRenderPass,
      .framebuffer = m_cascadeFramebuffers[i],
      .renderArea = {
        .offset = {0, 0},
        .extent = {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION},
      },
      .clearValueCount = 1,
      .pClearValues = &depthClear,
    };
    vkCmdBeginRenderPass(a_cmdBuff, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdPushConstants(a_cmdBuff, m_deferredLandscapePipeline.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
          | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            0, sizeof(LiteMath::float4x4), &m_cascadeProjMats[i]);

      vkCmdPushConstants(a_cmdBuff, m_deferredLandscapePipeline.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
          | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            sizeof(LiteMath::float4x4), sizeof(LiteMath::float4x4), &m_cascadeViewMats[i]);

      RecordStaticMeshRendering(a_cmdBuff, m_cascadeVisInfo[i], true);

      RecordLandscapeRendering(a_cmdBuff, m_cascadeVisInfo[i], true);
      
      RecordGrassRendering(a_cmdBuff, m_cascadeVisInfo[i], true);
    }
    vkCmdEndRenderPass(a_cmdBuff);

    
    VkClearValue colorClear {
      .color = {{0.f, 0.f, 0.f, 0.f}},
    };
    VkRenderPassBeginInfo shadowPassInfo2 {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_vsmRenderPass,
      .framebuffer = m_vsmFramebuffers[i],
      .renderArea = {
        .offset = {0, 0},
        .extent = {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION},
      },
      .clearValueCount = 1,
      .pClearValues = &colorClear,
    };
    vkCmdBeginRenderPass(a_cmdBuff, &shadowPassInfo2, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vsmPipeline.pipeline);
      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vsmPipeline.layout,
          0, 1, &m_vsmDescriptorSets[i], 0, nullptr);
      vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(a_cmdBuff);

    cmdEndRegion(a_cmdBuff);
  }

  cmdEndRegion(a_cmdBuff);
}

void SimpleRender::RecordFrameCommandBuffer(VkCommandBuffer a_cmdBuff, uint32_t swapchainIdx)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  RecordShadowmapRendering(a_cmdBuff);

  RecordStaticMeshCulling(a_cmdBuff, m_mainVisInfo);
  RecordLandscapeCulling(a_cmdBuff, m_mainVisInfo);

  vk_utils::setDefaultViewport(a_cmdBuff, static_cast<float>(m_width), static_cast<float>(m_height));
  vk_utils::setDefaultScissor(a_cmdBuff, m_width, m_height);

  {
    std::array mainPassClearValues {
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
      VkClearValue {
        .depthStencil = {1.0f, 0}
      },
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
    };

    VkRenderPassBeginInfo mainPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_gbuffer.renderpass,
      .framebuffer = m_mainPassFrameBuffer,
      .renderArea = {
        .offset = {0, 0},
        .extent = m_swapchain.GetExtent(),
      },
      .clearValueCount = static_cast<uint32_t>(mainPassClearValues.size()),
      .pClearValues = mainPassClearValues.data(),
    };

    vkCmdBeginRenderPass(a_cmdBuff, &mainPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdPushConstants(a_cmdBuff, m_deferredLandscapePipeline.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
          | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 0,
            sizeof(graphicsPushConsts), &graphicsPushConsts);
      {
        RecordStaticMeshRendering(a_cmdBuff, m_mainVisInfo, false);

        RecordLandscapeRendering(a_cmdBuff, m_mainVisInfo, false);
        
        RecordGrassRendering(a_cmdBuff, m_mainVisInfo, false);
      }

      vkCmdNextSubpass(a_cmdBuff, VK_SUBPASS_CONTENTS_INLINE);

      {
        RecordLightResolve(a_cmdBuff);
      }
    }
    vkCmdEndRenderPass(a_cmdBuff);
    
    std::array prePostFxClearValues {
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}} // fog
      },
    };
    VkRenderPassBeginInfo prePostFxInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_prePostFxRenderPass,
      .framebuffer = m_prePostFxFramebuffer,
      .renderArea = {
        .offset = {0, 0},
        .extent = {m_width / POSTFX_DOWNSCALE_FACTOR, m_height / POSTFX_DOWNSCALE_FACTOR},
      },
      .clearValueCount = static_cast<uint32_t>(prePostFxClearValues.size()),
      .pClearValues = prePostFxClearValues.data(),
    };

    vkCmdBeginRenderPass(a_cmdBuff, &prePostFxInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fogPipeline.pipeline);
      vkCmdPushConstants(a_cmdBuff, m_fogPipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
          0, sizeof(graphicsPushConsts), &graphicsPushConsts);
      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fogPipeline.layout,
          0, 1, &m_fogDescriptorSet, 0, nullptr);
      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fogPipeline.layout,
          1, 1, &m_lightingFragmentDescriptorSet, 0, nullptr);

      vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);


      if (m_ssao)
      {
        vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline.pipeline);
        vkCmdPushConstants(a_cmdBuff, m_ssaoPipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(graphicsPushConsts), &graphicsPushConsts);
        vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline.layout,
            0, 1, &m_ssaoDescriptorSet, 0, nullptr);

        vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
      }
    }
    vkCmdEndRenderPass(a_cmdBuff);

    
    std::array postFxClearValues {
      VkClearValue {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
      },
    };
    VkRenderPassBeginInfo postFxInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = m_postFxRenderPass,
      .framebuffer = m_framebuffers[swapchainIdx],
      .renderArea = {
        .offset = {0, 0},
        .extent = m_swapchain.GetExtent(),
      },
      .clearValueCount = static_cast<uint32_t>(postFxClearValues.size()),
      .pClearValues = postFxClearValues.data(),
    };

    vkCmdBeginRenderPass(a_cmdBuff, &postFxInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
      vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postFxPipeline.pipeline);
      vkCmdPushConstants(a_cmdBuff, m_postFxPipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
          0, sizeof(graphicsPushConsts), &graphicsPushConsts);
      vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postFxPipeline.layout,
          0, 1, &m_postFxDescriptorSet, 0, nullptr);

      vkCmdDraw(a_cmdBuff, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(a_cmdBuff);
  }

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}


void SimpleRender::CleanupPipelineAndSwapchain()
{
  if (!m_cmdBuffersDrawMain.empty())
  {
    vkFreeCommandBuffers(m_device, m_commandPool, static_cast<uint32_t>(m_cmdBuffersDrawMain.size()),
                         m_cmdBuffersDrawMain.data());
    m_cmdBuffersDrawMain.clear();
  }

  for (size_t i = 0; i < m_frameFences.size(); i++)
  {
    vkDestroyFence(m_device, m_frameFences[i], nullptr);
  }
  m_frameFences.clear();

  ClearGBuffer();
  ClearPostFx();
  ClearShadowmaps();

  m_swapchain.Cleanup();
}

void SimpleRender::RecreateSwapChain()
{
  vkDeviceWaitIdle(m_device);

  ClearPipeline(m_deferredPipeline);
  ClearPipeline(m_deferredLandscapePipeline);
  ClearPipeline(m_deferredGrassPipeline);
  ClearPipeline(m_lightingPipeline);
  ClearPipeline(m_globalLightingPipeline);
  ClearPipeline(m_vsmPipeline);
  ClearPipeline(m_fogPipeline);
  ClearPipeline(m_ssaoPipeline);
  ClearPipeline(m_postFxPipeline);

  CleanupPipelineAndSwapchain();
  auto oldImagesNum = m_swapchain.GetImageCount();
  m_presentationResources.queue = m_swapchain.CreateSwapChain(m_physicalDevice, m_device, m_surface, m_width, m_height,
    oldImagesNum, m_vsync);

  CreateGBuffer();
  CreatePostFx();
  CreateShadowmaps();
  SetupStaticMeshPipeline();
  SetupLandscapePipeline();
  SetupLightingPipeline();
  SetupPostfxPipeline();

  m_frameFences.resize(m_framesInFlight);
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < m_framesInFlight; i++)
  {
    VK_CHECK_RESULT(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameFences[i]));
  }

  m_cmdBuffersDrawMain = vk_utils::createCommandBuffers(m_device, m_commandPool, m_framesInFlight);

  m_pGUIRender->OnSwapchainChanged(m_swapchain);
}

void SimpleRender::Cleanup()
{
  m_pGUIRender = nullptr;
  ImGui::DestroyContext();
  CleanupPipelineAndSwapchain();
  if(m_surface != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }

  if (m_landscapeHeightmapSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_device, m_landscapeHeightmapSampler, nullptr);
    m_landscapeHeightmapSampler = VK_NULL_HANDLE;
  }

  if (m_shadowmapSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_device, m_shadowmapSampler, nullptr);
    m_shadowmapSampler = nullptr;
  }

  if (m_noiseSampler != VK_NULL_HANDLE)
  {
    vkDestroySampler(m_device, m_noiseSampler, nullptr);
    m_noiseSampler = VK_NULL_HANDLE;
  }

  ClearAllPipelines();

  if (m_presentationResources.imageAvailable != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.imageAvailable, nullptr);
    m_presentationResources.imageAvailable = VK_NULL_HANDLE;
  }
  if (m_presentationResources.renderingFinished != VK_NULL_HANDLE)
  {
    vkDestroySemaphore(m_device, m_presentationResources.renderingFinished, nullptr);
    m_presentationResources.renderingFinished = VK_NULL_HANDLE;
  }

  if (m_commandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
  }

  if (m_ubo != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_ubo, nullptr);
    m_ubo = VK_NULL_HANDLE;
  }

  if (m_shadowmapUbo != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_shadowmapUbo, nullptr);
    m_shadowmapUbo = VK_NULL_HANDLE;
  }

  if (m_ssaoKernel != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_ssaoKernel, nullptr);
    m_ssaoKernel = VK_NULL_HANDLE;
  }

  for (auto* visInfo : m_visibilityInfos)
  {
    if (visInfo->indirectDrawBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_device, visInfo->indirectDrawBuffer, nullptr);
      visInfo->indirectDrawBuffer = VK_NULL_HANDLE;
    }

    if (visInfo->landscapeIndirectDrawBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_device, visInfo->landscapeIndirectDrawBuffer, nullptr);
      visInfo->landscapeIndirectDrawBuffer = VK_NULL_HANDLE;
    }

    for (auto& buffer : visInfo->landscapeTileBuffers)
    {
      vkDestroyBuffer(m_device, buffer, nullptr);
    }
    visInfo->landscapeTileBuffers.clear();

    if(visInfo->instanceMappingBuffer != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(m_device, visInfo->instanceMappingBuffer, nullptr);
      visInfo->instanceMappingBuffer = VK_NULL_HANDLE;
    }
  }

  if(m_uboAlloc != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_uboAlloc, nullptr);
    m_uboAlloc = VK_NULL_HANDLE;
  }

  if(m_indirectRenderingMemory != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_indirectRenderingMemory, nullptr);
    m_indirectRenderingMemory = VK_NULL_HANDLE;
  }

  m_pBindings = nullptr;
  m_pScnMgr   = nullptr;

  if(m_device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }

  if(m_debugReportCallback != VK_NULL_HANDLE)
  {
    vkDestroyDebugReportCallbackEXT(m_instance, m_debugReportCallback, nullptr);
    m_debugReportCallback = VK_NULL_HANDLE;
  }

  if(m_instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
}

void SimpleRender::ProcessInput(const AppInput &input)
{
  // add keyboard controls here
  // camera movement is processed separately

  // recreate pipeline to reload shaders
  if(input.keyPressed[GLFW_KEY_B])
  {
#ifdef WIN32
    std::system("cd ../resources/shaders && python compile_simple_render_shaders.py");
#else
    std::system("cd ../resources/shaders && python3 compile_simple_render_shaders.py");
#endif

    ClearAllPipelines();
    
    SetupStaticMeshPipeline();
    SetupLandscapePipeline();
    SetupLightingPipeline();
    SetupPostfxPipeline();
    SetupCullingPipeline();
  }

}

void SimpleRender::UpdateCamera(const Camera* cams, uint32_t a_camsCount)
{
  assert(a_camsCount > 0);
  m_cam = cams[0];
  UpdateView();
}

void SimpleRender::UpdateView()
{
  const float aspect   = float(m_width) / float(m_height);
  const float nearClip = 0.1f;
  const float farClip = 500.0f;
  const auto mProjFix        = OpenglToVulkanProjectionMatrixFix();
  const auto mProj           = projectionMatrix(m_cam.fov, aspect, nearClip, farClip);
  const auto mLookAt         = LiteMath::lookAt(m_cam.pos, m_cam.lookAt, m_cam.up);
  const auto mWorldViewProj  = mProjFix * mProj * mLookAt;
  graphicsPushConsts.proj = mProjFix * mProj;
  graphicsPushConsts.view = mLookAt;

  m_mainVisInfo.cullingPushConsts.projView = mWorldViewProj;
  m_mainVisInfo.landscapeCullingPushConsts.projView = mWorldViewProj;

  {
    const auto lightDir = normalize(-1*float3(0, std::sin(m_sunAngle), std::cos(m_sunAngle)));
    const float clipRange = farClip - nearClip;

    const float minZ = nearClip;
    const float maxZ = nearClip + clipRange;

    const float range = maxZ - minZ;
    const float ratio = maxZ / minZ;

    std::array<float, SHADOW_MAP_CASCADE_COUNT> cascadeSplits;

    // Calculate split depths based on view camera frustum
    // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
    for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
      const float p = (i + 1) / static_cast<float>(SHADOW_MAP_CASCADE_COUNT);
      const float log = minZ * std::pow(ratio, p);
      const float uniform = minZ + range * p;
      const float d = m_cascadeSplitLambda * (log - uniform) + uniform;
      cascadeSplits[i] = (d - nearClip) / clipRange;
    }

    float lastSplitDist = 0.0;
		for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++)
		{
			const float splitDist = cascadeSplits[i];

			std::array frustumCorners {
				float3(-1.0f,  1.0f,  0.0f),
				float3( 1.0f,  1.0f,  0.0f),
				float3( 1.0f, -1.0f,  0.0f),
				float3(-1.0f, -1.0f,  0.0f),
				float3(-1.0f,  1.0f,  1.0f),
				float3( 1.0f,  1.0f,  1.0f),
				float3( 1.0f, -1.0f,  1.0f),
				float3(-1.0f, -1.0f,  1.0f),
			};

			// Project frustum corners into world space
			const auto invCam = LiteMath::inverse4x4(mWorldViewProj);
			for (auto& corner : frustumCorners) {
				auto invCorner = invCam * float4(corner.x, corner.y, corner.z, 1.0f);
				corner = float3(invCorner.x, invCorner.y, invCorner.z) / invCorner.w;
			}

			for (size_t j = 0; j < 4; j++) {
				auto dist = frustumCorners[j + 4] - frustumCorners[j];
				frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
				frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
			}

			// Get frustum center
			auto frustumCenter = float3(0.0f);
			for (auto& corner : frustumCorners) {
				frustumCenter += corner;
			}
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (uint32_t j = 0; j < 8; j++) {
				float distance = length(frustumCorners[j] - frustumCenter);
				radius = std::max(radius, distance);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			const auto maxExtents = float3(radius);
			const auto minExtents = float3(0,0,0) - maxExtents;

            
			const auto lightViewMatrix =
        LiteMath::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, float3(0.0f, 1.0f, 0.0f));
			const auto lightOrthoMatrix =
        ortoMatrix(minExtents.x, maxExtents.x,
            minExtents.y, maxExtents.y,
            -100, maxExtents.z - minExtents.z);

			// Store split distance and matrix in cascade

			m_cascadeViewMats[i] = lightViewMatrix;
			m_cascadeProjMats[i] = mProjFix * lightOrthoMatrix;

      auto viewProj = mProjFix * lightOrthoMatrix * lightViewMatrix;

      m_shadowmapUboData.cascadeViewProjMats[i] = viewProj;
      m_shadowmapUboData.cascadeSplitDepths[i] = (nearClip + splitDist * clipRange) * -1.0f;
      
      m_cascadeVisInfo[i].cullingPushConsts.projView = viewProj;
      m_cascadeVisInfo[i].landscapeCullingPushConsts.projView = viewProj;

			lastSplitDist = cascadeSplits[i];
		}
  }


  // TODO: should this really be here?
  for (auto* visInfo : m_visibilityInfos)
  {
    visInfo->cullingPushConsts.instanceCount = m_pScnMgr->InstancesNum();
    visInfo->cullingPushConsts.modelCount = m_pScnMgr->MeshesNum();
  }
}

void SimpleRender::LoadScene(const char* path, bool transpose_inst_matrices)
{
  m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

  CreateUniformBuffer();
  SetupStaticMeshPipeline();
  SetupLandscapePipeline();
  SetupLightingPipeline();
  SetupPostfxPipeline();
  SetupCullingPipeline();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov = loadedCam.fov;
  m_cam.pos = float3(loadedCam.pos);
  m_cam.up  = float3(loadedCam.up);
  m_cam.lookAt = float3(loadedCam.lookAt);
  m_cam.tdist  = loadedCam.farPlane;

  UpdateView();
}

void SimpleRender::ClearPipeline(pipeline_data_t& pipeline)
{
  if(pipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
    pipeline.layout = VK_NULL_HANDLE;
  }
  if(pipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
  }
}

void SimpleRender::ClearPipeline(SceneGeometryPipeline& pipeline)
{
  if(pipeline.layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
    pipeline.layout = VK_NULL_HANDLE;
  }
  if(pipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
  }
  if(pipeline.shadow != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, pipeline.shadow, nullptr);
    pipeline.shadow = VK_NULL_HANDLE;
  }
  if(pipeline.wireframe != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(m_device, pipeline.wireframe, nullptr);
    pipeline.wireframe = VK_NULL_HANDLE;
  }
}

void SimpleRender::ClearAllPipelines()
{
  ClearPipeline(m_deferredPipeline);
  ClearPipeline(m_deferredLandscapePipeline);
  ClearPipeline(m_deferredGrassPipeline);
  ClearPipeline(m_lightingPipeline);
  ClearPipeline(m_globalLightingPipeline);
  ClearPipeline(m_vsmPipeline);
  ClearPipeline(m_postFxPipeline);
  ClearPipeline(m_fogPipeline);
  ClearPipeline(m_ssaoPipeline);
  ClearPipeline(m_cullingPipeline);
  ClearPipeline(m_landscapeCullingPipeline);
}

void SimpleRender::DrawFrameSimple()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  RecordFrameCommandBuffer(currentCmdBuf, imageIdx);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &currentCmdBuf;

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
                                                 m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}

void SimpleRender::DrawFrame(float a_time, DrawMode a_mode)
{
  UpdateUniformBuffer(a_time);
  switch (a_mode)
  {
  case DrawMode::WITH_GUI:
    SetupGUIElements();
    DrawFrameWithGUI();
    break;
  case DrawMode::NO_GUI:
    DrawFrameSimple();
    break;
  default:
    DrawFrameSimple();
  }
}


/////////////////////////////////

void SimpleRender::SetupGUIElements()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  {
//    ImGui::ShowDemoWindow();
    ImGui::Begin("Simple render settings");

    ImGui::Checkbox("Wireframe", &m_wireframe);
    ImGui::Checkbox("Landscape Shadows", &m_landscapeShadows);
    ImGui::Checkbox("Screenspace ambient occlusion", &m_ssao);

    static const std::array tmNames{"None", "Reinhard", "Hable Filmic", "Exposure", "Approximate ACES"};
    ImGui::Combo("Tone mapping", &m_tonemappingMode, tmNames.data(), static_cast<int>(tmNames.size()));
    ImGui::SliderFloat("Exposure", &m_exposure, 0.0, 10.0);
    ImGui::SliderAngle("Sun", &m_sunAngle);

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::Text("Camera pos: %.3f %.3f %.3f", m_cam.pos.x, m_cam.pos.y, m_cam.pos.z);

    ImGui::NewLine();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),"Press 'B' to recompile and reload shaders");
    ImGui::Text("Changing bindings is not supported.");
    ImGui::End();
  }

  /*
  for (std::size_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
  {
    auto instInfo = m_pScnMgr->GetInstanceInfo(i);
    auto bbox = m_pScnMgr->GetMeshBBox(instInfo.mesh_id);
    auto model = m_pScnMgr->GetInstanceMatrix(i);
    std::array boxPoints {
      float3{bbox.boxMin.x, bbox.boxMin.y, bbox.boxMin.z},
      float3{bbox.boxMin.x, bbox.boxMin.y, bbox.boxMax.z},
      float3{bbox.boxMin.x, bbox.boxMax.y, bbox.boxMin.z},
      float3{bbox.boxMin.x, bbox.boxMax.y, bbox.boxMax.z},
      float3{bbox.boxMax.x, bbox.boxMin.y, bbox.boxMin.z},
      float3{bbox.boxMax.x, bbox.boxMin.y, bbox.boxMax.z},
      float3{bbox.boxMax.x, bbox.boxMax.y, bbox.boxMin.z},
      float3{bbox.boxMax.x, bbox.boxMax.y, bbox.boxMax.z}
    };
    for (auto point : boxPoints)
    {
      auto transformed = graphicsPushConsts.projView * model * float4{point.x, point.y, point.z, 1};
      transformed /= transformed.w;
      ImVec4 color(0, 1, 0, 1);
      if (transformed.x < -0.5 || transformed.x > 0.5 || transformed.y < -0.5 || transformed.y > 0.5)
      {
        color = ImVec4(1, 0, 0, 1);
      }
      if (transformed.z < 0 || transformed.z > 1)
      {
        continue;
      }
      ImGui::GetBackgroundDrawList()->AddCircleFilled({(1 + transformed.x)/2 * m_width, (1 + transformed.y)/2 * m_height}, 2,
          ImGui::GetColorU32(color));
    }
  }

  std::array pts{
    std::pair{ImVec2{m_width/4.f, m_height/4.f}, ImVec2{3*m_width/4.f, m_height/4.f}},
    std::pair{ImVec2{m_width/4.f, m_height/4.f}, ImVec2{m_width/4.f, 3*m_height/4.f}},
    std::pair{ImVec2{3*m_width/4.f, 3*m_height/4.f}, ImVec2{3*m_width/4.f, m_height/4.f}},
    std::pair{ImVec2{3*m_width/4.f, 3*m_height/4.f}, ImVec2{m_width/4.f, 3*m_height/4.f}},
  };

  for (auto[a, b] : pts)
  {
    ImGui::GetBackgroundDrawList()->AddLine(a, b, ImGui::GetColorU32(ImVec4(1, 0, 0, 1)), 2);
  }
  */

  // Rendering
  ImGui::Render();
}

void SimpleRender::DrawFrameWithGUI()
{
  vkWaitForFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame], VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &m_frameFences[m_presentationResources.currentFrame]);

  uint32_t imageIdx;
  auto result = m_swapchain.AcquireNextImage(m_presentationResources.imageAvailable, &imageIdx);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    RecreateSwapChain();
    return;
  }
  else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
  {
    RUN_TIME_ERROR("Failed to acquire the next swapchain image!");
  }

  auto currentCmdBuf = m_cmdBuffersDrawMain[m_presentationResources.currentFrame];

  VkSemaphore waitSemaphores[] = {m_presentationResources.imageAvailable};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  RecordFrameCommandBuffer(currentCmdBuf, imageIdx);

  ImDrawData* pDrawData = ImGui::GetDrawData();
  auto currentGUICmdBuf = m_pGUIRender->BuildGUIRenderCommand(imageIdx, pDrawData);

  std::vector<VkCommandBuffer> submitCmdBufs = { currentCmdBuf, currentGUICmdBuf};

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = (uint32_t)submitCmdBufs.size();
  submitInfo.pCommandBuffers = submitCmdBufs.data();

  VkSemaphore signalSemaphores[] = {m_presentationResources.renderingFinished};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VK_CHECK_RESULT(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameFences[m_presentationResources.currentFrame]));

  VkResult presentRes = m_swapchain.QueuePresent(m_presentationResources.queue, imageIdx,
    m_presentationResources.renderingFinished);

  if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
  {
    RecreateSwapChain();
  }
  else if (presentRes != VK_SUCCESS)
  {
    RUN_TIME_ERROR("Failed to present swapchain image");
  }

  m_presentationResources.currentFrame = (m_presentationResources.currentFrame + 1) % m_framesInFlight;

  vkQueueWaitIdle(m_presentationResources.queue);
}

void SimpleRender::ClearGBuffer()
{
  if (m_gbuffer.renderpass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_gbuffer.renderpass, nullptr);
    m_gbuffer.renderpass = VK_NULL_HANDLE;
  }

  if (m_mainPassFrameBuffer != VK_NULL_HANDLE)
  {
    vkDestroyFramebuffer(m_device, m_mainPassFrameBuffer, nullptr);
    m_mainPassFrameBuffer = VK_NULL_HANDLE;
  }

  auto clearLayer = [this](GBufferLayer& layer)
    {
      vk_utils::deleteImg(m_device, &layer.image);
    };

  for (auto& layer : m_gbuffer.color_layers)
  {
    clearLayer(layer);
  }

  m_gbuffer.color_layers.clear();
  vk_utils::deleteImg(m_device, &m_gbuffer.resolved);

  clearLayer(m_gbuffer.depth_stencil_layer);
}

void SimpleRender::ClearPostFx()
{
  for (auto framebuf : m_framebuffers)
  {
    vkDestroyFramebuffer(m_device, framebuf, nullptr);
  }

  m_framebuffers.clear();

  if (m_prePostFxFramebuffer != VK_NULL_HANDLE)
  {
    vkDestroyFramebuffer(m_device, m_prePostFxFramebuffer , nullptr);
    m_prePostFxFramebuffer = VK_NULL_HANDLE;
  }
  
  if (m_postFxRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_postFxRenderPass, nullptr);
    m_postFxRenderPass = VK_NULL_HANDLE;
  }

  if (m_prePostFxRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_prePostFxRenderPass, nullptr);
    m_prePostFxRenderPass = VK_NULL_HANDLE;
  }
  
  vk_utils::deleteImg(m_device, &m_fogImage);
  vk_utils::deleteImg(m_device, &m_ssaoImage);
  vk_utils::deleteImg(m_device, &m_ssaoNoise);
}

void SimpleRender::ClearShadowmaps()
{
  if (m_shadowmapRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_shadowmapRenderPass, nullptr);
    m_shadowmapRenderPass = VK_NULL_HANDLE;
  }

  if (m_vsmRenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(m_device, m_vsmRenderPass, nullptr);
    m_vsmRenderPass = VK_NULL_HANDLE;
  }

  for (auto& fbuf : m_cascadeFramebuffers)
  {
    if (fbuf != VK_NULL_HANDLE)
    {
      vkDestroyFramebuffer(m_device, fbuf, nullptr);
      fbuf = VK_NULL_HANDLE;
    }
  }

  for (auto& fbuf : m_vsmFramebuffers)
  {
    if (fbuf != VK_NULL_HANDLE)
    {
      vkDestroyFramebuffer(m_device, fbuf, nullptr);
      fbuf = VK_NULL_HANDLE;
    }
  }

  auto destroyViews = [this](std::span<VkImageView> views)
    {
      for (auto& image : views)
      {
        if (image != VK_NULL_HANDLE)
        {
          vkDestroyImageView(m_device, image, nullptr);
          image = VK_NULL_HANDLE;
        }
      }  
    };
  
  destroyViews(m_cascadeViews);
  destroyViews(m_rsmNormalViews);
  destroyViews(m_vsmViews);
  
  vk_utils::deleteImg(m_device, &m_shadowmap);
  vk_utils::deleteImg(m_device, &m_rsmNormals);
  vk_utils::deleteImg(m_device, &m_vsm);
}

void SimpleRender::CreateGBuffer()
{
  auto makeLayer = [this](VkFormat format, VkImageUsageFlagBits usage)
      -> GBufferLayer
    {
      GBufferLayer result{};

      if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      {
        result.image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      }

      if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
      {
        result.image.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      }

      vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height,
        format, usage, &result.image);

      return result;
    };

  std::array layers {
    // Normal
    std::tuple{VK_FORMAT_R16G16B16A16_SFLOAT,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
    // Tangent
    std::tuple{VK_FORMAT_R16G16B16A16_SFLOAT,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
    // Albedo
    std::tuple{VK_FORMAT_R8G8B8A8_UNORM,
      VkImageUsageFlagBits(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)},
  };

  m_gbuffer.color_layers.reserve(layers.size());

  for (auto[format, usage] : layers)
  {
    m_gbuffer.color_layers.push_back(makeLayer(format, usage));
  }

  std::vector<VkFormat> depthFormats = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D32_SFLOAT_S8_UINT,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM_S8_UINT,
      VK_FORMAT_D16_UNORM,
  };
  VkFormat dformat;
  vk_utils::getSupportedDepthFormat(m_physicalDevice, depthFormats, &dformat);

  m_gbuffer.depth_stencil_layer = makeLayer(dformat,
    VkImageUsageFlagBits(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT));


  m_gbuffer.resolved.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width, m_height,
    VK_FORMAT_R16G16B16A16_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &m_gbuffer.resolved);


  // Renderpass
  {
    std::array<VkAttachmentDescription, layers.size() + 2> attachmentDescs;

    attachmentDescs.fill(VkAttachmentDescription {
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      });

    // Color GBuffer layers
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
      attachmentDescs[i].format = m_gbuffer.color_layers[i].image.format;
    }

    // Depth layer
    {
      auto& depth = attachmentDescs[layers.size()];
      depth.format = m_gbuffer.depth_stencil_layer.image.format;
    }
    // Present image
    {
      auto& present = attachmentDescs[layers.size() + 1];
      present.format = m_gbuffer.resolved.format;
    }

    std::array<VkAttachmentReference, layers.size()> gBufferColorRefs;
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
      gBufferColorRefs[i] = VkAttachmentReference
        {static_cast<uint32_t>(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }

    VkAttachmentReference depthRef
      {static_cast<uint32_t>(layers.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    std::array resolveColorRefs {
      VkAttachmentReference
        {static_cast<uint32_t>(layers.size() + 1), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };

    std::array<VkAttachmentReference, layers.size() + 1> resolveInputRefs;
    for (std::size_t i = 0; i <= layers.size(); ++i)
    {
      resolveInputRefs[i] = VkAttachmentReference
        {static_cast<uint32_t>(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }


    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(gBufferColorRefs.size()),
        .pColorAttachments = gBufferColorRefs.data(),
        .pDepthStencilAttachment = &depthRef,
      },
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = static_cast<uint32_t>(resolveInputRefs.size()),
        .pInputAttachments = resolveInputRefs.data(),
        .colorAttachmentCount = static_cast<uint32_t>(resolveColorRefs.size()),
        .pColorAttachments = resolveColorRefs.data(),
      }
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = 0,
        .dstSubpass = 1,
        // Source is gbuffer being written
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        // Destination is reading gbuffer as input attachments in fragment shader
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
      VkSubpassDependency {
        .srcSubpass = 1,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_gbuffer.renderpass))

    setObjectName(m_gbuffer.renderpass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "Build g-buffer RP");
  }

  // Framebuffer
  std::array<VkImageView, layers.size() + 2> attachments;
  for (std::size_t j = 0; j < layers.size(); ++j)
  {
    attachments[j] = m_gbuffer.color_layers[j].image.view;
  }

  attachments[layers.size()] = m_gbuffer.depth_stencil_layer.image.view;
  attachments.back() = m_gbuffer.resolved.view;

  VkFramebufferCreateInfo fbufCreateInfo {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .pNext = NULL,
    .renderPass = m_gbuffer.renderpass,
    .attachmentCount = static_cast<uint32_t>(attachments.size()),
    .pAttachments = attachments.data(),
    .width = m_width,
    .height = m_height,
    .layers = 1,
  };
  VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_mainPassFrameBuffer));
}

void SimpleRender::CreatePostFx()
{
  m_fogImage.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width / POSTFX_DOWNSCALE_FACTOR, m_height / POSTFX_DOWNSCALE_FACTOR,
    VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    &m_fogImage);

  m_ssaoImage.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, m_width / POSTFX_DOWNSCALE_FACTOR, m_height / POSTFX_DOWNSCALE_FACTOR,
    VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    &m_ssaoImage);

  {
    std::vector<LiteMath::float2> ssaoNoise(SSAO_NOISE_DIM * SSAO_NOISE_DIM);
    for (uint32_t i = 0; i < static_cast<uint32_t>(ssaoNoise.size()); i++)
    {
      ssaoNoise[i] = LiteMath::float2(randUNorm(rndEngine) * 2.0f - 1.0f, randUNorm(rndEngine) * 2.0f - 1.0f);
    }

    m_ssaoNoise = vk_utils::allocateColorTextureFromDataLDR(m_device, m_physicalDevice,
      reinterpret_cast<const unsigned char*>(ssaoNoise.data()), SSAO_NOISE_DIM, SSAO_NOISE_DIM,
      1, VK_FORMAT_R8G8_SNORM, m_pScnMgr->GetCopyHelper());
  }
  

  // pre post fx renderpass
  {
    std::array attachmentDescs{
      VkAttachmentDescription {
        .format = m_fogImage.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // no stencil
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      VkAttachmentDescription {
        .format = m_ssaoImage.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // no stencil
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    std::array postfxColorRefs{
      VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      VkAttachmentReference{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };

    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(postfxColorRefs.size()),
        .pColorAttachments = postfxColorRefs.data(),
      },
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_prePostFxRenderPass))

    setObjectName(m_prePostFxRenderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "Pre-postfx RP");
  }

  // Post fx apply renderpass
  {
    std::array attachmentDescs{
      VkAttachmentDescription {
        .format = m_swapchain.GetFormat(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        // no stencil in present img
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      },
    };

    std::array postfxColorRefs{
      VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };

    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(postfxColorRefs.size()),
        .pColorAttachments = postfxColorRefs.data(),
      },
    };

    // Use subpass dependencies for attachment layout transitions
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        // Source is THE PRESENT SEMAPHORE BEING SIGNALED ON THIS PRECISE STAGE!!!!!
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // Destination is swapchain image being filled with gbuffer resolution
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // Semaphore waiting doesn't do any memory ops
        .srcAccessMask = {},
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_postFxRenderPass))
    
    setObjectName(m_postFxRenderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "Postfx RP");
  }

  // Framebuffer
  m_framebuffers.resize(m_swapchain.GetImageCount());
  for (uint32_t i = 0; i < m_framebuffers.size(); ++i)
  {
    std::array attachments{
      m_swapchain.GetAttachment(i).view,
    };

    VkFramebufferCreateInfo fbufCreateInfo {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = nullptr,
      .renderPass = m_postFxRenderPass,
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .width = m_width,
      .height = m_height,
      .layers = 1,
    };

    VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_framebuffers[i]));
  }

  {
    std::array attachments{
      m_fogImage.view,
      m_ssaoImage.view,
    };

    VkFramebufferCreateInfo fbufCreateInfo {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = nullptr,
      .renderPass = m_prePostFxRenderPass,
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .width = m_width / POSTFX_DOWNSCALE_FACTOR,
      .height = m_height / POSTFX_DOWNSCALE_FACTOR,
      .layers = 1,
    };

    VK_CHECK_RESULT(vkCreateFramebuffer(m_device, &fbufCreateInfo, nullptr, &m_prePostFxFramebuffer))
  }
}

void SimpleRender::CreateStackedTexture(vk_utils::VulkanImageMem& mem,
  std::span<VkImageView, SHADOW_MAP_CASCADE_COUNT> cascadeViews,
    VkFormat format, VkImageUsageFlags usage)
{
  VkImageCreateInfo imgInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = VkExtent3D{
      .width = SHADOW_MAP_RESOLUTION,
      .height = SHADOW_MAP_RESOLUTION,
      .depth = 1,
    },
    .mipLevels = 1,
    .arrayLayers = SHADOW_MAP_CASCADE_COUNT,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  auto aspect = static_cast<VkImageAspectFlags>(vk_utils::isDepthFormat(format)
    ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);

  VkImageViewCreateInfo viewInfo {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
    .format = format,
    .subresourceRange = VkImageSubresourceRange{
      .aspectMask = aspect,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = SHADOW_MAP_CASCADE_COUNT,
    },
  };
  
  vk_utils::createImgAllocAndBind(m_device, m_physicalDevice, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION,
    format, 0, &mem, &imgInfo, &viewInfo);

  for (size_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
  {
    VkImageViewCreateInfo info {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = mem.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = VkImageSubresourceRange{
        .aspectMask = aspect,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = static_cast<uint32_t>(i),
        .layerCount = 1,
      },
    };
    vkCreateImageView(m_device, &info, nullptr, &cascadeViews[i]);
  }
}

void SimpleRender::CreateShadowmaps()
{
  auto format = m_gbuffer.depth_stencil_layer.image.format;
  auto rsmNormalsFormat = VK_FORMAT_R8G8B8A8_UNORM;
  auto vsmFormat = VK_FORMAT_R32G32_SFLOAT;

  {
    CreateStackedTexture(m_shadowmap, m_cascadeViews, format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    CreateStackedTexture(m_rsmNormals, m_rsmNormalViews, rsmNormalsFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    CreateStackedTexture(m_vsm, m_vsmViews, vsmFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  }

  {
    std::array attachmentDescs{
      VkAttachmentDescription{
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    VkAttachmentReference depthAttachmentRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pDepthStencilAttachment = &depthAttachmentRef,
      },
    };
    
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_shadowmapRenderPass))

    setObjectName(m_shadowmapRenderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "Shadowmap RP");
  }

  
  {
    std::array attachmentDescs{
      VkAttachmentDescription{
        .format = vsmFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
    };

    std::array colorRefs{
      VkAttachmentReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    
    std::array subpasses {
      VkSubpassDescription {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32_t>(colorRefs.size()),
        .pColorAttachments = colorRefs.data(),
      },
    };
    
    std::array dependencies {
      VkSubpassDependency {
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      },
    };

    VkRenderPassCreateInfo renderPassInfo {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachmentDescs.size()),
      .pAttachments = attachmentDescs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
      .dependencyCount = static_cast<uint32_t>(dependencies.size()),
      .pDependencies = dependencies.data(),
    };

    VK_CHECK_RESULT(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_vsmRenderPass))

    setObjectName(m_vsmRenderPass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "VSM RP");
  }

  for (size_t i = 0; i < m_cascadeViews.size(); ++i)
  {
    VkFramebufferCreateInfo fbufInfo{
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = m_shadowmapRenderPass,
      .attachmentCount = 1,
      .pAttachments = &m_cascadeViews[i],
      .width = SHADOW_MAP_RESOLUTION,
      .height = SHADOW_MAP_RESOLUTION,
      .layers = 1,
    };
    vkCreateFramebuffer(m_device, &fbufInfo, nullptr, &m_cascadeFramebuffers[i]);
    fbufInfo.renderPass = m_vsmRenderPass;
    fbufInfo.pAttachments = &m_vsmViews[i];
    vkCreateFramebuffer(m_device, &fbufInfo, nullptr, &m_vsmFramebuffers[i]);
  }
}
