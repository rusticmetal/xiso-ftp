#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "xdvdfs.h"

uint8_t * global_offset; //start of the actual xdvd partition

volume_descriptor * find_magic_signature(uint8_t * iso, size_t iso_file_size) {
    /*
    Scans through iso_file_size bytes of the iso file to find a correctly formatted MAGIC_SIGNATURE.
    If successful, returns a pointer to where the volume descriptor starts, and calculates and sets the global_offset of the XDVD partition.
    Returns NULL if the signature could not be found.
    */
    for (off_t i = 0; i <= iso_file_size - MAGIC_SIGNATURE_LENGTH; i++) {
        if ((memcmp(iso + i, MAGIC_SIGNATURE, MAGIC_SIGNATURE_LENGTH) == 0) && //check for the existence of both signatures
            (memcmp(iso + i + sizeof(volume_descriptor) - MAGIC_SIGNATURE_LENGTH, MAGIC_SIGNATURE, MAGIC_SIGNATURE_LENGTH) == 0)) { 
            volume_descriptor * vol_descriptor = (volume_descriptor *) (iso + i); //with this we can calculate where the partition data is on the iso file
            global_offset = iso + i - (VOLUME_DESCRIPTOR_SECTOR * SECTOR_SIZE); //this is because the iso file and xdvdfs partition are not aligned (there is empty space)
            printf("XDVD signature detected. This is likely a valid game xiso.\n");
            return vol_descriptor;
        }
    }
}

bool entry_has_value(directory_entry * current_entry) {
    {
    /*
    Returns true if the current entry is a leaf node with a value (e.g. is a file or directory, not a travel node/empty entry), else returns false.
    */
    return (current_entry->filename_length > 0  && 
        (current_entry->file_start_sector != 0 || current_entry->flags & IS_DIRECTORY_FLAG));
    }
}