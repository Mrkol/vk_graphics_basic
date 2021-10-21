#include "simple_compute.h"

#include <iomanip>

#include <vk_pipeline.h>
#include <vk_buffers.h>
#include <vk_utils.h>

SimpleCompute::SimpleCompute(uint32_t a_length) : m_length(a_length)
{
#ifdef NDEBUG
  m_enableValidation = false;
#else
  m_enableValidation = true;
#endif
}

void SimpleCompute::SetupValidationLayers()
{
  m_validationLayers.push_back("VK_LAYER_KHRONOS_validation");
  m_validationLayers.push_back("VK_LAYER_LUNARG_monitor");
}

void SimpleCompute::InitVulkan(const char** a_instanceExtensions, uint32_t a_instanceExtensionsCount, uint32_t a_deviceId)
{
  m_instanceExtensions.clear();
  for (uint32_t i = 0; i < a_instanceExtensionsCount; ++i) {
    m_instanceExtensions.push_back(a_instanceExtensions[i]);
  }
  SetupValidationLayers();
  VK_CHECK_RESULT(volkInitialize());
  CreateInstance();
  volkLoadInstance(m_instance);

  CreateDevice(a_deviceId);
  volkLoadDevice(m_device);

  m_commandPool = vk_utils::createCommandPool(m_device, m_queueFamilyIDXs.compute, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  m_cmdBufferCompute = vk_utils::createCommandBuffers(m_device, m_commandPool, 1)[0];
  
  m_pCopyHelper = std::make_unique<vk_utils::SimpleCopyHelper>(m_physicalDevice, m_device, m_transferQueue, m_queueFamilyIDXs.compute, 8*1024*1024);
}


void SimpleCompute::CreateInstance()
{
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext = nullptr;
  appInfo.pApplicationName = "VkRender";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "SimpleCompute";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_MAKE_VERSION(1, 1, 0);

  m_instance = vk_utils::createInstance(m_enableValidation, m_validationLayers, m_instanceExtensions, &appInfo);
  if (m_enableValidation)
    vk_utils::initDebugReportCallback(m_instance, &debugReportCallbackFn, &m_debugReportCallback);
}

void SimpleCompute::CreateDevice(uint32_t a_deviceId)
{
  m_physicalDevice = vk_utils::findPhysicalDevice(m_instance, true, a_deviceId, m_deviceExtensions);

  m_device = vk_utils::createLogicalDevice(m_physicalDevice, m_validationLayers, m_deviceExtensions,
                                           m_enabledDeviceFeatures, m_queueFamilyIDXs,
                                           VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);

  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.compute, 0, &m_computeQueue);
  vkGetDeviceQueue(m_device, m_queueFamilyIDXs.transfer, 0, &m_transferQueue);
}


void SimpleCompute::SetupSimplePipeline()
{
  // Создание и аллокация буферов
  m_A = vk_utils::createBuffer(m_device, sizeof(float) * round(m_length),
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  for (auto sz = round(m_length); sz > BLOCK_SIZE; sz = round(div(sz)))
  {
    m_bufSizes.push_back(sz);
    m_recursiveSums.push_back(
      vk_utils::createBuffer(m_device, sizeof(float) * sz,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    );
  }

  m_bufSizes.push_back(BLOCK_SIZE);
  m_recursiveSums.push_back(
      vk_utils::createBuffer(m_device, sizeof(float) * BLOCK_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
  );
  
  vk_utils::allocateAndBindWithPadding(m_device, m_physicalDevice, {m_A}, {});
  vk_utils::allocateAndBindWithPadding(m_device, m_physicalDevice, m_recursiveSums, {});


  m_pBindings = std::make_unique<vk_utils::DescriptorMaker>(m_device,
    std::vector{std::pair{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      uint32_t(2*(m_bufSizes.size() + 1))}}, m_bufSizes.size() + 1);

  // Создание descriptor set для передачи буферов в шейдер

  VkBuffer prev = nullptr;
  VkBuffer curr = m_A;
  for (auto buf : m_recursiveSums)
  {
    prev = curr;
    curr = buf;
    m_pBindings->BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
    m_pBindings->BindBuffer(0, prev);
    m_pBindings->BindBuffer(1, curr);
    m_pBindings->BindEnd(&m_firstPassDS.emplace_back(), &m_firstPassDSLayout);
  }

  m_pBindings->BindBegin(VK_SHADER_STAGE_COMPUTE_BIT);
  m_pBindings->BindBuffer(0, m_recursiveSums.back());
  m_pBindings->BindBuffer(1, m_recursiveSums.back()); // THIS IS A KOSTYL, THIS SHOULD BE NULL
  m_pBindings->BindEnd(&m_firstPassDS.emplace_back(), &m_firstPassDSLayout);

  // Заполнение буферов
  std::vector<float> values(m_bufSizes.front());
  for (uint32_t i = 0; i < m_length; ++i) {
    values[i] = 1.f / (i + 1) * (i % 2 == 0 ? 1 : -1);
  }
  for (size_t i = m_length; i < m_bufSizes.front(); ++i)
  {
    values[i] = 0;
  }
  m_pCopyHelper->UpdateBuffer(m_A, 0, values.data(), sizeof(float) * values.size());
}

void SimpleCompute::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  // Заполняем буфер команд
  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));


  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[0]);

  vkCmdPushConstants(a_cmdBuff, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_length), &m_length);

  for (uint32_t i = 0; i < m_bufSizes.size(); ++i)
  {
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_firstPassDS[i], 0, NULL);

    vkCmdPushConstants(a_cmdBuff, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(m_length), sizeof(i), &i);

    vkCmdDispatch(a_cmdBuff, m_bufSizes[i] / BLOCK_SIZE, 1, 1);

    {
      VkBufferMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = m_recursiveSums[i],
        .offset = 0,
        .size = m_bufSizes[i] * sizeof(float)
      };

      vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {},
        0, nullptr,
        1, &barrier,
        0, nullptr
      );
    }
  }
    
  vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelines[1]);

  for (uint32_t i_ = 0; i_ < m_bufSizes.size(); ++i_)
  {
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0,
      1, &m_firstPassDS[m_firstPassDS.size() - 1 - i_], 0, nullptr);

    vkCmdPushConstants(a_cmdBuff, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(m_length), sizeof(i_), &i_);

    uint32_t i = m_bufSizes.size() - 1 - i_;

    vkCmdDispatch(a_cmdBuff, m_bufSizes[i] / BLOCK_SIZE, 1, 1);

    {
      VkBufferMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = m_recursiveSums[i],
        .offset = 0,
        .size = m_bufSizes[i] * sizeof(float)
      };

      vkCmdPipelineBarrier(a_cmdBuff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {},
        0, nullptr,
        1, &barrier,
        0, nullptr
      );
    }
  }


  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}


