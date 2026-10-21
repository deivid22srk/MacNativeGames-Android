#version 450
// M1 probe — small centered triangle with per-vertex colors.
// Zero vertex buffers: data comes from gl_VertexIndex so the pipeline keeps
// an empty vertex input state (matches VulkanRenderer::create_pipeline()).
vec2 positions[3] = vec2[](
    vec2( 0.0, -0.6),   // top
    vec2(-0.55, 0.5),   // bottom-left
    vec2( 0.55, 0.5)    // bottom-right
);
vec3 colors[3] = vec3[](
    vec3(1.00, 0.35, 0.30),  // warm red
    vec3(0.30, 1.00, 0.45),  // green
    vec3(0.35, 0.55, 1.00)   // blue
);
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vColor = colors[gl_VertexIndex];
}
