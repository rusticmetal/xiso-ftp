#ifndef UTILS
#define UTILS

#include <stdbool.h>
#include <7zTypes.h>

char * format_filename(char * filename);
bool has_utf16_iso_extension(const UInt16 * filename);
char * convert_utf16_to_utf8(const UInt16 * filename);

#endif