void SimpleCompute::CleanupPipeline()
{
  if (m_cmdBufferCompute)
  {
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_cmdBufferCompute);
  }

  vkDestroyBuffer(m_device, m_A, nullptr);
  for (auto buf : m_recursiveSums)
  {
    vkDestroyBuffer(m_device, buf, nullptr);
  }

  vkDestroyPipelineLayout(m_device, m_layout, nullptr);
  for (auto pipeline : m_pipelines)
  {
    vkDestroyPipeline(m_device, pipeline, nullptr);
  }
}

void SimpleCompute::Cleanup()
{
  CleanupPipeline();

  if (m_commandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  }
}


void SimpleCompute::CreateComputePipeline()
{
  // Загружаем шейдер
  auto createSM = [this](const char* path)
    {
      std::vector<uint32_t> code = vk_utils::readSPVFile(path);
      VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size()*sizeof(uint32_t),
        .pCode = code.data()
      };
        
      VkShaderModule shaderModule;
      VK_CHECK_RESULT(vkCreateShaderModule(m_device, &createInfo, NULL, &shaderModule));

      return shaderModule;
    };

  auto firstSM = createSM("../resources/shaders/simple_first_pass.comp.spv");
  auto secondSM = createSM("../resources/shaders/simple_second_pass.comp.spv");





  VkPushConstantRange pcRange{
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .offset = 0,
    .size =  sizeof(m_length) + sizeof(uint32_t)
  };

  // Создаём layout для pipeline
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
  pipelineLayoutCreateInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCreateInfo.setLayoutCount = 1;
  pipelineLayoutCreateInfo.pSetLayouts    = &m_firstPassDSLayout;
  pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
  pipelineLayoutCreateInfo.pPushConstantRanges = &pcRange;
  VK_CHECK_RESULT(vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, NULL, &m_layout));

  std::array pipelineCreateInfos {
    VkComputePipelineCreateInfo {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = firstSM,
        .pName = "main"
      },
      .layout = m_layout
    },
    VkComputePipelineCreateInfo {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = secondSM,
        .pName = "main"
      },
      .layout = m_layout
    }
  };

  // Создаём pipeline - объект, который выставляет шейдер и его параметры
  m_pipelines.resize(pipelineCreateInfos.size());
  VK_CHECK_RESULT(vkCreateComputePipelines(m_device, VK_NULL_HANDLE,
    pipelineCreateInfos.size(), pipelineCreateInfos.data(), NULL, m_pipelines.data()));

  vkDestroyShaderModule(m_device, firstSM, nullptr);
  vkDestroyShaderModule(m_device, secondSM, nullptr);
}


void SimpleCompute::Execute()
{
  SetupSimplePipeline();
  CreateComputePipeline();

  BuildCommandBufferSimple(m_cmdBufferCompute);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_cmdBufferCompute;

  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCreateInfo.flags = 0;
  VK_CHECK_RESULT(vkCreateFence(m_device, &fenceCreateInfo, NULL, &m_fence));

  // Отправляем буфер команд на выполнение
  VK_CHECK_RESULT(vkQueueSubmit(m_computeQueue, 1, &submitInfo, m_fence));

  //Ждём конца выполнения команд
  VK_CHECK_RESULT(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, 100000000000));


  std::cout << "Output:\n";
  {
    std::vector<float> values(m_length);
    m_pCopyHelper->ReadBuffer(m_recursiveSums.front(), 0, values.data(), sizeof(float) * values.size());
    std::cout << std::setprecision(16);
    for (auto v : values)
    {
      std::cout << v << ' ';
    }
    std::cout << '\n';
  }
  
}
