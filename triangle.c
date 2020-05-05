/*
 * A single-function Vulkan triangle example in only 41 steps!
 * Note: this is not good code. Leaks are abundant, and functions would greatly improve the quality of certain sections of code.
 * That being said, the point of this program is to demonstrate the process of setting up Vulkan as linearly as possible.
 * It is designed to read like a recipe, not an actual codebase.
 * *Heavily inspired by* (copy-pasted with modifications from) https://github.com/SaschaWillems/Vulkan and https://vulkan-tutorial.com
*/

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH  720
#define HEIGHT 480

extern unsigned char frag_shader_spv[];
const int frag_shader_spv_size = 500;

extern unsigned char vert_shader_spv[];
const int vert_shader_spv_size = 1372;

int main(int argc, char **argv) {
	/*
		1) Initialise GLFW.
		   GLFW handles OS-specific interfaces such as creating and accessing a window and gathering input.
	*/
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Test", NULL, NULL);
	if (!window) {
		fprintf(stderr, "Error creating a GLFW window\n");
		return 1;
	}

	/*
		2) Retrieves the names of the Vulkan *instance* extensions that are necessary.
		   If NULL is returned, then Vulkan is not usable (likely not installed).
	*/
	unsigned int n_inst_exts = 0;
	const char **req_inst_exts = glfwGetRequiredInstanceExtensions(&n_inst_exts);
	if (!req_inst_exts) {
		fprintf(stderr, "Could not find any Vulkan extensions\n");
		return 2;
	}

	/*
		3) Create a Vulkan Instance.
		   We provide Vulkan information about our program and the extensions available on this system,
		    and it returns a unique Vulkan instance
	*/
	VkApplicationInfo app_info = {0};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Hello Triangle";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.enabledExtensionCount = n_inst_exts;
	create_info.ppEnabledExtensionNames = req_inst_exts;

	VkInstance instance;
	VkResult res = vkCreateInstance(&create_info, NULL, &instance);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance() failed (%d)\n", res);
		return 3;
	}

	/*
		4) Determine the list of graphics hardware devices in this computer.
		   In this example we just select the first device on the list.
	*/
	int n_gpus = 0;
	res = vkEnumeratePhysicalDevices(instance, &n_gpus, NULL);
	if (n_gpus <= 0) {
		fprintf(stderr, "No graphics hardware was found (physical device count = %d) (%d)\n", n_gpus, res);
		return 4;
	}

	n_gpus = 1;
	VkPhysicalDevice gpu = {0};
	res = vkEnumeratePhysicalDevices(instance, &n_gpus, &gpu);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkEnumeratePhysicalDevices() failed (%d)\n", res);
		return 4;
	}

	/*
		5) Determine which queue family to use.
		   In this case, we just look for the first one that can do graphics.
	*/
	int n_queues = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n_queues, NULL);
	if (n_queues <= 0) {
		fprintf(stderr, "No queue families were found\n");
		return 5;
	}

	VkQueueFamilyProperties *qfp = malloc(n_queues * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n_queues, qfp);

	int queue_index = -1;
	for (int i = 0; i < n_queues; i++) {
		if (qfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queue_index = i;
			break;
		}
	}
	free(qfp);

	if (queue_index < 0) {
		fprintf(stderr, "Could not find a queue family with graphics support\n");
		return 5;
	}

	/*
		6) Check that the chosen queue family supports presentation.
	*/
	if (!glfwGetPhysicalDevicePresentationSupport(instance, gpu, queue_index)) {
		fprintf(stderr, "The selected queue family does not support present mode\n");
		return 6;
	}

	/*
		7) Get all Vulkan *device* extensions (as opposed to instance extensions)
	*/
	unsigned int n_dev_exts = 0;
	res = vkEnumerateDeviceExtensionProperties(gpu, NULL, &n_dev_exts, NULL);
	if (n_dev_exts <= 0 || res != VK_SUCCESS) {
		fprintf(stderr, "Could not find any Vulkan device extensions (found %d, error %d)\n", n_dev_exts, res);
		return 7;
	}

	VkExtensionProperties *dev_ext_props = calloc(n_dev_exts, sizeof(VkExtensionProperties));
	res = vkEnumerateDeviceExtensionProperties(gpu, NULL, &n_dev_exts, dev_ext_props);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkEnumerateDeviceExtensionProperties() failed (%d)\n", res);
		return 7;
	}

	const char **dev_exts = calloc(n_dev_exts, sizeof(void*));
	for (int i = 0; i < n_dev_exts; i++)
		dev_exts[i] = &dev_ext_props[i].extensionName[0];

	/*
		8) Create a virtual device for Vulkan.
		   We pass in information regarding the hardware features we want to use as well as the set of queues,
		    which are essentially the interface between our program and the GPU.
	*/
	float priority = 0.0f;
	VkDeviceQueueCreateInfo queue_info = {0};
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = queue_index;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;

	VkDeviceCreateInfo device_info = {0};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledExtensionCount = n_dev_exts;
	device_info.ppEnabledExtensionNames = dev_exts;

	VkDevice device = {0};
	res = vkCreateDevice(gpu, &device_info, NULL, &device);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateDevice() failed (%d)\n", res);
		return 8;
	}

	/*
		9) Get implementation-specific function pointers.
		   This lets us use parts of the Vulkan API that aren't generalised.
	*/
	const int total_fptrs = 7;
	int tally = 0;

	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR
		= (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR
		= (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");

	PFN_vkCreateSwapchainKHR CreateSwapchainKHR       = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
	PFN_vkDestroySwapchainKHR DestroySwapchainKHR     = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
	PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
	PFN_vkAcquireNextImageKHR AcquireNextImageKHR     = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
	PFN_vkQueuePresentKHR QueuePresentKHR             = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");

	tally += GetPhysicalDeviceSurfaceCapabilitiesKHR != NULL;
	tally += GetPhysicalDeviceSurfaceFormatsKHR != NULL;
	tally += CreateSwapchainKHR != NULL;
	tally += DestroySwapchainKHR != NULL;
	tally += GetSwapchainImagesKHR != NULL;
	tally += AcquireNextImageKHR != NULL;
	tally += QueuePresentKHR != NULL;

	if (tally != total_fptrs) {
		fprintf(stderr, "Error loading KHR extension methods (found %d/%d)\n", tally, total_fptrs);
		return 9;
	}

	/*
		10) Creating the window surface.
		    In this example I use GLFW's equivalent API, which is platform-agnostic.
	*/
	VkSurfaceKHR surface;
	res = glfwCreateWindowSurface(instance, window, NULL, &surface);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "glfwCreateWindowSurface() failed (%d)\n", res);
		return 10;
	}

	/*
		11) Determine the color format.
	*/
	int n_color_formats = 0;
	res = GetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n_color_formats, NULL);
	if (n_color_formats <= 0 || res != VK_SUCCESS) {
		fprintf(stderr, "Could not find any color formats for the window surface\n");
		return 11;
	}

	VkSurfaceFormatKHR *colors = malloc(n_color_formats * sizeof(VkSurfaceFormatKHR));
	res = GetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n_color_formats, colors);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "GetPhysicalDeviceSurfaceFormatsKHR() failed (%d)\n", res);
		return 11;
	}

	VkSurfaceFormatKHR color_fmt = {0};
	for (int i = 0; i < n_color_formats; i++) {
		if (colors[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
			color_fmt = colors[i];
			break;
		}
	}
	free(colors);

	if (color_fmt.format == VK_FORMAT_UNDEFINED) {
		fprintf(stderr, "The window surface does not define a B8G8R8A8 color format\n");
		return 11;
	}

	/*
		12) Get information about the OS-specific surface.
	*/
	VkSurfaceCapabilitiesKHR surf_caps;
	res = GetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surf_caps);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed (%d)\n", res);
		return 12;
	}

	if (surf_caps.currentExtent.width == 0xffffffff) {
		surf_caps.currentExtent.width  = WIDTH;
		surf_caps.currentExtent.height = HEIGHT;
	}

	// This would be where you might want to call vkGetPhysicalDeviceSurfacePresentModeKHR()
	//  to use a non-Vsync presentation mode

	/*
		13) Select the composite alpha format.
	*/
	VkCompositeAlphaFlagBitsKHR alpha_fmt = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR alpha_list[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};

	for (int i = 0; i < sizeof(alpha_list) / sizeof(VkCompositeAlphaFlagBitsKHR); i++) {
		if (surf_caps.supportedCompositeAlpha & alpha_list[i]) {
			alpha_fmt = alpha_list[i];
			break;
		}
	}

	/*
		14) Create a swapchain.
		    This lets us maintain a rotating cast of framebuffers.
		    In this example, we set it up for double-buffering.
	*/
	int n_swap_images = surf_caps.minImageCount + 1;
	if (surf_caps.maxImageCount > 0 && n_swap_images > surf_caps.maxImageCount)
		n_swap_images = surf_caps.maxImageCount;

	VkImageUsageFlags img_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		(surf_caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

	VkSwapchainCreateInfoKHR swap_info = {0};
	swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swap_info.pNext = NULL;
	swap_info.surface = surface;
	swap_info.minImageCount = n_swap_images;
	swap_info.imageFormat = color_fmt.format;
	swap_info.imageColorSpace = color_fmt.colorSpace;
	swap_info.imageExtent = surf_caps.currentExtent;
	swap_info.imageUsage = img_usage;
	swap_info.preTransform = (VkSurfaceTransformFlagBitsKHR)surf_caps.currentTransform;
	swap_info.imageArrayLayers = 1;
	swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swap_info.queueFamilyIndexCount = 0;
	swap_info.pQueueFamilyIndices = NULL;
	swap_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swap_info.oldSwapchain = NULL;
	swap_info.clipped = VK_TRUE;
	swap_info.compositeAlpha = alpha_fmt;

	VkSwapchainKHR swapchain;
	res = CreateSwapchainKHR(device, &swap_info, NULL, &swapchain);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateSwapchainKHR() failed (%d)\n", res);
		return 14;
	}

	/*
		15) Get swapchain images.
		    These are the endpoints for our framebuffers.
	*/
	int n_images = 0;
	res = GetSwapchainImagesKHR(device, swapchain, &n_images, NULL);
	if (n_images <= 0 || res != VK_SUCCESS) {
		fprintf(stderr, "Could not find any swapchain images\n");
		return 15;
	}

	VkImage *images = calloc(n_images, sizeof(VkImage));
	res = GetSwapchainImagesKHR(device, swapchain, &n_images, images);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkGetSwapchainImagesKHR() failed (%d)\n", res);
		return 15;
	}

	/*
		16) Create image views for the swapchain.
	*/
	VkImageViewCreateInfo iv_info = {0};
	iv_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	iv_info.pNext = NULL;
	iv_info.format = color_fmt.format;
	iv_info.components = (VkComponentMapping){
		.r = VK_COMPONENT_SWIZZLE_R,
		.g = VK_COMPONENT_SWIZZLE_G,
		.b = VK_COMPONENT_SWIZZLE_B,
		.a = VK_COMPONENT_SWIZZLE_A
	};
	iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	iv_info.subresourceRange.baseMipLevel = 0;
	iv_info.subresourceRange.levelCount = 1;
	iv_info.subresourceRange.baseArrayLayer = 0;
	iv_info.subresourceRange.layerCount = 1;
	iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	iv_info.flags = 0;

	VkImageView *img_views = calloc(n_images, sizeof(VkImageView));
	for (int i = 0; i < n_images; i++) {
		iv_info.image = images[i];
		res = vkCreateImageView(device, &iv_info, NULL, &img_views[i]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkCreateImageView() %d failed (%d)\n", i, res);
			return 16;
		}
	}

	/*
		17) Create a command pool.
		    A command pool is essentially a thread-specific block of memory that is used for allocating commands.
	*/
	VkCommandPoolCreateInfo cpool_info = {0};
	cpool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpool_info.queueFamilyIndex = queue_index;
	cpool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkCommandPool cmd_pool;
	res = vkCreateCommandPool(device, &cpool_info, NULL, &cmd_pool);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateCommandPool() failed (%d)\n", res);
		return 17;
	}

	/*
		18) Allocate command buffers - one for each image
	*/
	VkCommandBufferAllocateInfo cbuf_alloc_info = {0};
	cbuf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbuf_alloc_info.commandPool = cmd_pool;
	cbuf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbuf_alloc_info.commandBufferCount = n_images;

	VkCommandBuffer *cmd_buffers = calloc(n_images, sizeof(VkCommandBuffer));
	res = vkAllocateCommandBuffers(device, &cbuf_alloc_info, cmd_buffers);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkAllocateCommandBuffers() failed (%d)\n", res);
		return 18;
	}

	/*
		19) Select the depth format.
		    Used in the creation of the depth stencil.
	*/
	VkFormat formats[] = {
		VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM
	};

	VkFormat depth_fmt = VK_FORMAT_UNDEFINED;
	for (int i = 0; i < sizeof(formats) / sizeof(VkFormat); i++) {
		VkFormatProperties cfg;
		vkGetPhysicalDeviceFormatProperties(gpu, formats[i], &cfg);
		if (cfg.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depth_fmt = formats[i];
			break;
		}
	}

	if (depth_fmt == VK_FORMAT_UNDEFINED) {
		fprintf(stderr, "Could not find a suitable depth format\n");
		return 19;
	}

	/*
		20) Create depth stencil image.
	*/
	VkImageCreateInfo dimg_info = {0};
	dimg_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	dimg_info.imageType = VK_IMAGE_TYPE_2D;
	dimg_info.format = depth_fmt;
	dimg_info.extent = (VkExtent3D){
		.width  = surf_caps.currentExtent.width,
		.height = surf_caps.currentExtent.height,
		.depth  = 1
	};
	dimg_info.mipLevels = 1;
	dimg_info.arrayLayers = 1;
	dimg_info.samples = VK_SAMPLE_COUNT_1_BIT;
	dimg_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	dimg_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImage depth_img;
	res = vkCreateImage(device, &dimg_info, NULL, &depth_img);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateImage() for depth stencil failed (%d)\n", res);
		return 20;
	}

	/*
		21) Allocate memory for the depth stencil.
	*/
	VkPhysicalDeviceMemoryProperties gpu_mem;
	vkGetPhysicalDeviceMemoryProperties(gpu, &gpu_mem);

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(device, depth_img, &mem_reqs);

	int mem_type_idx = -1;
	for (int i = 0; i < gpu_mem.memoryTypeCount; i++) {
		if ((mem_reqs.memoryTypeBits & (1 << i)) &&
			(gpu_mem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			mem_type_idx = i;
			break;
		}
	}

	if (mem_type_idx < 0) {
		fprintf(stderr, "Could not find a suitable type of graphics memory\n");
		return 21;
	}

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = mem_type_idx;

	VkDeviceMemory depth_mem;
	res = vkAllocateMemory(device, &alloc_info, NULL, &depth_mem);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkAllocateMemory() for depth stencil failed (%d)\n", res);
		return 21;
	}

	/*
		22) Bind the newly allocated memory to the depth stencil image.
	*/
	res = vkBindImageMemory(device, depth_img, depth_mem, 0);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkBindImageMemory() for depth stencil failed (%d)\n", res);
		return 22;
	}

	/*
		23) Create depth stencil view.
		    This is passed to each framebuffer.
	*/
	VkImageAspectFlagBits aspect =
		depth_fmt >= VK_FORMAT_D16_UNORM_S8_UINT ?
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT :
		VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageViewCreateInfo dview_info = {0};
	dview_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	dview_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	dview_info.image = depth_img;
	dview_info.format = depth_fmt;
	dview_info.subresourceRange.baseMipLevel = 0;
	dview_info.subresourceRange.levelCount = 1;
	dview_info.subresourceRange.baseArrayLayer = 0;
	dview_info.subresourceRange.layerCount = 1;
	dview_info.subresourceRange.aspectMask = aspect;

	VkImageView depth_view;
	res = vkCreateImageView(device, &dview_info, NULL, &depth_view);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateImageView() for depth stencil failed (%d)\n", res);
		return 23;
	}

	/*
		24) Set up the render pass.
	*/
	VkAttachmentDescription attachments[] = {
		{ // Color attachment
			.flags          = 0,
			.format         = color_fmt.format,
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		},
		{ // Depth attachment
			.flags          = 0,
			.format         = depth_fmt,
			.samples        = VK_SAMPLE_COUNT_1_BIT,
			.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		}
	};

	VkAttachmentReference color_ref = {0};
	color_ref.attachment = 0;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_ref = {0};
	depth_ref.attachment = 1;
	depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {0};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;
	subpass.pDepthStencilAttachment = &depth_ref;

	VkSubpassDependency dependencies[] = {
		{
			.srcSubpass      = VK_SUBPASS_EXTERNAL,
			.dstSubpass      = 0,
			.srcStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask   = VK_ACCESS_MEMORY_READ_BIT,
			.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			.srcSubpass      = 0,
			.dstSubpass      = VK_SUBPASS_EXTERNAL,
			.srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	VkRenderPassCreateInfo pass_info = {0};
	pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass_info.attachmentCount = 2;
	pass_info.pAttachments = attachments;
	pass_info.subpassCount = 1;
	pass_info.pSubpasses = &subpass;
	pass_info.dependencyCount = 2;
	pass_info.pDependencies = dependencies;

	VkRenderPass renderpass;
	res = vkCreateRenderPass(device, &pass_info, NULL, &renderpass);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateRenderPass() failed (%d)\n", res);
		return 24;
	}

	/*
		25) Create the frame buffers.
	*/
	VkImageView fb_views[2];
	fb_views[1] = depth_view;

	VkFramebufferCreateInfo fb_info = {0};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.renderPass = renderpass;
	fb_info.attachmentCount = 2;
	fb_info.pAttachments = fb_views;
	fb_info.width = surf_caps.currentExtent.width;
	fb_info.height = surf_caps.currentExtent.height;
	fb_info.layers = 1;

	VkFramebuffer *fbuffers = calloc(n_images, sizeof(VkFramebuffer));
	for (int i = 0; i < n_images; i++) {
		fb_views[0] = img_views[i];
		res = vkCreateFramebuffer(device, &fb_info, NULL, &fbuffers[i]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkCreateFramebuffer() %d failed (%d)\n", i, res);
			return 25;
		}
	}

	/*
		26) Create semaphores for synchronising draw commands and image presentation.
	*/
	VkSemaphoreCreateInfo bake_sema = {0};
	bake_sema.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore sema_present, sema_render;
	if (vkCreateSemaphore(device, &bake_sema, NULL, &sema_present) != VK_SUCCESS ||
	    vkCreateSemaphore(device, &bake_sema, NULL, &sema_render) != VK_SUCCESS)
	{
		fprintf(stderr, "Failed to create Vulkan semaphores\n");
		return 26;
	}

	/*
		27) Create wait fences - one for each image.
	*/
	VkFenceCreateInfo fence_info = {0};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence *fences = calloc(n_images, sizeof(VkFence));
	for (int i = 0; i < n_images; i++) {
		res = vkCreateFence(device, &fence_info, NULL, &fences[i]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkCreateFence() failed (%d)\n", res);
			return 27;
		}
	}

	/*
		28) Prepare the triangle!
		    Note: don't write code like this. Modularising code into functions is usually better than making loops with ad-hoc data structures.
			 This code was explicitly written this way for demonstration purposes.
	*/
	float vertices[] = {
		// Position           Color
		 1.0f,  1.0f,  0.0f,  1.0f,  0.0f,  0.0f,
		-1.0f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
		 0.0f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f
	};
	int indices[] = {0, 1, 2};
	float mvp[] = {
		// Projection Matrix (60deg FOV, 3:2 aspect ratio, [1.0, 256.0] clipping plane range)
		1.155,  0.000,  0.000,  0.000,
		0.000,  1.732,  0.000,  0.000,
		0.000,  0.000, -1.008, -1.000,
		0.000,  0.000, -2.008,  0.000,
		// Model Matrix (identity)
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
		// View Matrix (distance of 2.5)
		1.0f,  0.0f,  0.0f,  0.0f,
		0.0f,  1.0f,  0.0f,  0.0f,
		0.0f,  0.0f,  1.0f,  0.0f,
		0.0f,  0.0f, -2.5f,  1.0f
	};

	struct {
		void *bytes;
		int size;
		VkBufferUsageFlagBits usage;
		VkDeviceMemory memory;
		VkBuffer buffer;
	} data[] = {
		{(void*)vertices, 18 * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,  VK_NULL_HANDLE, VK_NULL_HANDLE},
		{(void*)indices,   3 * sizeof(int),   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,   VK_NULL_HANDLE, VK_NULL_HANDLE},
		{(void*)mvp,      48 * sizeof(float), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE}
	};

	for (int i = 0; i < 3; i++) {
		VkBufferCreateInfo buf_info = {0};
		buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buf_info.size = data[i].size;
		buf_info.usage = data[i].usage;

		res = vkCreateBuffer(device, &buf_info, NULL, &data[i].buffer);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkCreateBuffer() %d failed (%d)\n", i, res);
			return 28;
		}

		VkMemoryRequirements mem_reqs;
		vkGetBufferMemoryRequirements(device, data[i].buffer, &mem_reqs);

		unsigned int flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		int type_idx = -1;
		for (int j = 0; j < gpu_mem.memoryTypeCount; j++) {
			if ((mem_reqs.memoryTypeBits & (1 << j)) &&
				(gpu_mem.memoryTypes[j].propertyFlags & flags) == flags)
			{
				mem_type_idx = j;
				break;
			}
		}

		if (mem_type_idx < 0) {
			fprintf(stderr, "Could not find an appropriate memory type (%d)\n", i);
			return 28;
		}

		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = mem_type_idx;

		res = vkAllocateMemory(device, &alloc_info, NULL, &data[i].memory);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkAllocateMemory() %d failed (%d)\n", i, res);
			return 28;
		}

		void *buf;
		res = vkMapMemory(device, data[i].memory, 0, alloc_info.allocationSize, 0, &buf);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkMapMemory() %d failed (%d)\n", i, res);
			return 28;
		}

		memcpy(buf, data[i].bytes, data[i].size);
		vkUnmapMemory(device, data[i].memory);

		res = vkBindBufferMemory(device, data[i].buffer, data[i].memory, 0);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkBindBufferMemory() %d failed (%d)\n", i, res);
			return 28;
		}
	}

	/*
		29) Describe the MVP to a uniform descriptor.
	*/
	VkDescriptorBufferInfo uniform_info = {0};
	uniform_info.buffer = data[2].buffer;
	uniform_info.offset = 0;
	uniform_info.range = data[2].size;

	/*
		30) Create descriptor set layout.
	*/
	VkDescriptorSetLayoutBinding ds_bind = {0};
	ds_bind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	ds_bind.descriptorCount = 1;
	ds_bind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo ds_info = {0};
	ds_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ds_info.bindingCount = 1;
	ds_info.pBindings = &ds_bind;

	VkDescriptorSetLayout ds_layout;
	res = vkCreateDescriptorSetLayout(device, &ds_info, NULL, &ds_layout);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateDescriptorSetLayout() failed (%d)\n", res);
		return 30;
	}

	/*
		31) Create pipeline layout.
	*/
	VkPipelineLayoutCreateInfo pl_info = {0};
	pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_info.setLayoutCount = 1;
	pl_info.pSetLayouts = &ds_layout;

	VkPipelineLayout pl_layout;
	res = vkCreatePipelineLayout(device, &pl_info, NULL, &pl_layout);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreatePipelineLayout() failed (%d)\n", res);
		return 31;
	}

	/*
		32) Prepare shaders.
	*/
	VkShaderModuleCreateInfo mod_info = {0};
	mod_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	mod_info.codeSize = frag_shader_spv_size;
	mod_info.pCode = (unsigned int *)frag_shader_spv;

	VkShaderModule frag_shader;
	res = vkCreateShaderModule(device, &mod_info, NULL, &frag_shader);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateShaderModule() for fragment shader failed (%d)\n", res);
		return 32;
	}

	mod_info.codeSize = vert_shader_spv_size;
	mod_info.pCode = (unsigned int *)vert_shader_spv;

	VkShaderModule vert_shader;
	res = vkCreateShaderModule(device, &mod_info, NULL, &vert_shader);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateShaderModule() for vertex shader failed (%d)\n", res);
		return 32;
	}

	/*
		33) Create graphics pipeline.
	*/
	VkPipelineInputAssemblyStateCreateInfo asm_info = {0};
	asm_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	asm_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo raster_info = {0};
	raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster_info.polygonMode = VK_POLYGON_MODE_FILL;
	raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster_info.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState cblend_att = {0};
	cblend_att.colorWriteMask = 0xf;

	VkPipelineColorBlendStateCreateInfo cblend_info = {0};
	cblend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cblend_info.attachmentCount = 1;
	cblend_info.pAttachments = &cblend_att;

	VkPipelineViewportStateCreateInfo vp_info = {0};
	vp_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp_info.viewportCount = 1;
	vp_info.scissorCount = 1;

	VkDynamicState dyn_vars[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dyn_info = {0};
	dyn_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dyn_info.pDynamicStates = dyn_vars;
	dyn_info.dynamicStateCount = 2;

	VkPipelineDepthStencilStateCreateInfo depth_info = {0};
	depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_info.depthTestEnable = VK_TRUE;
	depth_info.depthWriteEnable = VK_TRUE;
	depth_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_info.back.failOp = VK_STENCIL_OP_KEEP;
	depth_info.back.passOp = VK_STENCIL_OP_KEEP;
	depth_info.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depth_info.front = depth_info.back;

	VkPipelineMultisampleStateCreateInfo ms_info = {0};
	ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkVertexInputBindingDescription vb_info = {0};
	vb_info.binding = 0;
	vb_info.stride = 6 * sizeof(float); // position and color
	vb_info.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vert_att[] = {
		{ // Position
			.binding  = 0,
			.location = 0,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = 0
		},
		{ // Color
			.binding  = 0,
			.location = 1,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = 3 * sizeof(float)
		}
	};

	VkPipelineVertexInputStateCreateInfo vert_info = {0};
	vert_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vert_info.vertexBindingDescriptionCount = 1;
	vert_info.pVertexBindingDescriptions = &vb_info;
	vert_info.vertexAttributeDescriptionCount = 2;
	vert_info.pVertexAttributeDescriptions = vert_att;

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vert_shader,
			.pName  = "main"
		},
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_shader,
			.pName  = "main"
		}
	};

	VkGraphicsPipelineCreateInfo pipe_info = {0};
	pipe_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe_info.layout = pl_layout;
	pipe_info.stageCount = 2;
	pipe_info.pStages = shader_stages;
	pipe_info.pVertexInputState = &vert_info;
	pipe_info.pInputAssemblyState = &asm_info;
	pipe_info.pRasterizationState = &raster_info;
	pipe_info.pColorBlendState = &cblend_info;
	pipe_info.pMultisampleState = &ms_info;
	pipe_info.pViewportState = &vp_info;
	pipe_info.pDepthStencilState = &depth_info;
	pipe_info.renderPass = renderpass;
	pipe_info.pDynamicState = &dyn_info;

	VkPipeline pipeline;
	res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateGraphicsPipelines() failed (%d)\n", res);
		return 33;
	}

	/*
		34) Destroy shader modules (now that they have already been incorporated into the pipeline).
	*/
	vkDestroyShaderModule(device, vert_shader, NULL);
	vkDestroyShaderModule(device, frag_shader, NULL);

	/*
		35) Create a descriptor pool for our descriptor set.
	*/
	VkDescriptorPoolSize ps_info = {0};
	ps_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	ps_info.descriptorCount = 1;

	VkDescriptorPoolCreateInfo dpool_info = {0};
	dpool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpool_info.poolSizeCount = 1;
	dpool_info.pPoolSizes = &ps_info;
	dpool_info.maxSets = 1;

	VkDescriptorPool dpool;
	res = vkCreateDescriptorPool(device, &dpool_info, NULL, &dpool);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkCreateDescriptorPool() failed (%d)\n", res);
		return 35;
	}

	/*
		36) Allocate a descriptor set.
		    Descriptor sets let us pass additional data into our shaders.
	*/
	VkDescriptorSetAllocateInfo ds_alloc_info = {};
	ds_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ds_alloc_info.descriptorPool = dpool;
	ds_alloc_info.descriptorSetCount = 1;
	ds_alloc_info.pSetLayouts = &ds_layout;

	VkDescriptorSet desc_set;
	res = vkAllocateDescriptorSets(device, &ds_alloc_info, &desc_set);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "vkAllocateDescriptorSets() failed (%d)\n", res);
		return 36;
	}

	/*
		37) Set up the descriptor set.
	*/
	VkWriteDescriptorSet write_info = {0};
	write_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write_info.dstSet = desc_set;
	write_info.descriptorCount = 1;
	write_info.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write_info.pBufferInfo = &uniform_info;
	write_info.dstBinding = 0;

	vkUpdateDescriptorSets(device, 1, &write_info, 0, NULL);

	/*
		38) Construct the command buffers.
		    This is where we place the draw commands, which are executed by the GPU later.
	*/
	VkCommandBufferBeginInfo cbuf_info = {0};
	cbuf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VkClearValue clear_values[] = {
		{
			.color = { { 0.0f, 0.0f, 0.2f, 1.0f } }
		}, {
			.depthStencil = { 1.0f, 0 }
		}
	};

	VkRenderPassBeginInfo rp_info = {0};
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_info.renderPass = renderpass;
	rp_info.renderArea.offset.x = 0;
	rp_info.renderArea.offset.y = 0;
	rp_info.renderArea.extent = surf_caps.currentExtent;
	rp_info.clearValueCount = 2;
	rp_info.pClearValues = clear_values;

	VkRect2D *scissor = &rp_info.renderArea;

	VkViewport viewport = {};
	viewport.height = (float)surf_caps.currentExtent.height;
	viewport.width = (float)surf_caps.currentExtent.width;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	for (int i = 0; i < n_images; i++) {
		res = vkBeginCommandBuffer(cmd_buffers[i], &cbuf_info);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkBeginCommandBuffer() %d failed (%d)\n", i, res);
			return 38;
		}

		rp_info.framebuffer = fbuffers[i];
		vkCmdBeginRenderPass(cmd_buffers[i], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdSetViewport(cmd_buffers[i], 0, 1, &viewport);
		vkCmdSetScissor(cmd_buffers[i], 0, 1, scissor);

		vkCmdBindDescriptorSets(cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pl_layout, 0, 1, &desc_set, 0, NULL);
		vkCmdBindPipeline(cmd_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd_buffers[i], 0, 1, &data[0].buffer, &offset);
		vkCmdBindIndexBuffer(cmd_buffers[i], data[1].buffer, 0, VK_INDEX_TYPE_UINT32);

		const int n_indices = 3;
		vkCmdDrawIndexed(cmd_buffers[i], n_indices, 1, 0, 0, 1);

		vkCmdEndRenderPass(cmd_buffers[i]);
		res = vkEndCommandBuffer(cmd_buffers[i]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkEndCommandBuffer() %d failed (%d)\n", i, res);
			return 38;
		}
	}

	/*
		39) Prepare main loop.
	*/
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.pWaitSemaphores = &sema_present;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &sema_render;
	submit_info.signalSemaphoreCount = 1;
	submit_info.commandBufferCount = 1;

	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &swapchain;
	present_info.pWaitSemaphores = &sema_render;
	present_info.waitSemaphoreCount = 1;

	VkQueue queue;
	vkGetDeviceQueue(device, queue_index, 0, &queue);

	/*
		40) Main loop.
	*/
	unsigned long long max64 = -1;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		int idx;
		res = AcquireNextImageKHR(device, swapchain, max64, sema_present, NULL, &idx);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkAcquireNextImageKHR() failed (%d)\n", res);
			return 40;
		}

		res = vkWaitForFences(device, 1, &fences[idx], VK_TRUE, max64);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkWaitForFences() failed (%d)\n", res);
			return 40;
		}

		res = vkResetFences(device, 1, &fences[idx]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkResetFences() failed (%d)\n", res);
			return 40;
		}

		submit_info.pCommandBuffers = &cmd_buffers[idx];
		res = vkQueueSubmit(queue, 1, &submit_info, fences[idx]);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "vkQueueSubmit() failed (%d)\n", res);
			return 40;
		}

		present_info.pImageIndices = &idx;
		res = QueuePresentKHR(queue, &present_info);
		if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			fprintf(stderr, "vkQueuePresentKHR() failed (%d)\n", res);
			return 40;
		}
	}

	/*
		41) Clean-up.
	*/
	for (int i = 0; i < 3; i++) {
		vkDestroyBuffer(device, data[i].buffer, NULL);
		vkFreeMemory(device, data[i].memory, NULL);
	}

	vkDestroyImageView(device, depth_view, NULL);
	vkDestroyImage(device, depth_img, NULL);
	vkFreeMemory(device, depth_mem, NULL);

	vkDestroySemaphore(device, sema_present, NULL);
	vkDestroySemaphore(device, sema_render, NULL);

	for (int i = 0; i < n_images; i++)
		vkDestroyFence(device, fences[i], NULL);
	free(fences);

	for (int i = 0; i < n_images; i++)
		vkDestroyFramebuffer(device, fbuffers[i], NULL);
	free(fbuffers);

	//vkFreeCommandBuffers(device, cmd_pool, n_images, cmd_buffers);
	vkDestroyCommandPool(device, cmd_pool, NULL);
	free(cmd_buffers);

	vkDestroyDescriptorPool(device, dpool, NULL);
	vkDestroyDescriptorSetLayout(device, ds_layout, NULL);

	vkDestroyPipelineLayout(device, pl_layout, NULL);
	vkDestroyPipeline(device, pipeline, NULL);
	vkDestroyRenderPass(device, renderpass, NULL);

	for (int i = 0; i < n_images; i++)
		vkDestroyImageView(device, img_views[i], NULL);
	free(img_views);

	free(images);

	DestroySwapchainKHR(device, swapchain, NULL);
	vkDestroyDevice(device, NULL);

	vkDestroySurfaceKHR(instance, surface, NULL);
	vkDestroyInstance(instance, NULL);

	free(dev_exts);
	free(dev_ext_props);

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

