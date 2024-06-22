/* Stdlib headers */
#include <stdio.h>            /* For printf(), mainly */
#include <stdlib.h>           /* For allocation with malloc() and free() */

/* Neushoorn headers */
#include <nh_base.h>          /* For consts, types and macros */
#include <nh_vector.h>
#include <nh_arena.h>         /* For an arena allocator */
#include <nh_fileio.h>        /* For file I/O */

/* SDL headers */
#include <SDL2/SDL.h>         /* Window management */
#include <SDL2/SDL_vulkan.h>  /* For required extensions and Vulkan surface */

/* Vulkan headers */
#include <vulkan/vulkan.h>    /* Graphics API */

/* Consts */
#define ARENA_SIZE          (1024 * 1024 * 4) /* Allocate 4MiB arenas */
#ifdef DEBUG
#define USE_VALIDATION      (1)               /* Enable validation layers */
#define NUM_LAYERS          (2)               /* Number of layers */
#define NUM_INST_EXTS       (1)               /* Number of instance extensions */
const char *layers[] = {
  "VK_LAYER_KHRONOS_validation",
  "VK_LAYER_NV_optimus"
};
const char *inst_extensions[] = {
  VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
#else
#define USE_VALIDATION      (0)               /* Disable validation layers */
#define NUM_LAYERS          (0)               /* Number of layers */
#define NUM_INST_EXTS       (0)               /* Number of instance extensions */
const char **layers = NULL;
const char **inst_extensions = NULL;
#endif
const char *dev_extensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#define NUM_DEV_EXTS        (1)               /* Number of device extensions */

/* Macros */
/* Check for Vulkan errors */
#define VK_CHECK(expr)      do{VkResult r=(expr);if(r!=VK_SUCCESS){printf("ERROR %d: %s\n",r,#expr);}}while(0)

/* Window struct */
typedef struct window_t {
  SDL_Window *handle;                     /* The window handle */
  bool running;                           /* Is the window running? */
  nh_vec2i_t size;                        /* The window size */
  usize ticks;                            /* The ticks so far (main loop) */
  bool resized;                           /* Has the window been resized? */
} window_t;

/* Vulkan renderer struct */
typedef struct renderer_t {
  nh_arena_t arena;                       /* The renderer arena allocator */
  window_t *window;                       /* The window */
  VkInstance instance;                    /* The Vulkan instance */
  VkDebugUtilsMessengerEXT debug_messenger; /* The debug messenger */
  VkSurfaceKHR surface;                   /* The window surface */
  VkPhysicalDevice physical_device;       /* The physical device */
  VkDevice device;                        /* The logical device */
  struct {
    u32 graphics_family;                  /* The index of the graphics queue */
    u32 present_family;                   /* The index of the present queue */
    u32 indices[2];                       /* The queue family indices */
  } queue_family_indices;                 /* The queue family indices */
  VkQueue graphics_queue;                 /* The graphics queue */
  VkQueue present_queue;                  /* The present queue */
  struct {
    VkSurfaceCapabilitiesKHR surface_caps;/* The surface capabilities */
    VkSurfaceFormatKHR surface_format;    /* The surface format */
    VkPresentModeKHR present_mode;        /* The present mode */
  } surface_info;                         /* The surface information */            
  VkExtent2D swapchain_extent;            /* The swapchain extent */
  VkSwapchainKHR swapchain;               /* The swapchain */
  u32 swapchain_image_count;              /* The number of swapchain images */
  VkImage *swapchain_images;              /* The swapchain images */
  VkImageView *swapchain_image_views;     /* The swapchain image views */
  VkFramebuffer *swapchain_framebuffers;  /* The swapchain framebuffers */
  VkRenderPass render_pass;               /* The render pass */
  VkShaderModule vert_shader_module;      /* The vertex shader module */
  VkShaderModule frag_shader_module;      /* The fragment shader module */
  VkViewport viewport;                    /* The viewport */
  VkRect2D scissor;                       /* The scissor */
  VkPipelineLayout pipeline_layout;       /* The pipeline layout */
  VkPipeline graphics_pipeline;           /* The graphics pipeline */
  VkCommandPool command_pool;             /* The command pool */
  VkCommandBuffer command_buffer;         /* The command buffer */
  VkSemaphore image_available_semaphore;  /* The image available semaphore */
  VkSemaphore render_finished_semaphore;  /* The render finished semaphore */
  VkFence in_flight_fence;                /* The in flight fence */
} renderer_t;

/* Debug messenger callback */
static VkBool32 debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
  switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      printf("ERROR (VULKAN): %s\n", pCallbackData->pMessage);
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      printf("WARNING (VULKAN): %s\n", pCallbackData->pMessage);
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      printf("INFO (VULKAN): %s\n", pCallbackData->pMessage);
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      printf("VERBOSE (VULKAN): %s\n", pCallbackData->pMessage);
      break;
    default:
      printf("UNKNOWN (VULKAN): %s\n", pCallbackData->pMessage);
      break;
  }
  (void)messageType;
  (void)pUserData;
  return VK_FALSE;
}

/* Load vulkan extension functions */
/* vkCreateDebugUtilsMessengerEXT */
VkResult vkCreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger
) {
  PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
    (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance,
      "vkCreateDebugUtilsMessengerEXT"
    );
  return vkCreateDebugUtilsMessengerEXT(
    instance,
    pCreateInfo,
    pAllocator,
    pDebugMessenger
  );
}
/* vkDestroyDebugUtilsMessengerEXT */
void vkDestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator
) {
  PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT =
    (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance,
      "vkDestroyDebugUtilsMessengerEXT"
    );
  vkDestroyDebugUtilsMessengerEXT(
    instance,
    debugMessenger,
    pAllocator
  );
}

/* Window functions */
/* Create a window */
void create_window(window_t *window, const char *title, nh_vec2i_t size) {
  printf("INFO: Creating window...\n");
  /* Create window */
  window->handle = SDL_CreateWindow(
    title,                                    /* Title */
    SDL_WINDOWPOS_UNDEFINED,                  /* Position - x */
    SDL_WINDOWPOS_UNDEFINED,                  /* Position - y */
    size.x,                                   /* Width */
    size.y,                                   /* Height */
    SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE  /* Flags */
  );

  /* Set initial state */
  window->ticks = 0;
  window->size = size;
  window->running = true;
  window->resized = false;
}
/* Update a window */
void update_window(window_t *window) {
  SDL_Event event;
  window->resized = false;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      window->running = false;
    }
    if (event.type == SDL_WINDOWEVENT) {
      if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
        window->size.x = event.window.data1;
        window->size.y = event.window.data2;
        window->resized = true;
      }
    }
  }
  window->ticks++;
}
/* Create Vulkan surface for a window */
VkSurfaceKHR create_window_surface(window_t *window, VkInstance instance) {
  VkSurfaceKHR surface;
  SDL_Vulkan_CreateSurface(window->handle, instance, &surface);
  return surface;
}
/* Destroy a window */
void destroy_window(window_t *window) {
  printf("INFO: Destroying window...\n");
  SDL_DestroyWindow(window->handle);
}

