#include <map>
#include <array>
#include <random>
#include "scene_mgr.h"
#include "vk_utils.h"
#include "vk_buffers.h"
#include "../loader_utils/hydraxml.h"
#include "perlin.h"


VkTransformMatrixKHR transformMatrixFromFloat4x4(const LiteMath::float4x4 &m)
{
  VkTransformMatrixKHR transformMatrix;
  for(int i = 0; i < 3; ++i)
  {
    for(int j = 0; j < 4; ++j)
    {
      transformMatrix.matrix[i][j] = m(i, j);
    }
  }

  return transformMatrix;
}

SceneManager::SceneManager(VkDevice a_device, VkPhysicalDevice a_physDevice,
  uint32_t a_transferQId, uint32_t a_graphicsQId, bool)
  : m_device(a_device)
  , m_physDevice(a_physDevice)
  , m_transferQId(a_transferQId)
  , m_graphicsQId(a_graphicsQId)
{
  vkGetDeviceQueue(m_device, m_transferQId, 0, &m_transferQ);
  vkGetDeviceQueue(m_device, m_graphicsQId, 0, &m_graphicsQ);
  VkDeviceSize scratchMemSize = 64 * 1024 * 1024;
  m_pCopyHelper = std::make_shared<vk_utils::PingPongCopyHelper>(m_physDevice, m_device, m_transferQ, m_transferQId, scratchMemSize);
  m_pMeshData   = std::make_shared<Mesh8F>();

}

bool SceneManager::LoadSceneXML(const std::string &scenePath, bool transpose)
{
  auto hscene_main = std::make_shared<hydra_xml::HydraScene>();
  auto res         = hscene_main->LoadState(scenePath);

  if(res < 0)
  {
    RUN_TIME_ERROR("LoadSceneXML error");
    return false;
  }

  for(auto loc : hscene_main->MeshFiles())
  {
    auto meshId    = AddMeshFromFile(loc);
    auto instances = hscene_main->GetAllInstancesOfMeshLoc(loc); 
    for(size_t j = 0; j < instances.size(); ++j)
    {
      if(transpose)
        InstanceMesh(meshId, LiteMath::transpose(instances[j]));
      else
        InstanceMesh(meshId, instances[j]);
    }
  }

  for(auto cam : hscene_main->Cameras())
  {
    m_sceneCameras.push_back(cam);
  }

  for (auto light : hscene_main->InstancesLights())
  {
    m_sceneLights.push_back(light);
  }

  /*
  // quick and dirty hacks
  auto randS = [] () { return float(rand()) / float(RAND_MAX) * 2.f - 1.f; };
  auto randU = [] () { return float(rand()) / float(RAND_MAX); };
  for (std::size_t i = 0; i < 200; ++i)
  {
    m_sceneLights.emplace_back(hydra_xml::LightInstance{
        0, 0,
        {}, {},
        LiteMath::translate4x4(float3{randS() * 100, randU() * 10, randS() * 100})
      });
  }
  */

  LoadGeoDataOnGPU();
  hscene_main = nullptr;

  return true;
}

hydra_xml::Camera SceneManager::GetCamera(uint32_t camId) const
{
  if(camId >= m_sceneCameras.size())
  {
    std::stringstream ss;
    ss << "[SceneManager::GetCamera] camera with id = " << camId << " was not loaded, using default camera.";
    vk_utils::logWarning(ss.str());

    hydra_xml::Camera res = {};
    res.fov = 60;
    res.nearPlane = 0.1f;
    res.farPlane  = 1000.0f;
    res.pos[0] = 0.0f; res.pos[1] = 0.0f; res.pos[2] = 15.0f;
    res.up[0] = 0.0f; res.up[1] = 1.0f; res.up[2] = 0.0f;
    res.lookAt[0] = 0.0f; res.lookAt[1] = 0.0f; res.lookAt[2] = 0.0f;

    return res;
  }

  return m_sceneCameras[camId];
}

