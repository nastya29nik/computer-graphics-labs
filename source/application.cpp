#include "application.hpp"

#include <imgui.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstring>

namespace application {

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;
};

struct GlobalUniforms {
    glm::mat4 mvp;
    glm::vec4 base_color;
};

std::vector<Vertex> torusVertices;
std::vector<uint32_t> torusIndices;

VkBuffer vk_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation vk_vertex_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_index_buffer = VK_NULL_HANDLE;
VmaAllocation vk_index_buffer_allocation = VK_NULL_HANDLE;

VkBuffer vk_uniform_buffer1 = VK_NULL_HANDLE;
VmaAllocation vk_uniform_buffer_allocation1 = VK_NULL_HANDLE;
void* mapped_uniform_data1 = nullptr;
VkDescriptorSet descriptor_set1;

VkBuffer vk_uniform_buffer2 = VK_NULL_HANDLE;
VmaAllocation vk_uniform_buffer_allocation2 = VK_NULL_HANDLE;
void* mapped_uniform_data2 = nullptr;
VkDescriptorSet descriptor_set2;

VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;
VkPipelineLayout pipeline_layout;
VkPipeline graphics_pipeline;

bool is_perspective = true;
float torus_position[3] = { 0.0f, 0.0f, -3.0f };
float torus_rotation[3] = { 0.0f, 0.0f, 0.0f };
float torus_scale[3] = { 1.0f, 1.0f, 1.0f };
float torus_color[3] = { 1.0f, 1.0f, 1.0f };

bool animate = false;
float anim_speed = 1.0f;
float anim_radius = 1.5f;
float anim_time = 0.0f;

void GenerateTorus(float mainRadius, float tubeRadius, int mainSegments, int tubeSegments,
    std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    for (int i = 0; i <= mainSegments; ++i) {
        float theta = i * 2.0f * glm::pi<float>() / mainSegments;
        float cosTheta = cos(theta);
        float sinTheta = sin(theta);

        for (int j = 0; j <= tubeSegments; ++j) {
            float phi = j * 2.0f * glm::pi<float>() / tubeSegments;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);

            float x = (mainRadius + tubeRadius * cosPhi) * cosTheta;
            float y = (mainRadius + tubeRadius * cosPhi) * sinTheta;
            float z = tubeRadius * sinPhi;

            float r = (cosTheta + 1.0f) * 0.5f;
            float g = (sinTheta + 1.0f) * 0.5f;
            float b = (sinPhi + 1.0f) * 0.5f;

            outVertices.push_back({ glm::vec3(x, y, z), glm::vec3(r, g, b) });
        }
    }

    for (int i = 0; i < mainSegments; ++i) {
        for (int j = 0; j < tubeSegments; ++j) {
            uint32_t first = (i * (tubeSegments + 1)) + j;
            uint32_t second = first + tubeSegments + 1;

            outIndices.push_back(first);
            outIndices.push_back(second);
            outIndices.push_back(first + 1);

            outIndices.push_back(second);
            outIndices.push_back(second + 1);
            outIndices.push_back(first + 1);
        }
    }
}

std::vector<uint32_t> readShader(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filename << "\n";
        return {};
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size() * sizeof(uint32_t), .pCode = code.data() };
    VkShaderModule shaderModule;
    vkCreateShaderModule(graphics::internal::context.device, &createInfo, nullptr, &shaderModule);
    return shaderModule;
}

