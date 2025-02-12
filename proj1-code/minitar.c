#include "minitar.h"

#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 128
#define BLOCK_SIZE 512

// Constants for tar compatibility information
#define MAGIC "ustar"

// Constants to represent different file types
// We'll only use regular files in this project
#define REGTYPE '0'
#define DIRTYPE '5'

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *) header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100);    // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o",
             stat_buf.st_mode & 07777);    // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid);    // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid);       // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32);    // Owner name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid);    // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid);        // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32);    // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o",
             (unsigned) stat_buf.st_size);    // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o",
             (unsigned) stat_buf.st_mtime);    // Modification time, 0-padded octal
    header->typeflag = REGTYPE;                // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6);          // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2);          // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o",
             major(stat_buf.st_dev));    // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o",
             minor(stat_buf.st_dev));    // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];

    struct stat stat_buf;
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    off_t file_size = stat_buf.st_size;
    if (nbytes > file_size) {
        file_size = 0;
    } else {
        file_size -= nbytes;
    }

    if (truncate(file_name, file_size) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

int create_archive(const char *archive_name, const file_list_t *files) {
    char err_msg[MAX_MSG_LEN];
    FILE *minitar = fopen(archive_name, "w");
    // error check file creation of the archive
    if (minitar == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file for writing %s\n", archive_name);
        perror(err_msg);
        return -1;
    }
    node_t *curr_file = files->head;
    // malloc space for the headers of the files to be archived
    tar_header *curr_header = malloc(sizeof(tar_header));
    while (curr_file != NULL) {
        // use function to fill header, then error check
        int tar_error = fill_tar_header(curr_header, curr_file->name);
        if (tar_error != 0) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to create minitar header for %s\n",
                     curr_file->name);
            perror(err_msg);
            return -1;
        }
        // write the file header to the archive file
        size_t header_bytes_written = fwrite(curr_header, sizeof(tar_header), 1, minitar);
        // should only write 1 element
        if (header_bytes_written != 1) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write minitar header for %s\n",
                     curr_file->name);
            perror(err_msg);
            return -1;
        }
        // open the current file name in reading mode
        FILE *fp_curr = fopen(curr_file->name, "r");
        if (fp_curr == NULL) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file for reading: %s\n",
                     curr_file->name);
            perror(err_msg);
            return -1;
        }
        // initialize variables that will be used in current file reading and minitar writing
        size_t bytes_read;
        size_t bytes_written;
        char stored_data[BLOCK_SIZE];
        // reads 1 byte 512 times from the current file
        while ((bytes_read = fread(stored_data, 1, BLOCK_SIZE, fp_curr)) > 0) {
            // write 1 byte 512 times to minitar
            bytes_written = fwrite(stored_data, 1, bytes_read, minitar);
            if (bytes_written != bytes_read) {
                free(curr_header);
                fclose(minitar);
                fclose(fp_curr);
                snprintf(err_msg, MAX_MSG_LEN,
                         "Bytes lost while reading from %s and writing to %s\n", curr_file->name,
                         archive_name);
                perror(err_msg);
                return -1;
            }
            // if file size isnt a multiple of 512 then w fill the rest with 0's
            if (bytes_read < BLOCK_SIZE) {
                char padding[BLOCK_SIZE] = {0};
                fwrite(padding, 1, BLOCK_SIZE - bytes_read, minitar);
            }
        }
        fclose(fp_curr);
        curr_file = curr_file->next;
    }
    // fill two blocks of 0's to indicate the end of the minitar
    char zeros[BLOCK_SIZE * 2] = {0};
    size_t bytes_written_zeros = fwrite(zeros, 1, sizeof(zeros), minitar);
    if (bytes_written_zeros != sizeof(zeros)) {
        free(curr_header);
        fclose(minitar);
        return -1;
    }
    free(curr_header);
    fclose(minitar);
    return 0;
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    char err_msg[MAX_MSG_LEN];
    // open the archive in read + write mode and error check
    FILE *minitar = fopen(archive_name, "r+");
    if (minitar == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open archive for appending: %s\n", archive_name);
        perror(err_msg);
        return -1;
    }
    // seek to the end of the archive minus the last two 0 header blocks
    tar_header header;
    while (fread(&header, sizeof(header), 1, minitar) == 1) {
        if (header.name[0] == '\0') {  // found the end header
            fseek(minitar, -sizeof(header), SEEK_CUR);
            break;
        }
        // skipping the actual file contents
        int file_size = strtol(header.size, NULL, 8);
        int blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        fseek(minitar, blocks * BLOCK_SIZE, SEEK_CUR);
    }
    // get the first file node from the argument list
    node_t *curr_file = files->head;
    tar_header *curr_header = malloc(sizeof(tar_header));
    while (curr_file != NULL) {
        // fill the tar header and error check
        int tar_error = fill_tar_header(curr_header, curr_file->name);
        if (tar_error != 0) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to create header for %s\n", curr_file->name);
            perror(err_msg);
            return -1;
        }
        // write the file header and error check
        if (fwrite(curr_header, sizeof(tar_header), 1, minitar) != 1) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to write header for %s\n", curr_file->name);
            perror(err_msg);
            return -1;
        }
        FILE *fp = fopen(curr_file->name, "r");
        if (fp == NULL) {
            free(curr_header);
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to open file for reading: %s\n", curr_file->name);
            perror(err_msg);
            return -1;
        }
        // write file block data
        size_t bytes_read;
        char stored_data[BLOCK_SIZE];
        while ((bytes_read = fread(stored_data, 1, BLOCK_SIZE, fp)) > 0) {
            // bytes read shall always = bytes written
            if (fwrite(stored_data, 1, bytes_read, minitar) != bytes_read) {
                free(curr_header);
                fclose(minitar);
                fclose(fp);
                snprintf(err_msg, MAX_MSG_LEN, "Error writing file data for %s\n", curr_file->name);
                perror(err_msg);
                return -1;
            }
            // pad to 512 bytes
            if (bytes_read < BLOCK_SIZE) {
                char padding[BLOCK_SIZE] = {0};
                fwrite(padding, 1, BLOCK_SIZE - bytes_read, minitar);
            }
        }
        fclose(fp);
        curr_file = curr_file->next;
    }
    // fill two blocks of 0's as footer
    char zeros[BLOCK_SIZE * 2] = {0};
    fwrite(zeros, 1, sizeof(zeros), minitar);
    free(curr_header);
    fclose(minitar);
    return 0;
}


