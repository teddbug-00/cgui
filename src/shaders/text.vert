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
    /* Unit quad: vertices 0-5 form two triangles */
    float lx[6] = float[](0.0, 1.0, 1.0,  0.0, 0.0, 1.0);
    float ly[6] = float[](0.0, 0.0, 1.0,  0.0, 1.0, 1.0);

    float fx = push.rect.x + lx[gl_VertexIndex] * push.rect.z;
    float fy = push.rect.y + ly[gl_VertexIndex] * push.rect.w;

    /* NDC: map [0,screen] → [-1,+1] */
    gl_Position = vec4(
        fx / push.screen.x * 2.0 - 1.0,
        fy / push.screen.y * 2.0 - 1.0,
        0.0, 1.0
    );

    frag_uv = vec2(
        push.uv_rect.x + lx[gl_VertexIndex] * push.uv_rect.z,
        push.uv_rect.y + ly[gl_VertexIndex] * push.uv_rect.w
    );
    frag_color = push.color;
}
