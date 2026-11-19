#include "liquidglassvulkan.h"

#ifdef NEKO_HAS_VULKAN_GLASS

#include <QLibrary>
#include <QLoggingCategory>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace LiquidGlass {
namespace {

Q_LOGGING_CATEGORY(glassLog, "neko.liquidglass.vulkan")

constexpr uint32_t blurShader[] = {
#include "liquidglass_blur_spirv.inc"
};
constexpr uint32_t materialShader[] = {
#include "liquidglass_spirv.inc"
};
constexpr uint32_t fieldShader[] = {
#include "liquidglass_field_spirv.inc"
};

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kFieldFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct MaterialPush {
    float size[2];
    float origin[2];
    float sourceScale[2];
    float radius, pixelWidth, bezel, bend, dispersion, frost;
    float tint[4];
    float light[2];
    float shadow, highlight;
};
static_assert(sizeof(MaterialPush) == 80);
static_assert(offsetof(MaterialPush, tint) == 48);

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize capacity = 0;
};

struct Image {
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    QSize size;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Program {
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

#define NEKO_GLASS_INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties) X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define NEKO_GLASS_DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) \
    X(vkDestroyPipeline) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkResetCommandPool) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdBindPipeline) \
    X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDispatch) \
    X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit)

class Renderer
{
public:
    bool initialize();
    ~Renderer();
    QImage render(const QImage &image, const QRect &bounds, const QRectF &source,
                  const QSize &pixels, const Optics &optics);

private:
    bool createInstance();
    bool selectDevice();
    bool setupCommands();
    bool createProgram(Program &program, const uint32_t *code, size_t size,
                       uint32_t inputs, uint32_t pushSize);
    bool allocateMemory(VkDeviceMemory &memory, const VkMemoryRequirements &requirements,
                        VkMemoryPropertyFlags flags);
    bool ensureImage(Image &image, const QSize &size, VkImageUsageFlags usage,
                     VkFormat format = kFormat);
    bool ensureBuffer(Buffer &buffer, VkDeviceSize capacity, VkBufferUsageFlags usage);
    void destroyImage(Image &image);
    void destroyBuffer(Buffer &buffer);
    void destroyProgram(const Program &program);
    void transition(Image &image, VkImageLayout layout, VkPipelineStageFlags before,
                    VkAccessFlags from, VkPipelineStageFlags after, VkAccessFlags to);
    void setDescriptors(VkDescriptorSet set, std::initializer_list<const Image *> inputs,
                        const Image &output);
    void dispatch(const Program &program, VkDescriptorSet set, const QSize &size,
                  const void *parameters, uint32_t parameterSize);
    bool submit();

    QLibrary m_loader;
    PFN_vkGetInstanceProcAddr m_getProc = nullptr;
#define DECLARE_FUNCTION(name) PFN_##name name = nullptr;
    NEKO_GLASS_INSTANCE_FUNCTIONS(DECLARE_FUNCTION)
    NEKO_GLASS_DEVICE_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memory{};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_family = 0;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkCommandPool m_commands = VK_NULL_HANDLE;
    VkCommandBuffer m_command = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 4> m_sets{};
    Program m_blur, m_field, m_material;
    Buffer m_upload, m_readback;
    Image m_input, m_horizontal, m_diffuse, m_shape, m_output;
    qint64 m_inputKey = 0;
    QSize m_boundsSize;
    bool m_failed = false;
};

bool Renderer::createInstance()
{
#ifdef Q_OS_WIN
    m_loader.setFileName(QStringLiteral("vulkan-1"));
#elif defined(Q_OS_MACOS)
    m_loader.setFileName(QStringLiteral("vulkan"));
#else
    m_loader.setFileNameAndVersion(QStringLiteral("vulkan"), 1);
#endif
    m_getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(m_loader.resolve("vkGetInstanceProcAddr"));
    if (!m_getProc)
        return false;
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(m_getProc(nullptr, "vkCreateInstance"));
    if (!create)
        return false;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NekoMusic Liquid Glass";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
#if defined(Q_OS_MACOS) && defined(VK_KHR_portability_enumeration)
    const char *portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &portability;
#endif
    if (create(&info, nullptr, &m_instance) != VK_SUCCESS)
        return false;

#define LOAD_INSTANCE(name) \
    name = reinterpret_cast<PFN_##name>(m_getProc(m_instance, #name)); \
    if (!name) return false;
    NEKO_GLASS_INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    return true;
}

