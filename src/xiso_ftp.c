#define _POSIX_C_SOURCE 2
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <curl/curl.h>
#include "xdvdfs.h"
#include "ftp.h"

/*
To compile:
mkdir build
cd build
cmake ..
cmake --build .

libcurl is needed for this program: https://ec.haxx.se/install/linux.html

File system source material: https://multimedia.cx/xdvdfs.html
*/

void traverse_and_extract(uint8_t * directory_table, directory_entry * current_entry, char * path) {
    /*
    Transfers or locally writes (depending on if there is an active curl instance) the file or folder with the current entry's data to path.
    Recursively goes through each directory entry in the directory table using the current entry's right and left offsets and does the same afterwards.
    Additionally, this is called whenever current entry corresponds to a directory, and in that case the start of that directory entry's sector is passed. Since this happens
    before we recurse to the entries at the right and left offsets, this would be a depth-first algorithm.
    */

    if (entry_has_value(current_entry)) { //this entry is either a file or folder
        //start by obtaining the filename
        char filename[current_entry->filename_length + 1];
        memcpy(filename, current_entry->filename, current_entry->filename_length);
        filename[current_entry->filename_length] = '\0';

        char updated_path[MAX_FILE_PATH_LENGTH];

        if (current_entry->flags & IS_DIRECTORY_FLAG) {
            snprintf(updated_path, sizeof(updated_path), "%s%s/", path, filename);

            if (curl_folders) {
                //all thats needed to create the directory is the path, with a slash at the end
                curl_easy_setopt(curl_folders, CURLOPT_URL, updated_path);
                CURLcode res = curl_easy_perform(curl_folders);
                if (res != CURLE_OK) {
                    fprintf(stderr, "Error creating remote directory: %s\n", curl_easy_strerror(res));
                    return;
                }

            } else { //just create a local directory
                if (mkdir(updated_path, 0755) == -1 && errno != EEXIST) {
                    perror("Failed to make directory.");
                    return;
                }
            }
            //start traversing the directory table for this folder
            traverse_and_extract((uint8_t *) (global_offset + (current_entry->file_start_sector * SECTOR_SIZE)), 
            (directory_entry *) (global_offset + (current_entry->file_start_sector * SECTOR_SIZE)), updated_path);

        } else { //this entry is a file
            snprintf(updated_path, sizeof(updated_path), "%s%s", path, filename);
            
            if (curl_files) {
                ftp_file_data file_data;
                //this will extract the data and transfer the files, without writing anything to the disk
                CURLcode response = begin_file_curl(current_entry, &file_data, updated_path);

                if (response == CURLE_URL_MALFORMAT) { //rarely, the filename will have a space or possibly some other character that isn't allowed, and must be formatted
                    fprintf(stderr, "Upload for %s failed, likely has to do with the filename containing a space or other character: %s\n", updated_path, curl_easy_strerror(response));
                    printf("REPEATING WITH FIX FOR SPACES IN FILENAME!\n");
                    char url_safe[(MAX_FILE_PATH_LENGTH * 3) + 1];
                    format_spaces(updated_path, url_safe);

                    CURLcode response = begin_file_curl(current_entry, &file_data, url_safe);
                    if (response != CURLE_OK) {
                        fprintf(stderr, "Upload failed again,: %s\n", curl_easy_strerror(response));
                        printf("Filename: %s\n", updated_path);
                    }

                } else if (response != CURLE_OK) {
                    fprintf(stderr, "Upload for %s failed, NOT due to filename: %s\n", updated_path, curl_easy_strerror(response));
                }

            } else { //just write the bytes locally
                FILE * file = fopen(updated_path, "wb");
                if (!file) {
                    perror("Failed to create a new file.");
                    return;
                }

                size_t bytes_written = fwrite(global_offset + (current_entry->file_start_sector * SECTOR_SIZE), 1, current_entry->file_size, file);
                if (bytes_written != current_entry->file_size) {
                    perror("Failed to write an entire file.");
                    fclose(file);
                    return;
                }
                fclose(file);
            }
        }
    }

    //now recurse to the left and right entries in the table
    if (current_entry->left_entry_offset != 0) {
        traverse_and_extract(directory_table, (directory_entry *)(directory_table + (current_entry->left_entry_offset) * 4), path);
    }

    if (current_entry->right_entry_offset != 0) {
        traverse_and_extract(directory_table, (directory_entry *)(directory_table + (current_entry->right_entry_offset) * 4), path);
    }
}

