#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* strprbrk(const char* s, const char* charset)
{
    const char* latestMatch = NULL;
    for (; s = strpbrk(s, charset), s != NULL; latestMatch = s++) {
    }
    return latestMatch;
}

char* readFile(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        printf("Error opening file: %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        printf("Error allocating memory for file: %s\n", path);
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, fileSize, file) != fileSize) {
        printf("Error reading file: %s\n", path);
        fclose(file);
        free(buffer);
        return NULL;
    }

    buffer[fileSize] = '\0';

    fclose(file);

    return buffer;
}

void getDirectoryPath(const char* sourcePath, char* basePath)
{
    if (sourcePath[1] != ':' && sourcePath[0] != '\\' && sourcePath[0] != '/') {
        basePath[0] = '.';
        basePath[1] = '/';
    }

    const char* lastSlash = strprbrk(sourcePath, "\\/");
    if (lastSlash) {
        if (lastSlash == sourcePath) {
            basePath[0] = sourcePath[0];
            basePath[1] = '\0';
        } else {
            char* basePathPtr = basePath;
            if ((sourcePath[1] != ':') && (sourcePath[0] != '\\') && (sourcePath[0] != '/'))
                basePathPtr += 2;
            memcpy(basePathPtr, sourcePath, strlen(sourcePath) - (strlen(lastSlash) - 1));
            basePath[strlen(sourcePath) - strlen(lastSlash) + (((sourcePath[1] != ':') && (sourcePath[0] != '\\') && (sourcePath[0] != '/')) ? 2 : 0)] = '\0';
        }
    }
}
