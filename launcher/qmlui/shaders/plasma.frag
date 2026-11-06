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

// Soft gradient mesh: four colored glows slowly wandering around.
void main() {
    vec2 uv = vec2(qt_TexCoord0.x * aspect, qt_TexCoord0.y);
    float t = time * 0.07 + seed;
    vec3 col = colorA.rgb;
    vec3 tones[4];
    tones[0] = colorB.rgb;
    tones[1] = colorC.rgb;
    tones[2] = mix(colorB.rgb, colorC.rgb, 0.5);
    tones[3] = mix(colorA.rgb, colorB.rgb, 0.6);
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 c = vec2(aspect * (0.5 + 0.42 * sin(t * (0.7 + fi * 0.21) + fi * 1.9)),
                      0.5 + 0.42 * cos(t * (0.55 + fi * 0.17) + fi * 2.7));
        float d = length(uv - c);
        col = mix(col, tones[i], 0.6 * exp(-d * d * 3.2));
    }
    // a hint of grain against banding
    col += (hash21(qt_TexCoord0 * 913.0 + fract(time)) - 0.5) / 128.0;
    fragColor = vec4(col, 1.0) * qt_Opacity;
}
