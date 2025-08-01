#pragma once

#include <iostream>
#include <fstream>

#include <cstring>
#include <string>

#include <functional>
#include <algorithm>

#include <vector>
#include <deque>
#include <unordered_map>
#include <queue>

#include <sys/stat.h>
#include <direct.h>
#include <windows.h>
#include <cstdint>
#include <filesystem>

using namespace std;

struct LZ77 {
    uint16_t offset;
    uint16_t length;
    unsigned char character;
};

struct HuffmanNode {
    int value;
    uint64_t frequency;
    HuffmanNode *left, *right;

    HuffmanNode(int val, uint64_t freq, HuffmanNode* l = nullptr, HuffmanNode *r = nullptr) : value(val), frequency(freq), left(l), right(r) {}
};

constexpr int WRITE_BUFFER_SIZE = 4096;
constexpr int READ_BUFFER_SIZE = 4096; //must be at least 4060

constexpr int LOOKAHEAD_SIZE = 258;
constexpr int WINDOW_SIZE = 32768;

constexpr int MIN_MATCH = 3;
constexpr int MOD = 65521;
constexpr int BASE = 256;

extern string bytesFromTheLastRead;
extern string tempFileName;
extern unsigned char writeBuffer[WRITE_BUFFER_SIZE], writeBuffer_2[WRITE_BUFFER_SIZE];
extern int writeBufferIndex, byteIndex, writeBufferIndex_2, byteIndex_2;
extern const char* BYTE_TO_BITS[256];

extern bool archive_corrupted_help;