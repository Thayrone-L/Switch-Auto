#version 460

layout(binding = 0) uniform sampler2D lumaPlane;
layout(binding = 1) uniform sampler2D chromaPlane;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main() {
    float y = texture(lumaPlane, uv).r;
    vec2 chroma = texture(chromaPlane, uv).rg - vec2(0.5);
    y = max(0.0, (y - 16.0 / 255.0) * (255.0 / 219.0));
    vec3 rgb = vec3(
        y + 1.5748 * chroma.y,
        y - 0.1873 * chroma.x - 0.4681 * chroma.y,
        y + 1.8556 * chroma.x);
    color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