bool Renderer::selectDevice()
{
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(m_instance, &count, nullptr) != VK_SUCCESS || count == 0)
        return false;
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(m_instance, &count, devices.data()) != VK_SUCCESS)
        return false;

    int best = -1;
    for (const auto device : devices) {
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(device, kFormat, &format);
        constexpr auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
            | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((format.optimalTilingFeatures & required) != required)
            continue;
        vkGetPhysicalDeviceFormatProperties(device, kFieldFormat, &format);
        constexpr auto fieldFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((format.optimalTilingFeatures & fieldFeatures) != fieldFeatures)
            continue;
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data());
        for (uint32_t i = 0; i < families; ++i) {
            if (!(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) || !queues[i].queueCount)
                continue;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(device, &props);
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1;
            if (score > best) {
                best = score;
                m_physical = device;
                m_family = i;
            }
            break;
        }
    }
    if (m_physical == VK_NULL_HANDLE)
        return false;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    const char *portability = "VK_KHR_portability_subset";
    uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extensionCount, nullptr) == VK_SUCCESS) {
        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &extensionCount, extensions.data()) == VK_SUCCESS
            && std::any_of(extensions.cbegin(), extensions.cend(), [&](const VkExtensionProperties &extension) {
                   return std::strcmp(extension.extensionName, portability) == 0;
               })) {
            info.enabledExtensionCount = 1;
            info.ppEnabledExtensionNames = &portability;
        }
    }
    if (vkCreateDevice(m_physical, &info, nullptr, &m_device) != VK_SUCCESS)
        return false;

#define LOAD_DEVICE(name) \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(m_device, #name)); \
    if (!name) return false;
    NEKO_GLASS_DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    vkGetDeviceQueue(m_device, m_family, 0, &m_queue);
    vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory);
    return true;
}

bool Renderer::createProgram(Program &program, const uint32_t *code, size_t size,
                             uint32_t inputs, uint32_t pushSize)
{
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t i = 0; i <= inputs; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i < inputs ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = inputs + 1;
    descriptorInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &descriptorInfo, nullptr, &program.descriptors) != VK_SUCCESS)
        return false;

    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &program.descriptors;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &program.layout) != VK_SUCCESS)
        return false;

    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = size;
    moduleInfo.pCode = code;
    if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        return false;
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = program.layout;
    const auto result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                  nullptr, &program.pipeline);
    vkDestroyShaderModule(m_device, module, nullptr);
    return result == VK_SUCCESS;
}

bool Renderer::setupCommands()
{
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.minFilter = sampler.magFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &sampler, nullptr, &m_sampler) != VK_SUCCESS)
        return false;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = m_family;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commands) != VK_SUCCESS)
        return false;
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = m_commands;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &commandInfo, &m_command) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(m_device, &fence, nullptr, &m_fence) != VK_SUCCESS)
        return false;

    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}
    };
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = uint32_t(m_sets.size());
    descriptors.poolSizeCount = uint32_t(std::size(sizes));
    descriptors.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &descriptors, nullptr, &m_pool) != VK_SUCCESS)
        return false;
    const VkDescriptorSetLayout layouts[] = {m_blur.descriptors, m_blur.descriptors,
                                              m_field.descriptors, m_material.descriptors};
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = m_pool;
    allocate.descriptorSetCount = uint32_t(std::size(layouts));
    allocate.pSetLayouts = layouts;
    return vkAllocateDescriptorSets(m_device, &allocate, m_sets.data()) == VK_SUCCESS;
}

bool Renderer::initialize()
{
    return createInstance() && selectDevice()
        && createProgram(m_blur, blurShader, sizeof(blurShader), 1, sizeof(float) * 2)
        && createProgram(m_field, fieldShader, sizeof(fieldShader), 0, sizeof(float) * 4)
        && createProgram(m_material, materialShader, sizeof(materialShader), 3, sizeof(MaterialPush))
        && setupCommands();
}

