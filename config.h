//
//  config.h
//  Trees
//
//  A single place to keep all major configuration parameters.
//

// TODO Connect the equivalent Lua-visible parameters to this file.

// This controls whether or not the rendering is controlled by the Lua scripts.
// The value is expected to be YES or NO.
#define do_use_lua YES

#define max_tree_height    10

// This modifies the view transform by effectively zooming toward or away from
// the tree.
#define zoom_scale         2.3

#define branch_size_factor 0.79
#define max_ring_corners   8
