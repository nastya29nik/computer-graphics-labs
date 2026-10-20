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
#include <string>

namespace application {

struct Vertex {
	glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
};

struct PointLightData {
    glm::vec4 position;
    glm::vec4 color;
};

struct SpotLightData {
    glm::vec4 position;
    glm::vec4 direction;
    glm::vec4 color;
    glm::vec4 angles;
};

struct SceneUniforms {
    glm::mat4 view_projection;
    alignas(16) glm::vec3 camera_position;
    alignas(16) glm::vec3 directional_light_direction;
    alignas(16) glm::vec3 directional_light_color;
    alignas(16) glm::vec3 ambient_light;
    uint32_t point_light_count;
    uint32_t spot_light_count;
};

struct ModelUniforms {
    glm::mat4 model;
    alignas(16) glm::vec3 albedo;
    alignas(16) glm::vec3 specular;
    float alpha;
    uint32_t is_unlit;
    uint32_t use_vertex_color;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped_data = nullptr;

    bool create(VkDeviceSize size, VkBufferUsageFlags usage, bool map_memory = false) {
        auto& context = graphics::internal::context;

        VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        const VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     (map_memory ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0u),
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo result_info{};
        if (vmaCreateBuffer(context.allocator, &buffer_info, &alloc_info,
            &buffer, &allocation, &result_info) != VK_SUCCESS) {
            std::cerr << "Failed to allocate Vulkan buffer\n";
            return false;
        }

        if (map_memory) {
            mapped_data = result_info.pMappedData;
        }
        return true;
    }

    void upload(const void* data, size_t size) {
        auto& context = graphics::internal::context;
        if (mapped_data) {
            std::memcpy(mapped_data, data, size);
        }
        else {
            void* staging = nullptr;
            vmaMapMemory(context.allocator, allocation, &staging);
            std::memcpy(staging, data, size);
            vmaUnmapMemory(context.allocator, allocation);
        }
    }

    void destroy() {
        auto& context = graphics::internal::context;
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context.allocator, buffer, allocation);
            buffer = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
            mapped_data = nullptr;
        }
    }
};

constexpr size_t MAX_LIGHTS = 64;
constexpr size_t MAX_MARKERS = 64;

std::vector<Vertex> torusVertices;
std::vector<uint32_t> torusIndices;
std::vector<Vertex> planeVertices;
std::vector<uint32_t> planeIndices;
std::vector<Vertex> cubeVertices;
std::vector<uint32_t> cubeIndices;

Buffer vertex_buffer;
Buffer index_buffer;
Buffer plane_vertex_buffer;
Buffer plane_index_buffer;
Buffer cube_vertex_buffer;
Buffer cube_index_buffer;
Buffer cone_lines_vertex_buffer;
Buffer scene_uniform_buffer;
Buffer point_lights_ssbo;
Buffer spot_lights_ssbo;
Buffer model1_uniform_buffer;
Buffer model2_uniform_buffer;
Buffer floor_uniform_buffer;
Buffer light_marker_uniform_buffer;

Buffer marker_uniform_buffers[MAX_MARKERS];
VkDescriptorSet marker_descriptor_sets[MAX_MARKERS];

VkDescriptorSetLayout scene_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout model_descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet scene_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet model1_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet model2_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet floor_descriptor_set = VK_NULL_HANDLE;
VkDescriptorSet light_marker_descriptor_set = VK_NULL_HANDLE;

VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline graphics_pipeline = VK_NULL_HANDLE;
VkPipeline lines_pipeline = VK_NULL_HANDLE;

uint32_t cone_vertex_count = 0;

float camera_position[3] = { 0.0f, 2.0f, 5.0f };
float camera_target[3] = { 0.0f, 0.0f, 0.0f };
float camera_fov = 45.0f;
bool is_perspective = true;

float ambient_color[3] = { 0.1f, 0.1f, 0.12f };
float dir_light_direction[3] = { -0.5f, -1.0f, -0.5f };
float dir_light_color[3] = { 0.8f, 0.8f, 0.8f };
float dir_light_intensity = 0.5f;

struct PointLightUI {
    glm::vec3 position{ 1.8f, 1.0f, 1.0f };
    glm::vec3 color{ 1.0f, 0.6f, 0.2f };
    float intensity = 5.0f;
};

