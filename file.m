#include "file.h"

#define path_len 4096

// Internal functions.

NSString *nsstr_save_dir() {
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
  return [paths[0] stringByAppendingPathComponent:@"Trees"];
}


// Public functions.

char *file__get_path(const char *filename) {
  static char path[path_len];
  
  NSString *name        = [NSString stringWithUTF8String:filename];
  NSString *name_wo_ext = [name stringByDeletingPathExtension];
  NSString *ext         = [name pathExtension];
  
  NSString *nsstr_path = [[NSBundle mainBundle] pathForResource:name_wo_ext ofType:ext];
  strncpy(path, [nsstr_path UTF8String], path_len);
  
  return path;
}

char *file__save_dir() {
  static char save_dir[path_len];
  
  NSString *nsstr_dir = nsstr_save_dir();
  strncpy(save_dir, [nsstr_dir UTF8String], path_len);
  
  return save_dir;
}

char *file__account_path() {
  static char path[path_len];
  
  NSString *nsstr_path = [nsstr_save_dir() stringByAppendingPathComponent:@"account"];
  strncpy(path, [nsstr_path UTF8String], path_len);
  
  return path;
}

int file__make_dir_if_needed(const char *dir) {
  
  NSFileManager *fm = [NSFileManager defaultManager];
  NSString *nsstr_dir = [NSString stringWithUTF8String:dir];
  
  if ([fm fileExistsAtPath:nsstr_dir]) return true;
  
  NSError *error = nil;
  [fm createDirectoryAtPath:nsstr_dir withIntermediateDirectories:YES attributes:nil error:&error];
  
  if (!error) return true;
  
  NSLog(@"Error creating %@: %@", nsstr_dir, error);
  return false;
}

int file__exists(const char *path) {
  NSString *nsstr_path = [NSString stringWithUTF8String:path];
  return [[NSFileManager defaultManager] fileExistsAtPath:nsstr_path];
}

char *file__contents(const char *path, size_t *size) {
  FILE *f = fopen(path, "r");  // TODO Will have to update this bit to be cross-platform (e.g. "rb" etc).
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *file_contents = malloc(*size + 1);
  fread(file_contents, 1, *size, f);
  file_contents[*size] = '\0';
  fclose(f);
  
  return file_contents;
}

int file__write(const char *path, const char *contents) {
  FILE *f = fopen(path, "w");  // TODO I probably want "wb" here for windows.
  if (f == NULL) {
    printf("Error: fopen failed in %s.\n", __func__);
    return 0;
  }
  size_t num_bytes = strlen(contents);
  size_t bytes_written = fwrite(contents, 1 /* elt size */, num_bytes /* num elts */, f);
  fclose(f);
  if (bytes_written < num_bytes) {
    printf("Error: only wrote %zu bytes in %s; tried to write %zu.\n", bytes_written, __func__, num_bytes);
    return 0;
  }
  return 1;
}