/* Renderer functions */
/* Create the instance */
void renderer_create_instance(renderer_t *renderer) {
  /* Variables */
  VkApplicationInfo app_info;
  VkInstanceCreateInfo inst_cinfo;
  u32 i, j;
  char **layers_used = NULL;
  char **inst_extensions_used = NULL;
  u32 layers_used_count = 0, inst_extensions_used_count = 0;
  VkLayerProperties *layers_supported;
  VkExtensionProperties *inst_extensions_supported = NULL;
  u32 layers_supported_count = 0, inst_extensions_supported_count = 0;

  printf("INFO: Creating instance...\n");

  /* Set the layers used */
#ifdef USE_VALIDATION
  layers_used =
    (char**)nh_arena_alloc(&renderer->arena, sizeof(char *) * NUM_LAYERS);
  layers_used_count = NUM_LAYERS;
  memcpy(layers_used, layers, sizeof(char *) * NUM_LAYERS);
  /* Print the layers used */
  for (i = 0; i < layers_used_count; i++) {
    printf("INFO: Using layer %d: %s\n", i, layers_used[i]);
  }
#endif

  /* Get the required instance extensions */
  SDL_Vulkan_GetInstanceExtensions(
      renderer->window->handle,
      &inst_extensions_used_count,
      NULL
  );
  inst_extensions_used = (char**)nh_arena_alloc(
      &renderer->arena,
      sizeof(char *) * (inst_extensions_used_count+NUM_INST_EXTS)
  );
  SDL_Vulkan_GetInstanceExtensions(
      renderer->window->handle,
      &inst_extensions_used_count,
      (const char **)inst_extensions_used
  );
  memcpy(
      inst_extensions_used+inst_extensions_used_count,
      inst_extensions,
      sizeof(char *) * NUM_INST_EXTS
  );
  inst_extensions_used_count += NUM_INST_EXTS;
  /* Print the extensions used */
  for (i = 0; i < inst_extensions_used_count; i++) {
    printf("INFO: Using extension %d: %s\n", i, inst_extensions_used[i]);
  }

  /* Check for supported layers */
  /* Get layers */
  VK_CHECK(vkEnumerateInstanceLayerProperties(&layers_supported_count, NULL));
  layers_supported = (VkLayerProperties*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkLayerProperties) * layers_supported_count
  );
  VK_CHECK(vkEnumerateInstanceLayerProperties(&layers_supported_count, layers_supported));
  /* Check for layers */
  for (i = 0; i < layers_used_count; i++) {
    for (j = 0; j < layers_supported_count; j++) {
      if (strcmp(layers_used[i], layers_supported[j].layerName) == 0) {
        break;
      }
    }
    if (j == layers_supported_count) {
      printf("ERROR: Layer %s not supported\n", layers_used[i]);
      exit(1);
    }
  }
  printf("INFO: All layers supported\n");

  /* Check for supported extensions */
  /* Get extensions */
  VK_CHECK(vkEnumerateInstanceExtensionProperties(
      NULL,
      &inst_extensions_supported_count,
      NULL
  ));
  inst_extensions_supported = (VkExtensionProperties*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkExtensionProperties) * inst_extensions_supported_count
  );
  VK_CHECK(vkEnumerateInstanceExtensionProperties(
      NULL,
      &inst_extensions_supported_count,
      inst_extensions_supported
  ));
  /* Check for extensions */
  for (i = 0; i < inst_extensions_used_count; i++) {
    for (j = 0; j < inst_extensions_supported_count; j++) {
      if (strcmp(
            inst_extensions_used[i],
            inst_extensions_supported[j].extensionName
          ) == 0) {
        break;
      }
    }
    if (j == inst_extensions_supported_count) {
      printf("ERROR: Extension %s not supported\n", inst_extensions_used[i]);
      exit(1);
    }
  }
  printf("INFO: All extensions supported\n");

  /* Set the application info */
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = NULL;
  app_info.pApplicationName = "Test";
  app_info.applicationVersion = 0;
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = 0;
  app_info.apiVersion = VK_API_VERSION_1_0;

  /* Set the instance creation info */
  inst_cinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_cinfo.pNext = NULL;
  inst_cinfo.flags = 0;
  inst_cinfo.pApplicationInfo = &app_info;
  inst_cinfo.enabledLayerCount = layers_used_count;
  inst_cinfo.ppEnabledLayerNames = (const char **)layers_used;
  inst_cinfo.enabledExtensionCount = inst_extensions_used_count;
  inst_cinfo.ppEnabledExtensionNames = (const char **)inst_extensions_used;

  /* Create the instance */
  VK_CHECK(vkCreateInstance(&inst_cinfo, NULL, &renderer->instance));
}
/* Create the debug messenger */
void renderer_create_debug_messenger(renderer_t *renderer) {
  /* Variables */
  VkDebugUtilsMessengerCreateInfoEXT debug_messenger_cinfo;

  printf("INFO: Creating debug messenger...\n");

  /* Set the debug messenger info */
  debug_messenger_cinfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debug_messenger_cinfo.pNext = NULL;
  debug_messenger_cinfo.flags = 0;
  debug_messenger_cinfo.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug_messenger_cinfo.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  debug_messenger_cinfo.pfnUserCallback = debug_messenger_callback;

  /* Create the debug messenger */
  VK_CHECK(vkCreateDebugUtilsMessengerEXT(
      renderer->instance,
      &debug_messenger_cinfo,
      NULL,
      &renderer->debug_messenger
  ));
}
/* Create the surface */
void renderer_create_surface(renderer_t *renderer) {
  printf("INFO: Creating surface...\n");
  SDL_Vulkan_CreateSurface(
      renderer->window->handle,
      renderer->instance,
      &renderer->surface
  );
}
/* Pick a physical device */
void renderer_pick_physical_device(renderer_t *renderer) {
  /* Varialbes */
  u32 i;
  u32 physical_device_count = 0;
  VkPhysicalDevice *physical_devices = NULL;

  /* Get the physical devices */
  VK_CHECK(vkEnumeratePhysicalDevices(
      renderer->instance,
      &physical_device_count,
      NULL
  ));
  physical_devices = (VkPhysicalDevice*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkPhysicalDevice) * physical_device_count
  );
  VK_CHECK(vkEnumeratePhysicalDevices(
      renderer->instance,
      &physical_device_count,
      physical_devices
  ));

  /* Check for a discrete GPU */
  for (i = 0; i < physical_device_count; i++) {
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(
        physical_devices[i],
        &physical_device_properties
    );
    if (physical_device_properties.deviceType
        == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
    ) {
      renderer->physical_device = physical_devices[i];
      printf(
          "INFO: Selected device: %s\n",
          physical_device_properties.deviceName
      );
      return;
    }
  }

  /* Check for an integrated GPU */
  for (i = 0; i < physical_device_count; i++) {
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(
        physical_devices[i],
        &physical_device_properties
    );
    if (physical_device_properties.deviceType
        == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    ) {
      renderer->physical_device = physical_devices[i];
      printf(
          "INFO: Selected device: %s\n",
          physical_device_properties.deviceName
      );
      return;
    }
  }

  /* Pick the first device */
  renderer->physical_device = physical_devices[0];
  {
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(
      renderer->physical_device,
      &physical_device_properties
    );
    printf("INFO: Selected device: %s\n",
      physical_device_properties.deviceName
    );
  }
}
/* Find the queue families */
void renderer_find_queue_families(renderer_t *renderer) {
  /* Variables */
  u32 i;
  u32 queue_family_count = 0;
  bool graphics_found = false, present_found = false;
  VkQueueFamilyProperties *queue_families = NULL;

  printf("INFO: Finding queue families...\n");

  /* Get the queue families */
  vkGetPhysicalDeviceQueueFamilyProperties(
      renderer->physical_device,
      &queue_family_count,
      NULL
  );
  queue_families = (VkQueueFamilyProperties*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkQueueFamilyProperties) * queue_family_count
  );
  vkGetPhysicalDeviceQueueFamilyProperties(
      renderer->physical_device,
      &queue_family_count,
      queue_families
  );

  /* Find the queue families */
  for (i = 0; i < queue_family_count; i++) {
    VkBool32 present_support = false;

    vkGetPhysicalDeviceSurfaceSupportKHR(
        renderer->physical_device,
        i,
        renderer->surface,
        &present_support
    );
    if (present_support) {
      renderer->queue_family_indices.present_family = i;
      printf("INFO: Present queue family found\n");
      present_found = true;
      renderer->queue_family_indices.indices[1] = i;
    }

    if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      renderer->queue_family_indices.graphics_family = i;
      printf("INFO: Graphics queue family found\n");
      graphics_found = true;
      renderer->queue_family_indices.indices[0] = i;
    }
  }

  /* Check for queue families */
  if (!graphics_found) {
    printf("ERROR: Graphics queue family not found\n");
    exit(1);
  }
  if (!present_found) {
    printf("ERROR: Present queue family not found\n");
    exit(1);
  }
}
/* Create the logical device */
void renderer_create_logical_device(renderer_t *renderer) {
  /* Variables */
  VkDeviceQueueCreateInfo graphics_queue_cinfo, present_queue_cinfo;
  VkDeviceQueueCreateInfo queue_create_infos[2];
  VkDeviceCreateInfo device_cinfo;
  f32 queue_priority = 1.0f;
  VkExtensionProperties *extensions_supported = NULL;
  u32 extensions_supported_count = 0;
  u32 i, j;

  printf("INFO: Creating logical device...\n");

  /* Check for supported extensions */
  /* Get extensions */
  VK_CHECK(vkEnumerateDeviceExtensionProperties(
      renderer->physical_device,
      NULL,
      &extensions_supported_count,
      NULL
  ));
  extensions_supported = (VkExtensionProperties*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkExtensionProperties) * extensions_supported_count
  );
  VK_CHECK(vkEnumerateDeviceExtensionProperties(
      renderer->physical_device,
      NULL,
      &extensions_supported_count,
      extensions_supported
  ));
  /* Print the extensions used */
  for (i = 0; i < NUM_DEV_EXTS; i++) {
    printf("INFO: Using device extension %d: %s\n", i, dev_extensions[i]);
  }
  /* Make sure all device extensions are supported */
  for (i = 0; i < NUM_DEV_EXTS; i++) {
    for (j = 0; j < extensions_supported_count; j++) {
      if (strcmp(
            dev_extensions[i],
            extensions_supported[j].extensionName
          ) == 0) {
        break;
      }
    }
    if (j == extensions_supported_count) {
      printf("ERROR: Device extension %s not supported\n", dev_extensions[i]);
      exit(1);
    }
  }

  /* Set the graphics queue info */
  graphics_queue_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  graphics_queue_cinfo.pNext = NULL;
  graphics_queue_cinfo.flags = 0;
  graphics_queue_cinfo.queueFamilyIndex = renderer->queue_family_indices.graphics_family;
  graphics_queue_cinfo.queueCount = 1;
  graphics_queue_cinfo.pQueuePriorities = &queue_priority;

  /* Set the present queue info */
  present_queue_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  present_queue_cinfo.pNext = NULL;
  present_queue_cinfo.flags = 0;
  present_queue_cinfo.queueFamilyIndex = renderer->queue_family_indices.present_family;
  present_queue_cinfo.queueCount = 1;
  present_queue_cinfo.pQueuePriorities = &queue_priority;

  /* Set the queue infos */
  queue_create_infos[0] = graphics_queue_cinfo;
  queue_create_infos[1] = present_queue_cinfo;

  /* Set the device info */
  device_cinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_cinfo.pNext = NULL;
  device_cinfo.flags = 0;
  device_cinfo.queueCreateInfoCount = 2;
  device_cinfo.pQueueCreateInfos = queue_create_infos;
  device_cinfo.enabledLayerCount = 0;
  device_cinfo.ppEnabledLayerNames = NULL;
  device_cinfo.enabledExtensionCount = NUM_DEV_EXTS;
  device_cinfo.ppEnabledExtensionNames = dev_extensions;
  device_cinfo.pEnabledFeatures = NULL;

  /* Create the logical device */
  VK_CHECK(vkCreateDevice(
      renderer->physical_device,
      &device_cinfo,
      NULL,
      &renderer->device
  ));
}
/* Create the queues */
void renderer_create_queues(renderer_t *renderer) {
  printf("INFO: Creating queues...\n");

  /* Create the graphics queue */
  vkGetDeviceQueue(
      renderer->device,
      renderer->queue_family_indices.graphics_family,
      0,
      &renderer->graphics_queue
  );
  /* Create the present queue */
  vkGetDeviceQueue(
      renderer->device,
      renderer->queue_family_indices.present_family,
      0,
      &renderer->present_queue
  );
}
/* Create the swapchain */
void renderer_create_swapchain(renderer_t *renderer) {
  /* Variables */
  VkSwapchainCreateInfoKHR swapchain_cinfo;
  VkSurfaceFormatKHR* surface_formats;
  VkPresentModeKHR* present_modes;
  u32 surface_format_count, present_mode_count;
  bool found_format = false, found_mode = false;
  u32 i;

  printf("INFO: Creating swapchain...\n");

  /* Get surface formats */
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
      renderer->physical_device,
      renderer->surface,
      &surface_format_count,
      NULL
  ));
  surface_formats = (VkSurfaceFormatKHR*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkSurfaceFormatKHR) * surface_format_count
  );
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
      renderer->physical_device,
      renderer->surface,
      &surface_format_count,
      surface_formats
  ));
  /* Look for a supported format */
  for (i = 0; i < surface_format_count; i++) {
    if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB
        && surface_formats[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
      renderer->surface_info.surface_format = surface_formats[i];
      found_format = true;
      break;
    }
  }
  /* Settle for fist one if not found */
  if (!found_format)
    renderer->surface_info.surface_format = surface_formats[0];

  /* Get the present modes */
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
      renderer->physical_device,
      renderer->surface,
      &present_mode_count,
      NULL
  ));
  present_modes = (VkPresentModeKHR*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkPresentModeKHR) * present_mode_count
  );
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
      renderer->physical_device,
      renderer->surface,
      &present_mode_count,
      present_modes
  ));
  /* Look for a supported present mode */
  for (i = 0; i < present_mode_count; i++) {
    if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
      renderer->surface_info.present_mode = present_modes[i];
      break;
    }
  }
  /* Settle for first one if not found */
  if (!found_mode)
    renderer->surface_info.present_mode = present_modes[0];

  /* Get surface capabilities */
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      renderer->physical_device,
      renderer->surface,
      &renderer->surface_info.surface_caps
  ));

  /* Set the image count */
  renderer->swapchain_image_count =
    renderer->surface_info.surface_caps.minImageCount + 1;
  if (renderer->surface_info.surface_caps.maxImageCount > 0
      && renderer->surface_info.surface_caps.maxImageCount
        < renderer->surface_info.surface_caps.minImageCount) {
    renderer->swapchain_image_count =
      renderer->surface_info.surface_caps.maxImageCount;
  }
  /* Set the extent */
  if (renderer->surface_info.surface_caps.currentExtent.width == 0xFFFFFFFF) {
    /* Set the extent */
    renderer->swapchain_extent =
      renderer->surface_info.surface_caps.currentExtent;
  }
  else {
    /* Set the extent */
    renderer->swapchain_extent.width = NH_CLAMP(
        (u32)renderer->window->size.x,
        renderer->surface_info.surface_caps.minImageExtent.width,
        renderer->surface_info.surface_caps.maxImageExtent.width
    );
    renderer->swapchain_extent.height = NH_CLAMP(
        (u32)renderer->window->size.y,
        renderer->surface_info.surface_caps.minImageExtent.height,
        renderer->surface_info.surface_caps.maxImageExtent.height
    );
  }
  printf(
      "INFO: Initial extent: %d x %d\n",
      renderer->swapchain_extent.width,
      renderer->swapchain_extent.height
  );

  /* Set the swapchain info */
  swapchain_cinfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_cinfo.pNext = NULL;
  swapchain_cinfo.flags = 0;
  swapchain_cinfo.surface = renderer->surface;
  swapchain_cinfo.minImageCount = renderer->surface_info.surface_caps.minImageCount;
  swapchain_cinfo.imageFormat = renderer->surface_info.surface_format.format;
  swapchain_cinfo.imageColorSpace = renderer->surface_info.surface_format.colorSpace;
  swapchain_cinfo.imageExtent = renderer->surface_info.surface_caps.currentExtent;
  swapchain_cinfo.imageArrayLayers = 1;
  swapchain_cinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (
      renderer->queue_family_indices.present_family
      == renderer->queue_family_indices.graphics_family
  ) {
    swapchain_cinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_cinfo.queueFamilyIndexCount = 0;
    swapchain_cinfo.pQueueFamilyIndices = NULL;
  } else {
    swapchain_cinfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchain_cinfo.queueFamilyIndexCount = 2;
    swapchain_cinfo.pQueueFamilyIndices = renderer->queue_family_indices.indices;
  }
  swapchain_cinfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  swapchain_cinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchain_cinfo.presentMode = renderer->surface_info.present_mode;
  swapchain_cinfo.clipped = VK_TRUE;
  swapchain_cinfo.oldSwapchain = VK_NULL_HANDLE;

  /* Create the swapchain */
  VK_CHECK(vkCreateSwapchainKHR(
      renderer->device,
      &swapchain_cinfo,
      NULL,
      &renderer->swapchain
  ));
}
/* Get the swapchain images */
void renderer_get_swapchain_images(renderer_t *renderer) {
  /* Variables */
  u32 i;

  printf("INFO: Getting swapchain images...\n");

  /* Get the swapchain images */
  VK_CHECK(vkGetSwapchainImagesKHR(
      renderer->device,
      renderer->swapchain,
      &renderer->swapchain_image_count,
      NULL
  ));
  renderer->swapchain_images = (VkImage*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkImage) * renderer->swapchain_image_count
  );
  VK_CHECK(vkGetSwapchainImagesKHR(
      renderer->device,
      renderer->swapchain,
      &renderer->swapchain_image_count,
      renderer->swapchain_images
  ));

  /* Create the image views */
  renderer->swapchain_image_views = (VkImageView*)nh_arena_alloc(
      &renderer->arena,
      sizeof(VkImageView) * renderer->swapchain_image_count
  );
  for (i = 0; i < renderer->swapchain_image_count; i++) {
    VkImageViewCreateInfo image_view_cinfo;
    image_view_cinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_cinfo.pNext = NULL;
    image_view_cinfo.flags = 0;
    image_view_cinfo.image = renderer->swapchain_images[i];
    image_view_cinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_cinfo.format = renderer->surface_info.surface_format.format;
    image_view_cinfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_cinfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_cinfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_cinfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_cinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_cinfo.subresourceRange.baseMipLevel = 0;
    image_view_cinfo.subresourceRange.levelCount = 1;
    image_view_cinfo.subresourceRange.baseArrayLayer = 0;
    image_view_cinfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(
        renderer->device,
        &image_view_cinfo,
        NULL,
        &renderer->swapchain_image_views[i]
    ));
  }
}
/* Create a shader module */
VkShaderModule renderer_create_shader_module(
    renderer_t *renderer,
    const char *path
) {
  /* Variables */
  VkShaderModuleCreateInfo shader_module_cinfo;
  VkShaderModule shader_module;
  u8 *data = NULL;
  u32 size = 0;

  printf("INFO: Creating shader module...\n");

  /* Read the shader file */
  size = nh_file_size(path);
  data = (u8*)malloc(size);
  nh_read_file(path, data);

  /* Create the shader module */
  shader_module_cinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_cinfo.pNext = NULL;
  shader_module_cinfo.flags = 0;
  shader_module_cinfo.codeSize = size-1; /* Don't need NULL terminator */
  shader_module_cinfo.pCode = (u32*)data;
  VK_CHECK(vkCreateShaderModule(
      renderer->device,
      &shader_module_cinfo,
      NULL,
      &shader_module
  ));

  /* Free the shader file */
  free(data);

  return shader_module;
}
/* Create the render pass */
void renderer_create_render_pass(renderer_t *renderer) {
  /* Variables */
  VkAttachmentDescription color_attachment;
  VkAttachmentReference color_attachment_ref;
  VkSubpassDescription subpass;
  VkRenderPassCreateInfo render_pass_cinfo;
  VkSubpassDependency subpass_dependency;

  /* Create the render pass */
  printf("INFO: Creating render pass...\n");

  /* Create the attachment description */
  color_attachment.flags = 0;
  color_attachment.format = renderer->surface_info.surface_format.format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  /* Create the attachment reference */
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  /* Create the subpass description */
  subpass.flags = 0;
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = NULL;
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = NULL;
  subpass.pResolveAttachments = NULL;
  subpass.pDepthStencilAttachment = NULL;

  /* Create the subpass dependency */
  subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependency.dstSubpass = 0;
  subpass_dependency.srcStageMask =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpass_dependency.srcAccessMask = 0;
  subpass_dependency.dstStageMask =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpass_dependency.dstAccessMask =
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  subpass_dependency.dependencyFlags = 0;

  /* Create the render pass */
  render_pass_cinfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_cinfo.pNext = NULL;
  render_pass_cinfo.flags = 0;
  render_pass_cinfo.attachmentCount = 1;
  render_pass_cinfo.pAttachments = &color_attachment;
  render_pass_cinfo.subpassCount = 1;
  render_pass_cinfo.pSubpasses = &subpass;
  render_pass_cinfo.dependencyCount = 1;
  render_pass_cinfo.pDependencies = &subpass_dependency;
  VK_CHECK(vkCreateRenderPass(
      renderer->device,
      &render_pass_cinfo,
      NULL,
      &renderer->render_pass
  ));
}
/* Create the graphics pipeline */
void renderer_create_pipeline(renderer_t *renderer) {
  /* Variables */
  VkPipelineShaderStageCreateInfo vert_shader_stage, frag_shader_stage;
  VkPipelineShaderStageCreateInfo shader_stages[2];
  VkPipelineDynamicStateCreateInfo dynamic_state;
  VkPipelineVertexInputStateCreateInfo vertex_input_state;
  VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
  VkPipelineViewportStateCreateInfo viewport_state;
  VkPipelineRasterizationStateCreateInfo rasterization_state;
  VkPipelineMultisampleStateCreateInfo multisample_state;
  VkPipelineColorBlendStateCreateInfo color_blend_state;
  VkPipelineColorBlendAttachmentState color_blend_attachment;
  VkPipelineLayoutCreateInfo pipeline_layout_cinfo;
  VkGraphicsPipelineCreateInfo pipeline_cinfo;
  u32 dynamic_state_count = 2;
  VkDynamicState dynamic_states[2] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
  };

  printf("INFO: Creating graphics pipeline...\n");

  /* Create the shader modules */
  renderer->vert_shader_module = renderer_create_shader_module(
      renderer,
      "bin/vert.spv"
  );
  renderer->frag_shader_module = renderer_create_shader_module(
      renderer,
      "bin/frag.spv"
  );

  /* Create the vertex shader stage */
  printf("INFO: Creating pipeline layout: vertex shader stage\n");
  vert_shader_stage.sType =
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vert_shader_stage.pNext = NULL;
  vert_shader_stage.flags = 0;
  vert_shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vert_shader_stage.module = renderer->vert_shader_module;
  vert_shader_stage.pName = "main";
  vert_shader_stage.pSpecializationInfo = NULL;
  shader_stages[0] = vert_shader_stage;
  /* Create the fragment shader stage */
  printf("INFO: Creating pipeline layout: fragment shader stage\n");
  frag_shader_stage.sType =
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  frag_shader_stage.pNext = NULL;
  frag_shader_stage.flags = 0;
  frag_shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  frag_shader_stage.module = renderer->frag_shader_module;
  frag_shader_stage.pName = "main";
  frag_shader_stage.pSpecializationInfo = NULL;
  shader_stages[1] = frag_shader_stage;
  /* Create the dynamic state */
  printf("INFO: Creating pipeline layout: dynamic state\n");
  dynamic_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.pNext = NULL;
  dynamic_state.flags = 0;
  dynamic_state.dynamicStateCount = dynamic_state_count;
  dynamic_state.pDynamicStates = dynamic_states;
  /* Create vertex input state */
  printf("INFO: Creating pipeline layout: vertex input state\n");
  vertex_input_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_state.pNext = NULL;
  vertex_input_state.flags = 0;
  vertex_input_state.vertexBindingDescriptionCount = 0;
  vertex_input_state.pVertexBindingDescriptions = NULL;
  vertex_input_state.vertexAttributeDescriptionCount = 0;
  vertex_input_state.pVertexAttributeDescriptions = NULL;
  /* Create input assembly state */
  printf("INFO: Creating pipeline layout: input assembly state\n");
  input_assembly_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.pNext = NULL;
  input_assembly_state.flags = 0;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly_state.primitiveRestartEnable = VK_FALSE;
  /* Create viewport state */
  printf("INFO: Creating pipeline layout: viewport state\n");
  renderer->viewport.x = 0.0f;
  renderer->viewport.y = 0.0f;
  renderer->viewport.width = renderer->swapchain_extent.width;
  renderer->viewport.height = renderer->swapchain_extent.height;
  renderer->scissor.extent = renderer->swapchain_extent;
  renderer->scissor.offset.x = 0;
  renderer->scissor.offset.y = 0;
  viewport_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = NULL;
  viewport_state.flags = 0;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &renderer->viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &renderer->scissor;
  /* Create rasterization state */
  printf("INFO: Creating pipeline layout: rasterization state\n");
  rasterization_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_state.pNext = NULL;
  rasterization_state.flags = 0;
  rasterization_state.depthClampEnable = VK_FALSE;
  rasterization_state.rasterizerDiscardEnable = VK_FALSE;
  rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_state.lineWidth = 1.0f;
  rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterization_state.depthBiasEnable = VK_FALSE;
  rasterization_state.depthBiasConstantFactor = 0.0f;
  rasterization_state.depthBiasClamp = 0.0f;
  rasterization_state.depthBiasSlopeFactor = 0.0f;
  /* Create multisample state */
  printf("INFO: Creating pipeline layout: multisample state\n");
  multisample_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.pNext = NULL;
  multisample_state.flags = 0;
  multisample_state.sampleShadingEnable = VK_FALSE;
  multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisample_state.minSampleShading = 1.0f;
  multisample_state.pSampleMask = NULL;
  multisample_state.alphaToCoverageEnable = VK_FALSE;
  multisample_state.alphaToOneEnable = VK_FALSE;
  /* Create color blend state */
  printf("INFO: Creating pipeline layout: color blend state\n");
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor =
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_state.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.pNext = NULL;
  color_blend_state.flags = 0;
  color_blend_state.logicOpEnable = VK_FALSE;
  color_blend_state.logicOp = VK_LOGIC_OP_COPY;
  color_blend_state.attachmentCount = 1;
  color_blend_state.pAttachments = &color_blend_attachment;
  color_blend_state.blendConstants[0] = 0.0f;
  color_blend_state.blendConstants[1] = 0.0f;
  color_blend_state.blendConstants[2] = 0.0f;
  color_blend_state.blendConstants[3] = 0.0f;
  /* Create pipeline layout */
  printf("INFO: Creating pipeline layout\n");
  pipeline_layout_cinfo.sType =
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_cinfo.pNext = NULL;
  pipeline_layout_cinfo.flags = 0;
  pipeline_layout_cinfo.setLayoutCount = 0;
  pipeline_layout_cinfo.pSetLayouts = NULL;
  pipeline_layout_cinfo.pushConstantRangeCount = 0;
  pipeline_layout_cinfo.pPushConstantRanges = NULL;
  VK_CHECK(vkCreatePipelineLayout(
      renderer->device,
      &pipeline_layout_cinfo,
      NULL,
      &renderer->pipeline_layout
  ));

  /* Create graphics pipeline */
  printf("INFO: Creating graphics pipeline...\n");
  pipeline_cinfo.sType =
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_cinfo.pNext = NULL;
  pipeline_cinfo.flags = 0;
  pipeline_cinfo.stageCount = 2;
  pipeline_cinfo.pStages = shader_stages;
  pipeline_cinfo.pVertexInputState = &vertex_input_state;
  pipeline_cinfo.pInputAssemblyState = &input_assembly_state;
  pipeline_cinfo.pViewportState = &viewport_state;
  pipeline_cinfo.pRasterizationState = &rasterization_state;
  pipeline_cinfo.pMultisampleState = &multisample_state;
  pipeline_cinfo.pDepthStencilState = NULL;
  pipeline_cinfo.pColorBlendState = &color_blend_state;
  pipeline_cinfo.pDynamicState = &dynamic_state;
  pipeline_cinfo.layout = renderer->pipeline_layout;
  pipeline_cinfo.renderPass = renderer->render_pass;
  pipeline_cinfo.subpass = 0;
  pipeline_cinfo.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_cinfo.basePipelineIndex = -1;
  VK_CHECK(vkCreateGraphicsPipelines(
        renderer->device,
        VK_NULL_HANDLE,
        1, &pipeline_cinfo,
        NULL,
        &renderer->graphics_pipeline
  ));
}
/* Create the framebuffers */
void renderer_create_framebuffers(renderer_t *renderer) {
  /* Variables */
  VkFramebufferCreateInfo framebuffer_cinfo;
  u32 i;

  printf("INFO: Creating framebuffers...\n");

  /* Create the framebuffers */
  renderer->swapchain_framebuffers =
    (VkFramebuffer*)nh_arena_alloc(
        &renderer->arena,
        sizeof(VkFramebuffer) * renderer->swapchain_image_count
    );
  for (i = 0; i < renderer->swapchain_image_count; i++) {
    /* Create the framebuffer */
    framebuffer_cinfo.sType =
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_cinfo.pNext = NULL;
    framebuffer_cinfo.flags = 0;
    framebuffer_cinfo.renderPass = renderer->render_pass;
    framebuffer_cinfo.attachmentCount = 1;
    framebuffer_cinfo.pAttachments = &renderer->swapchain_image_views[i];
    framebuffer_cinfo.width = renderer->swapchain_extent.width;
    framebuffer_cinfo.height = renderer->swapchain_extent.height;
    framebuffer_cinfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(
          renderer->device,
          &framebuffer_cinfo,
          NULL,
          &renderer->swapchain_framebuffers[i]
    ));
  }
}
/* Create the command pool */
void renderer_create_command_pool(renderer_t *renderer) {
  /* Variables */
  VkCommandPoolCreateInfo command_pool_cinfo;

  printf("INFO: Creating command pool...\n");

  /* Create the command pool */
  command_pool_cinfo.sType =
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_cinfo.pNext = NULL;
  command_pool_cinfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_cinfo.queueFamilyIndex = renderer->queue_family_indices.graphics_family;
  VK_CHECK(vkCreateCommandPool(
        renderer->device,
        &command_pool_cinfo,
        NULL,
        &renderer->command_pool
  ));
}
/* Allocate the command buffer */
void renderer_allocate_command_buffer(renderer_t *renderer) {
  /* Variables */
  VkCommandBufferAllocateInfo command_buffer_cinfo;

  printf("INFO: Allocating command buffer...\n");

  /* Allocate the command buffer */
  command_buffer_cinfo.sType =
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_cinfo.pNext = NULL;
  command_buffer_cinfo.commandPool = renderer->command_pool;
  command_buffer_cinfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_cinfo.commandBufferCount = 1;
  VK_CHECK(vkAllocateCommandBuffers(
        renderer->device,
        &command_buffer_cinfo,
        &renderer->command_buffer
  ));
}
/* Record the command buffer */
void renderer_record_command_buffer(renderer_t *renderer, u32 image_index) {
  /* Variables */
  VkCommandBufferBeginInfo command_buffer_binfo;
  VkRenderPassBeginInfo render_pass_binfo;
  VkClearValue clear_value;

  /* Begin the command buffer */
  command_buffer_binfo.sType =
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_binfo.pNext = NULL;
  command_buffer_binfo.flags = 0;
  command_buffer_binfo.pInheritanceInfo = NULL;
  VK_CHECK(vkBeginCommandBuffer(
        renderer->command_buffer,
        &command_buffer_binfo
  ));
  /* Begin the render pass */
  clear_value.color.float32[0] = 1.00f;
  clear_value.color.float32[1] = 0.50f;
  clear_value.color.float32[2] = 0.25f;
  clear_value.color.float32[3] = 1.00f;
  render_pass_binfo.sType =
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_binfo.pNext = NULL;
  render_pass_binfo.renderPass = renderer->render_pass;
  render_pass_binfo.framebuffer = renderer->swapchain_framebuffers[image_index];
  render_pass_binfo.renderArea.offset.x = 0;
  render_pass_binfo.renderArea.offset.y = 0;
  render_pass_binfo.renderArea.extent = renderer->swapchain_extent;
  render_pass_binfo.clearValueCount = 1;
  render_pass_binfo.pClearValues = &clear_value;
  vkCmdBeginRenderPass(
        renderer->command_buffer,
        &render_pass_binfo,
        VK_SUBPASS_CONTENTS_INLINE
  );
  /* Bind the graphics pipeline */
  vkCmdBindPipeline(
        renderer->command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        renderer->graphics_pipeline
  );
  /* Set the viewport */
  renderer->viewport.x = 0.0f;
  renderer->viewport.y = 0.0f;
  renderer->viewport.width = renderer->swapchain_extent.width;
  renderer->viewport.height = renderer->swapchain_extent.height;
  vkCmdSetViewport(
        renderer->command_buffer,
        0,
        1,
        &renderer->viewport
  );
  /* Set the scissor */
  renderer->scissor.extent = renderer->swapchain_extent;
  renderer->scissor.offset.x = 0;
  renderer->scissor.offset.y = 0;
  vkCmdSetScissor(
        renderer->command_buffer,
        0,
        1,
        &renderer->scissor
  );
  /* Draw the triangle */
  vkCmdDraw(renderer->command_buffer, 3, 1, 0, 0);
  /* End the render pass */
  vkCmdEndRenderPass(renderer->command_buffer);
  /* End the command buffer */
  VK_CHECK(vkEndCommandBuffer(renderer->command_buffer));
}
/* Create the sync objects */
void renderer_create_sync_objects(renderer_t *renderer) {
  /* Variables */
  VkSemaphoreCreateInfo semaphore_cinfo;
  VkFenceCreateInfo fence_cinfo;

  printf("INFO: Creating sync objects...\n");

  /* Create the image available semaphore */
  printf("INFO: Creating sync objects: image available semaphore...\n");
  semaphore_cinfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_cinfo.pNext = NULL;
  semaphore_cinfo.flags = 0;
  VK_CHECK(vkCreateSemaphore(
        renderer->device,
        &semaphore_cinfo,
        NULL,
        &renderer->image_available_semaphore
  ));
  /* Create the render finished semaphore */
  printf("INFO: Creating sync objects: render finished semaphore...\n");
  semaphore_cinfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_cinfo.pNext = NULL;
  semaphore_cinfo.flags = 0;
  VK_CHECK(vkCreateSemaphore(
        renderer->device,
        &semaphore_cinfo,
        NULL,
        &renderer->render_finished_semaphore
  ));
  /* Create the in flight fence */
  printf("INFO: Creating sync objects: in flight fence...\n");
  fence_cinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_cinfo.pNext = NULL;
  fence_cinfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VK_CHECK(vkCreateFence(
        renderer->device,
        &fence_cinfo,
        NULL,
        &renderer->in_flight_fence
  ));
}
/* Create the renderer */
renderer_t create_renderer(window_t *window) {
  renderer_t renderer;
  renderer.window = window;
  printf("INFO: Creating renderer...\n");

  /* Create the arena */
  renderer.arena = nh_arena_create(ARENA_SIZE, malloc(ARENA_SIZE));

  /* Vulkan initialization */
  renderer_create_instance(&renderer);        /* Create the instance */
#ifdef USE_VALIDATION
  renderer_create_debug_messenger(&renderer); /* Create the debug messenger */
#endif
  renderer_create_surface(&renderer);         /* Create the surface */
  renderer_pick_physical_device(&renderer);   /* Pick the physical device */
  renderer_find_queue_families(&renderer);    /* Find the queue families */
  renderer_create_logical_device(&renderer);  /* Create the logical device */
  renderer_create_queues(&renderer);          /* Create the queues */
  renderer_create_swapchain(&renderer);       /* Create the swapchain */
  renderer_get_swapchain_images(&renderer);   /* Get the swapchain images */
  renderer_create_render_pass(&renderer);     /* Create the render pass */
  renderer_create_pipeline(&renderer);        /* Create the graphics pipeline */
  renderer_create_framebuffers(&renderer);    /* Create the framebuffers */
  renderer_create_command_pool(&renderer);    /* Create the command pool */
  renderer_allocate_command_buffer(&renderer);/* Allocate the command buffer */
  renderer_create_sync_objects(&renderer);    /* Create the sync objects */

  return renderer;
}
/* Destroy the renderer */
void renderer_destroy(renderer_t *renderer) {
  /* Variables */
  u32 i;

  printf("INFO: Destroying renderer...\n");
  
  /* Wait for device idle */
  printf("INFO: Waiting for device idle...\n");
  vkDeviceWaitIdle(renderer->device);

  /* Destroy the semaphores */
  printf("INFO: Destroying semaphores...\n");
  vkDestroySemaphore(renderer->device, renderer->image_available_semaphore, NULL);
  vkDestroySemaphore(renderer->device, renderer->render_finished_semaphore, NULL);

  /* Destroy the fence */
  printf("INFO: Destroying fence...\n");
  vkDestroyFence(renderer->device, renderer->in_flight_fence, NULL);

  /* Destroy command pool */
  printf("INFO: Destroying command pool...\n");
  vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);

  /* Destroy framebuffers */
  printf("INFO: Destroying framebuffers...\n");
  for (i = 0; i < renderer->swapchain_image_count; i++) {
    vkDestroyFramebuffer(renderer->device, renderer->swapchain_framebuffers[i], NULL);
  }

  /* Destroy pipeline */
  printf("INFO: Destroying graphics pipeline...\n");
  vkDestroyPipeline(renderer->device, renderer->graphics_pipeline, NULL);

  /* Destroy pipeline layout */
  printf("INFO: Destroying pipeline layout...\n");
  vkDestroyPipelineLayout(renderer->device, renderer->pipeline_layout, NULL);

  /* Destroy render pass */
  printf("INFO: Destroying render pass...\n");
  vkDestroyRenderPass(renderer->device, renderer->render_pass, NULL);

  /* Destroy shader modules */
  printf("INFO: Destroying shader modules...\n");
  vkDestroyShaderModule(renderer->device, renderer->frag_shader_module, NULL);
  vkDestroyShaderModule(renderer->device, renderer->vert_shader_module, NULL);

  /* Destroy swapchain image views */
  printf("INFO: Destroying swapchain image views...\n");
  for (i = 0; i < renderer->swapchain_image_count; i++) {
    vkDestroyImageView(renderer->device, renderer->swapchain_image_views[i], NULL);
  }

  /* Destroy swapchain */
  printf("INFO: Destroying swapchain...\n");
  vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
  
  /* Destroy physical device */
  printf("INFO: Destroying physical device...\n");
  vkDestroyDevice(renderer->device, NULL);

  /* Destroy surface */
  printf("INFO: Destroying surface...\n");
  vkDestroySurfaceKHR(renderer->instance, renderer->surface, NULL);

  /* Destroy debug messenger */
