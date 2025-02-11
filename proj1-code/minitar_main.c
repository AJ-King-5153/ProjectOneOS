#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    file_list_t files;
    file_list_init(&files);



    // TODO: Parse command-line arguments and invoke functions from 'minitar.h'
    // to execute archive operations

    char operation = argv[1][1];
    if (argv[1][0] != '-') {
        perror("Improper command line arguments");
        return 1;
    }
    if (strcmp(argv[2], "-f") != 0) {
        perror("Improper command line arguments");
        return 1;
    }

    char *archiveName = argv[3];

    for (int i = 4; i < argc; i++) {
        file_list_add(&files, argv[i]);
    }

    int result = 0;
    switch(operation) {
        case 'c':
            result = create_archive(archiveName, &files);
            break;
        case 'a':
            result = append_files_to_archive(archiveName, &files);
            break;
        case 't':
            result = get_archive_file_list(archiveName, &files);
            break;
        case 'u':
            //result = -1;//our own update function
            break;
        case 'x':
            //result = minitar_extract(archiveName);
            break;
        default:
            //perror("Improper command line arguments");
            return -1;

    }

    file_list_clear(&files);
    return result;
}
