//#version 460 core
//#extension GL_ARB_uniform_buffer_object : enable
#version 450 core

in vec3 vertex;
in vec3 normal;

out vec3 frag_normal;
uniform mat4 mv_matrix;
uniform mat4 proj_matrix;
uniform mat3 normal_matrix;

void main() {
  gl_Position = proj_matrix * mv_matrix * vec4(vertex, 1);
  frag_normal = normal_matrix * normal;
};
