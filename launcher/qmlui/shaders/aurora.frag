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

// Aurora: soft curtains of light waving over a dark sky.
void main() {
    vec2 uv = vec2(qt_TexCoord0.x * aspect, 1.0 - qt_TexCoord0.y);
    float t = time * 0.05 + seed;
    vec3 col = mix(colorA.rgb * 0.6, colorA.rgb, uv.y);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float x = uv.x * (0.9 + fi * 0.35) + fi * 3.1;
        float centre = 0.62 + fi * 0.08 + 0.13 * (fbm(vec2(x * 0.9, t + fi)) - 0.5) * 2.0;
        float d = uv.y - centre;
        // sharp lower edge, long fade upward, like real curtains
        float band = d < 0.0 ? exp(-d * d * 900.0) : exp(-d * 9.0);
        float rays = 0.55 + 0.45 * noise(vec2(x * 14.0, t * 3.0 + fi));
        vec3 tint = mix(colorB.rgb, colorC.rgb, fi * 0.45 + 0.25 * sin(t + x));
        col += tint * band * rays * (0.55 - fi * 0.12);
    }
    // a few faint stars
    vec2 g = floor(uv * 160.0);
    float s = step(0.996, hash21(g + seed)) * (0.4 + 0.6 * sin(time * 0.8 + hash21(g) * 30.0));
    col += vec3(s) * 0.5 * smoothstep(0.35, 0.9, uv.y);
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
