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

// Falling blocks: columns of square blocks drifting down at different speeds.
void main() {
    float cols = 22.0;
    vec2 p = vec2(qt_TexCoord0.x * cols, qt_TexCoord0.y * cols / aspect);
    float col = floor(p.x);
    float speed = 0.25 + 0.75 * hash21(vec2(col, seed));
    float y = p.y - time * speed * 0.35;
    float row = floor(y);
    float h = hash21(vec2(col * 1.7, row + seed * 3.0));
    vec2 f = vec2(fract(p.x), fract(y));
    float inBlock = step(0.86, h) * step(0.08, f.x) * step(f.x, 0.92) * step(0.08, f.y) * step(f.y, 0.92);
    // simple bevel: lighter top-left, darker bottom-right
    float bevel = (f.x < 0.2 || f.y < 0.2) ? 1.25 : (f.x > 0.8 || f.y > 0.8) ? 0.75 : 1.0;
    vec3 tone = mix(colorB.rgb, colorC.rgb, hash21(vec2(row, col))) * bevel;
    float depth = 0.35 + 0.65 * speed;   // slow columns look further away
    vec3 bg = mix(colorA.rgb, colorA.rgb * 0.7, qt_TexCoord0.y);
    vec3 c = mix(bg, mix(bg, tone, depth), inBlock * 0.8);
    fragColor = vec4(c, 1.0) * qt_Opacity;
}
