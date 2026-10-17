/* src/frontend/sdl/logfile.h - the flight log as a local file (plan Task 5, Review Focus 3; spec section 10: the original's log disk is replaced by a
 * file). The core hands over and takes a byte blob (game_log_export / game_log_import); this module only moves bytes. Nothing here is fatal: every
 * failure is a negative return the caller prints once. */
#ifndef SDL_LOGFILE_H_
#define SDL_LOGFILE_H_
#include <stddef.h>

/* Writes the default path ($XDG_CONFIG_HOME/interceptor/flightlog.bin, else $HOME/.config/interceptor/flightlog.bin) into out; returns 0, or -1 when
 * neither variable is usable or the path does not fit. */
int logfile_default_path(char *out, size_t cap);
/* Reads the file into buf (at most cap bytes). Returns the number of bytes read (0 for an empty file), -1 when the file does not exist (the normal first
 * run), -2 on a read error, -3 when the file is longer than cap (buf then holds the first cap bytes; the core refuses a blob of the wrong size). */
int logfile_load(const char *path, unsigned char *buf, size_t cap);
/* Writes len bytes, creating the directories of the path (mode 0700), through a temporary file renamed over the target so that a failed write never
 * truncates the existing log. Returns 0, or -1 (cannot create the directory or the file) or -2 (write error). */
int logfile_save(const char *path, const unsigned char *buf, size_t len);
#endif
