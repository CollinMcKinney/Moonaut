#version 330 core
in vec4 vColor;
in vec2 vCorner;
out vec4 FragColor;

void main() {
    float d = length(vCorner);
    if (d > 1.0) discard;
    float alpha = 1.0 - smoothstep(0.0, 1.0, d);
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}