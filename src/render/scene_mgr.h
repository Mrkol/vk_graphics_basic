#ifndef CHIMERA_SCENE_MGR_H
#define CHIMERA_SCENE_MGR_H

#include <vector>

#include <geom/vk_mesh.h>
#include "LiteMath.h"
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
  LiteMath::float3 AABB_min{};
  LiteMath::float3 AABB_max{};
};

struct Landscape
{
  vk_utils::VulkanImageMem heightmap{};
};

struct LandscapeGpuInfo
{
  LiteMath::float4x4 model;
};

struct GpuLight
{
  LiteMath::float4 positionAndOuterRadius{};
  LiteMath::float4 colorAndInnerRadius{};
};

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

  uint32_t InstanceMesh(uint32_t meshId, const LiteMath::float4x4 &matrix, bool markForRender = true);

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

  // Debug stuff
  GpuInstanceInfo GetInstanceInfo(std::size_t i) const { return m_instanceInfos[i]; }
  LiteMath::float4x4 GetInstanceMatrix(std::size_t i) const { return m_instanceMatrices[i]; }
  LiteMath::Box4f GetMeshBBox(std::size_t i) const { return m_meshBboxes[i]; }
  // /debug stuff

  std::shared_ptr<vk_utils::ICopyEngine> GetCopyHelper() { return  m_pCopyHelper; }

  uint32_t MeshesNum() const {return (uint32_t)m_meshInfos.size();}
  uint32_t InstancesNum() const {return (uint32_t)m_instanceInfos.size();}

  hydra_xml::Camera GetCamera(uint32_t camId) const;
  MeshInfo GetMeshInfo(uint32_t meshId) const {assert(meshId < m_meshInfos.size()); return m_meshInfos[meshId];}

  LiteMath::float4x4 GetInstanceMatrix(uint32_t instId) const {assert(instId < m_instanceMatrices.size()); return m_instanceMatrices[instId];}
  LiteMath::Box4f GetSceneBbox() const {return sceneBbox;}

  void ReloadGPUData();

private:
  void LoadGeoDataOnGPU();
  void FreeGPUResource();

  std::vector<MeshInfo> m_meshInfos = {};
  std::vector<LiteMath::Box4f> m_meshBboxes = {};
  std::shared_ptr<IMeshData> m_pMeshData = nullptr;

  std::vector<GpuInstanceInfo> m_instanceInfos = {};
  std::vector<LiteMath::float4x4> m_instanceMatrices = {};

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

#endif//CHIMERA_SCENE_MGR_H
