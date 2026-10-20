#version 450 core

layout(location = 0) in vec3 in_frag_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_vertex_color;

layout(location = 0) out vec4 out_color;

struct PointLight {
    vec4 position;
    vec4 color;
};

struct SpotLight {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 angles;
};

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

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
    PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
    SpotLight spot_lights[];
};

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
    mat4 model;
    vec3 albedo;
    float padding0;
    vec3 specular;
    float alpha;
    uint is_unlit;
    uint use_vertex_color;
} model_data;

vec3 calcBlinnPhong(vec3 light_dir, vec3 light_color, vec3 normal, vec3 view_dir, vec3 current_albedo) {
    float diff = max(dot(normal, light_dir), 0.0);
    vec3 diffuse = diff * current_albedo * light_color;
    vec3 halfway_dir = normalize(light_dir + view_dir);
    float spec = pow(max(dot(normal, halfway_dir), 0.0), model_data.alpha);
    vec3 specular = spec * model_data.specular * light_color;
    return diffuse + specular;
}

void main() {
    if (model_data.is_unlit == 1u) {
        vec3 col = (model_data.use_vertex_color == 1u) ? in_vertex_color : model_data.albedo;
        out_color = vec4(col, 1.0);
        return;
    }

    vec3 base_albedo = (model_data.use_vertex_color == 1u) ? in_vertex_color : model_data.albedo;
    vec3 normal = normalize(in_normal);
    vec3 view_dir = normalize(scene.camera_position - in_frag_pos);
    vec3 result = scene.ambient_light * base_albedo;

    vec3 dir_light_dir = normalize(-scene.directional_light_direction);
    result += calcBlinnPhong(dir_light_dir, scene.directional_light_color, normal, view_dir, base_albedo);

    for (uint i = 0u; i < scene.point_light_count; ++i) {
        vec3 light_vec = point_lights[i].position.xyz - in_frag_pos;
        float dist = length(light_vec);
        vec3 light_dir = normalize(light_vec);
        float attenuation = 1.0 / max(dist * dist, 0.0001);
        vec3 light_color = point_lights[i].color.rgb * attenuation;
        result += calcBlinnPhong(light_dir, light_color, normal, view_dir, base_albedo);
    }

    for (uint i = 0u; i < scene.spot_light_count; ++i) {
        vec3 light_vec = spot_lights[i].position.xyz - in_frag_pos;
        float dist = length(light_vec);
        vec3 light_dir = normalize(light_vec);
        float attenuation = 1.0 / max(dist * dist, 0.0001);
        float theta = dot(light_dir, normalize(-spot_lights[i].direction.xyz));
        float cos_inner = spot_lights[i].angles.x;
        float cos_outer = spot_lights[i].angles.y;
        float spot_factor = clamp((theta - cos_outer) / max(cos_inner - cos_outer, 0.0001), 0.0, 1.0);
        vec3 light_color = spot_lights[i].color.rgb * attenuation * spot_factor;
        result += calcBlinnPhong(light_dir, light_color, normal, view_dir, base_albedo);
    }

    out_color = vec4(result, 1.0);
}