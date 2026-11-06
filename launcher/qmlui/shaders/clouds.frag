#version 440
// Live background. Colors come from the theme: colorA = deep background, colorB = accent, colorC = second tone.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float aspect;
    float seed;
    vec4 colorA;
    vec4 colorB;
    vec4 colorC;
};

float hash21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), u.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return v;
}

// Drifting clouds: soft layered clouds sliding across a tinted sky.
void main() {
    vec2 uv = vec2(qt_TexCoord0.x * aspect, 1.0 - qt_TexCoord0.y);
    float t = time * 0.012;
    vec3 sky = mix(colorA.rgb, mix(colorA.rgb, colorC.rgb, 0.7), uv.y);
    vec3 col = sky;
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        vec2 p = uv * vec2(1.4, 3.0) * (1.0 + fi * 0.6) + vec2(t * (1.0 + fi), seed + fi * 5.0);
        float c = smoothstep(0.45, 0.85, fbm(p));
        vec3 tone = mix(colorC.rgb, colorB.rgb, 0.25 + 0.2 * fi) * (1.0 + 0.3 * (uv.y - 0.5));
        col = mix(col, mix(tone, vec3(1.0), 0.12), c * (0.55 - fi * 0.15));
    }
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
