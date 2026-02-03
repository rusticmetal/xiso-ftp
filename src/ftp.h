#ifndef FTP
#define FTP
#include <stdint.h>

typedef struct {
    /*
    This struct is needed so that libcurl's ftp callback for reading data knows how and where exactly to copy over data from memory (the mmap'd iso),
    and to know how much data is remaining to be read.
    */
    const uint8_t * file_ptr; //where the beginning of the file actually starts on the iso
    size_t file_size;
    size_t bytes_sent;
} ftp_file_data;

extern CURL * curl_files; //curl instance for ftp
extern CURL * curl_folders; //curl instance for creating remote folders

CURLcode begin_file_curl(const directory_entry * current_entry, ftp_file_data * file_data, const char * url);
void ftp_format(const char * input, char * output);
size_t read_callback(void * ptr, size_t size, size_t nitems, void * data);
#endif