// vertex_array.cpp
//

#include "vertex_array.h"

extern "C" {
#include "cstructs/cstructs.h"
#include "file.h"
#include "glhelp.h"
#include "lua/lauxlib.h"
}

#include "glm/glm.hpp"
#define GLM_FORCE_RADIANS
#include "glm/gtc/matrix_transform.hpp"
using namespace glm;

#include <string.h>

#define for_i_3 for(int i = 0; i < 3; ++i)

#define vertex_array_metatable "Trees.VertexArray"


// Internal types and globals.

// State shared across all VertexArray instances.
static GLuint            program;
static GLint             mvp_loc;
static GLint    normal_xform_loc;
static GLint           color_loc;
static vertex_array__TransformCallback          mvp_callback = NULL;
static vertex_array__TransformCallback normal_xform_callback = NULL;

typedef enum {
  mode_triangle_strip,
  mode_triangles,
  mode_points,
  mode_lines
} Mode;

// State owned by any single VertexArray instance.
typedef struct {
  GLuint vao;
  GLuint vertices_vbo;
  GLuint normals_vbo;
  int    num_pts;
  Mode   draw_mode;
  vec3   color;
} VertexArray;

// Names for vertex attribute indexes in our vertex shader.
enum {
  v_position,
  color,
  normal
};


// Internal: OpenGL utility code.

// Initialize data that's constant across all instances.
// This function is expected to be called only once.
static void gl_init() {
  program = glhelp__load_program("bark.vert.glsl",
                                 "bark.frag.glsl");

  mvp_loc            = glGetUniformLocation(program, "mvp");
  normal_xform_loc   = glGetUniformLocation(program, "normal_xform");
  color_loc          = glGetUniformLocation(program, "color");
}

static void set_array_as_buffer_data(Array array) {
  glBufferData(GL_ARRAY_BUFFER,                  // buffer use target
               array->count * array->item_size,  // size
               array->items,                     // data
               GL_STATIC_DRAW);                  // usage hint
}

static void gl_setup_new_vertex_array(VertexArray *v_array,
                                      Array v_pts) {

  // Compute the normals of each triangle.
  Array n_vecs = array__new(v_pts->count, sizeof(GLfloat));
  float sign = 1;
  array__for(GLfloat *, pt, v_pts, j) {
    if (j % 3) continue;  // Only use pt when it is 3-aligned.
    if (j < 6) {
      // The first two normals can be all-zero.
      for_i_3 array__new_val(n_vecs, GLfloat) = 0.0f;
      continue;
    }

    vec3 pt0 = vec3(pt[-6], pt[-5], pt[-4]);
    vec3 pt1 = vec3(pt[-3], pt[-2], pt[-1]);
    vec3 pt2 = vec3(pt[ 0], pt[ 1], pt[ 2]);

    vec3 n = sign * normalize(cross(pt1 - pt0, pt2 - pt1));
    for_i_3 array__new_val(n_vecs, GLfloat) = n[i];

    if (v_array->draw_mode == mode_triangle_strip) sign *= -1;
  }

  // Set up and bind the vao.
  glGenVertexArrays(1, &v_array->vao);
  glBindVertexArray(v_array->vao);

  // Set up the vertex position vbo.
  glGenBuffers(1, &v_array->vertices_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, v_array->vertices_vbo);
  set_array_as_buffer_data(v_pts);
  glEnableVertexAttribArray(v_position);
  glVertexAttribPointer(v_position,    // attrib index
                        3,             // num coords
                        GL_FLOAT,      // coord type
                        GL_FALSE,      // gpu should normalize
                        0,             // stride
                        (void *)(0));  // offset

  // Set up num_pts.
  v_array->num_pts = v_pts->count / 3;

  // Set up the normal vectors vbo.
  glGenBuffers(1, &v_array->normals_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, v_array->normals_vbo);
  set_array_as_buffer_data(n_vecs);
  glEnableVertexAttribArray(normal);
  glVertexAttribPointer(normal,        // attrib index
                        3,             // num coords
                        GL_FLOAT,      // coord type
                        GL_FALSE,      // gpu should normalize
                        0,             // stride
                        (void *)(0));  // offset

  glhelp__error_check;
}


// Internal: Lua C functions.

// This function expects an error string to be on top of the stack. It prints
// that message along with a stack trace starting with the first callee before
// the current C function. This function does not return.
//
// This function was originally based on code found here:
// http://stackoverflow.com/questions/12256455/
static int lua_traceback(lua_State *L) {
  int msg_index = lua_gettop(L);
  lua_getglobal(L, "debug");
  lua_getfield(L, -1, "traceback");
  lua_pushvalue(L, msg_index);
  lua_pushinteger(L, 2);
  lua_call(L, 2, 1);
  return lua_error(L);
}