struct SpotLightUI {
    glm::vec3 position{ 0.0f, 3.5f, 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 0.2f, 0.7f, 1.0f };
    float intensity = 12.0f;
    float inner_angle = 15.0f;
    float outer_angle = 30.0f;
};

std::vector<PointLightUI> point_lights;
std::vector<SpotLightUI> spot_lights;


float torus_position[3] = { -0.8f, 0.0f, 0.0f };
float torus_rotation[3] = { 0.0f, 0.0f, 0.0f };
float torus_scale[3] = { 1.0f, 1.0f, 1.0f };
float torus_albedo[3] = { 0.85f, 0.15f, 0.15f };
float torus_specular[3] = { 1.0f, 1.0f, 1.0f };
float torus_shininess = 32.0f;
bool torus_use_vertex_gradient = false;
float torus2_position[3] = { 1.4f, 0.0f, 0.0f };
float torus2_scale[3] = { 0.5f, 0.5f, 0.5f };
float torus2_albedo[3] = { 0.2f, 0.8f, 0.3f };
float torus2_specular[3] = { 0.8f, 0.8f, 0.8f };
float torus2_shininess = 16.0f;
bool torus2_use_vertex_gradient = false;

float floor_albedo[3] = { 0.6f, 0.6f, 0.65f };
float floor_specular[3] = { 0.3f, 0.3f, 0.3f };
float floor_shininess = 8.0f;

bool animate = false;
float anim_speed = 1.0f;
float anim_radius = 1.5f;
float anim_time = 0.0f;

bool show_light_visualizers = true;

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

            float nx = cosPhi * cosTheta;
            float ny = cosPhi * sinTheta;
            float nz = sinPhi;

            outVertices.push_back({ glm::vec3(x, y, z), glm::vec3(r, g, b), glm::vec3(nx, ny, nz) });
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

void GeneratePlane(float size, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    outVertices = {
        { glm::vec3(-size, -1.2f, -size), glm::vec3(0.7f), glm::vec3(0.0f, 1.0f, 0.0f) },
        { glm::vec3(size, -1.2f, -size), glm::vec3(0.7f), glm::vec3(0.0f, 1.0f, 0.0f) },
        { glm::vec3(size, -1.2f,  size), glm::vec3(0.7f), glm::vec3(0.0f, 1.0f, 0.0f) },
        { glm::vec3(-size, -1.2f,  size), glm::vec3(0.7f), glm::vec3(0.0f, 1.0f, 0.0f) }
    };
    outIndices = { 0, 1, 2, 2, 3, 0 };
}

