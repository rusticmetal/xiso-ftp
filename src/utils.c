#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "xdvdfs.h"
#include <7zTypes.h>

char * format_filename(char * filename) {
    /*
    Formats and truncates filename so that it is suitable as a folder name. 
    Returns a pointer to the filename since it may have been incremented to get rid of the path up to the file.
    e.x. "./folder/folder/gamefile.iso" -> "gamefile"
    */

    //first remove the leading '.'
    if (* filename == '.') {
        filename++;
    }

    //next remove every slash
    char * slash = strchr(filename, '/');
    while (slash) {
        filename++;
        slash = strchr(filename, '/');
    }
    if (filename != "" && filename[strlen(filename) - 1] == '/') {
        filename++;
    }

    //now remove the file extension
    char * dot = strchr(filename, '.');
    if (dot && dot != filename) {
        *dot = '\0';
    }

    //replace all remaining special characters with underscores
    char * filename_i = filename;
    while (* filename_i) {
        if (* filename_i == ' ' || * filename_i == ',') {
            * filename_i = '_';
        }
        filename_i++;
    }

    //truncate filename
    if (strlen(filename) >= MAX_XBOX_FILENAME_LENGTH) {
        filename[MAX_XBOX_FILENAME_LENGTH - 1] = '\0';
    }
    return filename;
}

bool has_utf16_iso_extension(const UInt16 * filename) {
    /*
    Returns true if the filename in utf-16 ends in .iso. Needed because of .7z archives being in utf-16.
    */
    size_t i = 0;
    while (filename[i] != 0) //null terminator in hex
        i++;

    if (i < 4)
        return 0;

    if (filename[i - 4] == '.' && filename[i - 3] == 'i' && filename[i - 2] == 's' && filename[i - 1] == 'o') {
        return true;
    }
    return false;
}

char * convert_utf16_to_utf8(const UInt16* filename) {
    /*
    Converts a utf16 filename to utf8. Allocates space for the new filename with the same amount of characters.
    Returns the new utf8 filename, or NULL if malloc failed.
    Only works for ascii characters, non-ascii characters won't cast correctly.
    */
    size_t i = 0;
    while (filename[i] != 0) //null terminator in hex
        i++;

    char * filename_utf8 = malloc((i + 1) * sizeof(char));
    if (!filename_utf8) {
        return NULL;
    }
    for (size_t index = 0; index < i; index++)
        filename_utf8[index] = (char)filename[index];
    filename_utf8[i] = '\0';
    return filename_utf8;
}