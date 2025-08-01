#include "Utils.h"
#include <cstdint>


bool FileExists(string filename) {
    std::ifstream f(filename);
    return f.good();
}

bool is_directory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
        return false;
    return (info.st_mode & S_IFMT) == S_IFDIR;
}

uint32_t Hash(unsigned char buffer[], const int &bufferIdx, const int SIZE) {
    uint32_t h = 0;

    for(int i = bufferIdx; i < bufferIdx + MIN_MATCH; i++)
        h = (h * BASE + buffer[i % SIZE]) %  MOD;

    return h;
}

void ReadDataToCompress(unsigned char bytes[], int &bytesLength, int &bytesLengthIdx, ifstream &file) {
    file.read(reinterpret_cast<char*>(bytes), READ_BUFFER_SIZE);

    bytesLength = static_cast<int>(file.gcount());
    bytesLengthIdx = 0;
}

int ReadDataFromBuffer(unsigned char buffer[], int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile, uint8_t size) {
    int ans = 0;
    if(bufferByteIdx == 0) {
        if(size == 8) {
            ans = buffer[bufferIdx];
            bufferIdx++;
        }
        else {
            ans = buffer[bufferIdx] >> (8 - size);
            bufferByteIdx = size;
        }

        size = 0;
    }
    else {
        if(size <= 8 - bufferByteIdx) {
            ans = (buffer[bufferIdx] << bufferByteIdx) >> (8 - size);

            if(size + bufferByteIdx == 8) {
                bufferByteIdx = 0;
                bufferIdx++;
            }
            else
                bufferByteIdx += size;

            size = 0;
        }
        else {
            uint8_t dim = 8 - bufferByteIdx;
            size -= dim;
            ans = (buffer[bufferIdx] << bufferByteIdx) >> bufferByteIdx;
            bufferByteIdx = 0;
            bufferIdx++;
        }
    }

    if(bufferIdx == bufferSize) {
        inputFile.read(reinterpret_cast<char*>(buffer), READ_BUFFER_SIZE);
        bufferIdx = 0;
        bufferSize = static_cast<int>(inputFile.gcount());
    }

    if(size > 0) {
        ans = (ans << size) | (buffer[bufferIdx] >> (8 - size));
        bufferByteIdx = size;
    }

    return ans;
}

int ReadDataFromBufferBig(unsigned char buffer[], int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile, uint8_t size) {
    int ans = 0;
    while(size >= 8) {
        ans = (ans << 8) | (ReadDataFromBuffer(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile) & 255);

        size -= 8;
    }

    if(size > 0) {
        ans = (ans << size) | (ReadDataFromBuffer(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile, size) & ((1 << size) - 1));
    }

    return ans;
}

LZ77 ReadTokenFromBuffer(unsigned char buffer[],  int &bufferIdx, int &bufferByteIdx, int &bufferSize, ifstream &inputFile) {
    LZ77 token;
    token.character = ReadDataFromBuffer(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile);
    token.length = ReadDataFromBufferBig(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile, 9);

    if(token.length > 0) {
        token.offset = ReadDataFromBufferBig(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile, 16);
    }
    else
        token.offset = 0;

    return token;
}

void WriteToBuffer(ofstream &file, const unsigned char &byte, uint8_t size) {
    unsigned char toWrite;
    if(size != 8)
        toWrite = ((1 << size) - 1) & byte;
    else
        toWrite = byte;
    
    if(byteIndex == 0) {
        writeBuffer[writeBufferIndex] = toWrite;
        if(size == 8)
            writeBufferIndex++;
        else
            byteIndex = size;
         
        size = 0;
    }
    else {
        if(size <= 8 - byteIndex) {
            writeBuffer[writeBufferIndex] = (writeBuffer[writeBufferIndex] << size) | toWrite;

            if(byteIndex + size == 8) {
                writeBufferIndex++;
                byteIndex = 0;
            }
            else
                byteIndex += size;

            size = 0;
        }
        else {
            uint8_t dim = 8 - byteIndex;
            size -= dim;
            writeBuffer[writeBufferIndex] = (writeBuffer[writeBufferIndex] << dim) | (toWrite >> size);
            writeBufferIndex++;
            byteIndex = 0;
        }
    }

    if(writeBufferIndex == WRITE_BUFFER_SIZE) {
        file.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex);

        writeBufferIndex = 0;
    }

    if(size != 0) {
        writeBuffer[writeBufferIndex] = toWrite & ((1 << size) - 1);
        byteIndex = size;
    }
}

