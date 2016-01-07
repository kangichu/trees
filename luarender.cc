//
//  luarender.cc
//  Trees
//

#include "luarender.h"

// C-only includes.
extern "C" {

#include "clua.h"
#include "file.h"
#include "lines.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
  
}

// C++ friendly includes.
#include "config.h"

#include "glm.hpp"
#define GLM_FORCE_RADIANS
#include "matrix_transform.hpp"
using namespace glm;


// Define YES/NO so this file can work with Objective-C style macro values.
#define YES 1
#define NO  0


// Internal globals.

static lua_State *L = NULL;

static float aspect_ratio;
static float angle = 0.0f;

static mat4 mvp;


// Internal functions.

static void transform_callback(GLint transform_loc) {
  glUniformMatrix4fv(transform_loc,  // uniform location
                     1,              // count
                     GL_FALSE,       // don't use transpose
                     &mvp[0][0]);    // src matrix
}

#define set_lua_global_num(name)   \
    lua_pushnumber(L, name);       \
    lua_setglobal(L, #name);

#define set_lua_global_bool(name)   \
    lua_pushboolean(L, name);       \
    lua_setglobal(L, #name);

static void set_lua_config_constants() {
  set_lua_global_num(max_tree_height);
  set_lua_global_num(branch_size_factor);
  set_lua_global_bool(is_tree_2d);
}


// Public functions.

extern "C" void luarender__init() {
  L = clua__new_state();
  
  // Load the standard library.
  luaL_openlibs(L);

  // Set shared constants from the conifg.h file.
  set_lua_config_constants();
  
  // Load the render modules.
  char *filepath = file__get_path("render.lua");
    // stack = []
  luaL_dofile(L, filepath);
    // stack = [render or error_msg]
  if (lua_type(L, 1) == LUA_TSTRING) {
    const char *error_msg = lua_tostring(L, 1);
    // stack = [error_msg]
    printf("%s\n", error_msg);
    exit(1);
  }
    // stack = [render]
  lua_setglobal(L, "render");
    // stack = []
  
  // Load and set up the lines module.
  lines__load_lib(L);
    // stack = []
  lines__set_transform_callback(transform_callback);
  
  // Call render.init.
  clua__call(L, "render", "init", "");  // "" --> no input, no output
}

extern "C" void luarender__draw(int w, int h) {
  
  // Clear view and set the aspect ratio and rotation angle.
  glViewport(0, 0, w, h);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  aspect_ratio = (float)w / h;
  if (!is_tree_2d) angle += 0.01;
  
  // Recompute the mvp matrix.
  mat4 projection = perspective(45.0f, aspect_ratio, 0.1f, 1000.0f);
  mat4 view  = lookAt(vec3(4.0, 4.0, 2.0), vec3(0.0), vec3(0.0, 1.0, 0.0));

  if (is_tree_2d) {
    float d = 5.5;
    float y = -0.3;
    view = lookAt(vec3(0.0, y, d),       // eye
                  vec3(0.0, y, 0.0),     // at
                  vec3(0.0, 1.0, 0.0));  // up
  }

  mat4 model = rotate(mat4(1.0), angle, vec3(0.0, 1.0, 0.0));
  model = translate(model, vec3(0, -3, 0));
  model = scale(model, vec3(zoom_scale));
  mvp = projection * view * model;

  // Call Lua render.draw() to finish.
  clua__call(L, "render", "draw", "");  // "" --> no input or output
}