void GenerateCube(float size, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    float s = size * 0.5f;
    outVertices = {
        { {-s, -s,  s}, {1,1,1}, {0, 0, 1} }, { { s, -s,  s}, {1,1,1}, {0, 0, 1} },
        { { s,  s,  s}, {1,1,1}, {0, 0, 1} }, { {-s,  s,  s}, {1,1,1}, {0, 0, 1} },
        
        { {-s, -s, -s}, {1,1,1}, {0, 0, -1} }, { {-s,  s, -s}, {1,1,1}, {0, 0, -1} },
        { { s,  s, -s}, {1,1,1}, {0, 0, -1} }, { { s, -s, -s}, {1,1,1}, {0, 0, -1} },

        { {-s,  s, -s}, {1,1,1}, {0, 1, 0} }, { {-s,  s,  s}, {1,1,1}, {0, 1, 0} },
        { { s,  s,  s}, {1,1,1}, {0, 1, 0} }, { { s,  s, -s}, {1,1,1}, {0, 1, 0} },

        { {-s, -s, -s}, {1,1,1}, {0, -1, 0} }, { { s, -s, -s}, {1,1,1}, {0, -1, 0} },
        { { s, -s,  s}, {1,1,1}, {0, -1, 0} }, { {-s, -s,  s}, {1,1,1}, {0, -1, 0} },

        { { s, -s, -s}, {1,1,1}, {1, 0, 0} }, { { s,  s, -s}, {1,1,1}, {1, 0, 0} },
        { { s,  s,  s}, {1,1,1}, {1, 0, 0} }, { { s, -s,  s}, {1,1,1}, {1, 0, 0} },
 
        { {-s, -s, -s}, {1,1,1}, {-1, 0, 0} }, { {-s, -s,  s}, {1,1,1}, {-1, 0, 0} },
        { {-s,  s,  s}, {1,1,1}, {-1, 0, 0} }, { {-s,  s, -s}, {1,1,1}, {-1, 0, 0} }
    };
    outIndices = {
         0,  1,  2,  2,  3,  0,
         4,  5,  6,  6,  7,  4,
         8,  9, 10, 10, 11,  8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20
    };
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
    GeneratePlane(6.0f, planeVertices, planeIndices);
    GenerateCube(0.2f, cubeVertices, cubeIndices);

    size_t vb_size = sizeof(Vertex) * torusVertices.size();
    if (!vertex_buffer.create(vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
    vertex_buffer.upload(torusVertices.data(), vb_size);

    size_t ib_size = sizeof(uint32_t) * torusIndices.size();
    if (!index_buffer.create(ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
    index_buffer.upload(torusIndices.data(), ib_size);

    size_t pvb_size = sizeof(Vertex) * planeVertices.size();
    if (!plane_vertex_buffer.create(pvb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
    plane_vertex_buffer.upload(planeVertices.data(), pvb_size);

    size_t pib_size = sizeof(uint32_t) * planeIndices.size();
    if (!plane_index_buffer.create(pib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
    plane_index_buffer.upload(planeIndices.data(), pib_size);

    size_t cb_size = sizeof(Vertex) * cubeVertices.size();
    if (!cube_vertex_buffer.create(cb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
    cube_vertex_buffer.upload(cubeVertices.data(), cb_size);

    size_t cib_size = sizeof(uint32_t) * cubeIndices.size();
    if (!cube_index_buffer.create(cib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
    cube_index_buffer.upload(cubeIndices.data(), cib_size);
    
    point_lights.push_back({ glm::vec3(1.5f, 1.0f, 1.0f), glm::vec3(1.0f, 0.8f, 0.4f), 5.0f });
    spot_lights.push_back({ glm::vec3(0.0f, 2.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.3f, 0.7f, 1.0f), 8.0f, 15.0f, 25.0f });

    if (!scene_uniform_buffer.create(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    if (!point_lights_ssbo.create(sizeof(PointLightData) * MAX_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return false;
    if (!spot_lights_ssbo.create(sizeof(SpotLightData) * MAX_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return false;

    if (!model1_uniform_buffer.create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    if (!model2_uniform_buffer.create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    if (!floor_uniform_buffer.create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    if (!light_marker_uniform_buffer.create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    if (!cone_lines_vertex_buffer.create(sizeof(Vertex) * 4096, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true)) return false;

    for (size_t i = 0; i < MAX_MARKERS; ++i) {
        if (!marker_uniform_buffers[i].create(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
    }

    VkDescriptorSetLayoutBinding scene_bindings[] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    VkDescriptorSetLayoutCreateInfo scene_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = scene_bindings
    };
    vkCreateDescriptorSetLayout(context.device, &scene_layout_info, nullptr, &scene_descriptor_set_layout);

    VkDescriptorSetLayoutBinding model_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo model_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &model_binding
    };
    vkCreateDescriptorSetLayout(context.device, &model_layout_info, nullptr, &model_descriptor_set_layout);

    VkDescriptorPoolSize pool_sizes[] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 100 },
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 10 }
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 100,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes
    };
    vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool);

    VkDescriptorSetAllocateInfo scene_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &scene_descriptor_set_layout
    };
    vkAllocateDescriptorSets(context.device, &scene_alloc_info, &scene_descriptor_set);
    VkDescriptorBufferInfo scene_ubo_info = { .buffer = scene_uniform_buffer.buffer, .offset = 0, .range = sizeof(SceneUniforms) };
    VkDescriptorBufferInfo point_ssbo_info = { .buffer = point_lights_ssbo.buffer, .offset = 0, .range = sizeof(PointLightData) * MAX_LIGHTS };
    VkDescriptorBufferInfo spot_ssbo_info = { .buffer = spot_lights_ssbo.buffer, .offset = 0, .range = sizeof(SpotLightData) * MAX_LIGHTS };
    VkWriteDescriptorSet scene_writes[] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_descriptor_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &scene_ubo_info },
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_descriptor_set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &point_ssbo_info },
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_descriptor_set, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &spot_ssbo_info }
    };
    vkUpdateDescriptorSets(context.device, 3, scene_writes, 0, nullptr);

    VkDescriptorSetAllocateInfo model1_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &model_descriptor_set_layout
    };
    vkAllocateDescriptorSets(context.device, &model1_alloc_info, &model1_descriptor_set);
    VkDescriptorBufferInfo model1_ubo_info = { .buffer = model1_uniform_buffer.buffer, .offset = 0, .range = sizeof(ModelUniforms) };
    VkWriteDescriptorSet model1_write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = model1_descriptor_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &model1_ubo_info };
    vkUpdateDescriptorSets(context.device, 1, &model1_write, 0, nullptr);
    VkDescriptorSetAllocateInfo model2_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &model_descriptor_set_layout
    };
    vkAllocateDescriptorSets(context.device, &model2_alloc_info, &model2_descriptor_set);
    VkDescriptorBufferInfo model2_ubo_info = { .buffer = model2_uniform_buffer.buffer, .offset = 0, .range = sizeof(ModelUniforms) };
    VkWriteDescriptorSet model2_write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = model2_descriptor_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &model2_ubo_info };
    vkUpdateDescriptorSets(context.device, 1, &model2_write, 0, nullptr);
    
    VkDescriptorSetAllocateInfo floor_alloc_info = { 
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &model_descriptor_set_layout
    };
    vkAllocateDescriptorSets(context.device, &floor_alloc_info, &floor_descriptor_set);
    VkDescriptorBufferInfo floor_ubo_info = { .buffer = floor_uniform_buffer.buffer, .offset = 0, .range = sizeof(ModelUniforms) };
    VkWriteDescriptorSet floor_write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = floor_descriptor_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &floor_ubo_info };
    vkUpdateDescriptorSets(context.device, 1, &floor_write, 0, nullptr);
    
    VkDescriptorSetAllocateInfo marker_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &model_descriptor_set_layout
    };
    vkAllocateDescriptorSets(context.device, &marker_alloc_info, &light_marker_descriptor_set);
    VkDescriptorBufferInfo marker_ubo_info = { .buffer = light_marker_uniform_buffer.buffer, .offset = 0, .range = sizeof(ModelUniforms) };
    VkWriteDescriptorSet marker_write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = light_marker_descriptor_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &marker_ubo_info };
    vkUpdateDescriptorSets(context.device, 1, &marker_write, 0, nullptr);
    
    for (size_t i = 0; i < MAX_MARKERS; ++i) {
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &model_descriptor_set_layout
        };
        vkAllocateDescriptorSets(context.device, &alloc_info, &marker_descriptor_sets[i]);
        VkDescriptorBufferInfo b_info = { .buffer = marker_uniform_buffers[i].buffer, .offset = 0, .range = sizeof(ModelUniforms) };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = marker_descriptor_sets[i], .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &b_info };
        vkUpdateDescriptorSets(context.device, 1, &w, 0, nullptr);
    }

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
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) }
    };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attributeDescriptions };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE };
    VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineDepthStencilStateCreateInfo depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = { .blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo colorBlending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates };

    VkDescriptorSetLayout setLayouts[] = { scene_descriptor_set_layout, model_descriptor_set_layout };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = setLayouts
    };
    vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending, .pDynamicState = &dynamicState, .layout = pipeline_layout, .renderPass = context.render_pass, .subpass = 0
    };
    vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphics_pipeline);
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &lines_pipeline);

    vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
    vkDestroyShaderModule(context.device, vertShaderModule, nullptr);

    std::cout << "Successfully created Lighting Pipeline with Set 0 and Set 1!\n";
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
    if (model_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, model_descriptor_set_layout, nullptr);
        model_descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (scene_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, scene_descriptor_set_layout, nullptr);
        scene_descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (lines_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, lines_pipeline, nullptr);
        lines_pipeline = VK_NULL_HANDLE;
    }
    for (size_t i = 0; i < MAX_MARKERS; ++i) {
        marker_uniform_buffers[i].destroy();
    }

    cone_lines_vertex_buffer.destroy();
    cube_index_buffer.destroy();
    cube_vertex_buffer.destroy();
    light_marker_uniform_buffer.destroy();
    floor_uniform_buffer.destroy();
    model2_uniform_buffer.destroy();
    model1_uniform_buffer.destroy();
    spot_lights_ssbo.destroy();
    point_lights_ssbo.destroy();
    scene_uniform_buffer.destroy();
    plane_index_buffer.destroy();
    plane_vertex_buffer.destroy();
    index_buffer.destroy();
    vertex_buffer.destroy();
}