bool initialize() {
    auto& context = graphics::internal::context;
    GenerateTorus(1.0f, 0.3f, 32, 16, torusVertices, torusIndices);

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBufferCreateInfo vb_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(Vertex) * torusVertices.size(), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    vmaCreateBuffer(context.allocator, &vb_info, &alloc_info, &vk_vertex_buffer, &vk_vertex_buffer_allocation, nullptr);
    void* data; vmaMapMemory(context.allocator, vk_vertex_buffer_allocation, &data);
    std::memcpy(data, torusVertices.data(), vb_info.size);
    vmaUnmapMemory(context.allocator, vk_vertex_buffer_allocation);
    VkBufferCreateInfo ib_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(uint32_t) * torusIndices.size(), .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT };
    vmaCreateBuffer(context.allocator, &ib_info, &alloc_info, &vk_index_buffer, &vk_index_buffer_allocation, nullptr);
    vmaMapMemory(context.allocator, vk_index_buffer_allocation, &data);
    std::memcpy(data, torusIndices.data(), ib_info.size);
    vmaUnmapMemory(context.allocator, vk_index_buffer_allocation);

    VkBufferCreateInfo ub_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(GlobalUniforms), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT };
    vmaCreateBuffer(context.allocator, &ub_info, &alloc_info, &vk_uniform_buffer1, &vk_uniform_buffer_allocation1, nullptr);
    vmaMapMemory(context.allocator, vk_uniform_buffer_allocation1, &mapped_uniform_data1);

    vmaCreateBuffer(context.allocator, &ub_info, &alloc_info, &vk_uniform_buffer2, &vk_uniform_buffer_allocation2, nullptr);
    vmaMapMemory(context.allocator, vk_uniform_buffer_allocation2, &mapped_uniform_data2);
    
    VkDescriptorSetLayoutBinding uboLayoutBinding = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT };
    VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &uboLayoutBinding };
    vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &descriptor_set_layout);

    VkDescriptorPoolSize poolSize = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2 };
    VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize };
    vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptor_pool);

    VkDescriptorSetAllocateInfo allocInfo1 = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor_set_layout };
    vkAllocateDescriptorSets(context.device, &allocInfo1, &descriptor_set1);
    VkDescriptorBufferInfo bufferInfo1 = { .buffer = vk_uniform_buffer1, .offset = 0, .range = sizeof(GlobalUniforms) };
    VkWriteDescriptorSet write1 = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor_set1, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bufferInfo1 };
    vkUpdateDescriptorSets(context.device, 1, &write1, 0, nullptr);

    VkDescriptorSetAllocateInfo allocInfo2 = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor_set_layout };
    vkAllocateDescriptorSets(context.device, &allocInfo2, &descriptor_set2);
    VkDescriptorBufferInfo bufferInfo2 = { .buffer = vk_uniform_buffer2, .offset = 0, .range = sizeof(GlobalUniforms) };
    VkWriteDescriptorSet write2 = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptor_set2, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bufferInfo2 };
    vkUpdateDescriptorSets(context.device, 1, &write2, 0, nullptr);

    auto vertShaderCode = readShader("shaders/shader.vert.spv");
    if (vertShaderCode.empty()) vertShaderCode = readShader("../shaders/shader.vert.spv");
    auto fragShaderCode = readShader("shaders/shader.frag.spv");
    if (fragShaderCode.empty()) fragShaderCode = readShader("../shaders/shader.frag.spv");

    if (vertShaderCode.empty() || fragShaderCode.empty()) return false;

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertShaderModule, .pName = "main" },
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragShaderModule, .pName = "main" }
    };

    VkVertexInputBindingDescription bindingDescription = { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attributeDescriptions[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) }
    };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributeDescriptions };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE };
    VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineDepthStencilStateCreateInfo depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = { .blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo colorBlending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &descriptor_set_layout };
    vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending, .pDynamicState = &dynamicState, .layout = pipeline_layout, .renderPass = context.render_pass, .subpass = 0
    };
    vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphics_pipeline);

    vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
    vkDestroyShaderModule(context.device, vertShaderModule, nullptr);


    std::cout << "Successfully copied Torus data to GPU VRAM and created Pipeline!\n";
	return true;
}

void shutdown() {
    auto& context = graphics::internal::context;
    vkQueueWaitIdle(context.graphics_queue);

    if (graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, graphics_pipeline, nullptr);
        graphics_pipeline = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
        descriptor_pool = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
        descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (mapped_uniform_data2) {
        vmaUnmapMemory(context.allocator, vk_uniform_buffer_allocation2);
        mapped_uniform_data2 = nullptr;
    }
    if (vk_uniform_buffer2 != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vk_uniform_buffer2, vk_uniform_buffer_allocation2);
    }
    if (mapped_uniform_data1) {
        vmaUnmapMemory(context.allocator, vk_uniform_buffer_allocation1);
        mapped_uniform_data1 = nullptr;
    }
    if (vk_uniform_buffer1 != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vk_uniform_buffer1, vk_uniform_buffer_allocation1);
    }
    if (vk_index_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vk_index_buffer, vk_index_buffer_allocation);
    }
    if (vk_vertex_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context.allocator, vk_vertex_buffer, vk_vertex_buffer_allocation);
    }
}

