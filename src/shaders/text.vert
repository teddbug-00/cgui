#version 450

layout(location = 0) in vec2 vertex_position;
layout(location = 1) in vec2 vertex_uv;

layout(push_constant) uniform Push {
    vec4 color;
    vec2 screen;
} push;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

void main()
{
    vec2 ndc = vec2(
        (vertex_position.x / push.screen.x) * 2.0 - 1.0,
        (vertex_position.y / push.screen.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_uv = vertex_uv;
    frag_color = push.color;
}