// helper function for checking if the block is all zeros
int is_all_zeros(const char *block) {
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return 0;// not a zero block
        }
    }
    return 1;// is a zero block
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    char err_msg[MAX_MSG_LEN];
    FILE *minitar = fopen(archive_name, "r");
    // error check file open of the archive
    if (minitar == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file for reading %s\n", archive_name);
        perror(err_msg);
        return -1;
    }
    // initialize header var to read headers into
    tar_header header;
    // initialize block to read the potential second zero block in
    char block[BLOCK_SIZE] = {0};
    while (1) {
        // resets header to 0 before reading in
        memset(&header, 0, sizeof(tar_header));
        // read the header block in
        size_t bytes_read = fread(&header, 1, BLOCK_SIZE, minitar);
        if (bytes_read != BLOCK_SIZE) {
            fclose(minitar);
            snprintf(err_msg, MAX_MSG_LEN, "Failed to read header from file %s\n", archive_name);
            perror(err_msg);
            return -1;
        }
        // check if block read is all zeros, if it is check next
        if (is_all_zeros((const char *) &header)) {
            // if in here, block just read is all zeros, need to check next block
            bytes_read = fread(block, 1, BLOCK_SIZE, minitar);
            if (bytes_read == BLOCK_SIZE && is_all_zeros(block)) {
                // two consecutive zero blocks so break from the loop
                break;
            }
        }
        // print file name
        printf("%s\n", header.name); // we need to figure out how to print the file names in main, not in here.
        // convert the octal string to a long
        long file_size = strtol(header.size, NULL, 8);
        // calculate the number of blocks to seek
        // (Block_Size represents the header we read in, file size is the length of the file we are
        // about to skip) by  diving by block size we get the number of blocks to skip, can multiply
        // by 512 to get bytes to seek
        size_t blocks_seek = (file_size + BLOCK_SIZE) / BLOCK_SIZE;
        fseek(minitar, blocks_seek * BLOCK_SIZE, SEEK_CUR);
    }
    fclose(minitar);
    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    // TODO: Not yet implemented
    return 0;
}
