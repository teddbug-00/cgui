#version 450

/* text.vert – screen-space quad for a single glyph from the atlas.
 *
 * Push constants layout (must match TextPush in vk_draw.c):
 *   vec4 rect    – (x, y, w, h) destination rect in screen pixels
 *   vec4 color   – RGBA tint [0,1]
 *   vec4 uv_rect – (u0, v0, u_w, v_h) atlas UV rect
 *   vec2 screen  – (width, height) in pixels
 */
layout(push_constant) uniform Push {
    vec4 rect;
    vec4 color;
    vec4 uv_rect;
    vec2 screen;
} push;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

void main()
{
    vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = corners[gl_VertexIndex];

    vec2 pos = push.rect.xy + uv * push.rect.zw;
    vec2 ndc = vec2(
        (pos.x / push.screen.x) * 2.0 - 1.0,
        (pos.y / push.screen.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);

    frag_uv = push.uv_rect.xy + uv * push.uv_rect.zw;
    frag_color = push.color;
}
