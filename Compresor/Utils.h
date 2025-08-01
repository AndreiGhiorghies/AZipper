#pragma once

#include "Globals.h"
#include <cstdint>

bool FileExists(string filename);

bool is_directory(const std::string& path);

uint32_t Hash(unsigned char buffer[], const int &bufferIdx, const int SIZE = WINDOW_SIZE);

void ReadDataToCompress(unsigned char bytes[], int &bytesLength, int &bytesLengthIdx, ifstream &file);

int ReadDataFromBuffer(unsigned char buffer[], int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile, uint8_t size = 8);
int ReadDataFromBufferBig(unsigned char buffer[], int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile, uint8_t size = 32);
LZ77 ReadTokenFromBuffer(unsigned char buffer[],  int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile);

void WriteToBuffer(ofstream &file, const unsigned char &byte, uint8_t size = 8);
void WriteToBufferBig(ofstream &outFile, const long long &byte, uint8_t size = 64);

void WriteToBuffer_2(ofstream &file, const unsigned char &byte, uint8_t size = 8);
void WriteToBufferBig_2(ofstream &outFile, const long long &byte, uint8_t size = 64);

void WriteTokenToFile(LZ77 &token, unsigned char compressedBytes[], int &compressedBytesIdx, ofstream &file);

void ReadDataToDecompress(string &s, ifstream &file, int &binaryLength, int &binaryPos);

vector<pair<string, bool>> GetCompressedFilesWithFile(ifstream &file);
vector<pair<string, bool>> GetCompressedFiles(const string &compressedFileAddress);

int CompareBinaryFiles(const string& file1, const string& file2);