Renderer::~Renderer()
{
    if (m_device && vkDestroyDevice) {
        if (vkDeviceWaitIdle)
            vkDeviceWaitIdle(m_device);
        if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        if (m_commands) vkDestroyCommandPool(m_device, m_commands, nullptr);
        if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
        destroyProgram(m_material);
        destroyProgram(m_field);
        destroyProgram(m_blur);
        if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
        destroyImage(m_output);
        destroyImage(m_shape);
        destroyImage(m_diffuse);
        destroyImage(m_horizontal);
        destroyImage(m_input);
        destroyBuffer(m_readback);
        destroyBuffer(m_upload);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance && vkDestroyInstance)
        vkDestroyInstance(m_instance, nullptr);
}

void Renderer::destroyProgram(const Program &program)
{
    if (program.pipeline) vkDestroyPipeline(m_device, program.pipeline, nullptr);
    if (program.layout) vkDestroyPipelineLayout(m_device, program.layout, nullptr);
    if (program.descriptors) vkDestroyDescriptorSetLayout(m_device, program.descriptors, nullptr);
}

void Renderer::destroyImage(Image &image)
{
    if (image.view) vkDestroyImageView(m_device, image.view, nullptr);
    if (image.handle) vkDestroyImage(m_device, image.handle, nullptr);
    if (image.memory) vkFreeMemory(m_device, image.memory, nullptr);
    image = {};
}

void Renderer::destroyBuffer(Buffer &buffer)
{
    if (buffer.mapped) vkUnmapMemory(m_device, buffer.memory);
    if (buffer.handle) vkDestroyBuffer(m_device, buffer.handle, nullptr);
    if (buffer.memory) vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}

bool Renderer::allocateMemory(VkDeviceMemory &memory, const VkMemoryRequirements &requirements,
                              VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++i) {
        if (!(requirements.memoryTypeBits & (1u << i))
            || (m_memory.memoryTypes[i].propertyFlags & flags) != flags)
            continue;
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = i;
        return vkAllocateMemory(m_device, &info, nullptr, &memory) == VK_SUCCESS;
    }
    return false;
}

bool Renderer::ensureImage(Image &image, const QSize &size, VkImageUsageFlags usage, VkFormat format)
{
    if (image.view && image.size == size)
        return true;
    destroyImage(image);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {uint32_t(size.width()), uint32_t(size.height()), 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &info, nullptr, &image.handle) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image.handle, &requirements);
    if (!allocateMemory(image.memory, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        || vkBindImageMemory(m_device, image.handle, image.memory, 0) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    image.size = size;
    return vkCreateImageView(m_device, &view, nullptr, &image.view) == VK_SUCCESS;
}

bool Renderer::ensureBuffer(Buffer &buffer, VkDeviceSize capacity, VkBufferUsageFlags usage)
{
    if (buffer.mapped && buffer.capacity >= capacity)
        return true;
    destroyBuffer(buffer);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = capacity;
    info.usage = usage;
    if (vkCreateBuffer(m_device, &info, nullptr, &buffer.handle) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.handle, &requirements);
    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!allocateMemory(buffer.memory, requirements, flags)
        || vkBindBufferMemory(m_device, buffer.handle, buffer.memory, 0) != VK_SUCCESS)
        return false;
    buffer.capacity = capacity;
    return vkMapMemory(m_device, buffer.memory, 0, capacity, 0, &buffer.mapped) == VK_SUCCESS;
}

void Renderer::transition(Image &image, VkImageLayout layout, VkPipelineStageFlags before,
                          VkAccessFlags from, VkPipelineStageFlags after, VkAccessFlags to)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = image.layout;
    barrier.newLayout = layout;
    barrier.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : from;
    barrier.dstAccessMask = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (image.layout == VK_IMAGE_LAYOUT_UNDEFINED)
        before = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    vkCmdPipelineBarrier(m_command, before, after, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    image.layout = layout;
}