void SceneManager::LoadSingleTriangle()
{
  std::vector<Vertex> vertices =
  {
    { {  1.0f,  1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f } },
    { {  0.0f, -1.0f, 0.0f } }
  };

  std::vector<uint32_t> indices = { 0, 1, 2 };
  m_totalIndices = static_cast<uint32_t>(indices.size());

  VkDeviceSize vertexBufSize = sizeof(Vertex) * vertices.size();
  VkDeviceSize indexBufSize  = sizeof(uint32_t) * indices.size();
  
  VkMemoryRequirements vertMemReq, idxMemReq; 
  m_geoVertBuf = vk_utils::createBuffer(m_device, vertexBufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &vertMemReq);
  m_geoIdxBuf  = vk_utils::createBuffer(m_device, indexBufSize,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &idxMemReq);

  size_t pad = vk_utils::getPaddedSize(vertMemReq.size, idxMemReq.alignment);

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.pNext           = nullptr;
  allocateInfo.allocationSize  = pad + idxMemReq.size;
  allocateInfo.memoryTypeIndex = vk_utils::findMemoryType(vertMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_physDevice);


  VK_CHECK_RESULT(vkAllocateMemory(m_device, &allocateInfo, nullptr, &m_geoMemAlloc));

  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_geoVertBuf, m_geoMemAlloc, 0));
  VK_CHECK_RESULT(vkBindBufferMemory(m_device, m_geoIdxBuf,  m_geoMemAlloc, pad));
  m_pCopyHelper->UpdateBuffer(m_geoVertBuf, 0, vertices.data(),  vertexBufSize);
  m_pCopyHelper->UpdateBuffer(m_geoIdxBuf,  0, indices.data(), indexBufSize);
}



uint32_t SceneManager::AddMeshFromFile(const std::string& meshPath)
{
  //@TODO: other file formats
  auto data = cmesh::LoadMeshFromVSGF(meshPath.c_str());

  if(data.VerticesNum() == 0)
    RUN_TIME_ERROR(("can't load mesh at " + meshPath).c_str());

  return AddMeshFromData(data);
}

uint32_t SceneManager::AddMeshFromData(cmesh::SimpleMesh &meshData)
{
  assert(meshData.VerticesNum() > 0);
  assert(meshData.IndicesNum() > 0);

  m_pMeshData->Append(meshData);

  MeshInfo info;
  info.m_vertNum = (uint32_t)meshData.VerticesNum();
  info.m_indNum  = (uint32_t)meshData.IndicesNum();

  info.m_vertexOffset = m_totalVertices;
  info.m_indexOffset  = m_totalIndices;

  info.m_vertexBufOffset = info.m_vertexOffset * m_pMeshData->SingleVertexSize();
  info.m_indexBufOffset  = info.m_indexOffset  * m_pMeshData->SingleIndexSize();

  m_totalVertices += (uint32_t)meshData.VerticesNum();
  m_totalIndices  += (uint32_t)meshData.IndicesNum();

  m_meshInfos.push_back(info);
  Box4f meshBox;
  for (uint32_t i = 0; i < meshData.VerticesNum(); ++i) {
    meshBox.include(reinterpret_cast<float4*>(meshData.vPos4f.data())[i]);
  }
  m_meshBboxes.push_back(meshBox);

  return (uint32_t)m_meshInfos.size() - 1;
}

void SceneManager::AddLandscape()
{
  constexpr std::size_t width = 1024;
  constexpr std::size_t height = 1024;
  constexpr std::size_t tileSize = 32;
  constexpr std::size_t grassDensity = 2048;
  constexpr std::array octaves{2.f, 10.f};

  assert(width % tileSize == 0 && height % tileSize == 0);

  const std::size_t tileWidth = width/tileSize;
  const std::size_t tileHeight = width/tileSize;

  std::vector<float> heights(width*height, 0);
  std::vector<LiteMath::float2> tileHeights(tileWidth*tileHeight,
    LiteMath::float2(std::numeric_limits<float>::max(),
      -std::numeric_limits<float>::max()));
  
  for (std::size_t i = 0; i < height; ++i)
  {
    for (std::size_t j = 0; j < width; ++j)
    {
      auto& pixel = heights[i*width + j];
      for (auto o : octaves)
      {
        pixel += .5f * perlin(
          10*o + static_cast<float>(i)/height*o,
          10*o + static_cast<float>(j)/width*o) / o;
      }
      auto& tile = tileHeights[i/tileSize*tileHeight + j/tileSize];
      tile.x = std::min(tile.x, pixel);
      tile.y = std::max(tile.y, pixel);
    }
  }
  
  auto& landscape = m_landscapes.emplace_back(
    Landscape{
      .heightmap = vk_utils::allocateColorTextureFromDataLDR(m_device, m_physDevice,
        reinterpret_cast<const unsigned char*>(heights.data()), width, height,
        1, VK_FORMAT_R32_SFLOAT, m_pCopyHelper),
      .tileMinMaxHeights = vk_utils::createBuffer(m_device,
        tileHeights.size() * sizeof(tileHeights[0]),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT),
    });

  landscape.allocation = vk_utils::allocateAndBindWithPadding(m_device, m_physDevice,
    {landscape.tileMinMaxHeights},
    VkMemoryAllocateFlags{});
  
  m_pCopyHelper->UpdateBuffer(landscape.tileMinMaxHeights, 0,
    tileHeights.data(), tileHeights.size() * sizeof(tileHeights[0]));

  constexpr float scale = 400.f;

  LiteMath::float4x4 mat =
    LiteMath::scale4x4(float3(scale))
      * LiteMath::translate4x4(float3(-0.5f, -0.1f, -0.5f));

  m_landscapeInfos.emplace_back(LandscapeGpuInfo{
    .model = mat,
    .width = static_cast<uint32_t>(width),
    .height = static_cast<uint32_t>(height),
    .tileSize = static_cast<uint32_t>(tileSize),
    .grassDensity = static_cast<uint32_t>(grassDensity),
  });


  // TODO: KOSTYL, REMOVE
  auto randU = [] () { return float(rand()) / float(RAND_MAX); };
  for (std::size_t i = 0; i < 100; ++i)
  {
    float x = randU();
    float y = randU();
    float z = heights[
      static_cast<size_t>(x*static_cast<float>(width))*height
        + static_cast<size_t>(y*static_cast<float>(height))]
          + randU() / scale;
    m_sceneLights.emplace_back(hydra_xml::LightInstance{
        0, 0,
        {}, {},
        mat * LiteMath::translate4x4(float3{x, z, y})
      });
  }
}

