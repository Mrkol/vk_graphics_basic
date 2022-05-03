#pragma once

#include <vector>

#include <geom/vk_mesh.h>
#include <glm/glm.hpp>
#include <vk_copy.h>

#include "vk_images.h"
#include "../loader_utils/hydraxml.h"
#include "../resources/shaders/common.h"



struct GpuInstanceInfo
{
  uint32_t mesh_id = 0u;
  VkBool32 renderMark = false;
};

struct GpuMeshInfo
{
  uint32_t indexCount;
  uint32_t indexOffset;
  uint32_t vertexOffset;
  glm::vec3 AABB_min{};
  glm::vec3 AABB_max{};
};

struct Landscape
{
  vk_utils::VulkanImageMem heightmap{};
  VkBuffer tileMinMaxHeights;
  VkDeviceMemory allocation;
};

struct LandscapeGpuInfo
{
  glm::mat4 model;
  uint32_t width;
  uint32_t height;
  uint32_t tileSize;
  uint32_t grassDensity;
  char padding[128 - sizeof(glm::vec4) - 4*sizeof(uint32_t)];
};

struct GpuLight
{
  glm::vec4 positionAndOuterRadius{};
  glm::vec4 colorAndInnerRadius{};
};

inline glm::mat4 lmToGlm(const LiteMath::float4x4& lmMat)
{
  glm::mat4 mat;
  for (int i = 0; i < 4; ++i)
  {
    for (int j = 0; j < 4; ++j)
    {
      mat[j][i] = lmMat(i, j);
    }
  }
  return mat;
}

struct SceneManager
{
  SceneManager(VkDevice a_device, VkPhysicalDevice a_physDevice, uint32_t a_transferQId, uint32_t a_graphicsQId,
    bool debug = false);
  ~SceneManager() { DestroyScene(); }

  bool LoadSceneXML(const std::string &scenePath, bool transpose = true);
  void LoadSingleTriangle();

  uint32_t AddMeshFromFile(const std::string& meshPath);
  uint32_t AddMeshFromData(cmesh::SimpleMesh &meshData);
  void AddLandscape();

  uint32_t InstanceMesh(uint32_t meshId, const glm::mat4& matrix, bool markForRender = true);

  void MarkInstance(uint32_t instId);
  void UnmarkInstance(uint32_t instId);

  void DestroyScene();

  VkPipelineVertexInputStateCreateInfo GetPipelineVertexInputStateCreateInfo() { return m_pMeshData->VertexInputLayout();}

  VkBuffer GetVertexBuffer() const { return m_geoVertBuf; }
  VkBuffer GetIndexBuffer()  const { return m_geoIdxBuf; }
  VkBuffer GetModelInfosBuffer() const { return m_meshInfoBuf; }
  
  VkBuffer GetInstanceInfosBuffer()  const { return m_instanceInfosBuffer; }
  VkBuffer GetInstanceMatricesBuffer() const { return m_instanceMatricesBuffer; }

  VkBuffer GetLightsBuffer() const { return m_lightsBuffer; }
  uint32_t LightsNum() const { return (uint32_t) m_sceneLights.size(); }

  std::vector<VkImageView> GetLandscapeHeightmaps() const
  {
    std::vector<VkImageView> result;
    result.reserve(m_landscapes.size());
    for (auto& landscape : m_landscapes)
    {
      result.emplace_back(landscape.heightmap.view);
    }
    return result;
  }

  std::size_t LandscapeNum() const { return m_landscapes.size(); }

  std::vector<uint32_t> LandscapeTileCounts() const
  {
    std::vector<uint32_t> result;
    result.reserve(m_landscapeInfos.size());
    for (auto& landscape : m_landscapeInfos)
    {
      result.emplace_back(landscape.width/landscape.tileSize * landscape.height/landscape.tileSize);
    }
    return result;
  }

  std::vector<VkBuffer> GetLandscapeMinMaxHeights() const
  {
    std::vector<VkBuffer> result;
    result.reserve(m_landscapes.size());
    for (auto& landscape : m_landscapes)
    {
      result.emplace_back(landscape.tileMinMaxHeights);
    }
    return result;
  }

  VkBuffer GetLandscapeInfos() const { return m_landscapeGpuInfos; }

  // Debug stuff
  GpuInstanceInfo GetInstanceInfo(std::size_t i) const { return m_instanceInfos[i]; }
  glm::mat4 GetInstanceMatrix(std::size_t i) const { return m_instanceMatrices[i]; }
  LiteMath::Box4f GetMeshBBox(std::size_t i) const { return m_meshBboxes[i]; }
  // /debug stuff

  std::shared_ptr<vk_utils::ICopyEngine> GetCopyHelper() { return  m_pCopyHelper; }

  uint32_t MeshesNum() const {return (uint32_t)m_meshInfos.size();}
  uint32_t InstancesNum() const {return (uint32_t)m_instanceInfos.size();}

  hydra_xml::Camera GetCamera(uint32_t camId) const;
  MeshInfo GetMeshInfo(uint32_t meshId) const {assert(meshId < m_meshInfos.size()); return m_meshInfos[meshId];}

  glm::mat4 GetInstanceMatrix(uint32_t instId) const {assert(instId < m_instanceMatrices.size()); return m_instanceMatrices[instId];}
  LiteMath::Box4f GetSceneBbox() const {return sceneBbox;}

  void ReloadGPUData();

private:
  void LoadGeoDataOnGPU();
  void FreeGPUResource();

  std::vector<MeshInfo> m_meshInfos = {};
  std::vector<LiteMath::Box4f> m_meshBboxes = {};
  std::shared_ptr<IMeshData> m_pMeshData = nullptr;

  std::vector<GpuInstanceInfo> m_instanceInfos = {};
  std::vector<glm::mat4> m_instanceMatrices = {};

  std::vector<hydra_xml::Camera> m_sceneCameras = {};
  std::vector<hydra_xml::LightInstance> m_sceneLights = {};
  LiteMath::Box4f sceneBbox;

  std::vector<Landscape> m_landscapes;
  std::vector<LandscapeGpuInfo> m_landscapeInfos;
  VkBuffer m_landscapeGpuInfos = VK_NULL_HANDLE;

  uint32_t m_totalVertices = 0u;
  uint32_t m_totalIndices  = 0u;

  VkBuffer m_geoVertBuf = VK_NULL_HANDLE;
  VkBuffer m_geoIdxBuf  = VK_NULL_HANDLE;
  VkBuffer m_meshInfoBuf  = VK_NULL_HANDLE;

  VkBuffer m_instanceInfosBuffer = VK_NULL_HANDLE;
  VkBuffer m_instanceMatricesBuffer = VK_NULL_HANDLE;

  VkBuffer m_lightsBuffer = VK_NULL_HANDLE;

  VkDeviceMemory m_geoMemAlloc = VK_NULL_HANDLE;

  VkDevice m_device = VK_NULL_HANDLE;
  VkPhysicalDevice m_physDevice = VK_NULL_HANDLE;
  uint32_t m_transferQId = UINT32_MAX;
  VkQueue m_transferQ = VK_NULL_HANDLE;

  uint32_t m_graphicsQId = UINT32_MAX;
  VkQueue m_graphicsQ = VK_NULL_HANDLE;
  std::shared_ptr<vk_utils::ICopyEngine> m_pCopyHelper;

  // for debugging
  struct Vertex
  {
    float pos[3];
  };
};
