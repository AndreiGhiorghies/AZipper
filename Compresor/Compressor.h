#pragma once

#include <vector>
#include <string>

extern bool &archive_corrupted;

void Compress(const std::vector<std::string> &filesToCompressAddress, const std::string &compressedFileAddress, float &progress);

void Decompress(const std::string &toDecompressFolderAddress, const std::string &compressedFileAddress);

void Decompress(const std::string &toDecompressFolderAddress, const std::string &compressedFileAddress, float &progress);

void Decompress(const std::string &toDecompressFolderAddress, const std::string &compressedFileAddress, std::vector<int> indices);

void Decompress(const std::string &toDecompressFolderAddress, const std::string &compressedFileAddress, std::vector<int> indices, float &progress);

void InsertFile(const std::string &fileToCompress, const std::string &compressedFile, const int &index, float &progress);

void DeleteFiles(const std::string &compressedFile, std::vector<int> indices, float &progress);

void MoveFiles(const std::string &compressedFile, std::vector<int> indices, const int &index, float &progress);

std::vector<std::pair<std::string, bool>> GetCompressedFiles(const std::string &compressedFileAddress);