void WriteToBufferBig(ofstream &outFile, const long long &byte, uint8_t size) {
    long long toWrite = byte;
    toWrite = (toWrite << (64 - size)) >> (64 - size);

    while(size >= 8) {
        WriteToBuffer(outFile, (toWrite >> (size - 8)));

        toWrite = (toWrite << (64 - size)) >> (64 - size);
        size -= 8;
    }

    if(size > 0)
        WriteToBuffer(outFile, toWrite, size);
}

void WriteToBuffer_2(ofstream &file, const unsigned char &byte, uint8_t size) {
    unsigned char toWrite;
    if(size != 8)
        toWrite = ((1 << size) - 1) & byte;
    else
        toWrite = byte;
    
    if(byteIndex_2 == 0) {
        writeBuffer_2[writeBufferIndex_2] = toWrite;
        if(size == 8)
            writeBufferIndex_2++;
        else
            byteIndex_2 = size;
        
        size = 0;
    }
    else {
        if(size <= 8 - byteIndex_2) {
            writeBuffer_2[writeBufferIndex_2] = (writeBuffer_2[writeBufferIndex_2] << size) | toWrite;

            if(byteIndex_2 + size == 8) {
                writeBufferIndex_2++;
                byteIndex_2 = 0;
            }
            else
                byteIndex_2 += size;

            size = 0;
        }
        else {
            uint8_t dim = 8 - byteIndex_2;
            size -= dim;
            writeBuffer_2[writeBufferIndex_2] = (writeBuffer_2[writeBufferIndex_2] << dim) | (toWrite >> size);
            writeBufferIndex_2++;
            byteIndex_2 = 0;
        }
    }

    if(writeBufferIndex_2 == WRITE_BUFFER_SIZE) {
        file.write(reinterpret_cast<char*>(writeBuffer_2), writeBufferIndex_2);

        writeBufferIndex_2 = 0;
    }

    if(size != 0) {
        writeBuffer_2[writeBufferIndex_2] = toWrite & ((1 << size) - 1);
        byteIndex_2 = size;
    }
}

void WriteToBufferBig_2(ofstream &outFile, const long long &byte, uint8_t size) {
    long long toWrite = byte;
    toWrite = (toWrite << (64 - size)) >> (64 - size);

    while(size >= 8) {
        WriteToBuffer_2(outFile, (toWrite >> (size - 8)));

        toWrite = (toWrite << (64 - size)) >> (64 - size);
        size -= 8;
    }

    if(size > 0)
        WriteToBuffer_2(outFile, toWrite, size);
}

void WriteTokenToFile(LZ77 &token, unsigned char compressedBytes[], int &compressedBytesIdx, ofstream &file) {
    if (token.length == 0 && token.offset == 0) {
        compressedBytes[compressedBytesIdx] = token.character;
        compressedBytesIdx++;

        if(compressedBytesIdx == WINDOW_SIZE) {
            file.write(reinterpret_cast<char*>(compressedBytes), WINDOW_SIZE);
        }
        else if(compressedBytesIdx == WINDOW_SIZE * 2) {
            file.write(reinterpret_cast<char*>(compressedBytes + WINDOW_SIZE), WINDOW_SIZE);

            compressedBytesIdx = 0;
        }
    } else {
        int start = compressedBytesIdx - token.offset;

        for (int i = 0; i < token.length; ++i) {
            if(start + i < 0)
                compressedBytes[compressedBytesIdx] = compressedBytes[start + i + 2 * WINDOW_SIZE];
            else
                compressedBytes[compressedBytesIdx] = compressedBytes[start + i];
            compressedBytesIdx++;

            if(compressedBytesIdx == WINDOW_SIZE) {
                file.write(reinterpret_cast<char*>(compressedBytes), WINDOW_SIZE);
            }
            else if(compressedBytesIdx == WINDOW_SIZE * 2) {
                file.write(reinterpret_cast<char*>(compressedBytes + WINDOW_SIZE), WINDOW_SIZE);
                compressedBytesIdx = 0;
            }
        }
    }

    token = {0, 0, 0};
}

