#include "simple_render.h"
#include "utils/glfw_window.h"

void initVulkanGLFW(IRender& app, GLFWwindow* window, int deviceID)
{
  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions  = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  if(glfwExtensions == nullptr)
  {
    std::cout << "WARNING. Can't connect Vulkan to GLFW window (glfwGetRequiredInstanceExtensions returns NULL)" << std::endl;
  }

  app.InitVulkan(glfwExtensions, glfwExtensionCount, deviceID);

  if(glfwExtensions != nullptr)
  {
    VkSurfaceKHR surface;
    VK_CHECK_RESULT(glfwCreateWindowSurface(app.GetVkInstance(), window, nullptr, &surface));
    setupImGuiContext(window);
    app.InitPresentation(surface);
  }
}

int main()
{
  constexpr int WIDTH = 1024;
  constexpr int HEIGHT = 1024;
  constexpr int VULKAN_DEVICE_ID = 0;


  SimpleRender app(WIDTH, HEIGHT);
  

  auto* window = initWindow(WIDTH, HEIGHT);

  initVulkanGLFW(app, window, VULKAN_DEVICE_ID);

  app.LoadScene("../external/scenes/01_simple_scenes/bunny_cornell.xml", false);

  bool showGUI = true;
  mainLoop(app, window, showGUI);

  return 0;
}