#ifdef USE_VALIDATION
  printf("INFO: Destroying debug messenger...\n");
  vkDestroyDebugUtilsMessengerEXT(
      renderer->instance,
      renderer->debug_messenger,
      NULL
  );
#endif

  /* Destroy instance */
  printf("INFO: Destroying instance...\n");
  vkDestroyInstance(renderer->instance, NULL);

  /* Free arena */
  nh_arena_free(&renderer->arena);
}
/* Draw the scene */
void renderer_draw(renderer_t *renderer) {
  /* Variables */
  u32 image_index;
  VkSubmitInfo submit_info;
  VkPresentInfoKHR present_info;
  VkPipelineStageFlags wait_stage =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  /* Wait for fence */
  vkWaitForFences(
      renderer->device,
      1,
      &renderer->in_flight_fence,
      VK_TRUE,
      UINT64_MAX
  );

  /* Acquire an image from the swapchain */
  switch (vkAcquireNextImageKHR(
      renderer->device,
      renderer->swapchain,
      UINT64_MAX,
      renderer->image_available_semaphore,
      VK_NULL_HANDLE,
      &image_index
  )) {
    case VK_SUCCESS:
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
      {
        /* Variables */
        u32 i;

        printf("INFO: Swapchain out of date, recreating...\n");

        /* Destroy the swapchain and related objects */
        vkDeviceWaitIdle(renderer->device);
        for (i = 0; i < renderer->swapchain_image_count; i++) {
          vkDestroyFramebuffer(renderer->device, renderer->swapchain_framebuffers[i], NULL);
        }
        for (i = 0; i < renderer->swapchain_image_count; i++) {
          vkDestroyImageView(renderer->device, renderer->swapchain_image_views[i], NULL);
        }
        vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
        /* Recreate the swapchain */
        renderer_create_swapchain(renderer);
        renderer_get_swapchain_images(renderer);
        renderer_create_framebuffers(renderer);
        return;
      } break;
    default:
      printf("ERROR: Failed to acquire swapchain image!\n");
      exit(1);
      break;
  }
  /* Reset fence */
  vkResetFences(
      renderer->device,
      1,
      &renderer->in_flight_fence
  );

  /* Reset and record the command buffer */
  vkResetCommandBuffer(renderer->command_buffer, 0);
  renderer_record_command_buffer(renderer, image_index);

  /* Submit command buffer */
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = NULL;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &renderer->image_available_semaphore;
  submit_info.pWaitDstStageMask = &wait_stage;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &renderer->command_buffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &renderer->render_finished_semaphore;
  VK_CHECK(vkQueueSubmit(
        renderer->graphics_queue,
        1, &submit_info,
        renderer->in_flight_fence
  ));
  
  /* Present the swapchain */
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = NULL;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &renderer->render_finished_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &renderer->swapchain;
  present_info.pImageIndices = &image_index;
  present_info.pResults = NULL;
  switch (vkQueuePresentKHR(renderer->present_queue, &present_info)) {
    case VK_SUCCESS:
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
      {
        /* Variables */
        u32 i;

        printf("INFO: Swapchain out of date, recreating...\n");

        /* Destroy the swapchain and related objects */
        vkDeviceWaitIdle(renderer->device);
        for (i = 0; i < renderer->swapchain_image_count; i++) {
          vkDestroyFramebuffer(renderer->device, renderer->swapchain_framebuffers[i], NULL);
        }
        for (i = 0; i < renderer->swapchain_image_count; i++) {
          vkDestroyImageView(renderer->device, renderer->swapchain_image_views[i], NULL);
        }
        vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
        /* Recreate the swapchain */
        renderer_create_swapchain(renderer);
        renderer_get_swapchain_images(renderer);
        renderer_create_framebuffers(renderer);
        return;
      } break;
    default:
      printf("ERROR: Failed to present!\n");
      exit(1);
      break;
  }
}

/* Entry point */
int main(void) {
  /* Variables */
  window_t window;
  renderer_t renderer;
  nh_vec2i_t initial_size;

  /* Create the window */
  initial_size.x = 1280;
  initial_size.y = 720;
  create_window(&window, "Test", initial_size);
  /* Create the renderer */
  renderer = create_renderer(&window);

  /* Main loop */
  while (window.running) {
    /* Variables */
    u32 end, start;
    f32 delta_time;

    /* Delta time - part 1 */
    start = SDL_GetPerformanceCounter();
    
    /* Update the window */
    update_window(&window);

    /* Draw the screen */
    renderer_draw(&renderer);

    /* Delta time - part 2 */
    end = SDL_GetPerformanceCounter();
    delta_time = (f64)(end - start) / (f64)SDL_GetPerformanceFrequency();
    if (window.ticks % 100 == 0)
      printf("INFO: FPS: %.2f\n", (1.0f / delta_time));
  }

  /* Destroy the renderer */
  renderer_destroy(&renderer);
  /* Destroy the window */
  destroy_window(&window);
  return 0;
}
