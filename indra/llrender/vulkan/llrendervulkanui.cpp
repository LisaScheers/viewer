/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the license only.
 * $/LicenseInfo$
 */

#include "llrendervulkanui.h"
#include "llrendervulkanuishaders.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace LLRenderVulkan
{
namespace
{
void checked(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
struct PushConstants { float width, height; std::int32_t alphaMask; };
static_assert(sizeof(LLUIRender::Vertex) == 20);
}

struct UIRenderer::Impl
{
#define UI_COMMANDS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
    X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) \
    X(BindImageMemory) X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdCopyBufferToImage) \
    X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) X(QueueSubmit) X(DeviceWaitIdle) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdBindVertexBuffers) X(CmdSetViewport) X(CmdSetScissor) \
    X(CmdPushConstants) X(CmdDraw)
#define DECLARE(name) PFN_vk##name name = nullptr;
    UI_COMMANDS(DECLARE)
#undef DECLARE
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer uploadCommands = VK_NULL_HANDLE;
    VkFence uploadFence = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    struct Buffer { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize size = 0; void* mapped = nullptr; };
    struct Texture { VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; };
    Buffer vertices, staging;
    std::vector<Texture> textures;
    LLUIRender::Frame frame;
    std::string failure;

    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags)
    {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("UI allocation has no compatible memory type");
    }
    void release(Buffer& buffer)
    {
        if (buffer.mapped) UnmapMemory(device, buffer.memory);
        if (buffer.buffer) DestroyBuffer(device, buffer.buffer, nullptr);
        if (buffer.memory) FreeMemory(device, buffer.memory, nullptr);
        buffer = {};
    }
    void allocate(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        if (buffer.size >= size) return;
        release(buffer);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checked(CreateBuffer(device, &info, nullptr, &buffer.buffer), "create UI buffer");
        VkMemoryRequirements requirements{};
        GetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        checked(AllocateMemory(device, &allocation, nullptr, &buffer.memory), "allocate UI buffer");
        checked(BindBufferMemory(device, buffer.buffer, buffer.memory, 0), "bind UI buffer");
        checked(MapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map UI buffer");
        buffer.size = size;
    }
    void releaseImages()
    {
        if (descriptorPool) DestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        for (auto& texture : textures)
        {
            if (texture.view) DestroyImageView(device, texture.view, nullptr);
            if (texture.image) DestroyImage(device, texture.image, nullptr);
            if (texture.memory) FreeMemory(device, texture.memory, nullptr);
        }
        textures.clear();
    }
    void uploadImages(const std::vector<LLUIRender::Image>& images)
    {
        releaseImages();
        VkDeviceSize total = 0;
        for (const auto& image : images) total += image.rgba.size();
        allocate(staging, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        checked(ResetCommandPool(device, commandPool, 0), "reset UI upload commands");
        checked(ResetFences(device, 1, &uploadFence), "reset UI upload fence");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(BeginCommandBuffer(uploadCommands, &begin), "begin UI upload");
        VkDeviceSize offset = 0;
        textures.resize(images.size());
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            const auto& image = images[i];
            auto& texture = textures[i];
            std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + offset, image.rgba.data(), image.rgba.size());
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = {image.width, image.height, 1};
            info.mipLevels = 1;
            info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            checked(CreateImage(device, &info, nullptr, &texture.image), "create UI image");
            VkMemoryRequirements requirements{};
            GetImageMemoryRequirements(device, texture.image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            checked(AllocateMemory(device, &allocation, nullptr, &texture.memory), "allocate UI image");
            checked(BindImageMemory(device, texture.image, texture.memory, 0), "bind UI image");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = texture.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = info.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            checked(CreateImageView(device, &view, nullptr, &texture.view), "create UI image view");
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture.image;
            barrier.subresourceRange = view.subresourceRange;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            CmdPipelineBarrier(uploadCommands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                               0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = info.extent;
            CmdCopyBufferToImage(uploadCommands, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            CmdPipelineBarrier(uploadCommands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                               0, nullptr, 0, nullptr, 1, &barrier);
            offset += image.rgba.size();
        }
        checked(EndCommandBuffer(uploadCommands), "end UI upload");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &uploadCommands;
        checked(QueueSubmit(queue, 1, &submit, uploadFence), "submit UI upload");
        checked(WaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX), "complete UI upload");
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(images.size())};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = poolSize.descriptorCount;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        checked(CreateDescriptorPool(device, &pool, nullptr, &descriptorPool), "create UI descriptors");
        for (auto& texture : textures)
        {
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = descriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &descriptorLayout;
            checked(AllocateDescriptorSets(device, &allocation, &texture.set), "allocate UI descriptor");
            VkDescriptorImageInfo image{sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = texture.set;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image;
            UpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
    void createPipeline(VkRenderPass renderPass)
    {
        VkShaderModule modules[2]{};
        try
        {
            VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader.codeSize = sizeof(UI_VERTEX_SHADER);
            shader.pCode = UI_VERTEX_SHADER;
            checked(CreateShaderModule(device, &shader, nullptr, &modules[0]), "create UI vertex shader");
            shader.codeSize = sizeof(UI_FRAGMENT_SHADER);
            shader.pCode = UI_FRAGMENT_SHADER;
            checked(CreateShaderModule(device, &shader, nullptr, &modules[1]), "create UI fragment shader");
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (int i = 0; i < 2; ++i)
            {
                stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[i].module = modules[i];
                stages[i].pName = "main";
            }
            VkVertexInputBindingDescription binding{0, sizeof(LLUIRender::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            VkVertexInputAttributeDescription attributes[] = {
                {0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}, {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16}};
            VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            vertex.vertexBindingDescriptionCount = 1;
            vertex.pVertexBindingDescriptions = &binding;
            vertex.vertexAttributeDescriptionCount = 3;
            vertex.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.lineWidth = 1.f;
            VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamics;
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2;
            info.pStages = stages;
            info.pVertexInputState = &vertex;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = pipelineLayout;
            info.renderPass = renderPass;
            checked(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "create UI pipeline");
        }
        catch (...)
        {
            for (auto module : modules) if (module) DestroyShaderModule(device, module, nullptr);
            throw;
        }
        for (auto module : modules) DestroyShaderModule(device, module, nullptr);
    }
    ~Impl()
    {
        if (!device) return;
        if (DeviceWaitIdle) (void)DeviceWaitIdle(device);
        if (pipeline) DestroyPipeline(device, pipeline, nullptr);
        releaseImages();
        release(vertices);
        release(staging);
        if (pipelineLayout) DestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorLayout) DestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        if (sampler) DestroySampler(device, sampler, nullptr);
        if (uploadFence) DestroyFence(device, uploadFence, nullptr);
        if (commandPool) DestroyCommandPool(device, commandPool, nullptr);
    }
};

UIRenderer::UIRenderer() : mImpl(std::make_unique<Impl>()) {}
UIRenderer::~UIRenderer() = default;
const std::string& UIRenderer::error() const { return mImpl->failure; }

bool UIRenderer::initialize(PFN_vkGetInstanceProcAddr resolver, VkInstance instance, VkPhysicalDevice physical,
                            VkDevice device, VkQueue queue, std::uint32_t queueFamily)
{
    auto& self = *mImpl;
    try
    {
        if (!resolver || !instance || !physical || !device || !queue) throw std::runtime_error("missing UI device");
        const auto getDevice = reinterpret_cast<PFN_vkGetDeviceProcAddr>(resolver(instance, "vkGetDeviceProcAddr"));
        const auto getMemory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(resolver(instance, "vkGetPhysicalDeviceMemoryProperties"));
        if (!getDevice || !getMemory) throw std::runtime_error("missing UI instance dispatch");
#define RESOLVE(name) self.name = reinterpret_cast<PFN_vk##name>(getDevice(device, "vk" #name)); if (!self.name) throw std::runtime_error("missing vk" #name);
        UI_COMMANDS(RESOLVE)
#undef RESOLVE
#undef UI_COMMANDS
        self.device = device;
        self.queue = queue;
        getMemory(physical, &self.memory);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = queueFamily;
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        checked(self.CreateCommandPool(device, &pool, nullptr, &self.commandPool), "create UI upload pool");
        VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commands.commandPool = self.commandPool;
        commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commands.commandBufferCount = 1;
        checked(self.AllocateCommandBuffers(device, &commands, &self.uploadCommands), "allocate UI upload commands");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        checked(self.CreateFence(device, &fence, nullptr, &self.uploadFence), "create UI upload fence");
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        checked(self.CreateSampler(device, &sampler, nullptr, &self.sampler), "create UI sampler");
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        checked(self.CreateDescriptorSetLayout(device, &layout, nullptr, &self.descriptorLayout), "create UI descriptor layout");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &self.descriptorLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &push;
        checked(self.CreatePipelineLayout(device, &pipelineLayout, nullptr, &self.pipelineLayout), "create UI pipeline layout");
        return true;
    }
    catch (const std::exception& error) { self.failure = error.what(); return false; }
}

bool UIRenderer::prepare(LLUIRender::Frame frame, VkRenderPass renderPass)
{
    auto& self = *mImpl;
    try
    {
        if (!self.device || !renderPass || !frame.valid() || frame.images.size() > 128)
            throw std::runtime_error("invalid UI frame or render target");
        bool imagesChanged = frame.images.size() != self.frame.images.size();
        for (std::size_t i = 0; !imagesChanged && i < frame.images.size(); ++i)
            imagesChanged = frame.images[i].width != self.frame.images[i].width || frame.images[i].height != self.frame.images[i].height || frame.images[i].rgba != self.frame.images[i].rgba;
        if (imagesChanged) self.uploadImages(frame.images);
        self.allocate(self.vertices, frame.vertices.size() * sizeof(LLUIRender::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::memcpy(self.vertices.mapped, frame.vertices.data(), frame.vertices.size() * sizeof(LLUIRender::Vertex));
        if (!self.pipeline) self.createPipeline(renderPass);
        self.frame = std::move(frame);
        return true;
    }
    catch (const std::exception& error) { self.failure = error.what(); return false; }
}

void UIRenderer::record(VkCommandBuffer commands, VkExtent2D extent) noexcept
{
    auto& self = *mImpl;
    if (extent.width != self.frame.width || extent.height != self.frame.height) std::terminate();
    self.CmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, self.pipeline);
    VkDeviceSize offset = 0;
    self.CmdBindVertexBuffers(commands, 0, 1, &self.vertices.buffer, &offset);
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    self.CmdSetViewport(commands, 0, 1, &viewport);
    for (const auto& draw : self.frame.draws)
    {
        if (!draw.width || !draw.height) continue;
        VkRect2D scissor{{static_cast<std::int32_t>(draw.x), static_cast<std::int32_t>(draw.y)}, {draw.width, draw.height}};
        self.CmdSetScissor(commands, 0, 1, &scissor);
        self.CmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, self.pipelineLayout, 0, 1,
                                   &self.textures[draw.image].set, 0, nullptr);
        PushConstants push{float(extent.width), float(extent.height), draw.alphaMask ? 1 : 0};
        self.CmdPushConstants(commands, self.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        self.CmdDraw(commands, draw.count, 1, draw.first, 0);
    }
}

void UIRenderer::retireTarget() noexcept
{
    if (mImpl->pipeline) mImpl->DestroyPipeline(mImpl->device, mImpl->pipeline, nullptr);
    mImpl->pipeline = VK_NULL_HANDLE;
}
}
