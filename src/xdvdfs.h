/*
These are all the constants and data structures documented from the XDVD File System.
Source: https://multimedia.cx/xdvdfs.html

This is the typical general structure of a .xiso file (keep in mind that disk sectors besides sector 32 can contain directory tables, file content, or filler/emptiness):

                        ---------------------------
                        |    START OF .ISO FILE   |
                        |           ^             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |    (blank or garbage)   |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           v             |
                        |global_offset(xdvd start)|
                        |           ^             |
                        |           |             |
                        | 32 logical disk sectors |
                        |           |             |
                        |           v             |
                        | VOLUME DESCRIPTOR START |<-------MAGIC_SIGNATURE here
                        |           ^             |
                        |           |             |
                        |           v             |
                        |  VOLUME DESCRIPTOR END  |<-------MAGIC_SIGNATURE here again
                        |           ^             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |     more disk sectors   |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           |             |
                        |           v             |
                        ---------------------------
*/

#ifndef XDVDFS
#define XDVDFS
#include <stdint.h>
#include <stdbool.h>

//these macros are used to navigate the xiso and xbox file systems
#define MAGIC_SIGNATURE "MICROSOFT*XBOX*MEDIA"
#define MAGIC_SIGNATURE_LENGTH 20
#define VOLUME_DESCRIPTOR_PADDING 1992
#define SECTOR_SIZE 2048
#define VOLUME_DESCRIPTOR_SECTOR 32
#define MAX_FILE_PATH_LENGTH 250
#define MAX_XBOX_FILENAME_LENGTH 42

//directory entry flags, these describe types of files or folders on the xiso
#define READ_ONLY_FILE_FLAG 0x01
#define HIDDEN_FILE_FLAG 0x02
#define SYSTEM_FILE_FLAG 0x04
#define IS_DIRECTORY_FLAG 0x10
#define IS_ARCHIVE_FLAG 0x20
#define NORMAL_FLAG 0x80

extern uint8_t * global_offset; //this is where the xdvd partition will actually start, and should be used as the memory address of the first sector of the logical disk

#pragma pack(push, 1) //we need this so the compiler doesn't put gaps or misalign the bytes of the struct
typedef struct {
    /*
    This appears only once on the entire xiso, and is used to determine where the beginning of the file hierarhcy is to start traversing and find our files.
    */
    char magic_signature[MAGIC_SIGNATURE_LENGTH]; //this is always MAGIC_SIGNATURE
    uint32_t root_directory_sector;
    uint32_t root_directory_size;
    uint64_t file_time; //we don't need this
    uint8_t padding[VOLUME_DESCRIPTOR_PADDING];
    char magic_signature_end[MAGIC_SIGNATURE_LENGTH]; //also always MAGIC_SIGNATURE
} volume_descriptor;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    /*
    Every directory table is full of directory entries, spanning the entire (or multiple) disk sectors. These tell us where files (or other directory tables if it points to 
    a folder) are located within the xiso, how big they are, and their filenames.

    As per https://multimedia.cx/xdvdfs.html: "If a sub-directory is empty, the sector and file size of its entry in its parent will both be set to 0."
    */
    uint16_t left_entry_offset;
    uint16_t right_entry_offset;
    uint32_t file_start_sector;
    uint32_t file_size;
    uint8_t flags;
    uint8_t filename_length;
    char filename[];
    //padding would go here if we needed it
} directory_entry;
#pragma pack(pop)

volume_descriptor * find_magic_signature(uint8_t * iso, size_t iso_file_size);
bool entry_has_value(directory_entry * current_entry);
#endif