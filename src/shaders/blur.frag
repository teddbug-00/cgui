#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_tex;

layout(push_constant) uniform Push {
    vec2 direction;   /* (1/w, 0) or (0, 1/h) */
    float sigma;      /* Gaussian sigma in pixels */
    float _pad;
} pc;

void main()
{
    float sigma = max(pc.sigma, 0.5);
    float inv_two_sigma2 = 1.0 / (2.0 * sigma * sigma);

    /* Center sample (weight = 1.0) */
    vec4 sum = texture(u_tex, v_uv);
    float wsum = 1.0;

    /*
     * Bilinear sampling: each texture fetch samples 2 discrete Gaussian taps
     * simultaneously by placing the sample at the weighted sub-texel offset.
     * This halves the required texture fetches while matching the continuous
     * Gaussian distribution.
     */
    int max_pairs = int(ceil(sigma * 1.5));
    max_pairs = clamp(max_pairs, 1, 14);

    for (int i = 1; i <= max_pairs; ++i) {
        float a = float(2 * i - 1);
        float b = float(2 * i);

        float wa = exp(-a * a * inv_two_sigma2);
        float wb = exp(-b * b * inv_two_sigma2);
        float w = wa + wb;

        /* Hardware bilinear interpolation coordinate */
        float offset = (a * wa + b * wb) / max(w, 1e-6);

        vec2 offset_dir = pc.direction * offset;
        sum += (texture(u_tex, v_uv + offset_dir) +
                texture(u_tex, v_uv - offset_dir)) * w;
        wsum += 2.0 * w;
    }

    out_color = sum / wsum;
}
