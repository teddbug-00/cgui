#version 450

layout(push_constant) uniform Push {
    vec4 rect;     /* x, y, w, h in pixels */
    vec4 color;    /* tint color */
    vec4 uv_rect;  /* uv_x, uv_y, uv_w, uv_h in [0, 1] */
    vec2 screen;   /* screen_w, screen_h */
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = corners[gl_VertexIndex];
    v_uv = pc.uv_rect.xy + uv * pc.uv_rect.zw;
    v_color = pc.color;

    vec2 pos = pc.rect.xy + uv * pc.rect.zw;
    vec2 ndc = vec2(
        (pos.x / pc.screen.x) * 2.0 - 1.0,
        (pos.y / pc.screen.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