// Fragment Shader (https://github.com/SaschaWillems/Vulkan/data/shaders/triangle/triangle.frag)
/*
#version 450

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	outFragColor = vec4(inColor, 1.0);
}
*/
unsigned char frag_shader_spv[] = {
	0x03,0x02,0x23,0x07,0x00,0x00,0x01,0x00,0x07,0x00,0x08,0x00,0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x00,0x02,0x00,
	0x01,0x00,0x00,0x00,0x0b,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x47,0x4c,0x53,0x4c,0x2e,0x73,0x74,0x64,0x2e,0x34,0x35,0x30,
	0x00,0x00,0x00,0x00,0x0e,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x0f,0x00,0x07,0x00,0x04,0x00,0x00,0x00,
	0x04,0x00,0x00,0x00,0x6d,0x61,0x69,0x6e,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x10,0x00,0x03,0x00,
	0x04,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x02,0x00,0x00,0x00,0xc2,0x01,0x00,0x00,0x05,0x00,0x04,0x00,
	0x04,0x00,0x00,0x00,0x6d,0x61,0x69,0x6e,0x00,0x00,0x00,0x00,0x05,0x00,0x06,0x00,0x09,0x00,0x00,0x00,0x6f,0x75,0x74,0x46,
	0x72,0x61,0x67,0x43,0x6f,0x6c,0x6f,0x72,0x00,0x00,0x00,0x00,0x05,0x00,0x04,0x00,0x0c,0x00,0x00,0x00,0x69,0x6e,0x43,0x6f,
	0x6c,0x6f,0x72,0x00,0x47,0x00,0x04,0x00,0x09,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x47,0x00,0x04,0x00,
	0x0c,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x13,0x00,0x02,0x00,0x02,0x00,0x00,0x00,0x21,0x00,0x03,0x00,
	0x03,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x16,0x00,0x03,0x00,0x06,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x17,0x00,0x04,0x00,
	0x07,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x20,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
	0x07,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x17,0x00,0x04,0x00,
	0x0a,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x20,0x00,0x04,0x00,0x0b,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
	0x0a,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,0x0b,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x2b,0x00,0x04,0x00,
	0x06,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,0x00,0x00,0x80,0x3f,0x36,0x00,0x05,0x00,0x02,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0xf8,0x00,0x02,0x00,0x05,0x00,0x00,0x00,0x3d,0x00,0x04,0x00,0x0a,0x00,0x00,0x00,
	0x0d,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x51,0x00,0x05,0x00,0x06,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x51,0x00,0x05,0x00,0x06,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
	0x51,0x00,0x05,0x00,0x06,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x50,0x00,0x07,0x00,
	0x07,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,
	0x3e,0x00,0x03,0x00,0x09,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0xfd,0x00,0x01,0x00,0x38,0x00,0x01,0x00,
};

