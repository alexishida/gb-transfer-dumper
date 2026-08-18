/*
 * GB Transfer Dumper - error strings and debug logging.
 */

#include "app.h"

#include <libdragon.h>

#include <stdarg.h>
#include <stdio.h>

#ifdef ENABLE_DEBUG
static int debug_level = DEBUG_LEVEL_INFO;
#endif

const char *app_error_string(int result)
{
    switch (result) {
    case APP_ERR_SD_MOUNT: return "Unable to mount microSD";
    case APP_ERR_SD_DIRECTORY: return "Unable to access dump directory";
    case APP_ERR_FILE_OPEN: return "Unable to create file";
    case APP_ERR_FILE_WRITE: return "Error writing to microSD";
    case APP_ERR_FILE_CLOSE: return "Error closing file";
    case APP_ERR_NAME_EXHAUSTED: return "No available filename";
    case APP_ERR_INVALID_PARAM: return "Invalid parameter";
    case APP_ERR_FILE_READ: return "Error reading from microSD";
    case APP_ERR_FILE_NOT_FOUND: return "File not found";
    case APP_ERR_FILE_SIZE_MISMATCH: return "File size mismatch";
    case APP_ERR_CANCELLED: return "Operation cancelled";
    default: return trpak_error_string(result);
    }
}

void debug_log(int level, const char *format, ...)
{
#ifdef ENABLE_DEBUG
    char message[256];
    const char *level_str;
    va_list args;

    if (level > debug_level) {
        return;
    }

    switch (level) {
    case DEBUG_LEVEL_ERROR: level_str = "ERROR"; break;
    case DEBUG_LEVEL_INFO: level_str = "INFO"; break;
    case DEBUG_LEVEL_VERBOSE: level_str = "VERBOSE"; break;
    default: level_str = "UNKNOWN"; break;
    }

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    debugf("[%s] %s", level_str, message);
#else
    (void)level;
    (void)format;
#endif
}
