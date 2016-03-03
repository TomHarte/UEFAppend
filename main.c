//
//  main.c
//  UEFAppend
//
//  Created by Thomas Harte on 02/03/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#pragma mark - stdio extensions

uint16_t fget16(FILE *file)
{
	uint16_t result = fgetc(file);
	result |= fgetc(file) << 8;
	return result;
}

void fput16(FILE *file, uint16_t value)
{
	fputc(value & 0xff, file);
	fputc(value >> 8, file);
}

void fput32(FILE *file, uint16_t value)
{
	fputc((value >> 0) & 0xff, file);
	fputc((value >> 8) & 0xff, file);
	fputc((value >> 16) & 0xff, file);
	fputc((value >> 24) & 0xff, file);
}

#pragma mark - UEF output routines

void append16BitChunk(FILE *uef, uint16_t chunkID, uint16_t value)
{
	fput16(uef, chunkID);
	fput32(uef, 2);
	fput16(uef, value);
}

void appendCarrierTone(FILE *uef, uint16_t length)
{
	append16BitChunk(uef, 0x0110, length);
}

void appendGap(FILE *uef, uint16_t length)
{
	append16BitChunk(uef, 0x0112, length);
}

void setBaudRate(FILE *uef, uint16_t baudRate)
{
	append16BitChunk(uef, 0x0117, baudRate);
}

void appendData(FILE *uef, uint8_t *data, size_t length)
{
	fput16(uef, 0x0104);
	fput32(uef, length + 3);	// contents will be the supplied data plus the encoding marker

	fputc(8, uef);				// Atom encoding is 8N-1
	fputc('N', uef);
	fputc(-1, uef);

	fwrite(data, 1, length, uef);
}

#pragma mark - data assembly

uint8_t *write8(uint8_t *pointer, uint8_t *checksum, uint8_t value)
{
	*pointer = value;
	*checksum += value;
	return pointer + 1;
}

uint8_t *write16(uint8_t *pointer, uint8_t *checksum, uint16_t value)
{
	pointer = write8(pointer, checksum, value & 0xff);
	return write8(pointer, checksum, value >> 8);
}

#pragma mark - main

int main(int argc, const char * argv[])
{
	if(argc < 3)
	{
		fprintf(stderr, "Usage: uefappend <target.uef> {file.atm}\n");
		return -1;
	}

	const char *const uefHeader = "UEF File!";

	// check that this really is a UEF
	FILE *uefFile = fopen(argv[1], "rb");
	bool isNewFile = true;
	if(uefFile)
	{
		isNewFile = false;
		char testBuffer[10];
		fread(testBuffer, sizeof(char), strlen(uefHeader)+1, uefFile);
		fclose(uefFile);
		if(strcmp(testBuffer, uefHeader))
		{
			fprintf(stderr, "First argument exists but is not a UEF file\n");
			return -2;
		}
	}

	// open for writing this time
	uefFile = fopen(argv[1], "ab");
	if(!uefFile)
	{
		fprintf(stderr, "Could not open UEF file for writing\n");
		return -3;
	}

	// write a header if needed
	if(isNewFile)
	{
		fwrite(uefHeader, sizeof(char), strlen(uefHeader)+1, uefFile);

		// this will be a v0.10 UEF file (fairly arbitrarily)
		fputc(10, uefFile);
		fputc(0, uefFile);

		// output will be at 300 baud
		setBaudRate(uefFile, 300);
	}

	// append ATMs
	for(int argument = 2; argument < argc; argument++)
	{
		// open new ATM
		FILE *atmFile = fopen(argv[argument], "r");
		if(!atmFile)
		{
			fprintf(stderr, "Could not open %s as input; skipping...\n", argv[argument]);
			continue;
		}

		while(1)
		{
			// read the ATM header, obtaining filename, load and execute addresses, and a file length,
			// in case this is actually a TAP file
			char outputName[16];
			uint16_t loadAddress, executeAddress, fileLength;
			fread(outputName, sizeof(char), 16, atmFile);
			outputName[14] = '\0'; // file names may be up to 13 characters long
			loadAddress = fget16(atmFile);
			executeAddress = fget16(atmFile);
			fileLength = fget16(atmFile);

			// if that put us past the end of the file, we must already have finished with it
			if(feof(atmFile)) break;

			// add a 0.5 second gap if this isn't the very start of the UEF file
			if(!isNewFile) appendGap(uefFile, 1200);
			isNewFile = false;

			// add 4.5 seconds of tone
			appendCarrierTone(uefFile, 10800);

			// prepare file-length flags
			uint8_t fileFlags = 0xc0; // not last block, should load, is first block
			uint16_t blockNumber = 0;
			while(fileLength)
			{
				// read next section of data
				uint16_t nextLength = (fileLength > 256) ? 256 : fileLength;
				uint8_t fileContents[257];
				fread(fileContents, 1, nextLength, atmFile);

				// update the remaining length and the flags if this is it
				fileLength -= nextLength;
				if(!fileLength) fileFlags &= ~0x80;

				// zero the checksum
				uint8_t checkSum = 0;

				// create the header
				uint8_t header[26], *headerPointer = header;

				// start with four asterisks
				headerPointer = write8(headerPointer, &checkSum, '*');
				headerPointer = write8(headerPointer, &checkSum, '*');
				headerPointer = write8(headerPointer, &checkSum, '*');
				headerPointer = write8(headerPointer, &checkSum, '*');

				// append the file name
				char *namePointer = outputName;
				while(*namePointer)
				{
					headerPointer = write8(headerPointer, &checkSum, *namePointer);
					namePointer++;
				}
				headerPointer = write8(headerPointer, &checkSum, '\r');

				// write the flags and set no longer the first block
				headerPointer = write8(headerPointer, &checkSum, fileFlags);
				fileFlags |= 0x01;

				// write the block number, then the length, then the execute address, then the load address
				headerPointer = write16(headerPointer, &checkSum, blockNumber);
				headerPointer = write8(headerPointer, &checkSum, nextLength-1);
				headerPointer = write16(headerPointer, &checkSum, executeAddress);
				headerPointer = write16(headerPointer, &checkSum, loadAddress);
				loadAddress += nextLength;	// update the load address for next time around

				// put that all on tape
				appendData(uefFile, header, headerPointer - header);

				// add another one second of carrier tone
				appendCarrierTone(uefFile, 2400);

				// update the checksum for the actual data and write it
				for(int c = 0; c < nextLength; c++)
					checkSum += fileContents[c];
				fileContents[nextLength] = checkSum;
				appendData(uefFile, fileContents, nextLength+1);
			}
		}

		fclose(atmFile);
	}

	fclose(uefFile);

	return 0;
}
