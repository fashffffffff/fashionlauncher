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

// Starfield: three layers of slowly drifting, twinkling stars over a faint nebula.
float stars(vec2 uv, float scale, float t) {
    vec2 p = uv * scale;
    vec2 cell = floor(p), f = fract(p) - 0.5;
    float h = hash21(cell + seed);
    if (h < 0.92) return 0.0;
    vec2 offs = vec2(hash21(cell + 3.1), hash21(cell + 7.7)) - 0.5;
    float d = length(f - offs * 0.6);
    float tw = 0.6 + 0.4 * sin(t * (1.0 + h * 3.0) + h * 40.0);
    return smoothstep(0.09, 0.0, d) * tw * (h - 0.92) * 12.5;
}
void main() {
    vec2 uv = vec2(qt_TexCoord0.x * aspect, qt_TexCoord0.y);
    float t = time;
    float n = fbm(uv * 1.6 + vec2(t * 0.004, seed));
    vec3 col = colorA.rgb * 0.7 + mix(colorB.rgb, colorC.rgb, n) * pow(n, 2.5) * 0.45;
    col += vec3(stars(uv + vec2(t * 0.002, 0.0), 22.0, t)) * 0.55;
    col += vec3(stars(uv + vec2(t * 0.005, 0.0), 12.0, t)) * 0.8;
    col += mix(vec3(1.0), colorB.rgb, 0.35) * stars(uv + vec2(t * 0.010, 0.0), 6.0, t);
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
