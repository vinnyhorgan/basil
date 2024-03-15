#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* strprbrk(const char* s, const char* charset)
{
    const char* latestMatch = NULL;
    for (; s = strpbrk(s, charset), s != NULL; latestMatch = s++) { }
    return latestMatch;
}

char* readFile(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        printf("Error opening file: %s.\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        printf("Error allocating memory for file: %s.\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead < fileSize) {
        printf("Error reading file: %s.\n", path);
        fclose(file);
        free(buffer);
        return NULL;
    }

    buffer[bytesRead] = '\0';

    fclose(file);

    return buffer;
}

const char* getDirectoryPath(const char* filePath)
{
    const char* lastSlash = NULL;
    static char dirPath[MAX_PATH_LENGTH] = { 0 };
    memset(dirPath, 0, MAX_PATH_LENGTH);

    if (filePath[1] != ':' && filePath[0] != '\\' && filePath[0] != '/') {
        dirPath[0] = '.';
        dirPath[1] = '/';
    }

    lastSlash = strprbrk(filePath, "\\/");
    if (lastSlash) {
        if (lastSlash == filePath) {
            dirPath[0] = filePath[0];
            dirPath[1] = '\0';
        } else {
            char* dirPathPtr = dirPath;
            if ((filePath[1] != ':') && (filePath[0] != '\\') && (filePath[0] != '/'))
                dirPathPtr += 2;
            memcpy(dirPathPtr, filePath, strlen(filePath) - (strlen(lastSlash) - 1));
            dirPath[strlen(filePath) - strlen(lastSlash) + (((filePath[1] != ':') && (filePath[0] != '\\') && (filePath[0] != '/')) ? 2 : 0)] = '\0';
        }
    }

    return dirPath;
}
