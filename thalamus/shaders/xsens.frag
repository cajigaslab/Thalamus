//#version 460 core
//#extension GL_ARB_uniform_buffer_object : enable
#version 450 core

precision mediump float;
precision mediump sampler3D;

in vec3 tex_coord;
in vec3 frag_normal;

out vec4 frag_color;

uniform usampler2DArray source_image;
uniform bool is_thresholded;
uniform int window;
uniform float alpha;
uniform ivec2 filled_source_size;
uniform ivec2 source_size;
uniform bool draw_mask;

void main() {
  float brightness = abs(dot(frag_normal, vec3(0, 0, 1)));
  frag_color = vec4(brightness, brightness, brightness, 1);
};
