#version 450

/* text.frag – samples R8 glyph atlas and multiplies by tint color.
 *
 * The atlas is VK_FORMAT_R8_UNORM; the sampler delivers the coverage
 * in the red channel.  We output premultiplied alpha so the standard
 * VK_BLEND_FACTOR_ONE / VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA equation
 * composites text correctly over any background color.
 */
layout(binding = 0) uniform sampler2D atlas;

layout(location = 0) in  vec2 frag_uv;
layout(location = 1) in  vec4 frag_color;
layout(location = 0) out vec4 out_color;

void main()
{
    float coverage = texture(atlas, frag_uv).r;
    /* Premultiply: output = tint.rgb * coverage * tint.a, alpha = coverage * tint.a */
    float alpha = coverage * frag_color.a;
    out_color = vec4(frag_color.rgb * alpha, alpha);
}