uint32_t SceneManager::InstanceMesh(const uint32_t meshId,
  const LiteMath::float4x4 &matrix, bool markForRender)
{
  assert(meshId < m_meshInfos.size());

  //@TODO: maybe move
  m_instanceMatrices.push_back(matrix);

  GpuInstanceInfo info;
  info.mesh_id       = meshId;
  info.renderMark    = markForRender;
  m_instanceInfos.push_back(info);

  return static_cast<uint32_t>(m_instanceMatrices.size() - 1);
}

void SceneManager::MarkInstance(const uint32_t instId)
{
  assert(instId < m_instanceInfos.size());
  m_instanceInfos[instId].renderMark = true;
}

void SceneManager::UnmarkInstance(const uint32_t instId)
{
  assert(instId < m_instanceInfos.size());
  m_instanceInfos[instId].renderMark = false;
}

void SceneManager::ReloadGPUData()
{
  FreeGPUResource();
  LoadGeoDataOnGPU();
}

void SceneManager::LoadGeoDataOnGPU()
{
  VkDeviceSize vertexBufSize = m_pMeshData->VertexDataSize();
  VkDeviceSize indexBufSize  = m_pMeshData->IndexDataSize();
  VkDeviceSize infoBufSize   = m_meshInfos.size() * sizeof(GpuMeshInfo);
  VkDeviceSize instanceInfoBufSize = m_instanceInfos.size() * sizeof(GpuInstanceInfo);
  VkDeviceSize instanceMatrixBufSize = m_instanceMatrices.size() * sizeof(LiteMath::float4x4);
  VkDeviceSize lightsBufSize = m_sceneLights.size() * sizeof(GpuLight);
  VkDeviceSize landscapeInfoBufSize = m_landscapeInfos.size() * sizeof(LandscapeGpuInfo);
  
  m_geoVertBuf  = vk_utils::createBuffer(m_device, vertexBufSize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  m_geoIdxBuf   = vk_utils::createBuffer(m_device, indexBufSize,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  m_meshInfoBuf = vk_utils::createBuffer(m_device, infoBufSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_instanceInfosBuffer = vk_utils::createBuffer(m_device, instanceInfoBufSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_instanceMatricesBuffer = vk_utils::createBuffer(m_device, instanceMatrixBufSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_lightsBuffer = vk_utils::createBuffer(m_device, lightsBufSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  m_landscapeGpuInfos = vk_utils::createBuffer(m_device, landscapeInfoBufSize,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

  VkMemoryAllocateFlags allocFlags {};

  m_geoMemAlloc = vk_utils::allocateAndBindWithPadding(m_device, m_physDevice,
      {m_geoVertBuf, m_geoIdxBuf, m_meshInfoBuf, m_instanceInfosBuffer,
        m_instanceMatricesBuffer, m_lightsBuffer, m_landscapeGpuInfos},
      allocFlags);

  std::vector<GpuMeshInfo> mesh_info_tmp;
  mesh_info_tmp.reserve(m_meshInfos.size());
  for(std::size_t i = 0; i < m_meshInfos.size(); ++i)
  {
    const auto& info = m_meshInfos[i];
    const auto& aabb = m_meshBboxes[i];
    mesh_info_tmp.emplace_back(GpuMeshInfo {
        info.m_indNum, info.m_indexOffset, static_cast<uint32_t>(info.m_vertexOffset),
        LiteMath::float3(aabb.boxMin.x, aabb.boxMin.y, aabb.boxMin.z),
        LiteMath::float3(aabb.boxMax.x, aabb.boxMax.y, aabb.boxMax.z)
      });
  }

  // TODO: proper color and radius
  auto randU = [] () { return float(rand()) / float(RAND_MAX); };
  std::vector<GpuLight> lights_tmp;
  lights_tmp.reserve(m_sceneLights.size());
  for (auto light : m_sceneLights)
  {
    float4 pos = light.matrix.col(3);
    lights_tmp.emplace_back(GpuLight{
        LiteMath::float4(pos.x, pos.y, pos.z, 10 + randU() * 20),
        LiteMath::float4(randU()/2 + .5f, randU()/2 + .5f, randU()/2 + .5f, 0.1f)
      });
  }


  m_pCopyHelper->UpdateBuffer(m_geoVertBuf, 0,
      m_pMeshData->VertexData(), vertexBufSize);

  m_pCopyHelper->UpdateBuffer(m_geoIdxBuf, 0,
      m_pMeshData->IndexData(), indexBufSize);

  m_pCopyHelper->UpdateBuffer(m_meshInfoBuf, 0,
      mesh_info_tmp.data(), mesh_info_tmp.size() * sizeof(mesh_info_tmp[0]));

  m_pCopyHelper->UpdateBuffer(m_instanceInfosBuffer, 0,
      m_instanceInfos.data(), m_instanceInfos.size() * sizeof(m_instanceInfos[0]));

  m_pCopyHelper->UpdateBuffer(m_instanceMatricesBuffer, 0,
      m_instanceMatrices.data(), m_instanceMatrices.size() * sizeof(m_instanceMatrices[0]));

  m_pCopyHelper->UpdateBuffer(m_lightsBuffer, 0,
      lights_tmp.data(), lights_tmp.size() * sizeof(lights_tmp[0]));

  m_pCopyHelper->UpdateBuffer(m_landscapeGpuInfos, 0,
      m_landscapeInfos.data(), m_landscapeInfos.size() * sizeof(m_landscapeInfos[0]));
}

void SceneManager::FreeGPUResource()
{

  if(m_geoVertBuf != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_geoVertBuf, nullptr);
    m_geoVertBuf = VK_NULL_HANDLE;
  }

  if(m_geoIdxBuf != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_geoIdxBuf, nullptr);
    m_geoIdxBuf = VK_NULL_HANDLE;
  }

  if(m_meshInfoBuf != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_meshInfoBuf, nullptr);
    m_meshInfoBuf = VK_NULL_HANDLE;
  }

  if(m_instanceMatricesBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_instanceMatricesBuffer, nullptr);
    m_instanceMatricesBuffer = VK_NULL_HANDLE;
  }

  if(m_instanceInfosBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_instanceInfosBuffer, nullptr);
    m_instanceInfosBuffer = VK_NULL_HANDLE;
  }

  if(m_lightsBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_lightsBuffer, nullptr);
    m_lightsBuffer = VK_NULL_HANDLE;
  }

  if (m_landscapeGpuInfos != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(m_device, m_landscapeGpuInfos, nullptr);
    m_landscapeGpuInfos = VK_NULL_HANDLE;
  }

  if(m_geoMemAlloc != VK_NULL_HANDLE)
  {
    vkFreeMemory(m_device, m_geoMemAlloc, nullptr);
    m_geoMemAlloc = VK_NULL_HANDLE;
  }

  for (auto& landscape : m_landscapes)
  {
    vkDestroyBuffer(m_device, landscape.tileMinMaxHeights, nullptr);
    vkFreeMemory(m_device, landscape.allocation, nullptr);
    vk_utils::deleteImg(m_device, &landscape.heightmap);
  }
  m_landscapes.clear();
}

void SceneManager::DestroyScene()
{
  FreeGPUResource();

  m_pCopyHelper = nullptr;

  m_meshInfos.clear();
  m_pMeshData = nullptr;
  m_instanceInfos.clear();
  m_instanceMatrices.clear();
}