// This creates an Array of GLfloats from what is expected to be a Lua array at
// the given index on L's stack. The caller is responsible for calling
// array__delete on the returned Array. L's stack is preserved.
static Array c_array_from_lua_array(lua_State *L, int index) {

  int arr_len = (int)lua_rawlen(L, index);

  Array arr = array__new(arr_len, sizeof(GLfloat));
  for (int i = 1;; lua_pop(L, 1), ++i) {
      // stack = [.. lua_arr ..]
    lua_rawgeti(L, index, i);
      // stack = [.. lua_arr .. lua_arr[i]]
    if (lua_isnil(L, -1)) break;
    if (!lua_isnumber(L, -1)) {
      lua_pushstring(L, "Expected a flat array.");
      lua_traceback(L);  // This function never returns.
    }
    array__new_val(arr, GLfloat) = lua_tonumber(L, -1);
  }
      // stack = [.. lua_arr .. nil]
  lua_pop(L, 1);
      // stack = [.. lua_arr ..]
  return arr;
}

static void luaL_checkindexable(lua_State *L, int narg) {
  if (lua_istable(L, narg)) return;  // tables are indexable.
  if (!luaL_getmetafield(L, narg, "__index")) {
    // This function will show the user narg and the Lua-visible function name.
    luaL_argerror(L, narg, "expected an indexable value such as a table");
  }
  lua_pop(L, 1);  // Pop the value of getmetable(narg).__index.
}

// Lua C function.
// Expected parameters: {points table}, draw_mode, [color]
// where draw_mode is 'triangle strip', 'triangles', 'points', or 'lines'.
// The optional color is expected to have the format {R, G, B}, where each color
// component is a number in the range [0, 1].
static int vertex_array__new(lua_State *L) {

  // Expect the 1st value to be table-like.
  luaL_checkindexable(L, 2);
      // stack = [self, v_pts, ..]

  // Collect v_pts and draw_mode.
  Array v_pts = c_array_from_lua_array(L, 2);
  const char *mode_str = luaL_checkstring(L, 3);
  Mode draw_mode;
  if (strcmp(mode_str, "triangle strip") == 0) {
    draw_mode = mode_triangle_strip;
  } else if (strcmp(mode_str, "triangles") == 0) {
    draw_mode = mode_triangles;
  } else if (strcmp(mode_str, "points") == 0) {
    draw_mode = mode_points;

    // Check for an optional point size parameter.
    // HACKY For now, if we see this, we just set the point size immediately.
    // This breaks horrifically if the user ever does anything that would make
    // another call to glPointSize with a different value.
    int isnum;
    GLfloat point_size = lua_tonumberx(L, 5, &isnum);
    if (isnum) glPointSize(point_size);

  } else if (strcmp(mode_str, "lines") == 0) {
    draw_mode = mode_lines;
  } else {
    const char *msg = "Expected mode to be 'triangle strip', 'triangles', "
                      "'points', or 'lines'.";
    return luaL_argerror(L,     // state
                         3,     // arg
                         msg);  // msg
  }

  // Check for the optional color parameter.
  vec3 color = vec3(0.494, 0.349, 0.204);
  if (lua_istable(L, 4)) {
    for (int i = 1; i <= 3; ++i) {
      lua_rawgeti(L, 4, i);  // Push color[i] on top of the stack.
        // stack = [self, v_pts, draw_mode, color, .., color[i]]
      int isnum;
      color[i - 1] = lua_tonumberx(L, -1, &isnum);
      if (!isnum) {
        return luaL_argerror(L, 4, "Expected color to contain numeric values");
      }
      lua_pop(L, 1);
        // stack = [self, v_pts, draw_mode, color, ..]
    }
  }

  lua_settop(L, 0);
      // stack = []

  // Create a VertexArray instance and set its metatable.
  VertexArray *v_array =
      (VertexArray *)lua_newuserdata(L, sizeof(VertexArray));
      // stack = [v_array]
  luaL_getmetatable(L, vertex_array_metatable);
      // stack = [v_array, mt]
  lua_setmetatable(L, 1);
      // stack = [v_array]
  
  // Set up the C data.
  v_array->draw_mode = draw_mode;
  v_array->color     = color;
  gl_setup_new_vertex_array(v_array, v_pts);

  glhelp__error_check;

  array__delete(v_pts);

  return 1;  // --> 1 Lua return value
}