void update([[maybe_unused]] double time) {
    auto& context = graphics::internal::context;
    if (context.swapchain_extent.width == 0 || context.swapchain_extent.height == 0) {
        return;
    }

    ImGui::Begin("Lighting & Camera Controls");

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Perspective", &is_perspective);
        ImGui::SliderFloat3("Camera Position", camera_position, -10.0f, 10.0f);
        ImGui::SliderFloat3("Camera Target", camera_target, -5.0f, 5.0f);
        if (is_perspective) {
            ImGui::SliderFloat("FOV", &camera_fov, 20.0f, 90.0f);
        }
    }

    if (ImGui::CollapsingHeader("Directional & Ambient Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient Light", ambient_color);
        ImGui::SliderFloat3("Dir Light Direction", dir_light_direction, -1.0f, 1.0f);
        ImGui::ColorEdit3("Dir Light Color", dir_light_color);
        ImGui::SliderFloat("Dir Light Intensity", &dir_light_intensity, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Point Lights")) {
        if (ImGui::Button("Add Point Light") && point_lights.size() < MAX_LIGHTS) {
            point_lights.push_back({ glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 3.0f });
        }
        for (size_t i = 0; i < point_lights.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::string label = "Point Light #" + std::to_string(i + 1);
            if (ImGui::TreeNode(label.c_str())) {
                ImGui::SliderFloat3("Position", &point_lights[i].position.x, -5.0f, 5.0f);
                ImGui::ColorEdit3("Color", &point_lights[i].color.x);
                ImGui::SliderFloat("Intensity", &point_lights[i].intensity, 0.0f, 20.0f);
                if (ImGui::Button("Remove")) {
                    point_lights.erase(point_lights.begin() + i);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Spot Lights")) {
        if (ImGui::Button("Add Spot Light") && spot_lights.size() < MAX_LIGHTS) {
            spot_lights.push_back({ glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 5.0f, 15.0f, 25.0f });
        }
        for (size_t i = 0; i < spot_lights.size(); ++i) {
            ImGui::PushID(static_cast<int>(100 + i));
            std::string label = "Spot Light #" + std::to_string(i + 1);
            if (ImGui::TreeNode(label.c_str())) {
                ImGui::SliderFloat3("Position", &spot_lights[i].position.x, -5.0f, 5.0f);
                ImGui::SliderFloat3("Direction", &spot_lights[i].direction.x, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color", &spot_lights[i].color.x);
                ImGui::SliderFloat("Intensity", &spot_lights[i].intensity, 0.0f, 30.0f);
                ImGui::SliderFloat("Inner Angle", &spot_lights[i].inner_angle, 1.0f, 60.0f);
                ImGui::SliderFloat("Outer Angle", &spot_lights[i].outer_angle, spot_lights[i].inner_angle, 80.0f);
                if (spot_lights[i].inner_angle > spot_lights[i].outer_angle) {
                    spot_lights[i].outer_angle = spot_lights[i].inner_angle;
                }
                if (ImGui::Button("Remove")) {
                    spot_lights.erase(spot_lights.begin() + i);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Materials & Models", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Light Markers", &show_light_visualizers);

        ImGui::SeparatorText("Torus 1");
        ImGui::Checkbox("Use Vertex Gradient 1", &torus_use_vertex_gradient);
        ImGui::SliderFloat3("Position 1", torus_position, -5.0f, 5.0f);
        ImGui::SliderFloat3("Rotation 1", torus_rotation, 0.0f, glm::pi<float>() * 2.0f);
        ImGui::SliderFloat3("Scale 1", torus_scale, 0.1f, 3.0f);
        ImGui::ColorEdit3("Albedo 1", torus_albedo);
        ImGui::ColorEdit3("Specular 1", torus_specular);
        ImGui::SliderFloat("Shininess 1", &torus_shininess, 1.0f, 256.0f);

        ImGui::Text("Animation");
        ImGui::Checkbox("Play Animation", &animate);
        ImGui::SliderFloat("Speed", &anim_speed, 0.1f, 5.0f);
        ImGui::SliderFloat("Radius", &anim_radius, 0.5f, 3.0f);

        ImGui::SeparatorText("Torus 2");
        ImGui::Checkbox("Use Vertex Gradient 2", &torus2_use_vertex_gradient);
        ImGui::SliderFloat3("Position 2", torus2_position, -5.0f, 5.0f);
        ImGui::SliderFloat3("Scale 2", torus2_scale, 0.1f, 3.0f);
        ImGui::ColorEdit3("Albedo 2", torus2_albedo);
        ImGui::ColorEdit3("Specular 2", torus2_specular);
        ImGui::SliderFloat("Shininess 2", &torus2_shininess, 1.0f, 256.0f);

        ImGui::SeparatorText("Floor");
        ImGui::ColorEdit3("Floor Albedo", floor_albedo);
        ImGui::ColorEdit3("Floor Specular", floor_specular);
        ImGui::SliderFloat("Floor Shininess", &floor_shininess, 1.0f, 128.0f);
    }
    ImGui::End();

    if (animate) {
        anim_time += 0.016f * anim_speed;
    }

    float aspect = (float)context.swapchain_extent.width / (float)context.swapchain_extent.height;

    glm::mat4 proj;
    if (is_perspective) {
        proj = glm::perspective(glm::radians(camera_fov), aspect, 0.1f, 100.0f);
    }
    else {
        float ortho_size = 3.0f;
        proj = glm::ortho(-ortho_size * aspect, ortho_size * aspect, -ortho_size, ortho_size, -10.0f, 100.0f);
    }
    proj[1][1] *= -1;

    glm::vec3 eye = glm::vec3(camera_position[0], camera_position[1], camera_position[2]);
    glm::vec3 center = glm::vec3(camera_target[0], camera_target[1], camera_target[2]);
    glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

    SceneUniforms scene_ubo{};
    scene_ubo.view_projection = proj * view;
    scene_ubo.camera_position = eye;
    scene_ubo.directional_light_direction = glm::normalize(glm::vec3(dir_light_direction[0], dir_light_direction[1], dir_light_direction[2]));
    scene_ubo.directional_light_color = glm::vec3(dir_light_color[0], dir_light_color[1], dir_light_color[2]) * dir_light_intensity;
    scene_ubo.ambient_light = glm::vec3(ambient_color[0], ambient_color[1], ambient_color[2]);
    scene_ubo.point_light_count = static_cast<uint32_t>(point_lights.size());
    scene_ubo.spot_light_count = static_cast<uint32_t>(spot_lights.size());
    scene_uniform_buffer.upload(&scene_ubo, sizeof(scene_ubo));

    std::vector<PointLightData> point_data(point_lights.size());
    for (size_t i = 0; i < point_lights.size(); ++i) {
        point_data[i].position = glm::vec4(point_lights[i].position, 1.0f);
        point_data[i].color = glm::vec4(point_lights[i].color * point_lights[i].intensity, 1.0f);
    }
    if (!point_data.empty()) {
        point_lights_ssbo.upload(point_data.data(), sizeof(PointLightData) * point_data.size());
    }

    std::vector<SpotLightData> spot_data(spot_lights.size());
    for (size_t i = 0; i < spot_lights.size(); ++i) {
        spot_data[i].position = glm::vec4(spot_lights[i].position, 1.0f);
        spot_data[i].direction = glm::vec4(glm::normalize(spot_lights[i].direction), 0.0f);
        spot_data[i].color = glm::vec4(spot_lights[i].color * spot_lights[i].intensity, 1.0f);
        spot_data[i].angles = glm::vec4(
            cos(glm::radians(spot_lights[i].inner_angle)),
            cos(glm::radians(spot_lights[i].outer_angle)),
            0.0f, 0.0f
        );
    }
    if (!spot_data.empty()) {
        spot_lights_ssbo.upload(spot_data.data(), sizeof(SpotLightData) * spot_data.size());
    }
    
    glm::mat4 model1 = glm::mat4(1.0f);

    float posX = torus_position[0] + (animate ? cos(anim_time) * anim_radius : 0.0f);
    float posY = torus_position[1] + (animate ? sin(anim_time) * anim_radius : 0.0f);
    float rotZ = torus_rotation[2] + (animate ? anim_time * 2.0f : 0.0f);

    model1 = glm::translate(model1, glm::vec3(posX, posY, torus_position[2]));
    model1 = glm::rotate(model1, torus_rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
    model1 = glm::rotate(model1, torus_rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model1 = glm::rotate(model1, rotZ, glm::vec3(0.0f, 0.0f, 1.0f));
    model1 = glm::scale(model1, glm::vec3(torus_scale[0], torus_scale[1], torus_scale[2]));
    
    ModelUniforms model1_ubo{};
    model1_ubo.model = model1;
    model1_ubo.albedo = glm::vec3(torus_albedo[0], torus_albedo[1], torus_albedo[2]);
    model1_ubo.specular = glm::vec3(torus_specular[0], torus_specular[1], torus_specular[2]);
    model1_ubo.alpha = torus_shininess;
    model1_ubo.is_unlit = 0;
    model1_ubo.use_vertex_color = torus_use_vertex_gradient ? 1 : 0;
    model1_uniform_buffer.upload(&model1_ubo, sizeof(model1_ubo));

    glm::mat4 model2 = glm::mat4(1.0f);
    model2 = glm::translate(model2, glm::vec3(torus2_position[0], torus2_position[1], torus2_position[2]));
    model2 = glm::rotate(model2, (float)time * 1.5f, glm::vec3(0.0f, 1.0f, 0.5f));
    model2 = glm::scale(model2, glm::vec3(torus2_scale[0], torus2_scale[1], torus2_scale[2]));

    ModelUniforms model2_ubo{};
    model2_ubo.model = model2;
    model2_ubo.albedo = glm::vec3(torus2_albedo[0], torus2_albedo[1], torus2_albedo[2]);
    model2_ubo.specular = glm::vec3(torus2_specular[0], torus2_specular[1], torus2_specular[2]);
    model2_ubo.alpha = torus2_shininess;
    model2_ubo.is_unlit = 0;
    model2_ubo.use_vertex_color = torus2_use_vertex_gradient ? 1 : 0;
    model2_uniform_buffer.upload(&model2_ubo, sizeof(model2_ubo));

    ModelUniforms floor_ubo{};
    floor_ubo.model = glm::mat4(1.0f);
    floor_ubo.albedo = glm::vec3(floor_albedo[0], floor_albedo[1], floor_albedo[2]);
    floor_ubo.specular = glm::vec3(floor_specular[0], floor_specular[1], floor_specular[2]);
    floor_ubo.alpha = floor_shininess;
    floor_ubo.is_unlit = 0;
    floor_ubo.use_vertex_color = 0;
    floor_uniform_buffer.upload(&floor_ubo, sizeof(floor_ubo));

    std::vector<Vertex> cone_lines;
    for (const auto& spot : spot_lights) {
        glm::vec3 pos = spot.position;
        glm::vec3 dir = glm::normalize(spot.direction);
        float dist = 2.0f;
        float radius = dist * tan(glm::radians(spot.outer_angle));
        glm::vec3 base_center = pos + dir * dist;
        glm::vec3 up = (std::abs(dir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        up = glm::cross(right, dir);
        cone_lines.push_back({ pos, {1.0f, 1.0f, 0.0f}, {0,1,0} });
        cone_lines.push_back({ base_center, {1.0f, 1.0f, 0.0f}, {0,1,0} });

        constexpr int segments = 24;
        std::vector<glm::vec3> circle_pts(segments);
        for (int i = 0; i < segments; ++i) {
            float angle = i * 2.0f * glm::pi<float>() / segments;
            circle_pts[i] = base_center + (right * cos(angle) + up * sin(angle)) * radius;
        }

        for (int i = 0; i < segments; ++i) {
            cone_lines.push_back({ circle_pts[i], {1.0f, 1.0f, 1.0f}, {0,1,0} });
            cone_lines.push_back({ circle_pts[(i + 1) % segments], {1.0f, 1.0f, 1.0f}, {0,1,0} });

            if (i % (segments / 4) == 0) {
                cone_lines.push_back({ pos, {1.0f, 1.0f, 1.0f}, {0,1,0} });
                cone_lines.push_back({ circle_pts[i], {1.0f, 1.0f, 1.0f}, {0,1,0} });
            }
        }
    }
    cone_vertex_count = static_cast<uint32_t>(cone_lines.size());
    if (cone_vertex_count > 0) {
        cone_lines_vertex_buffer.upload(cone_lines.data(), sizeof(Vertex) * cone_vertex_count);
    }
}

void render(const graphics::internal::FrameData& fd) {
    if (fd.command_buffer == VK_NULL_HANDLE || fd.framebuffer == VK_NULL_HANDLE) {
        return;
    }

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

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &scene_descriptor_set, 0, nullptr);
    
    VkBuffer planeBuffers[] = { plane_vertex_buffer.buffer };
    VkDeviceSize planeOffsets[] = { 0 };
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, planeBuffers, planeOffsets);
    vkCmdBindIndexBuffer(fd.command_buffer, plane_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &floor_descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(planeIndices.size()), 1, 0, 0, 0);
    
    VkBuffer vertexBuffers[] = { vertex_buffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &model1_descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(torusIndices.size()), 1, 0, 0, 0);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &model2_descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(torusIndices.size()), 1, 0, 0, 0);

    if (show_light_visualizers) {
        uint32_t marker_idx = 0;

        VkBuffer cubeBufs[] = { cube_vertex_buffer.buffer };
        VkDeviceSize cubeOffsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, cubeBufs, cubeOffsets);
        vkCmdBindIndexBuffer(fd.command_buffer, cube_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& light : point_lights) {
            if (marker_idx >= MAX_MARKERS) break;

            glm::mat4 light_model = glm::translate(glm::mat4(1.0f), light.position);
            light_model = glm::scale(light_model, glm::vec3(0.12f));

            ModelUniforms unlit_data{};
            unlit_data.model = light_model;
            unlit_data.albedo = light.color;
            unlit_data.is_unlit = 1;
            unlit_data.use_vertex_color = 0;

            marker_uniform_buffers[marker_idx].upload(&unlit_data, sizeof(unlit_data));
            vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &marker_descriptor_sets[marker_idx], 0, nullptr);
            vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);
            marker_idx++;
        }

        for (const auto& spot : spot_lights) {
            if (marker_idx >= MAX_MARKERS) break;

            glm::mat4 spot_model = glm::translate(glm::mat4(1.0f), spot.position);
            spot_model = glm::scale(spot_model, glm::vec3(0.15f));

            ModelUniforms unlit_data{};
            unlit_data.model = spot_model;
            unlit_data.albedo = spot.color;
            unlit_data.is_unlit = 1;
            unlit_data.use_vertex_color = 0;

            marker_uniform_buffers[marker_idx].upload(&unlit_data, sizeof(unlit_data));
            vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &marker_descriptor_sets[marker_idx], 0, nullptr);
            vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);
            marker_idx++;
        }

        if (cone_vertex_count > 0 && marker_idx < MAX_MARKERS) {
            vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline);

            VkBuffer lineBufs[] = { cone_lines_vertex_buffer.buffer };
            VkDeviceSize lineOffsets[] = { 0 };
            vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, lineBufs, lineOffsets);

            ModelUniforms line_model_data{};
            line_model_data.model = glm::mat4(1.0f);
            line_model_data.is_unlit = 1;
            line_model_data.use_vertex_color = 1;

            marker_uniform_buffers[marker_idx].upload(&line_model_data, sizeof(line_model_data));
            vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &marker_descriptor_sets[marker_idx], 0, nullptr);
            vkCmdDraw(fd.command_buffer, cone_vertex_count, 1, 0, 0);
        }
    }
    
    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application