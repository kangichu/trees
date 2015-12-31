//
//  BNLOpenGLView.m
//  Trees
//
//  Created by Tyler Neylon on 6/5/14.
//  Copyright (c) 2014 Tyler Neylon. All rights reserved.
//

#import "BNLOpenGLView.h"

#include "file.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "render.h"


// Internal globals.

static BNLOpenGLView *glView = nil;
static lua_State *L = NULL;


// Internal class members.

@interface BNLOpenGLView() {
  CFMachPortRef eventTap;
  CVDisplayLinkRef displayLink;
}

@property (atomic) CGLContextObj cglContext;
@property int xWindowSize, yWindowSize;
@property bool mouseIsFree;

@end


// Functions.

static CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
  
  NSRect viewFrameInScreenCoords = [[glView window] convertRectToScreen:glView.frame];
  CGPoint mousePt = CGEventGetUnflippedLocation(event);
  mousePt.x -= viewFrameInScreenCoords.origin.x;
  mousePt.y -= viewFrameInScreenCoords.origin.y;
  
  if (type == kCGEventMouseMoved) {
    double dx = CGEventGetDoubleValueField(event, kCGMouseEventDeltaX);
    double dy = CGEventGetDoubleValueField(event, kCGMouseEventDeltaY);
    render__mouse_moved(mousePt.x, mousePt.y, dx, dy);
  }
  
  if (type == kCGEventLeftMouseDown) {
    render__mouse_down(mousePt.x, mousePt.y);
  }
  
  return event;
}

static void runloop() {
  CGLLockContext      (glView.cglContext);
  CGLSetCurrentContext(glView.cglContext);
  
  render__draw(glView.xWindowSize, glView.yWindowSize);
    
  CGLFlushDrawable(glView.cglContext);
  CGLUnlockContext(glView.cglContext);
}

static CVReturn displayLinkCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* now,
                                    const CVTimeStamp* outputTime, CVOptionFlags flagsIn,
                                    CVOptionFlags* flagsOut, void* displayLinkContext) {
  runloop();
  return kCVReturnSuccess;
}



// Implementation.

@implementation BNLOpenGLView

- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        // Initialization code here.
    }
    return self;
}

- (void)awakeFromNib {
  // TEMP TODO Uncomment the following line to produce a different tree every run.
  srand((unsigned int)time(NULL));
  
  glView = self;
  self.mouseIsFree = YES;
  
  NSOpenGLPixelFormatAttribute attrs[] = {
		NSOpenGLPFADoubleBuffer,
		NSOpenGLPFADepthSize, 24,
		NSOpenGLPFAOpenGLProfile,
		NSOpenGLProfileVersion3_2Core,
    NSOpenGLPFAMultisample,
    NSOpenGLPFASampleBuffers, (NSOpenGLPixelFormatAttribute)1,
    NSOpenGLPFASamples, (NSOpenGLPixelFormatAttribute)4,
		0
	};
  
	self.pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
	if (!self.pixelFormat) NSLog(@"No OpenGL pixel format.");
  
  self.openGLContext = [[NSOpenGLContext alloc] initWithFormat:self.pixelFormat shareContext:nil];
  GLint swap = 1;
  [self.openGLContext setValues:&swap forParameter:NSOpenGLCPSwapInterval];
  
  if (do_use_lua) {
    L = luaL_newstate();
    luaL_openlibs(L);
    char *filepath = file__get_path("render.lua");
      // stack = []
    luaL_dofile(L, filepath);
      // stack = [render]
    lua_setglobal(L, "render");
      // stack = []
    
    
    // TODO HERE Run the Lua init fn; next up run the draw fn every cycle.
    
  }
}

- (void)reshape {
  
  // We avoid nested actions with this bool.
  static bool isReshaping = false;
  if (isReshaping) return;
  isReshaping = true;
  
  self.frame = [[[self window] contentView] bounds];
  self.xWindowSize = self.frame.size.width;
  self.yWindowSize = self.frame.size.height;
  
  // Give render a way to know the mouse position before any mouse events occur.
  CGPoint mousePt = [NSEvent mouseLocation];
  render__mouse_moved(mousePt.x, mousePt.y, 0 /* dx */, 0 /* dy */);
  
  isReshaping = false;
}


- (void)drawRect:(NSRect)dirtyRect
{
    [super drawRect:dirtyRect];
    
    // Drawing code here.
}

- (void)prepareOpenGL {
  [super prepareOpenGL];
  [[self openGLContext] makeCurrentContext];
  self.cglContext = (CGLContextObj)[[self openGLContext] CGLContextObj];
  
  // Input init.
  eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, kCGEventMaskForAllEvents, eventCallback, NULL);
  if (eventTap == NULL) printf("Error: eventTap creation failed.\n");
  
  CFRunLoopSourceRef runLoopEventSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopEventSource, kCFRunLoopDefaultMode);
  CFRelease(runLoopEventSource);
  CGEventTapEnable(eventTap, true);
  
  // It's important to perform the following init items before setting up the display link
  // since the display link callback assumes the init items have already completed.
  
  render__init();
  
  // Set up and activate a display link.
	CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
  CVDisplayLinkSetOutputCallback(displayLink, &displayLinkCallback, (__bridge void *)self);
	CGLPixelFormatObj cglPixelFormat = (CGLPixelFormatObj)[[self pixelFormat] CGLPixelFormatObj];
	CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(displayLink, self.cglContext, cglPixelFormat);
	CVDisplayLinkStart(displayLink);
}


@end