// This performs common argument handling for the draw() and
// draw_without_setup() methods. This method will not return if there is an
// error.
// Expected parameters:
//   self      = a VertexArray instance.
//   [mode]    = a string with a valid mode value.
// Valid modes are 'triangle strip', 'triangles', 'points', and 'lines'.
static int get_self_and_mode(lua_State *L,
                              VertexArray **v_array,
                              GLenum *mode) {
  *v_array = (VertexArray *)luaL_checkudata(L, 1, vertex_array_metatable);

  // Set the default drawing mode.
  GLenum gl_mode[] = {GL_TRIANGLE_STRIP, GL_TRIANGLES, GL_POINTS, GL_LINES};
  *mode = gl_mode[(*v_array)->draw_mode];

  // TODO There are two places where we convert a C string into an enum value.
  //      Consider factoring out something in common.

  // Allow the user to override the initially set drawing mode -- although this
  // could potentially reverse some of the normal vectors.
  if (lua_isstring(L, 2)) {
    const char *mode_name = luaL_checkstring(L, 2);
    if (strcmp(mode_name, "triangle strip") == 0) {
      *mode = GL_TRIANGLE_STRIP;
    } else if (strcmp(mode_name, "triangles") == 0) {
      *mode = GL_TRIANGLES;
    } else if (strcmp(mode_name, "points") == 0) {
      *mode = GL_POINTS;
    } else if (strcmp(mode_name, "lines") == 0) {
      *mode = GL_LINES;
    } else {
      const char *msg = "Expected mode to be 'triangle strip', 'triangles', "
                        "'points', or 'lines'.";
      return luaL_argerror(L,     // state
                           2,     // arg
                           msg);  // msg
    }
  }

  return 0;  // This return value is only to support the return-on-error idiom.
}


// Public Lua methods.

// Lua C function.
// Expected parameters: none.
static int vertex_array__setup_drawing(lua_State *L) {
  // Prepare for OpenGL drawing.
  glUseProgram(program);
  mvp_callback(mvp_loc);
  normal_xform_callback(normal_xform_loc);

  return 0;  // --> 0 Lua return values
}


// Lua C function.
// Expected parameters:
//   self      = a VertexArray instance.
//   [mode]    = a string with a valid mode value.
static int vertex_array__draw_without_setup(lua_State *L) {

  // Parse arguments.
  VertexArray *v_array;
  GLenum mode;
  get_self_and_mode(L, &v_array, &mode);

  // Execute OpenGL drawing.
  glBindVertexArray(v_array->vao);
  glDrawArrays(mode,               // mode
               0,                  // start
               v_array->num_pts);  // count

  return 0;  // --> 0 Lua return values
}

// Lua C function.
// Expected parameters:
//   self      = a VertexArray instance.
//   [mode]    = a string with a valid mode value.
static int vertex_array__draw(lua_State *L) {

  // Parse arguments.
  VertexArray *v_array;
  GLenum mode;
  get_self_and_mode(L, &v_array, &mode);

  // Prepare for and execute OpenGL drawing.
  glUseProgram(program);
  glBindVertexArray(v_array->vao);
  mvp_callback(mvp_loc);
  normal_xform_callback(normal_xform_loc);
  glUniform3fv(color_loc,            // location
               1,                    // count
               &v_array->color[0]);  // data
  glDrawArrays(mode,               // mode
               0,                  // start
               v_array->num_pts);  // count

  return 0;  // --> 0 Lua return values
}

// TODO Add and test/debug a gc function.


// Public functions.

#define add_fn(fn, name)        \
    lua_pushcfunction(L, fn);   \
    lua_setfield(L, -2, name);

extern "C" void vertex_array__load_lib(lua_State *L) {

  // If this metatable already exists, the library is already loaded.
  if (!luaL_newmetatable(L, vertex_array_metatable)) return;

  // metatable.__index = metatable
  lua_pushvalue(L, -1);            // --> stack = [.., mt, mt]
  lua_setfield(L, -2, "__index");  // --> stack = [.., mt]

  // Add the instance methods.
  add_fn(vertex_array__draw, "draw");
  add_fn(vertex_array__draw_without_setup, "draw_without_setup");

  lua_pop(L, 1);  // --> stack = [..]

  // Add `VertexArray` as a global module table with a single `new` function.
  static const struct luaL_Reg lib[] = {
    {"new", vertex_array__new},
    {"setup_drawing", vertex_array__setup_drawing},
    {NULL, NULL}};
  luaL_newlib(L, lib);              // --> stack = [.., VertexArray]
  lua_setglobal(L, "VertexArray");  // --> stack = [..]

  gl_init();
}

void vertex_array__set_mvp_callback(vertex_array__TransformCallback cb) {
  mvp_callback = cb;
}

void vertex_array__set_normal_callback(vertex_array__TransformCallback cb) {
  normal_xform_callback = cb;
}
