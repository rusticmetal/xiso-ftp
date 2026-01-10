#include <string.h>
#include <curl/curl.h>
#include "xdvdfs.h"
#include "ftp.h"

CURL * curl_files; //curl instance for ftp
CURL * curl_folders; //curl instance for creating remote folders

CURLcode begin_file_curl(directory_entry * current_entry, ftp_file_data * file_data, char * url) {
    /*
    Takes the current directory entry and sets the given file data struct with its memory address and file size, then sets its current bytes sent to 0.
    Attempts to send the file (assuming the send mode option is set) via curl and the read_file_callback to the given url. Returns a CURLcode based on the success or failure
    of the curl operation.
    */
    curl_easy_setopt(curl_files, CURLOPT_URL, url);
    
    file_data->file_ptr = global_offset + (current_entry->file_start_sector * SECTOR_SIZE);
    file_data->file_size = current_entry->file_size;
    file_data->bytes_sent = 0;

    curl_easy_setopt(curl_files, CURLOPT_READDATA, file_data);
    curl_easy_setopt(curl_files, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_data->file_size);

    return curl_easy_perform(curl_files);
}

void format_spaces(char * input, char * output) {
    /*
    Copies a version of input with every space replaced to the output string with the three characters "%20".
    strlen(output) must be equal or greater to 3 * strlen(input) + 1.
    This is needed whenever we encounter the CURLE_URL_MALFORMAT error, because it means the url was messed up and needs to be formatted with this and resent.
    Note: I'm not sure if there are other characters that need to be replaced (like commas), though I have not encountered it yet
    */
    int j = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == ' ') {
            output[j] = '%';
            j++;
            output[j] = '2';
            j++;
            output[j] = '0';
            j++;
        } else {
            output[j] = input[i];
            j++;
        }
    }
    output[j] = '\0';
    return;
}

size_t read_callback(void * ptr, size_t size, size_t nitems, void * data) {
    /*
    Callback for libcurl's CURLOPT_READFUNCTION. Copies as many bytes as possible from the file data's pointer
    to libcurls pointer, and increments the amount of bytes sent. Returns the amount of bytes left remaining to send.
    */
    ftp_file_data * file_data = (ftp_file_data *) data;
    size_t bytes_left = file_data->file_size - file_data->bytes_sent;
    size_t bytes_to_send;
    if (bytes_left < size * nitems) { //we can only send size * nitems at a time
        bytes_to_send = bytes_left;
    } else {
        bytes_to_send = size * nitems; //send the rest of the file
    }

    if (bytes_to_send > 0) {
        memcpy(ptr, file_data->file_ptr + file_data->bytes_sent, bytes_to_send);
        file_data->bytes_sent += bytes_to_send;
        return bytes_to_send;
    } else {
        return 0;
    }
}