// Vertex Shader (https://github.com/SaschaWillems/Vulkan/data/shaders/triangle/triangle.vert)
/*
#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
} ubo;

layout (location = 0) out vec3 outColor;

out gl_PerVertex 
{
	vec4 gl_Position;   
};


void main() 
{
	outColor = inColor;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
*/
unsigned char vert_shader_spv[] = {
	0x03,0x02,0x23,0x07,0x00,0x00,0x01,0x00,0x07,0x00,0x08,0x00,0x2c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x00,0x02,0x00,
	0x01,0x00,0x00,0x00,0x0b,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x47,0x4c,0x53,0x4c,0x2e,0x73,0x74,0x64,0x2e,0x34,0x35,0x30,
	0x00,0x00,0x00,0x00,0x0e,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x0f,0x00,0x09,0x00,0x00,0x00,0x00,0x00,
	0x04,0x00,0x00,0x00,0x6d,0x61,0x69,0x6e,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
	0x22,0x00,0x00,0x00,0x03,0x00,0x03,0x00,0x02,0x00,0x00,0x00,0xc2,0x01,0x00,0x00,0x05,0x00,0x04,0x00,0x04,0x00,0x00,0x00,
	0x6d,0x61,0x69,0x6e,0x00,0x00,0x00,0x00,0x05,0x00,0x05,0x00,0x09,0x00,0x00,0x00,0x6f,0x75,0x74,0x43,0x6f,0x6c,0x6f,0x72,
	0x00,0x00,0x00,0x00,0x05,0x00,0x04,0x00,0x0b,0x00,0x00,0x00,0x69,0x6e,0x43,0x6f,0x6c,0x6f,0x72,0x00,0x05,0x00,0x06,0x00,
	0x0e,0x00,0x00,0x00,0x67,0x6c,0x5f,0x50,0x65,0x72,0x56,0x65,0x72,0x74,0x65,0x78,0x00,0x00,0x00,0x00,0x06,0x00,0x06,0x00,
	0x0e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x6c,0x5f,0x50,0x6f,0x73,0x69,0x74,0x69,0x6f,0x6e,0x00,0x05,0x00,0x03,0x00,
	0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x03,0x00,0x14,0x00,0x00,0x00,0x55,0x42,0x4f,0x00,0x06,0x00,0x08,0x00,
	0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x72,0x6f,0x6a,0x65,0x63,0x74,0x69,0x6f,0x6e,0x4d,0x61,0x74,0x72,0x69,0x78,
	0x00,0x00,0x00,0x00,0x06,0x00,0x06,0x00,0x14,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x6d,0x6f,0x64,0x65,0x6c,0x4d,0x61,0x74,
	0x72,0x69,0x78,0x00,0x06,0x00,0x06,0x00,0x14,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x76,0x69,0x65,0x77,0x4d,0x61,0x74,0x72,
	0x69,0x78,0x00,0x00,0x05,0x00,0x03,0x00,0x16,0x00,0x00,0x00,0x75,0x62,0x6f,0x00,0x05,0x00,0x04,0x00,0x22,0x00,0x00,0x00,
	0x69,0x6e,0x50,0x6f,0x73,0x00,0x00,0x00,0x47,0x00,0x04,0x00,0x09,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x47,0x00,0x04,0x00,0x0b,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x48,0x00,0x05,0x00,0x0e,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x47,0x00,0x03,0x00,0x0e,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
	0x48,0x00,0x04,0x00,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x48,0x00,0x05,0x00,0x14,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0x00,0x05,0x00,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x07,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x48,0x00,0x04,0x00,0x14,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x05,0x00,0x00,0x00,
	0x48,0x00,0x05,0x00,0x14,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x48,0x00,0x05,0x00,
	0x14,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x48,0x00,0x04,0x00,0x14,0x00,0x00,0x00,
	0x02,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x48,0x00,0x05,0x00,0x14,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x23,0x00,0x00,0x00,
	0x80,0x00,0x00,0x00,0x48,0x00,0x05,0x00,0x14,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
	0x47,0x00,0x03,0x00,0x14,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x47,0x00,0x04,0x00,0x16,0x00,0x00,0x00,0x22,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x47,0x00,0x04,0x00,0x16,0x00,0x00,0x00,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x47,0x00,0x04,0x00,
	0x22,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x13,0x00,0x02,0x00,0x02,0x00,0x00,0x00,0x21,0x00,0x03,0x00,
	0x03,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x16,0x00,0x03,0x00,0x06,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x17,0x00,0x04,0x00,
	0x07,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x20,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
	0x07,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,0x08,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x20,0x00,0x04,0x00,
	0x0a,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,0x0a,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,
	0x01,0x00,0x00,0x00,0x17,0x00,0x04,0x00,0x0d,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x1e,0x00,0x03,0x00,
	0x0e,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x20,0x00,0x04,0x00,0x0f,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0e,0x00,0x00,0x00,
	0x3b,0x00,0x04,0x00,0x0f,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x15,0x00,0x04,0x00,0x11,0x00,0x00,0x00,
	0x20,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x2b,0x00,0x04,0x00,0x11,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x18,0x00,0x04,0x00,0x13,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x1e,0x00,0x05,0x00,0x14,0x00,0x00,0x00,
	0x13,0x00,0x00,0x00,0x13,0x00,0x00,0x00,0x13,0x00,0x00,0x00,0x20,0x00,0x04,0x00,0x15,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
	0x14,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,0x15,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x20,0x00,0x04,0x00,
	0x17,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x13,0x00,0x00,0x00,0x2b,0x00,0x04,0x00,0x11,0x00,0x00,0x00,0x1a,0x00,0x00,0x00,
	0x02,0x00,0x00,0x00,0x2b,0x00,0x04,0x00,0x11,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x3b,0x00,0x04,0x00,
	0x0a,0x00,0x00,0x00,0x22,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x2b,0x00,0x04,0x00,0x06,0x00,0x00,0x00,0x24,0x00,0x00,0x00,
	0x00,0x00,0x80,0x3f,0x20,0x00,0x04,0x00,0x2a,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x36,0x00,0x05,0x00,
	0x02,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0xf8,0x00,0x02,0x00,0x05,0x00,0x00,0x00,
	0x3d,0x00,0x04,0x00,0x07,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0x3e,0x00,0x03,0x00,0x09,0x00,0x00,0x00,
	0x0c,0x00,0x00,0x00,0x41,0x00,0x05,0x00,0x17,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x12,0x00,0x00,0x00,
	0x3d,0x00,0x04,0x00,0x13,0x00,0x00,0x00,0x19,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x41,0x00,0x05,0x00,0x17,0x00,0x00,0x00,
	0x1b,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x1a,0x00,0x00,0x00,0x3d,0x00,0x04,0x00,0x13,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,
	0x1b,0x00,0x00,0x00,0x92,0x00,0x05,0x00,0x13,0x00,0x00,0x00,0x1d,0x00,0x00,0x00,0x19,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,
	0x41,0x00,0x05,0x00,0x17,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x3d,0x00,0x04,0x00,
	0x13,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x92,0x00,0x05,0x00,0x13,0x00,0x00,0x00,0x21,0x00,0x00,0x00,
	0x1d,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x3d,0x00,0x04,0x00,0x07,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x22,0x00,0x00,0x00,
	0x51,0x00,0x05,0x00,0x06,0x00,0x00,0x00,0x25,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x51,0x00,0x05,0x00,
	0x06,0x00,0x00,0x00,0x26,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x51,0x00,0x05,0x00,0x06,0x00,0x00,0x00,
	0x27,0x00,0x00,0x00,0x23,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x50,0x00,0x07,0x00,0x0d,0x00,0x00,0x00,0x28,0x00,0x00,0x00,
	0x25,0x00,0x00,0x00,0x26,0x00,0x00,0x00,0x27,0x00,0x00,0x00,0x24,0x00,0x00,0x00,0x91,0x00,0x05,0x00,0x0d,0x00,0x00,0x00,
	0x29,0x00,0x00,0x00,0x21,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x41,0x00,0x05,0x00,0x2a,0x00,0x00,0x00,0x2b,0x00,0x00,0x00,
	0x10,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x3e,0x00,0x03,0x00,0x2b,0x00,0x00,0x00,0x29,0x00,0x00,0x00,0xfd,0x00,0x01,0x00,
	0x38,0x00,0x01,0x00,
};