void ReadDataToDecompress(string &s, ifstream &file, int &binaryLength, int &binaryPos) {
    if(binaryPos > static_cast<int>(s.length())) {
        cout << "Error at reading for decompressing: binaryPos > binary.length() (" << binaryPos << " > " << s.length() << ")" << endl;
        exit(1);
    }
    s.erase(0, binaryPos);

    unsigned char bytes[READ_BUFFER_SIZE];
    file.read(reinterpret_cast<char*>(bytes), READ_BUFFER_SIZE);

    streamsize readLength = file.gcount();
    if(readLength == 0) {
        binaryLength -= binaryPos;
        binaryPos = 0;
        return;
    }

    vector<char> bitBuffer(readLength * 8);
    char* p = bitBuffer.data();

    for (size_t i = 0; i < readLength; ++i) {
        const char* const bits = BYTE_TO_BITS[bytes[i]];
        memcpy(p, bits, 8);
        p += 8;
    }

    s.append(bitBuffer.data(), readLength * 8);
    binaryLength = binaryLength - binaryPos + static_cast<int>(readLength) * 8;
    binaryPos = 0;
}

vector<pair<string, bool>> GetCompressedFilesWithFile(ifstream &file) {
    if(!file)
        return {};

    string binary = "";
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;
    vector<pair<string, bool>> addresses; // 1 - file; 0 - folder

    uint8_t fileNameLen = 0;

    if(binaryLength < 8)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);
    
    if(binaryLength < 8) {
        archive_corrupted_help = true;
        return {};
    }
    
    while(binaryPos < 8)
        fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');
    
    string folderAddress = "";

    while(fileNameLen != 0 || folderAddress != "") {
        if(fileNameLen == 0) {
            int i = static_cast<int>(folderAddress.length()) - 1;
            while(folderAddress != "" && folderAddress.back() != '/')
                folderAddress.pop_back();
            if(static_cast<int>(folderAddress.length()) == 0) {
                archive_corrupted_help = true;
                return {};
            }
            folderAddress.pop_back();

            addresses.push_back({"", 0});
            
            if(8 + binaryPos >= binaryLength)
                ReadDataToDecompress(binary, file, binaryLength, binaryPos);

            if(8 + binaryPos >= binaryLength) {
                archive_corrupted_help = true;
                return {};
            }
        
            fileNameLen = 0;
            for(int i = 0; i < 8; i++)
                fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');

            continue;
        }

        string newFilePath = folderAddress;
        if(newFilePath[newFilePath.length() - 1] != '/')
            newFilePath += "/";

        if(binaryPos + 8 * fileNameLen + 1 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);

        if(binaryPos + 8 * fileNameLen + 1 >= binaryLength) {
            archive_corrupted_help = true;
            return {};
        }

        for(int i = 0; i < fileNameLen; i++) {
            char tempVal = 0;
            for(int j = 0; j < 8; j++)
                tempVal = tempVal * 2 + (binary[binaryPos + j] == '1');
        
            binaryPos += 8;
            newFilePath += tempVal;
        }

        if(binary[binaryPos] == '0')
            folderAddress = newFilePath;
        addresses.push_back({newFilePath, binary[binaryPos++] == '1'});

        if(8 + binaryPos >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);

        if(8 + binaryPos >= binaryLength) {
            archive_corrupted_help = true;
            return {};
        }
        
        fileNameLen = 0;
        for(int i = 0; i < 8; i++)
            fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');
    }

    bytesFromTheLastRead = binary.substr(binaryPos);

    return addresses;
}

vector<pair<string, bool>> GetCompressedFiles(const string &compressedFileAddress) {
    ifstream file(compressedFileAddress, ios::binary);

    if(!file) {
        archive_corrupted_help = true;

        return {};
    }

    vector<pair<string, bool>> addresses; // 1 - file; 0 - folder

    addresses = GetCompressedFilesWithFile(file);

    file.close();

    return addresses;
}

int CompareBinaryFiles(const string& file1, const string& file2) {
    ifstream f1(file1, ios::binary);
    ifstream f2(file2, ios::binary);

    if (!f1.is_open() || !f2.is_open()) {
        cerr << "Error opening files." << endl;
        return 0;
    }

    size_t pos = 0;
    char byte1, byte2;

    while (f1.get(byte1) && f2.get(byte2)) {
        if (byte1 != byte2) {
            cout << endl << "Files differ at byte position: " << dec << pos << endl;
            cout << "File1: 0x" << hex << (static_cast<unsigned int>(static_cast<unsigned char>(byte1))) << endl;
            cout << "File2: 0x" << hex << (static_cast<unsigned int>(static_cast<unsigned char>(byte2))) << dec << endl;
            return 0;
        }
        ++pos;
    }

    if (f1.get(byte1)) {
        cout << "File1 is longer. Extra byte at position: " << pos << endl;
    } else if (f2.get(byte2)) {
        cout << "File2 is longer. Extra byte at position: " << pos << endl;
    } else {
        cout << "Files are identical." << endl;
        return 1;
    }

    return 0;
}