#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
    mat4 view_projection;
    vec3 camera_position;
    float padding0;
    vec3 directional_light_direction;
    float padding1;
    vec3 directional_light_color;
    float padding2;
    vec3 ambient_light;
    uint point_light_count;
    uint spot_light_count;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec3 albedo;
    float padding0;
    vec3 specular;
    float alpha;
    uint is_unlit;
    uint use_vertex_color;
} model_data;

layout(location = 0) out vec3 out_frag_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_vertex_color;

void main() {
    vec4 world_pos = model_data.model * vec4(in_position, 1.0);
    out_frag_pos = world_pos.xyz;
    out_normal = normalize(mat3(transpose(inverse(model_data.model))) * in_normal);
    out_vertex_color = in_color;
    gl_Position = scene.view_projection * world_pos;
}