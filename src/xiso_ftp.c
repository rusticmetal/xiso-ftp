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

//libcurl must be installed to compile this program: https://ec.haxx.se/install/linux.html
#include <curl/curl.h>

//these are all part of the LZMA SDK: https://www.7-zip.org/sdk.html, used for decoding .7z archives
#include "Precomp.h"
#include "7zFile.h"
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#define kInputBufSize ((size_t)1 << 18)

#include "xdvdfs.h"
#include "ftp.h"
#include "utils.h"

/*
To compile:
mkdir build
cd build
cmake ..
cmake --build .

File system source material: https://multimedia.cx/xdvdfs.html
*/

//if one of the the following is set, the other should be NULL
char * ftp_location = NULL; //this is the parent folder of where we extract the game to, this should be of the form '/F:/games/' or something
char * extraction_location = NULL; //this is where we locally extract files (if selected), '.' if extracting to cwd

int traverse_and_extract(uint8_t * directory_table, directory_entry * current_entry, char * path) {
    /*
    Call begin_extraction() first to ensure the filename is formatted, and that the proper ftp handles or local extraction folder exists.
    Transfers or locally writes (depending on if there is an active curl instance) the file or folder with the current entry's data to path.
    Returns 0 if the entire iso could be extracted, -1 if an error creating a file/folder was encountered.
    Recursively goes through each directory entry in the directory table using the current entry's right and left offsets.
    Stops recursing and returns -1 when file/folder could not be extracted.
    Additionally, this is called again whenever current entry corresponds to a directory, and in that case the start of that directory entry's sector is passed.
    */

    if (entry_has_value(current_entry)) { //this entry is either a file or folder
        //start by obtaining the filename of the current entry
        char filename[current_entry->filename_length + 1];
        memcpy(filename, current_entry->filename, current_entry->filename_length);
        filename[current_entry->filename_length] = '\0';

        char updated_path[MAX_FILE_PATH_LENGTH]; //this will become '{path}/{filename}' if file or '{path}/{filename}/' if folder
        //pass this along if recursing into new directory table

        if (current_entry->flags & IS_DIRECTORY_FLAG) {
            snprintf(updated_path, sizeof(updated_path), "%s%s/", path, filename);

            if (curl_folders) {
                //all thats needed to create the directory is the path, with a slash at the end
                curl_easy_setopt(curl_folders, CURLOPT_URL, updated_path);
                CURLcode res = curl_easy_perform(curl_folders);
                if (res != CURLE_OK) {
                    fprintf(stderr, "Error creating remote directory: %s\n", curl_easy_strerror(res));
                    return -1;
                }

            } else { //just create a local directory
                if (mkdir(updated_path, 0755) == -1 && errno != EEXIST) {
                    perror("Failed to make directory.");
                    return -1;
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

                if (response == CURLE_URL_MALFORMAT) {//rarely, the filename will have a space or possibly some other character that isn't allowed, and must be formatted
                    //in this case we retry, haven't found any other cases like this
                    fprintf(stderr, "Upload for %s failed, likely has to do with the filename containing a space or other character: %s\n", updated_path, curl_easy_strerror(response));
                    printf("REPEATING WITH FORMATTING FIX FOR PROBLEMATIC CHARACTERS IN FILENAME!\n");
                    char url_safe[(MAX_FILE_PATH_LENGTH * 3) + 1];
                    ftp_format(updated_path, url_safe);

                    CURLcode response = begin_file_curl(current_entry, &file_data, url_safe);
                    if (response != CURLE_OK) {
                        fprintf(stderr, "Upload failed again,: %s\n", curl_easy_strerror(response));
                        printf("Filename: %s\n", updated_path);
                        return -1;
                    }

                } else if (response != CURLE_OK) {
                    fprintf(stderr, "Upload for %s failed, NOT due to filename: %s\n", updated_path, curl_easy_strerror(response));
                    return -1;
                }

            } else { //just write the bytes locally
                FILE * file = fopen(updated_path, "wb");
                if (!file) {
                    perror("Failed to create a new file.");
                    return -1;
                }

                size_t bytes_written = fwrite(global_offset + (current_entry->file_start_sector * SECTOR_SIZE), 1, current_entry->file_size, file);
                if (bytes_written != current_entry->file_size) {
                    perror("Failed to write an entire file.");
                    fclose(file);
                    return -1;
                }
                fclose(file);
            }
        }
    }

    //now recurse to the left and right entries in the table
    if (current_entry->left_entry_offset) {
        if (traverse_and_extract(directory_table, (directory_entry *)(directory_table + (current_entry->left_entry_offset) * 4), path)) {
            return -1;
        }
    }
    if (current_entry->right_entry_offset) {
        if (traverse_and_extract(directory_table, (directory_entry *)(directory_table + (current_entry->right_entry_offset) * 4), path)) {
            return -1;
        }
    }
    return 0;
}

int begin_extraction(char * filename, directory_entry * current_entry) {
    /*
    Processes filename first to turn it into a suitable folder name. If ftp_location is set, prompts the user for their Xbox's IP, username and passowrd, then 
    sets up both the file and folder curl instances (closes handles after extraction). If not, we assume extraction_location is set, 
    and we create {extraction_location}/{formatted_filename}/. In both cases we call traverse_and_extract() after to begin the actual extraction. 
    Returns -1 if there was an error setting up curl handles or the local folder, else returns the value of traverse_and_extract().
    */

    filename = format_filename(filename);

    if (ftp_location) { //ftp extraction
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

        char remote_base_url[MAX_FILE_PATH_LENGTH];
        snprintf(remote_base_url, sizeof(remote_base_url), "ftp://%s/%s%s/", ip_address , ftp_location, filename);

        //we use one curl instance for remote folder creation and another for direct data upload (creating folders won't work with just one)
        curl_files = curl_easy_init();
        if (curl_files == NULL) {
            fprintf(stderr, "Could not initialize curl handle.\n");
            return -1;
        }
        curl_folders = curl_easy_init();
        if (curl_folders == NULL) {
            fprintf(stderr, "Could not initialize curl handle.\n");
            return -1;
        }

        //now to tell curl our credentials and specify our options for both curl handles
        curl_easy_setopt(curl_folders, CURLOPT_URL, remote_base_url);
        curl_easy_setopt(curl_folders, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl_folders, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl_folders, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);

        //our first curl will create the base directory for our game
        CURLcode response = curl_easy_perform(curl_folders);
        if (response != CURLE_OK) {
            fprintf(stderr, "Error with remote destination, are you sure the path given is valid/credentials are correct?: %s\n", curl_easy_strerror(response));
            return -1;
        }

        //similar to the other curl instance, but requires two additional options to send and read data
        curl_easy_setopt(curl_files, CURLOPT_URL, remote_base_url);
        curl_easy_setopt(curl_files, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl_files, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl_files, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
        curl_easy_setopt(curl_files, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl_files, CURLOPT_READFUNCTION, read_callback);
        //curl_easy_setopt(curl_files, CURLOPT_FTP_USE_EPSV, 1L); //this is for passive mode, not sure if its needed

        printf("Extracting and transferring to %s\n", remote_base_url);
        uint8_t * root_directory_table = (uint8_t *) current_entry; //this isnt necessary, just prevents confusion since the addresses of root directory == our first entry
        int retval = traverse_and_extract(root_directory_table, current_entry, remote_base_url);

        curl_easy_cleanup(curl_files);
        curl_easy_cleanup(curl_folders);
        return retval;

    } else { //local extraction
        char local_base_url[MAX_FILE_PATH_LENGTH];
        size_t extraction_location_len = strlen(extraction_location);
        if (extraction_location[extraction_location_len - 1] == '/') { //don't add another '/' if it was given
            snprintf(local_base_url, sizeof(local_base_url), "%s%s/", extraction_location, filename);
        } else {
            snprintf(local_base_url, sizeof(local_base_url), "%s/%s/", extraction_location, filename);
        }
        if (mkdir(local_base_url, 0755) == -1 && errno != EEXIST) {
            perror("Error with path. Does the directory exist?\n");
            return -1;
        }
        
        printf("Extracting to %s\n", local_base_url);
        uint8_t * root_directory_table = (uint8_t *) current_entry;
        return traverse_and_extract(root_directory_table, current_entry, local_base_url);
    }
}

int main(int argc, char *argv[]) {
    //handle the command lines flags first, and make sure that all flags go BEFORE the main argument
    int opt;
    bool file_is_archive = false;
    volume_descriptor * vol_descriptor = NULL;

    while ((opt = getopt(argc, argv, "x:f:ah")) != -1) {
        switch(opt) {
            case 'x': //local extraction
                extraction_location = optarg;
                break;
            case 'f': //file transfer
                ftp_location = optarg;
                break;
            case 'a': //.7z archive (containing iso) instead of .xiso
                file_is_archive = true;
                break;
            case 'h': //help
                printf("Example ftp usage: './xiso-ftp -f 'F:/games/' 'ExampleGame.iso'\n");
                printf("Example local usage: './xiso-ftp -x ./ExampleGame/ 'ExampleGame.iso'\n");
                printf("If the file is a .7z archive, use the -a flag. Warning: This will attempt to extract ALL iso files within the archive.\n");
                return 0;
            case '?':
                return -1;
            default:
                fprintf(stderr, "Error: %c is not an option.\n", opt);
                return -1;
        }
    }

    if ((extraction_location && ftp_location) || (!extraction_location && !ftp_location)) {
        fprintf(stderr, "Error: Please choose either one of local extraction or ftp transfer, not both.\n");
        return -1;
    }

    if (optind + 1 != argc) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        return -1;
    }

    if (ftp_location) {
        CURLcode g = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (g != CURLE_OK) {
            fprintf(stderr, "Curl failed to initialize.");
            return -1;
        }
    }

    char * filename = argv[optind];
    printf("You have selected the file: %s\n", filename);

    if (!file_is_archive) { //this is a .iso file
        //map our iso file into memory
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
        vol_descriptor = find_magic_signature(iso_file, st.st_size);
        if (vol_descriptor == NULL) {
            fprintf(stderr, "XDVD signature not detected. Is this a valid game file?\n");
            return -1;
        }

        //now we can traverse the directories and extract folders/files as we go, starting with the root directory from the volume descriptor
        directory_entry * current_entry = get_first_root_directory_entry(vol_descriptor);
        begin_extraction(filename, current_entry);

        if (munmap(iso_file, st.st_size)) {
            printf("Error unmapping .iso file. There may be an error with your game file, or the extraction.\n");
            return -1;
        }
   
    } else { //treat the file as a .7z archive
        /* 
        Below is mostly code adapted from /lib/lzma/Util/7z/7zMain.c by Igor Pavlov (Public Domain).
        Instead of extracting any files to the drive, we extract them to a buffer and copy them over to a pointer, 
        where we then can proceed to traverse and convert the iso just like if we were mmapping it.

        See line 545 of the included /lib/lzma/Util/7z/7zMain.c (the start of the main function).
        */

        static const ISzAlloc g_Alloc = {SzAlloc, SzFree};
        ISzAlloc allocImp;
        ISzAlloc allocTempImp;

        CFileInStream archiveStream;
        CLookToRead2 lookStream;
        CSzArEx db;
        SRes res;
        UInt16 * temp = NULL;
        size_t tempSize = 0;
        allocImp = g_Alloc;
        allocTempImp = g_Alloc;

        //open file for reading and initialize stream interfaces
        if (InFile_Open(&archiveStream.file, filename) != 0) {
            fprintf(stderr, "Error opening archive file.\n");
            return -1;
        }

        FileInStream_CreateVTable(&archiveStream);
        archiveStream.wres = 0;
        LookToRead2_CreateVTable(&lookStream, False);
        lookStream.buf = NULL;
        res = SZ_OK;

        lookStream.buf = (uint8_t *)ISzAlloc_Alloc(&allocImp, kInputBufSize);
        if (!lookStream.buf)
            res = SZ_ERROR_MEM;
        else {
            lookStream.bufSize = kInputBufSize;
            lookStream.realStream = &archiveStream.vt;
            LookToRead2_INIT(&lookStream)
        }
        
        //initialize the archive's structure
        CrcGenerateTable();
        SzArEx_Init(&db);     
        if (res == SZ_OK) {
            res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
            if (res != SZ_OK) {
                fprintf(stderr, "Error opening .7z archive structure. Is this a .7z archive file?\n");
                if (lookStream.buf) {
                    free(lookStream.buf);
                }
                File_Close(&archiveStream.file);
                return -1;
            }
        } else {
            fprintf(stderr, "Error initializing .7z archive structure.\n");
            if (lookStream.buf) {
                free(lookStream.buf);
            }
            File_Close(&archiveStream.file);
            return -1;
        }

        //these are specifically used as a temporary location to write our data
        UInt32 blockIndex = 0xFFFFFFFF;
        uint8_t * outBuffer = NULL;
        size_t outBufferSize = 0;

        //now we can loop through the archive like a directory, note that if any error is encountered, we return and any remaining isos are not converted
        printf("Total number of files in archive: %u\n", db.NumFiles);
        for (UInt32 i = 0; i < db.NumFiles; i++) {
            if (SzArEx_IsDir(&db, i))
                continue;

            size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL); //first call with NULL gives us the filename length
            UInt16 * name_buffer = (UInt16 *)malloc(len * sizeof(UInt16));
            SzArEx_GetFileNameUtf16(&db, i, name_buffer);

            char name_utf8[MAX_FILE_PATH_LENGTH];
            if (has_utf16_iso_extension(name_buffer)) {
                char * name_utf8 = convert_utf16_to_utf8(name_buffer); //need utf8 so we can append the filename to the destination
                printf("Found iso: %s\n", name_utf8);

                size_t offset = 0;
                size_t outSizeProcessed = 0;

                SRes res = SzArEx_Extract(
                    &db,
                    &lookStream.vt,
                    i, //this is our specific file's index in the archive, the rest are the same for other isos in the archive
                    &blockIndex,
                    &outBuffer,
                    &outBufferSize,
                    &offset,
                    &outSizeProcessed,
                    &allocImp,
                    &allocTempImp
                );

                if (res != SZ_OK) {
                    fprintf(stderr, "Error extracting file from archive.\n");
                    SzArEx_Free(&db, &allocImp);
                    if (lookStream.buf) {
                        free(lookStream.buf);
                    }
                    File_Close(&archiveStream.file);
                    return -1;
                }
                //end of LZMA SDK code

                //now we finally have access to the entire iso into memory, without putting it on the disk
                uint8_t * file_data = (uint8_t *)malloc(outSizeProcessed);
                if (!file_data) {
                    perror("Error allocating memory. There was likely not enough.\n");
                    SzArEx_Free(&db, &allocImp);
                    if (lookStream.buf) {
                        free(lookStream.buf);
                    }
                    File_Close(&archiveStream.file);
                    return -1;
                }
                memcpy(file_data, outBuffer + offset, outSizeProcessed); //we can't use outBuffer, it is not safe to use

                //from here to continue the exact same as if we just had a regular pointer to a mmap'd iso (we just have to free the pointer after instead of munmap)

                //need to find the magic signature to ensure this is a valid xiso file
                vol_descriptor = find_magic_signature(file_data, outSizeProcessed);
                if (vol_descriptor == NULL) {
                    fprintf(stderr, "XDVD signature not detected. Is this a valid game file?\n");
                    SzArEx_Free(&db, &allocImp);
                    if (lookStream.buf) {
                        free(lookStream.buf);
                    }
                    File_Close(&archiveStream.file);
                    return -1;
                }

                //now we can traverse the directories and extract folders/files as we go, starting with the root directory from the volume descriptor
                directory_entry * current_entry = get_first_root_directory_entry(vol_descriptor);
                int retval = begin_extraction(name_utf8, current_entry);
                if (!retval) {
                    printf("Extraction of %s successful.\n", name_utf8);
                }
                free(file_data);
                free(name_utf8);
            }
            free(name_buffer);
        }
        //free the data structures from the archive
        SzArEx_Free(&db, &allocImp);
        if (lookStream.buf) {
            free(lookStream.buf);
        }
        File_Close(&archiveStream.file);
    }

    if (curl_files) {
        curl_easy_cleanup(curl_files);
    }
    if (curl_folders) {
        curl_easy_cleanup(curl_folders);
    }
    if (ftp_location) {
        curl_global_cleanup();
    }
    return 0;
}