void Renderer::setDescriptors(VkDescriptorSet set, std::initializer_list<const Image *> inputs,
                              const Image &output)
{
    std::array<VkDescriptorImageInfo, 4> images{};
    std::array<VkWriteDescriptorSet, 4> writes{};
    const uint32_t inputCount = uint32_t(inputs.size());
    uint32_t i = 0;
    for (const Image *input : inputs)
        images[i++] = {m_sampler, input->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[inputCount] = {VK_NULL_HANDLE, output.view, VK_IMAGE_LAYOUT_GENERAL};
    for (i = 0; i <= inputCount; ++i) {
        auto &write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = i < inputCount ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                              : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_device, inputCount + 1, writes.data(), 0, nullptr);
}

void Renderer::dispatch(const Program &program, VkDescriptorSet set, const QSize &size,
                        const void *parameters, uint32_t parameterSize)
{
    vkCmdBindPipeline(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, program.pipeline);
    vkCmdBindDescriptorSets(m_command, VK_PIPELINE_BIND_POINT_COMPUTE, program.layout, 0,
                            1, &set, 0, nullptr);
    vkCmdPushConstants(m_command, program.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                        0, parameterSize, parameters);
    vkCmdDispatch(m_command, uint32_t(size.width() + 7) / 8, uint32_t(size.height() + 7) / 8, 1);
}

bool Renderer::submit()
{
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &m_command;
    return vkEndCommandBuffer(m_command) == VK_SUCCESS
        && vkResetFences(m_device, 1, &m_fence) == VK_SUCCESS
        && vkQueueSubmit(m_queue, 1, &info, m_fence) == VK_SUCCESS
        && vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

QImage Renderer::render(const QImage &image, const QRect &bounds, const QRectF &source,
                        const QSize &pixels, const Optics &optics)
{
    if (m_failed)
        return {};
    const auto fail = [&]() -> QImage {
        m_failed = true;
        qCWarning(glassLog) << "Liquid Glass Vulkan render failed";
        return {};
    };
    const bool refreshBackdrop = m_inputKey != image.cacheKey() || m_boundsSize != bounds.size();
    constexpr auto sampled = VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr auto storage = VK_IMAGE_USAGE_STORAGE_BIT;
    if (refreshBackdrop) {
        if (!ensureImage(m_input, image.size(), sampled | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            || !ensureImage(m_horizontal, image.size(), sampled | storage)
            || !ensureImage(m_diffuse, image.size(), sampled | storage)
            || !ensureBuffer(m_upload, image.sizeInBytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
            return fail();
        std::memcpy(m_upload.mapped, image.constBits(), size_t(image.sizeInBytes()));
        setDescriptors(m_sets[0], {&m_input}, m_horizontal);
        setDescriptors(m_sets[1], {&m_horizontal}, m_diffuse);
    }
    const VkDeviceSize bytes = VkDeviceSize(pixels.width()) * pixels.height() * 4;
    if (!ensureImage(m_shape, pixels, storage | sampled, kFieldFormat)
        || !ensureImage(m_output, pixels, storage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        || !ensureBuffer(m_readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        return fail();
    setDescriptors(m_sets[2], {}, m_shape);
    setDescriptors(m_sets[3], {&m_input, &m_diffuse, &m_shape}, m_output);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkResetCommandPool(m_device, m_commands, 0) != VK_SUCCESS
        || vkBeginCommandBuffer(m_command, &begin) != VK_SUCCESS)
        return fail();

    constexpr auto compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    constexpr auto transfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
    constexpr auto read = VK_ACCESS_SHADER_READ_BIT;
    constexpr auto write = VK_ACCESS_SHADER_WRITE_BIT;
    constexpr auto readOnly = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const float scaleX = float(image.width()) / bounds.width();
    const float scaleY = float(image.height()) / bounds.height();
    if (refreshBackdrop) {
        constexpr auto uploadLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transition(m_input, uploadLayout, compute, read, transfer, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy upload{};
        upload.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        upload.imageExtent = {uint32_t(image.width()), uint32_t(image.height()), 1};
        vkCmdCopyBufferToImage(m_command, m_upload.handle, m_input.handle, uploadLayout, 1, &upload);
        transition(m_input, readOnly, transfer, VK_ACCESS_TRANSFER_WRITE_BIT, compute, read);

        const float horizontal[] = {0.95f * scaleX, 0.0f};
        transition(m_horizontal, VK_IMAGE_LAYOUT_GENERAL, compute, read, compute, write);
        dispatch(m_blur, m_sets[0], image.size(), horizontal, sizeof(horizontal));
        transition(m_horizontal, readOnly, compute, write, compute, read);
        const float vertical[] = {0.0f, 0.95f * scaleY};
        transition(m_diffuse, VK_IMAGE_LAYOUT_GENERAL, compute, read, compute, write);
        dispatch(m_blur, m_sets[1], image.size(), vertical, sizeof(vertical));
        transition(m_diffuse, readOnly, compute, write, compute, read);
    }

    MaterialPush parameters{
        {float(source.width()), float(source.height())},
        {float(source.x() - bounds.x()) * scaleX, float(source.y() - bounds.y()) * scaleY},
        {scaleX, scaleY}, optics.radius,
        float(std::max(source.width() / pixels.width(), source.height() / pixels.height())),
        optics.bezel, optics.bend, optics.dispersion, optics.frost,
        {optics.tint[0], optics.tint[1], optics.tint[2], optics.tint[3]},
        {-0.58f, -0.81f}, optics.shadow, optics.highlight
    };
    const float shapeParameters[] = {parameters.size[0], parameters.size[1],
                                     parameters.radius, parameters.pixelWidth};
    transition(m_shape, VK_IMAGE_LAYOUT_GENERAL, compute, read, compute, write);
    dispatch(m_field, m_sets[2], pixels, shapeParameters, sizeof(shapeParameters));
    transition(m_shape, readOnly, compute, write, compute, read);
    transition(m_output, VK_IMAGE_LAYOUT_GENERAL, transfer, VK_ACCESS_TRANSFER_READ_BIT, compute, write);
    dispatch(m_material, m_sets[3], pixels, &parameters, sizeof(parameters));
    constexpr auto downloadLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    transition(m_output, downloadLayout, compute, write, transfer, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy download{};
    download.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    download.imageExtent = {uint32_t(pixels.width()), uint32_t(pixels.height()), 1};
    vkCmdCopyImageToBuffer(m_command, m_output.handle, downloadLayout, m_readback.handle, 1, &download);
    VkBufferMemoryBarrier hostRead{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostRead.srcQueueFamilyIndex = hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.buffer = m_readback.handle;
    hostRead.size = bytes;
    vkCmdPipelineBarrier(m_command, transfer, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         0, nullptr, 1, &hostRead, 0, nullptr);
    if (!submit())
        return fail();
    m_inputKey = image.cacheKey();
    m_boundsSize = bounds.size();
    // QWidget 的绘制由 Qt 的栅格 backing store 接管，效果在 GPU 上完成后将像素交回 Qt。
    const QImage result(static_cast<const uchar *>(m_readback.mapped), pixels.width(), pixels.height(),
                        pixels.width() * 4, QImage::Format_RGBA8888_Premultiplied);
    return result.copy();
}

} // namespace

QImage renderOnVulkan(const QImage &image, const QRect &bounds, const QRectF &source,
                     const QSize &pixels, const Optics &optics)
{
    // Surface 只在 QWidget 的 GUI 线程渲染，设备与暂存缓冲由所有玻璃共用。
    static Renderer renderer;
    static const bool available = renderer.initialize();
    if (!available) {
        static const auto logged = []() {
            qCInfo(glassLog) << "Vulkan loader or compute device unavailable";
            return true;
        }();
        Q_UNUSED(logged);
        return {};
    }
    return renderer.render(image, bounds, source, pixels, optics);
}

} // namespace LiquidGlass

#else

QImage LiquidGlass::renderOnVulkan(const QImage &, const QRect &, const QRectF &,
                                  const QSize &, const Optics &)
{
    return {};
}

#endif
