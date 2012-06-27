// TRXUtil by Ari Weinstein. Portions from the Embedded Xinu project.
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

static unsigned long *crc32 = NULL;

// TRX magic (HDR0)
#define TRX_MAGIC 0x30524448

// Structures
struct TRXHeader {
    unsigned int magic;                 
    unsigned int len;                   
    unsigned int crc;                   
    unsigned int flags_vers;             
    unsigned int offsets[3];            
};

int readFile(const char *filename, char **result) { 
    *result = NULL;
	FILE *f = fopen(filename, "rb");
	if (f == NULL) return -1;
	fseek(f, 0, SEEK_END);
    int size = ftell(f);
	fseek(f, 0, SEEK_SET);
	*result = (char *)malloc(size+1);
	if (size != fread(*result, sizeof(char), size, f)) { 
		free(*result);
		return -1;
	} 
	fclose(f);
	(*result)[size] = 0x00;
	return size;
}

int writeFile(const char *filename, char *data, int size) {
    FILE *f = fopen(filename, "wb");
    int written = fwrite(data, sizeof(char), size, f);
    fclose(f);
    return written;
}

unsigned int calcCRC(char *data, unsigned int length) {
    unsigned int crc = 0xFFFFFFFF;
    if (!crc32) {
        unsigned long crc, poly = 0xEDB88320L;
        int n, bit;
        if (crc32) return;
        crc32 = (unsigned long *)malloc(256 * sizeof(unsigned long));
        if (crc32 == NULL) return;

        for (n = 0; n < 256; n++) {
            crc = (unsigned long)n;
            for (bit = 0; bit < 8; bit++) {
                crc = (crc & 1) ? (poly ^ (crc >> 1)) : (crc >> 1);
            }
            crc32[n] = crc;
        }
    }
    for (; length; length--, data++) {
        crc = crc32[(crc ^ *data) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

int writeTRX(int newHeader, char *filename, struct TRXHeader *expectedHeader, struct TRXHeader **currentHeaderPointer) {
    struct TRXHeader *currentHeader = *currentHeaderPointer;
    int headerLength = sizeof(struct TRXHeader);
    if (newHeader) {
        // Write new header
        expectedHeader->len += headerLength;
        char *newData = malloc(expectedHeader->len+headerLength);
        memcpy(newData, expectedHeader, headerLength);
        memcpy(newData+headerLength, *currentHeaderPointer, expectedHeader->len);
        *currentHeaderPointer = (struct TRXHeader *)newData;
        currentHeader = *currentHeaderPointer;
    } else {
        // Overwrite existing header
        memcpy(currentHeader, expectedHeader, headerLength);
    }
    
    // Recalculate checksum
    currentHeader->crc = calcCRC((char *)(&currentHeader->flags_vers), currentHeader->len-3*sizeof(int));
    expectedHeader->crc = currentHeader->crc;
    
    // Write out revised file
    printf("Writing revised binary with TRX header to %s... ", filename);
    writeFile(filename, (char *)currentHeader, currentHeader->len);
    printf("done!\n");
}

int validateTRX(int size, char *data, char *filename) {
    int retVal = 1;

    // Append .trx to filename in case we write a new one
    char *outFilename = malloc(strlen(filename)+4);
    sprintf(outFilename, "%s.trx", filename);
    
    // Cast the beginning of the file as a TRX header, and fill in expected header values
    struct TRXHeader *expectedHeader = malloc(sizeof(struct TRXHeader));
    struct TRXHeader *currentHeader = (struct TRXHeader *)data;
    expectedHeader->magic = TRX_MAGIC;
    expectedHeader->len = size;
    if (size > sizeof(struct TRXHeader)) expectedHeader->crc = calcCRC((char *)(&currentHeader->flags_vers), expectedHeader->len-3*sizeof(int));
    expectedHeader->flags_vers = 0x10001; // Why? I don't know.
    expectedHeader->offsets[0] = 0x1C; // Again, no idea.
    expectedHeader->offsets[1] = 0x00;
    expectedHeader->offsets[2] = 0x00;

    // Validate magic number
    if (currentHeader->magic == expectedHeader->magic) {
        printf("TRX header found: %08X\n", currentHeader->magic);
    } else {
        printf("TRX header not found.\n");
        printf("\tMagic expected: %08X\t Magic found: %08X\n", expectedHeader->magic, currentHeader->magic);
        writeTRX(1, outFilename, expectedHeader, &currentHeader);
        retVal = -1;
    }
    
   // Validate file size is more than TRX header
    if (currentHeader->len < sizeof(struct TRXHeader)) {
        printf("Error: TRX file size is too small\n");
        printf("\tFile size is smaller than TRX header size (28 bytes)\n");
        writeTRX(0, outFilename, expectedHeader, &currentHeader);
        retVal = -2;
    }

    // Validate file length
    if (currentHeader->len == expectedHeader->len) {
        printf("TRX file length: %d\n", currentHeader->len);
    } else {
        printf("Error: Expected and actual file length do not match\n");
        printf("\tLength expected: %d\tLength found: %d\n", expectedHeader->len, currentHeader->len);
        writeTRX(0, outFilename, expectedHeader, &currentHeader);
        retVal = -2;
    }

    // Validate checksum
    if (retVal) {
        if (currentHeader->crc == expectedHeader->crc) {
            printf("TRX checksum is correct: %08X\n", expectedHeader->crc);
        } else {
            printf("Error: Bad TRX checksum\n");
            printf("\tExpected: %08X\tFound: %08X\n", expectedHeader->crc, currentHeader->crc);
            writeTRX(0, outFilename, expectedHeader, &currentHeader);
            retVal = -2;
        }
    }
    
    free(outFilename);
    free(data);
    if (retVal == -1) free(currentHeader); // If we created a brand new header (because there was none before), free it
    if (retVal != -2) free(expectedHeader); // If we re-used the original header (because there was one before, but it had an issue), don't double-free it
    return retVal;
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        char *data, *filename = argv[1];
        int size = readFile(filename, &data);
        int result = validateTRX(size, data, filename);
        if (result == 1) printf("TRX header is valid!\n");
	} else {
        printf("TRXUtil verifies the TRX header of a binary file. If it is incorrect or missing, a new file will be written to <oldfilename>.trx with a complete TRX header. Written by Ari Weinstein on 6/26/12.\nUsage: %s <file>\n\nExample:\%s WR1043ND_firmware.bin\n", argv[0], argv[0]);
	}
	return 0;
}