int main(int argc, char *argv[]) {

    //handle the command lines flags first, and make sure that all flags go BEFORE the main argument
    int opt;
    char * ftp_location = NULL; //this is the username and folder where we transfer files to (if selected), this should be of the form '/F:/games/'
    char * extraction_location = NULL; //this is where we locally extract files (if selected), '.' if extracting to cwd

    while ((opt = getopt(argc, argv, "x:f:h")) != -1) {
        switch(opt) {
            case 'x': //local extraction
                extraction_location = optarg;
                break;
            case 'f': //file transfer
                ftp_location = optarg;
                break;
            case 'h': //help
                printf("Example ftp usage: './xiso-ftp -f 'F:/games/' 'ExampleGame.iso'\nExample local usage: './xiso-ftp -x ./ExampleGame/ 'ExampleGame.iso'\n");
                return 0;
            case '?':
                return -1;
            default:
                fprintf(stderr, "Error: %c is not an option.\n", opt);
                return -1;
        }
    }

    if (extraction_location && ftp_location) {
        fprintf(stderr, "Error: Please choose either local extraction or ftp transfer, not both.\n");
        return -1;
    }

    if (optind + 1 != argc) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        return -1;
    }

    printf("You have selected the file: %s\n", argv[optind]);

    //map our iso file into memory
    char * filename = argv[optind];
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Could not find the xiso file. Check the spelling/location.\n");
        return -1;
    }

    struct stat st;
    fstat(fd, &st);
    uint8_t * iso_file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (iso_file != NULL) {
        printf("Successfully mapped file into memory.\n");
    } else {
        perror("Error mapping file into memory.\n");
        return -1;
    }

    //need to find the magic signature to ensure this is a valid xiso file
    volume_descriptor * vol_descriptor = find_magic_signature(iso_file, st.st_size);
    if (vol_descriptor == NULL) {
        fprintf(stderr, "XDVD signature not detected. Is this a valid game file?\n");
        return -1;
    }

    //now we traverse the directories and extract folders/files as we go, starting with the root directory from the volume descriptor
    directory_entry * current_entry = (directory_entry *) (global_offset + (vol_descriptor->root_directory_sector)*SECTOR_SIZE);

    if (ftp_location) { //ftp extraction
        CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (g != CURLE_OK) {
            fprintf(stderr, "Curl failed to initialize.");
            return 1;
        }

        char ip_address[16];
        char username[32];
        char password[32];
        printf("Enter your xbox's network IP address: \n");
        if (scanf("%15s", ip_address) == -1) {
            perror("Error inputting credentials.\n");
        }
        printf("Enter your xbox's network username: \n");
        if (scanf("%31s", username) == -1) {
            perror("Error inputting credentials.\n");
        }
        printf("Enter your xbox's network password: \n");
        if (scanf("%31s", password) == -1) {
            perror("Error inputting credentials.\n");
        }

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

        //truncate (unlikely to occur)
        if (strlen(filename) >= MAX_XBOX_FILENAME_LENGTH) {
            filename[MAX_XBOX_FILENAME_LENGTH - 1] = '\0';
        }

        char remote_base_url[MAX_FILE_PATH_LENGTH];
        snprintf(remote_base_url, sizeof(remote_base_url), "ftp://%s/%s%s/", ip_address , ftp_location, filename);

        //we use one curl instance for remote folder creation and another for direct data upload (creating folders won't work with just one)
        curl_files = curl_easy_init();
        if (curl_files == NULL) {
            return -1;
        }
        curl_folders = curl_easy_init();
        if (curl_folders == NULL) {
            return -1;
        }

        //now to tell curl our credentials and specify our options for both curl handles
        if (curl_folders) {
            curl_easy_setopt(curl_folders, CURLOPT_URL, remote_base_url);
            curl_easy_setopt(curl_folders, CURLOPT_USERNAME, username);
            curl_easy_setopt(curl_folders, CURLOPT_PASSWORD, password);
            curl_easy_setopt(curl_folders, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);

            //our first curl will create the base directory for our game
            CURLcode response = curl_easy_perform(curl_folders);
            if (response != CURLE_OK) {
                fprintf(stderr, "Error with remote destination, are you sure the path given is valid?: %s\n", curl_easy_strerror(response));
                return -1;
            }

        if (curl_files) {
            //similar to the other curl instance, but requires two additional options to send and read data
            curl_easy_setopt(curl_files, CURLOPT_URL, remote_base_url);
            curl_easy_setopt(curl_files, CURLOPT_USERNAME, username);
            curl_easy_setopt(curl_files, CURLOPT_PASSWORD, password);
            curl_easy_setopt(curl_files, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
            curl_easy_setopt(curl_files, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl_files, CURLOPT_READFUNCTION, read_callback);
            //curl_easy_setopt(curl_files, CURLOPT_FTP_USE_EPSV, 1L); //this is for passive mode, not sure if its needed
        }

        printf("Extracting and transferring to %s\n", remote_base_url);
        uint8_t * root_directory_table = (uint8_t *) current_entry;
        traverse_and_extract(root_directory_table, current_entry, remote_base_url);

        curl_easy_cleanup(curl_files);
        curl_easy_cleanup(curl_folders);
        curl_global_cleanup();
        }

    } else { //local extraction
        if (mkdir(extraction_location, 0755) == -1 && errno != EEXIST) {
            perror("Error with path. Does the parent directory exist?");
            return -1;
        }
        size_t extraction_location_len = strlen(extraction_location);
        if (extraction_location[extraction_location_len - 1] != '/' && strlen(extraction_location) < MAX_FILE_PATH_LENGTH - 1) {
            extraction_location[extraction_location_len] = '/';
            extraction_location[extraction_location_len + 1] = '\0';
        } else {
            fprintf(stderr, "Error with path.\n");
        }
        printf("Extracting to %s\n", extraction_location);
        uint8_t * root_directory_table = (uint8_t *) current_entry;
        traverse_and_extract(root_directory_table, current_entry, extraction_location);
    }

    return 0;
}