void update([[maybe_unused]] double time) {
    ImGui::Begin("Torus Controls");
    ImGui::Checkbox("Perspective Projection", &is_perspective);
    ImGui::SliderFloat3("Position", torus_position, -5.0f, 5.0f);
    ImGui::SliderFloat3("Rotation", torus_rotation, 0.0f, glm::pi<float>() * 2.0f);
    ImGui::SliderFloat3("Scale", torus_scale, 0.1f, 3.0f);
    ImGui::ColorEdit3("Base Color", torus_color);
    ImGui::Separator();
    ImGui::Text("Animation (Trajectory)");
    ImGui::Checkbox("Play Animation", &animate);
    ImGui::SliderFloat("Speed", &anim_speed, 0.1f, 5.0f);
    ImGui::SliderFloat("Radius", &anim_radius, 0.5f, 3.0f);
    ImGui::End();

    if (animate) {
        anim_time += 0.016f * anim_speed;
    }

    auto& context = graphics::internal::context;
    float aspect = (float)context.swapchain_extent.width / (float)context.swapchain_extent.height;

    glm::mat4 proj;
    if (is_perspective) {
        proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }
    else {
        float ortho_size = 2.0f;
        proj = glm::ortho(-ortho_size * aspect, ortho_size * aspect, -ortho_size, ortho_size, -10.0f, 10.0f);
    }
    proj[1][1] *= -1;

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 model1 = glm::mat4(1.0f);

    float posX = torus_position[0] + (animate ? cos(anim_time) * anim_radius : 0.0f);
    float posY = torus_position[1] + (animate ? sin(anim_time) * anim_radius : 0.0f);
    float rotZ = torus_rotation[2] + (animate ? anim_time * 2.0f : 0.0f);

    model1 = glm::translate(model1, glm::vec3(posX, posY, torus_position[2]));
    model1 = glm::rotate(model1, torus_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model1 = glm::rotate(model1, torus_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model1 = glm::rotate(model1, rotZ, glm::vec3(0.0f, 0.0f, 1.0f));
    model1 = glm::scale(model1, glm::vec3(torus_scale[0], torus_scale[1], torus_scale[2]));
    GlobalUniforms ubo1{};
    ubo1.mvp = proj * view * model1;
    ubo1.base_color = glm::vec4(torus_color[0], torus_color[1], torus_color[2], 1.0f);
    std::memcpy(mapped_uniform_data1, &ubo1, sizeof(ubo1));

    glm::mat4 model2 = glm::mat4(1.0f);
    model2 = glm::translate(model2, glm::vec3(1.8f, 0.0f, -3.0f));
    model2 = glm::rotate(model2, (float)time * 1.5f, glm::vec3(0.0f, 1.0f, 0.5f));
    model2 = glm::scale(model2, glm::vec3(0.4f, 0.4f, 0.4f));

    GlobalUniforms ubo2{};
    ubo2.mvp = proj * view * model2;
    ubo2.base_color = glm::vec4(0.3f, 1.0f, 0.5f, 1.0f);
    std::memcpy(mapped_uniform_data2, &ubo2, sizeof(ubo2));
}

void render(const graphics::internal::FrameData& fd) {
    auto& context = graphics::internal::context;

    if (context.swapchain_extent.width == 0 || context.swapchain_extent.height == 0) {
        return;
    }

    vkResetCommandBuffer(fd.command_buffer, 0);

    const VkCommandBufferBeginInfo command_buffer_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

    const VkClearValue clear_values[] = {
        {.color = {.float32 = { 0.15f, 0.15f, 0.18f, 1.0f } } },
        {.depthStencil = { 1.0f, 0 } },
    };

    const VkRenderPassBeginInfo render_pass_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = {.extent = context.swapchain_extent },
        .clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
        .pClearValues = clear_values,
    };

    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)context.swapchain_extent.width,
        .height = (float)context.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = context.swapchain_extent
    };
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = { vk_vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(fd.command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set1, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(torusIndices.size()), 1, 0, 0, 0);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set2, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(torusIndices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application