#version 430 core

// Full-screen triangle generated from gl_VertexID. No vertex buffer or
// attributes required; the composite pass draws 3 vertices and the GPU
// rasterises a triangle that covers the entire viewport.
//
// The three vertices generate:
//   id 0 -> (0,0)
//   id 1 -> (2,0)
//   id 2 -> (0,2)
// which become clip-space positions (-1,-1), (3,-1), (-1,3).
void main() {
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}