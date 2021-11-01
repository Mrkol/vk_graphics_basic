#ifndef SIMPLE_COMPUTE_H
#define SIMPLE_COMPUTE_H

#define VK_NO_PROTOTYPES
#include "../../render/compute_common.h"
#include "../resources/shaders/common.h"
#include <vk_descriptor_sets.h>
#include <vk_copy.h>

#include <string>
#include <iostream>
#include <memory>

class SimpleCompute : public ICompute
{
public:
  SimpleCompute(uint32_t a_length);
  ~SimpleCompute()  { Cleanup(); };

  inline VkInstance   GetVkInstance() const override { return m_instance; }
  void InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId) override;

  void Execute() override;

  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  // debugging utils
  //
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallbackFn(
    VkDebugReportFlagsEXT /*flags*/, VkDebugReportObjectTypeEXT /*objectType*/,
    uint64_t /*object*/, size_t /*location*/, int32_t /*messageCode*/,
    const char* pLayerPrefix,
    const char* pMessage,
    void* /*pUserData*/)
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
private:
  constexpr static uint32_t GROUP_SIZE = 32;
  constexpr static uint32_t BLOCK_SIZE = GROUP_SIZE*2;

  VkInstance       m_instance       = VK_NULL_HANDLE;
  VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice         m_device         = VK_NULL_HANDLE;
  VkQueue          m_computeQueue   = VK_NULL_HANDLE;
  VkQueue          m_transferQueue  = VK_NULL_HANDLE;

  vk_utils::QueueFID_T m_queueFamilyIDXs {UINT32_MAX, UINT32_MAX, UINT32_MAX};

  VkCommandBuffer m_cmdBufferCompute;
  VkFence m_fence;

  std::unique_ptr<vk_utils::DescriptorMaker> m_pBindings = nullptr;

  uint32_t m_length;
  
  VkPhysicalDeviceFeatures m_enabledDeviceFeatures = {};
  std::vector<const char*> m_deviceExtensions      = {};
  std::vector<const char*> m_instanceExtensions    = {};

  bool m_enableValidation;
  std::vector<const char*> m_validationLayers;
  std::unique_ptr<vk_utils::ICopyEngine> m_pCopyHelper;

  std::vector<VkDescriptorSet>       m_firstPassDS; 
  VkDescriptorSetLayout m_firstPassDSLayout = nullptr;
  
  std::vector<VkPipeline> m_pipelines;
  VkPipelineLayout m_layout;

  VkBuffer m_A;
  std::vector<uint32_t> m_bufSizes;
  std::vector<VkBuffer> m_recursiveSums;

  uint32_t div(uint32_t x) { return (x + BLOCK_SIZE - 1) / BLOCK_SIZE; }
  uint32_t round(uint32_t x) { return div(x) * BLOCK_SIZE; }
 
  void CreateInstance();
  void CreateDevice(uint32_t a_deviceId);

  void BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff);

  void SetupSimplePipeline();
  void CreateComputePipeline();
  void CleanupPipeline();

  void Cleanup();

  void SetupValidationLayers();
};


#endif //SIMPLE_COMPUTE_H
