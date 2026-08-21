/*
 * GB Transfer Dumper - application-wide definitions.
 *
 * Identity strings, the application error codes layered on top of libtrpak's
 * own result codes, and the debug logging facility shared by every module.
 *
 * Author: Alex Ishida
 * License: MIT
 */

#ifndef APP_H
#define APP_H

#include "libtrpak.h"

#define APP_NAME    "GB Transfer Dumper"
#define APP_AUTHOR  "Alex Ishida"
#define APP_VERSION "1.0.2"

/* Longest absolute path the application ever builds or accepts. */
#define APP_PATH_SIZE 128

/* Application error codes. They stay clear of libtrpak's range (-1 .. -18) so
 * app_error_string() can dispatch on the value alone. */
#define APP_ERR_SD_MOUNT           (-100)
#define APP_ERR_SD_DIRECTORY       (-101)
#define APP_ERR_FILE_OPEN          (-102)
#define APP_ERR_FILE_WRITE         (-103)
#define APP_ERR_FILE_CLOSE         (-104)
#define APP_ERR_NAME_EXHAUSTED     (-105)
#define APP_ERR_INVALID_PARAM      (-106)
#define APP_ERR_FILE_READ          (-107)
#define APP_ERR_FILE_NOT_FOUND     (-108)
#define APP_ERR_FILE_SIZE_MISMATCH (-109)
#define APP_ERR_CANCELLED          (-110)

/* Debug levels; only messages at or below the active level are emitted. */
#define DEBUG_LEVEL_NONE    0
#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

/**
 * @brief Returns a human readable message for an application or libtrpak code.
 *
 * @param result Value returned by any application or libtrpak entry point.
 * @return Static string; never NULL.
 */
const char *app_error_string(int result);

/**
 * @brief Writes one diagnostic line when the build enables debug logging.
 *
 * The body compiles away unless `ENABLE_DEBUG` is defined, but the prototype
 * keeps its `printf` format attribute in every build so the compiler still
 * type-checks the format string and its arguments.
 *
 * @param level  One of the `DEBUG_LEVEL_*` values.
 * @param format `printf`-style format string.
 */
void debug_log(int level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif /* APP_H */
