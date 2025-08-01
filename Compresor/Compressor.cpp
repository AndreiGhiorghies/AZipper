#include "Compressor.h"
#include "Globals.h"
#include "Utils.h"

bool &archive_corrupted = archive_corrupted_help;
float *progress, progress_ratio;

//------------------------------------------------ COMPRESSING ALGORITHM ------------------------------------------------------------

HuffmanNode* BuildHuffmanTree(const vector<uint64_t> &freqMap, const int &size) {
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, function<bool(HuffmanNode*, HuffmanNode*)>> heap(
        [](HuffmanNode* a, HuffmanNode* b) { return a->frequency > b->frequency || (a->frequency == b->frequency && a->value > b->value); }
    );

    for(int i = 0; i < size; i++)
        if(freqMap[i] > 0)
            heap.push(new HuffmanNode(i, freqMap[i]));

    if(heap.empty())
        return nullptr;
    
    while(heap.size() > 1) {
        HuffmanNode* left = heap.top();
        heap.pop();

        HuffmanNode* right = heap.top();
        heap.pop();

        heap.push(new HuffmanNode(0, left->frequency + right->frequency, left, right));
    }

    return heap.top();
}

void ExtractCodeLengths(HuffmanNode* root, int depth, vector<int>& codeLengths) {
    if (!root) return;

    if (!root->left && !root->right) {
        codeLengths[root->value] = depth;
        return;
    }

    ExtractCodeLengths(root->left, depth + 1, codeLengths);
    ExtractCodeLengths(root->right, depth + 1, codeLengths);
}

vector<pair<int, int>> GenerateCanonicalHuffmanCodes(const vector<int>& codeLengths, const int &size) {
    vector<pair<int, int>> symbols;

    for (int i = 0; i < codeLengths.size(); i++)
        if (codeLengths[i] > 0) 
            symbols.emplace_back(codeLengths[i], i);
    
    sort(symbols.begin(), symbols.end());

    vector<pair<int, int>> canonicalCodes(size, {-1, -1});
    int code = 0;
    int prevLen = 0;

    for (const auto& i : symbols) {
        if (i.first != prevLen) {
            code <<= (i.first - prevLen);
            prevLen = i.first;
        }

        canonicalCodes[i.second] = {code & ((1 << i.first) - 1), i.first};

        string s = "";
        for(int a = i.first - 1; a >= 0;a--)
            s += ((code >> a) & 1) ? '1' : '0';
        
        ++code;
    }

    canonicalCodes.push_back({static_cast<int>(symbols.size()), -1});

    return canonicalCodes;
}

void GetLZ77Frequency(const string &address, vector<uint64_t> &lengthFreqMap, vector<uint64_t> &offsetFreqMap) {
    ifstream file(address, ios::binary | ios::ate);
    uint64_t fileSize = 0, abs_pos = 0, search_buffer_pos = 0, lookahead_buffer_pos = 0;
    if(!file.is_open()) {
        cerr << "Error opening file: " << address << endl;
        file.close();
        archive_corrupted = true;
        return;
    }

    streampos pos = file.tellg();
    file.seekg(0, ios::beg);
    fileSize = static_cast<uint64_t>(pos);
    
    ofstream outFile(tempFileName, ios::binary);
    if(!outFile) {
        archive_corrupted = true;
        file.close();
        return;
    }
    writeBufferIndex_2 = 0;
    byteIndex_2 = 0;

    unsigned char search_buffer[WINDOW_SIZE], lookahead_buffer[LOOKAHEAD_SIZE];
    unsigned char byte;
    unordered_map<uint32_t, deque<uint64_t>> hashTable;
    hashTable.reserve(MOD);
    uint32_t hash;

    file.read(reinterpret_cast<char*>(search_buffer), WINDOW_SIZE);
    search_buffer_pos = static_cast<uint64_t>(file.gcount());

    file.read(reinterpret_cast<char*>(lookahead_buffer), LOOKAHEAD_SIZE);
    lookahead_buffer_pos = static_cast<uint64_t>(file.gcount());
    abs_pos = search_buffer_pos + lookahead_buffer_pos;

    for(int i = 0; i < search_buffer_pos; i++) {
        if(i > search_buffer_pos - MIN_MATCH) {
            lengthFreqMap[search_buffer[i]]++;

            WriteToBuffer_2(outFile, search_buffer[i]);
            WriteToBufferBig_2(outFile, 0, 9);

            continue;
        }

        LZ77 token = {0, 0, search_buffer[i]};
        uint32_t h = Hash(search_buffer, i);

        if(hashTable.count(h)) {
            for(uint64_t match_pos : hashTable[h]) {
                uint16_t match_length = 0;
                while(i + match_length < search_buffer_pos && match_pos + match_length < i && match_length < LOOKAHEAD_SIZE && search_buffer[i + match_length] == search_buffer[match_pos + match_length])
                    match_length++;
                
                if(match_length > token.length && match_length >= MIN_MATCH) {
                    token.offset = i - match_pos;
                    token.length = match_length;
                    token.character = '-';

                    if(token.offset < MIN_MATCH || token.offset > WINDOW_SIZE) {
                        cerr << "Error: Offset " << token.offset << " is out of bounds at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }
                    if(token.length < MIN_MATCH || token.length > LOOKAHEAD_SIZE) {
                        cerr << "Error: Length " << token.length << " is less than MIN_MATCH at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }
                    if(token.offset < token.length) {
                        cerr << "Error: Offset " << token.offset << " is less than Length " << token.length << " at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }

                    if(token.length == LOOKAHEAD_SIZE)
                        break;
                }
            }
        }

        if(i >= MIN_MATCH)
            hashTable[Hash(search_buffer, i - MIN_MATCH)].push_back(i - MIN_MATCH);

        WriteToBuffer_2(outFile, token.character);
        if(token.length == 0)
            WriteToBufferBig_2(outFile, 0, 9);
        else {
            WriteToBufferBig_2(outFile, token.length, 9);
            WriteToBufferBig_2(outFile, token.offset, 16);
        }

        if(token.length == 0 && token.offset == 0)
            lengthFreqMap[token.character]++;
        else {
            if(token.length <= 10)
                lengthFreqMap[257 + token.length - 3]++;
            else if(token.length <= 18)
                lengthFreqMap[265 + (token.length - 11) / 2]++;
            else if(token.length <= 34)
                lengthFreqMap[269 + (token.length - 19) / 4]++;
            else if(token.length <= 66)
                lengthFreqMap[273 + (token.length - 35) / 8]++;
            else if(token.length <= 130)
                lengthFreqMap[277 + (token.length - 67) / 16]++;
            else if(token.length <= 257)
                lengthFreqMap[281 + (token.length - 131) / 32]++;
            else if(token.length == 258)
                lengthFreqMap[285]++;
            else {
                cerr << "Error: Length " << token.length << " exceeds maximum expected length." << endl;
                archive_corrupted = true;
                file.close();
                outFile.close();
                return;
            }
        }

        if(token.offset == 0) {
            //do nothing
        }
        else if(token.offset <= 4)
            offsetFreqMap[token.offset - 1]++;
        else if(token.offset <= 8)
            offsetFreqMap[4 + (token.offset - 5) / 2]++;
        else if(token.offset <= 16)
            offsetFreqMap[6 + (token.offset - 9) / 4]++;
        else if(token.offset <= 32)
            offsetFreqMap[8 + (token.offset - 17) / 8]++;
        else if(token.offset <= 64)
            offsetFreqMap[10 + (token.offset - 33) / 16]++;
        else if(token.offset <= 128)
            offsetFreqMap[12 + (token.offset - 65) / 32]++;
        else if(token.offset <= 256)
            offsetFreqMap[14 + (token.offset - 129) / 64]++;
        else if(token.offset <= 512)
            offsetFreqMap[16 + (token.offset - 257) / 128]++;
        else if(token.offset <= 1024)
            offsetFreqMap[18 + (token.offset - 513) / 256]++;
        else if(token.offset <= 2048)
            offsetFreqMap[20 + (token.offset - 1025) / 512]++;
        else if(token.offset <= 4096)
            offsetFreqMap[22 + (token.offset - 2049) / 1024]++;
        else if(token.offset <= 8192)
            offsetFreqMap[24 + (token.offset - 4097) / 2048]++;
        else if(token.offset <= 16384)
            offsetFreqMap[26 + (token.offset - 8193) / 4096]++;
        else if(token.offset <= 32768)
            offsetFreqMap[28 + (token.offset - 16385) / 8192]++;
        else {
            cerr << "Error: Offset " << token.offset << " exceeds maximum expected offset." << endl;
            archive_corrupted = true;
            file.close();
            outFile.close();
            return;
        }
        
        for (int j = 1; j < token.length; j++)
            if(i + j < search_buffer_pos - MIN_MATCH)
                hashTable[Hash(search_buffer, i + j - MIN_MATCH)].push_back(i + j - MIN_MATCH);
        
        if(token.length > 0)
            i += (token.length - 1);
    }

    uint64_t mi = lookahead_buffer_pos;
    if(1LL * mi > LOOKAHEAD_SIZE)
        mi = LOOKAHEAD_SIZE;
    mi += fileSize;

    if(lookahead_buffer_pos < LOOKAHEAD_SIZE)
        lookahead_buffer_pos = 0;

    unsigned char bytes[READ_BUFFER_SIZE];
    int bytesLength, bytesLengthIdx;
    ReadDataToCompress(bytes, bytesLength, bytesLengthIdx, file);

    while(abs_pos < mi) {
        LZ77 token = {0, 0, lookahead_buffer[lookahead_buffer_pos % LOOKAHEAD_SIZE]};
        uint32_t h = Hash(lookahead_buffer, lookahead_buffer_pos, LOOKAHEAD_SIZE);

        if(hashTable.count(h)) {
            for(uint64_t match_pos : hashTable[h]) {
                if(search_buffer_pos - match_pos >= WINDOW_SIZE)
                    continue;
                
                uint16_t match_length = 0;
                while(match_pos + match_length < search_buffer_pos && abs_pos + match_length < fileSize && search_buffer[(match_pos + match_length) % WINDOW_SIZE] == lookahead_buffer[(lookahead_buffer_pos + match_length) % LOOKAHEAD_SIZE] && match_length < LOOKAHEAD_SIZE)
                    match_length++;
                
                if(match_length > token.length && match_length >= MIN_MATCH) {
                    token.offset = search_buffer_pos - match_pos;
                    token.length = match_length;
                    token.character = '-';
                    
                    if(token.offset < MIN_MATCH || token.offset > WINDOW_SIZE) {
                        cerr << "Error: Offset " << token.offset << " is out of bounds at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }
                    if(token.length < MIN_MATCH || token.length > LOOKAHEAD_SIZE) {
                        cerr << "Error: Length " << token.length << " is less than MIN_MATCH at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }
                    if(token.offset < token.length) {
                        cerr << "Error: Offset " << token.offset << " is less than Length " << token.length << " at abs_pos = " << abs_pos << endl;
                        archive_corrupted = true;
                        file.close();
                        outFile.close();
                        return;
                    }


                    if(token.length == LOOKAHEAD_SIZE)
                        break;
                }
            }
        }

        WriteToBuffer_2(outFile, token.character);
        if(token.length == 0)
            WriteToBufferBig_2(outFile, 0, 9);
        else {
            WriteToBufferBig_2(outFile, token.length, 9);
            WriteToBufferBig_2(outFile, token.offset, 16);
        }

        if(token.length == 0 && token.offset == 0)
            lengthFreqMap[token.character]++;
        else {
            if(token.length <= 10)
                lengthFreqMap[257 + token.length - 3]++;
            else if(token.length <= 18)
                lengthFreqMap[265 + (token.length - 11) / 2]++;
            else if(token.length <= 34)
                lengthFreqMap[269 + (token.length - 19) / 4]++;
            else if(token.length <= 66)
                lengthFreqMap[273 + (token.length - 35) / 8]++;
            else if(token.length <= 130)
                lengthFreqMap[277 + (token.length - 67) / 16]++;
            else if(token.length <= 257)
                lengthFreqMap[281 + (token.length - 131) / 32]++;
            else if(token.length == 258)
                lengthFreqMap[285]++;
            else {
                cerr << "Error: Length " << token.length << " exceeds maximum expected length." << endl;
                archive_corrupted = true;
                file.close();
                outFile.close();
                return;
            }
        }

        if(token.offset == 0) {
            //do nothing
        }
        else if(token.offset <= 4)
            offsetFreqMap[token.offset - 1]++;
        else if(token.offset <= 8)
            offsetFreqMap[4 + (token.offset - 5) / 2]++;
        else if(token.offset <= 16)
            offsetFreqMap[6 + (token.offset - 9) / 4]++;
        else if(token.offset <= 32)
            offsetFreqMap[8 + (token.offset - 17) / 8]++;
        else if(token.offset <= 64)
            offsetFreqMap[10 + (token.offset - 33) / 16]++;
        else if(token.offset <= 128)
            offsetFreqMap[12 + (token.offset - 65) / 32]++;
        else if(token.offset <= 256)
            offsetFreqMap[14 + (token.offset - 129) / 64]++;
        else if(token.offset <= 512)
            offsetFreqMap[16 + (token.offset - 257) / 128]++;
        else if(token.offset <= 1024)
            offsetFreqMap[18 + (token.offset - 513) / 256]++;
        else if(token.offset <= 2048)
            offsetFreqMap[20 + (token.offset - 1025) / 512]++;
        else if(token.offset <= 4096)
            offsetFreqMap[22 + (token.offset - 2049) / 1024]++;
        else if(token.offset <= 8192)
            offsetFreqMap[24 + (token.offset - 4097) / 2048]++;
        else if(token.offset <= 16384)
            offsetFreqMap[26 + (token.offset - 8193) / 4096]++;
        else if(token.offset <= 32768)
            offsetFreqMap[28 + (token.offset - 16385) / 8192]++;
        else {
            cerr << "Error: Offset " << token.offset << " exceeds maximum expected offset." << endl;
            archive_corrupted = true;
            file.close();
            outFile.close();
            return;
        }

        for (int i = 0; i <= token.length - (token.length != 0); ++i) {       
            search_buffer[search_buffer_pos % WINDOW_SIZE] = lookahead_buffer[lookahead_buffer_pos % LOOKAHEAD_SIZE];

            uint32_t h2 = Hash(search_buffer, (search_buffer_pos - MIN_MATCH) % WINDOW_SIZE);
            hashTable[h2].push_back(search_buffer_pos - MIN_MATCH);

            while (!hashTable[h2].empty() && search_buffer_pos - MIN_MATCH - hashTable[h2].front() >= WINDOW_SIZE)
                hashTable[h2].pop_front();
            
            search_buffer_pos++;

            if(bytesLengthIdx < bytesLength) {
                lookahead_buffer[lookahead_buffer_pos % LOOKAHEAD_SIZE] = bytes[bytesLengthIdx];
                bytesLengthIdx++;
            }
            else {
                ReadDataToCompress(bytes, bytesLength, bytesLengthIdx, file);
                if(bytesLengthIdx < bytesLength) {
                    lookahead_buffer[lookahead_buffer_pos % LOOKAHEAD_SIZE] = bytes[bytesLengthIdx];
                    bytesLengthIdx++;
                }
            }

            lookahead_buffer_pos++;
            abs_pos++;
        }
    }

    //mark the end of this file, token.length > token.offset, which is impossible
    WriteToBuffer_2(outFile, 0);
    WriteToBufferBig_2(outFile, 1, 9);
    WriteToBufferBig_2(outFile, 0, 16);

    if(byteIndex_2 > 0)
        writeBuffer_2[writeBufferIndex_2] <<= (8 - byteIndex_2);
    outFile.write(reinterpret_cast<char*>(writeBuffer_2), writeBufferIndex_2 + (byteIndex_2 > 0));

    lengthFreqMap[256]++;

    file.close();
    outFile.close();
}

void WriteCodesToFile(const string &fileAddress, ofstream &outFile, const vector<pair<int, int>> &lengthCodes, const vector<pair<int, int>> &offsetCodes) {
    uint64_t fileSize = 0, abs_pos = 0, search_buffer_pos = 0, lookahead_buffer_pos = 0;

//-------------------------------------------------LENGTH CODES-----------------------------------------------------------------

    int codesSize = static_cast<int>(lengthCodes[lengthCodes.size() - 1].first);
    if(codesSize >= 512) {
        cerr << "Error: Number of codes exceeds 512, cannot write to file." << endl;
        archive_corrupted = true;
        return;
    }

    WriteToBufferBig(outFile, codesSize, 9);

    for(int i = 0; i < 286; i++) {
        if(lengthCodes[i].second == -1) {
            continue;
        }
        
        WriteToBufferBig(outFile, i, 9);

        if(lengthCodes[i].second > 31) {
            cerr << "Error: Code length exceeds 5 bits for code " << lengthCodes[i].second << " with symbol " << i << endl;
            archive_corrupted = true;
            return;
        }
        
        WriteToBuffer(outFile, lengthCodes[i].second, 5);
    }

//-------------------------------------------------OFFSET CODES-----------------------------------------------------------------

    codesSize = static_cast<int>(offsetCodes[offsetCodes.size() - 1].first);
    if(codesSize >= 32) {
        cerr << "Error: Number of codes exceeds 32, cannot write to file:" << codesSize << endl;
        archive_corrupted = true;
        return;
    }

    WriteToBuffer(outFile, codesSize, 5);

    for(int i = 0; i < 30; i++) {
        if(offsetCodes[i].second == -1)
            continue;
        
        WriteToBuffer(outFile, i, 5);

        if(offsetCodes[i].second >= 16) {
            cerr << "Error: Code length exceeds 4 bits for offset " << i << " with length " << offsetCodes[i].second << endl;
            archive_corrupted = true;
            return;
        }
        
        WriteToBuffer(outFile, offsetCodes[i].second, 4);
    }

//-------------------------------------------------TOKENS-----------------------------------------------------------------

    unsigned char buffer[READ_BUFFER_SIZE];
    int bufferIdx = 0, bufferByteIdx = 0;
    ifstream inputFile(tempFileName, ios::binary);
    if(!inputFile) {
        archive_corrupted = true;
        return;
    }
    
    inputFile.read(reinterpret_cast<char*>(buffer), READ_BUFFER_SIZE);
    int bufferSize = static_cast<int>(inputFile.gcount());

    while(bufferSize > 0) {
        LZ77 token = ReadTokenFromBuffer(buffer, bufferIdx, bufferByteIdx, bufferSize, inputFile);

        //this mark the end of this file
        if(token.length > token.offset)
            break;

        //--------------------------- LENGTH -----------------------------------

        if(token.length == 0 && token.offset == 0) {
            WriteToBufferBig(outFile, lengthCodes[token.character].first, lengthCodes[token.character].second);
        }
        else if(token.length <= 10) {
            WriteToBufferBig(outFile, lengthCodes[257 + token.length - 3].first, lengthCodes[257 + token.length - 3].second);
        }
        else if(token.length <= 18) {
            WriteToBufferBig(outFile, lengthCodes[265 + (token.length - 11) / 2].first, lengthCodes[265 + (token.length - 11) / 2].second);
            WriteToBuffer(outFile, !(token.length % 2), 1); // 1 extra bit
        }
        else if(token.length <= 34) {
            WriteToBufferBig(outFile, lengthCodes[269 + (token.length - 19) / 4].first, lengthCodes[269 + (token.length - 19) / 4].second);
            WriteToBuffer(outFile, (token.length - 19) % 4, 2); // 2 extra biti
        }
        else if(token.length <= 66) {
            WriteToBufferBig(outFile, lengthCodes[273 + (token.length - 35) / 8].first, lengthCodes[273 + (token.length - 35) / 8].second);
            WriteToBuffer(outFile, (token.length - 35) % 8, 3); // 3 extra biti
        }
        else if(token.length <= 130) {
            WriteToBufferBig(outFile, lengthCodes[277 + (token.length - 67) / 16].first, lengthCodes[277 + (token.length - 67) / 16].second);
            WriteToBuffer(outFile, (token.length - 67) % 16, 4); // 4 extra biti
        }
        else if(token.length <= 257) {
            WriteToBufferBig(outFile, lengthCodes[281 + (token.length - 131) / 32].first, lengthCodes[281 + (token.length - 131) / 32].second);
            WriteToBuffer(outFile, (token.length - 131) % 32, 5); // 5 extra biti
        }
        else if(token.length == 258)
            WriteToBufferBig(outFile, lengthCodes[285].first, lengthCodes[285].second); //0 extra biti
        else
            WriteToBufferBig(outFile, lengthCodes[256].first, lengthCodes[256].second);


        //--------------------------- OFFSET -----------------------------------

        if(token.offset == 0) {
            //do nothing
        }
        else if(token.offset <= 4) {
            WriteToBufferBig(outFile, offsetCodes[token.offset - 1].first, offsetCodes[token.offset - 1].second);
        }
        else if(token.offset <= 8) {
            WriteToBufferBig(outFile, offsetCodes[4 + (token.offset - 5) / 2].first, offsetCodes[4 + (token.offset - 5) / 2].second);
            WriteToBuffer(outFile, !(token.offset % 2), 1); // 1 extra bit 
        }
        else if(token.offset <= 16) {
            WriteToBufferBig(outFile, offsetCodes[6 + (token.offset - 9) / 4].first, offsetCodes[6 + (token.offset - 9) / 4].second);
            WriteToBuffer(outFile, (token.offset - 9) % 4, 2); // 2 extra biti
        }
        else if(token.offset <= 32) {
            WriteToBufferBig(outFile, offsetCodes[8 + (token.offset - 17) / 8].first, offsetCodes[8 + (token.offset - 17) / 8].second);
            WriteToBuffer(outFile, (token.offset - 17) % 8, 3); // 3 extra biti
        }
        else if(token.offset <= 64) {
            WriteToBufferBig(outFile, offsetCodes[10 + (token.offset - 33) / 16].first, offsetCodes[10 + (token.offset - 33) / 16].second);
            WriteToBuffer(outFile, (token.offset - 33) % 16, 4); // 4 extra biti
        }
        else if(token.offset <= 128) {
            WriteToBufferBig(outFile, offsetCodes[12 + (token.offset - 65) / 32].first, offsetCodes[12 + (token.offset - 65) / 32].second);
            WriteToBuffer(outFile, (token.offset - 65) % 32, 5); // 5 extra biti
        }
        else if(token.offset <= 256) {
            WriteToBufferBig(outFile, offsetCodes[14 + (token.offset - 129) / 64].first, offsetCodes[14 + (token.offset - 129) / 64].second);
            WriteToBuffer(outFile, (token.offset - 129) % 64, 6); // 6 extra biti
        }
        else if(token.offset <= 512) {
            WriteToBufferBig(outFile, offsetCodes[16 + (token.offset - 257) / 128].first, offsetCodes[16 + (token.offset - 257) / 128].second);
            WriteToBuffer(outFile, (token.offset - 257) % 128, 7); // 7 extra biti
        }
        else if(token.offset <= 1024) {
            WriteToBufferBig(outFile, offsetCodes[18 + (token.offset - 513) / 256].first, offsetCodes[18 + (token.offset - 513) / 256].second);
            WriteToBuffer(outFile, (token.offset - 513) % 256); // 8 extra biti
        }
        else if(token.offset <= 2048) {
            WriteToBufferBig(outFile, offsetCodes[20 + (token.offset - 1025) / 512].first, offsetCodes[20 + (token.offset - 1025) / 512].second);
            WriteToBufferBig(outFile, (token.offset - 1025) % 512, 9); // 9 extra biti
        }
        else if(token.offset <= 4096) {
            WriteToBufferBig(outFile, offsetCodes[22 + (token.offset - 2049) / 1024].first, offsetCodes[22 + (token.offset - 2049) / 1024].second);
            WriteToBufferBig(outFile, (token.offset - 2049) % 1024, 10); // 10 extra biti
        }
        else if(token.offset <= 8192) {
            WriteToBufferBig(outFile, offsetCodes[24 + (token.offset - 4097) / 2048].first, offsetCodes[24 + (token.offset - 4097) / 2048].second);
            WriteToBufferBig(outFile, (token.offset - 4097) % 2048, 11); // 11 extra biti
        }
        else if(token.offset <= 16384) {
            WriteToBufferBig(outFile, offsetCodes[26 + (token.offset - 8193) / 4096].first, offsetCodes[26 + (token.offset - 8193) / 4096].second);
            WriteToBufferBig(outFile, (token.offset - 8193) % 4096, 12); // 12 extra biti
        }
        else {
            WriteToBufferBig(outFile, offsetCodes[28 + (token.offset - 16385) / 8192].first, offsetCodes[28 + (token.offset - 16385) / 8192].second);
            WriteToBufferBig(outFile, (token.offset - 16385) % 8192, 13); // 13 extra biti
        }
    }

    WriteToBufferBig(outFile, lengthCodes[256].first, lengthCodes[256].second); // end-of-block

    inputFile.close();
}

void CompressFileName(string fileAddress, ofstream &outFile, int is_file = -1) {
    string fileName = "";
    for(auto i = fileAddress.rbegin(); i != fileAddress.rend() && *i != '/' && *i != '\\'; i++)
        fileName += *i;
    reverse(fileName.begin(), fileName.end());

    char len = static_cast<char>(fileName.length());
    WriteToBuffer(outFile, len);

    for(int i = 0; i < len; i++) {
        WriteToBuffer(outFile, fileName[i]);
    }

    if(is_file != -1) {
        if(is_file)
            WriteToBuffer(outFile, 1, 1);
        else
            WriteToBuffer(outFile, 0, 1);
        
        return;
    }

    if(is_directory(fileAddress)) {
        WriteToBuffer(outFile, 0, 1);
    }
    else
        WriteToBuffer(outFile, 1, 1);
}

void Compress_help(const string &address, ofstream &outFile) {
    if(archive_corrupted)
        return;
    
    float save = *progress;

    vector<uint64_t> lengthFreqMap(286, 0), offsetFreqMap(30, 0);

    GetLZ77Frequency(address, lengthFreqMap, offsetFreqMap);

    if(archive_corrupted)
        return;

    *progress += 0.5f * progress_ratio;

    HuffmanNode* rootLength = BuildHuffmanTree(lengthFreqMap, 286);

    HuffmanNode* rootOffset = nullptr;
    if(!offsetFreqMap.empty())
        rootOffset = BuildHuffmanTree(offsetFreqMap, 30);

    *progress += 0.1f * progress_ratio;

    vector<int> codeLengths(286);
    ExtractCodeLengths(rootLength, 0, codeLengths);
    vector<pair<int, int>> codes = GenerateCanonicalHuffmanCodes(codeLengths, 286);

    vector<pair<int, int>> codesOffset;
    if(!offsetFreqMap.empty()) {
        vector<int> codeLengthsOffset(30);
        ExtractCodeLengths(rootOffset, 0, codeLengthsOffset);
        codesOffset = GenerateCanonicalHuffmanCodes(codeLengthsOffset, 30);
    }

    *progress += 0.1f * progress_ratio;

    WriteCodesToFile(address, outFile, codes, codesOffset);

    *progress = save + progress_ratio;
}

void CompressNames(const string &folderPath, ofstream &outFile, vector<string> &addresses) {
    CompressFileName(folderPath, outFile);
    addresses.push_back(folderPath);

    if(!is_directory(folderPath)) {
        return;
    }

    const char* path = folderPath.c_str();

    string searchPath = string(path) + "\\*";
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        cerr << "Error opening directory: " << path << endl;
        archive_corrupted = true;
        return;
    }

    do {
        // Ignore directories and hidden files
        if (findFileData.cFileName[0] == '.')
            continue;
        
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CompressNames(folderPath + "/" + findFileData.cFileName, outFile, addresses);

            continue;
        }

        string filePath = string(path) + "/" + findFileData.cFileName;

        CompressFileName(filePath, outFile);
        addresses.push_back(filePath);

    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    WriteToBuffer(outFile, 0);
}

void Compress(const vector<string> &filesToCompressAddress, const string &compressedFileAddress, float &prog) {
    archive_corrupted = false;

    prog = 0;
    progress = &prog;

    writeBufferIndex = 0;
    byteIndex = 0;
    int fileNumber = 0;
    while(fileNumber < INT_MAX && FileExists(filesystem::temp_directory_path().string() + "tempFile_" + to_string(fileNumber) + ".txt"))
        fileNumber++;
    tempFileName = filesystem::temp_directory_path().string() + "tempFile_" + to_string(fileNumber) + ".txt";

    ofstream outFile(compressedFileAddress, ios::binary);
    if(!outFile) {
        archive_corrupted = true;
        remove(tempFileName.c_str());
        return;
    }

    vector<string> addresses;
    for(const auto &i : filesToCompressAddress) {
        CompressNames(i, outFile, addresses);

        *progress += 0.2f / static_cast<float>(filesToCompressAddress.size());
    }
    WriteToBuffer(outFile, 0);

    *progress = 0.2f;
    progress_ratio = 0;
    for(const auto &i : addresses)
        if(!is_directory(i))
            progress_ratio += 1;

    progress_ratio = 0.8f / progress_ratio;
    
    for(const auto &i : addresses)
        if(!is_directory(i))
            Compress_help(i, outFile);

    //Write what is left
    if(byteIndex > 0)
        writeBuffer[writeBufferIndex] <<= (8 - byteIndex);
    outFile.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex + (byteIndex > 0));

    remove(tempFileName.c_str());
    outFile.close();

    *progress = 1;
}

//------------------------------------------------- END OF COMPRESSING ALGORITHM ------------------------------------------------------


//---------------------------------------------------- DECOMPRESSING ALGORITHM --------------------------------------------------

void ReverseCode(const vector<pair<int, int>> &sorted, unordered_map<string, int> &codes) {
    int code = 0;
    int prev_len = 0;

    for (auto i : sorted) {
        if (i.first != prev_len)
            code <<= (i.first - prev_len);

        string binary = "";
        for (int j = i.first - 1; j >= 0; --j)
            binary += ((code >> j) & 1) ? '1' : '0';

        codes[binary] = i.second;
        code++;
        prev_len = i.first;
    }
}

void DecompressFile(const string &address, const bool &fileBool, ifstream &file) {
    if(archive_corrupted)
        return;
    
    unsigned char byte;
    string binary = bytesFromTheLastRead;
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;

//---------------------------------------------- NAME -----------------------------------------

    if(fileBool == 0) {
        _mkdir(address.c_str());

        return;
    }

    ofstream outFile(address, ios::binary);
    if (!outFile.is_open()) {
        cerr << "Error opening output file: " << address << endl;
        archive_corrupted = true;
        return;
    }

//------------------------------------------------ LENGTH -------------------------------------

    if(binaryPos + 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + 9 > binaryLength) {
        archive_corrupted = true;
        outFile.close();
        return;
    }

    int codesSize = 0;
    for (int i = 0; i < 9; i++) {
        codesSize <<= 1;
        if (binary[binaryPos + i] == '1')
            codesSize |= 1;
    }
    binaryPos += 9;

    vector<pair<int, int>> codeLength(codesSize);
    int codeLengthIdx = 0;


    if(binaryPos + codesSize * 14 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 14 > binaryLength) {
        archive_corrupted = true;
        outFile.close();
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 9; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        for(int i = 9; i < 14; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        binaryPos += 14;

        codeLength[codeLengthIdx++] = {symbolLength, symbol};
    }

    sort(codeLength.begin(), codeLength.end());
    unordered_map<string, int> reverseCodes;
    reverseCodes.reserve(codesSize);
    ReverseCode(codeLength, reverseCodes);

//------------------------------------------------ OFFSET -------------------------------------

    if(binaryPos + 5 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + 5 > binaryLength) {
        archive_corrupted = true;
        outFile.close();
        return;
    }
    
    codesSize = 0;
    for (int i = 0; i < 5; i++) {
        codesSize <<= 1;
        if (binary[i + binaryPos] == '1')
            codesSize |= 1;
    }
    binaryPos += 5;

    vector<pair<int, int>> offsetCodes(codesSize);
    int offsetCodesIdx = 0;

    if(binaryPos + codesSize * 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 9 > binaryLength) {
        archive_corrupted = true;
        outFile.close();
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 5; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        for(int i = 5; i < 9; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        binaryPos += 9;

        offsetCodes[offsetCodesIdx++] = {symbolLength, symbol};
    }

    sort(offsetCodes.begin(), offsetCodes.end());
    unordered_map<string, int> reverseOffsetCodes;
    reverseOffsetCodes.reserve(codesSize);
    ReverseCode(offsetCodes, reverseOffsetCodes);

//------------------------------------------------- READ FILE -----------------------------------------------------------------

    unsigned char decompressedBytes[WINDOW_SIZE * 2];
    int decompressedBytesIdx = 0;
    auto getExtraBytes = [](int sizeLength, int &binaryPos, string &binary) {
        if(binaryPos + sizeLength > static_cast<int>(binary.length())) {
            archive_corrupted = true;

            return 0;
        }
        int add = 0;

        while(sizeLength--)
            add = add * 2 + (binary[binaryPos++] == '1');

        return add;
    };

    LZ77 token;
    bool readOffset = false;
    bool end_of_block = false;
    string value = "";
    int pos = 0;

    while(!end_of_block && !archive_corrupted) {
        if(binaryPos + 14 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);
        if(binaryPos > binaryLength) {
            archive_corrupted = true;
            outFile.close();
            return;
        }

        value += binary[binaryPos++];

        if(readOffset) {
            auto it = reverseOffsetCodes.find(value);
            if(it != reverseOffsetCodes.end()) {
                int nr = it -> second;
                if(nr <= 3)
                    token.offset = nr + 1;
                else if(nr <= 5) {
                    nr = 5 + (nr - 4) * 2 + getExtraBytes(1, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 7) {
                    nr = 9 + (nr - 6) * 4 + getExtraBytes(2, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 9) {
                    nr = 17 + (nr - 8) * 8 + getExtraBytes(3, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 11) {
                    nr = 33 + (nr - 10) * 16 + getExtraBytes(4, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 13) {
                    nr = 65 + (nr - 12) * 32 + getExtraBytes(5, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 15) {
                    nr = 129 + (nr - 14) * 64 + getExtraBytes(6, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 17) {
                    nr = 257 + (nr - 16) * 128 + getExtraBytes(7, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 19) {
                    nr = 513 + (nr - 18) * 256 + getExtraBytes(8, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 21) {
                    nr = 1025 + (nr - 20) * 512 + getExtraBytes(9, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 23) {
                    nr = 2049 + (nr - 22) * 1024 + getExtraBytes(10, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 25) {
                    nr = 4097 + (nr - 24) * 2048 + getExtraBytes(11, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 27) {
                    nr = 8193 + (nr - 26) * 4096 + getExtraBytes(12, binaryPos, binary);
                    token.offset = nr;
                }
                else if(nr <= 29) {
                    nr = 16385 + (nr - 28) * 8192 + getExtraBytes(13, binaryPos, binary);
                    token.offset = nr;
                }
                else {
                    cerr << "Error at decompressing the offset of the token " << endl;
                    archive_corrupted = true;
                    outFile.close();

                    return;
                }

                value = "";

                WriteTokenToFile(token, decompressedBytes, decompressedBytesIdx, outFile);

                readOffset = false;
            }
        }
        else {
            auto it = reverseCodes.find(value);
            if(it != reverseCodes.end()) {
                if(it -> second < 256) {
                    token.character = static_cast<unsigned char>(it -> second);
                    token.offset = 0;
                    token.length = 0;

                    WriteTokenToFile(token, decompressedBytes, decompressedBytesIdx, outFile);

                    value = "";
                }
                else if(it -> second == 256) {
                    bytesFromTheLastRead = binary.substr(binaryPos);
                    end_of_block = true;

                    break;
                }
                else {
                    int nr = it -> second;
                    if(nr <= 264)
                        nr = nr - 257 + 3;
                    else if(nr <= 268) {
                        nr = 11 + (nr - 265) * 2 + getExtraBytes(1, binaryPos, binary);
                    }
                    else if(nr <= 272) {
                        nr = 19 + (nr - 269) * 4 + getExtraBytes(2, binaryPos, binary);
                    }
                    else if(nr <= 276) {
                        nr = 35 + (nr - 273) * 8 + getExtraBytes(3, binaryPos, binary);
                    }
                    else if(nr <= 280) {
                        nr = 67 + (nr - 277) * 16 + getExtraBytes(4, binaryPos, binary);
                    }
                    else if(nr <= 284) {
                        nr = 131 + (nr - 281) * 32 + getExtraBytes(5, binaryPos, binary);
                    }
                    else if(nr == 285)
                        nr = 258;
                    else {
                        cerr << "Error at decompressing the length of the token" << endl;
                        archive_corrupted = true;
                        outFile.close();

                        return;
                    }
                    
                    token.length = nr;
                    token.character = '-';
                    readOffset = true;

                    value = "";
                }
            }
        }
    }

    if(decompressedBytesIdx > 0 && decompressedBytesIdx < WINDOW_SIZE)
        outFile.write(reinterpret_cast<char*>(decompressedBytes), decompressedBytesIdx);
    else if(decompressedBytesIdx > 0)
        outFile.write(reinterpret_cast<char*>(decompressedBytes + WINDOW_SIZE), decompressedBytesIdx - WINDOW_SIZE);
   
    outFile.close();
}

void Decompress(const string &toDecompressFolderAddress, const string &compressedFileAddress) {
    archive_corrupted = false;

    bytesFromTheLastRead = "";
    writeBufferIndex = 0;
    byteIndex = 0;

    ifstream file(compressedFileAddress, ios::binary);
    if(!file) {
        archive_corrupted = true;
        return;
    }

    string binary = bytesFromTheLastRead;
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;
    vector<pair<string, bool>> addresses; // 1 - file; 0 - folder

    uint8_t fileNameLen = 0;

    if(binaryLength < 8)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryLength < 8) {
        archive_corrupted = true;
        file.close();
        return;
    }
    
    while(binaryPos < 8)
        fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');
    
    string newDecompressAddress = toDecompressFolderAddress;
    string folderAddress = newDecompressAddress;

    if(newDecompressAddress != "" && newDecompressAddress.back() == '/')
        newDecompressAddress.pop_back();

    int len = 0;

    while(fileNameLen != 0 || folderAddress != newDecompressAddress) {
        if(fileNameLen == 0) {
            while(folderAddress != "" && folderAddress != compressedFileAddress && folderAddress.back() != '/')
                folderAddress.pop_back();
            
            if(folderAddress != "")
                folderAddress.pop_back();

            addresses.push_back({"", 0});
            
            if(8 + binaryPos >= binaryLength)
                ReadDataToDecompress(binary, file, binaryLength, binaryPos);

            if(8 + binaryPos > binaryLength) {
                archive_corrupted = true;
                file.close();
                return;
            }
        
            fileNameLen = 0;
            for(int i = 0; i < 8; i++)
                fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');

            continue;
        }

        string newFilePath = folderAddress;
        if(newFilePath != "" && newFilePath[newFilePath.length() - 1] != '/')
            newFilePath += "/";

        if(binaryPos + 8 * fileNameLen + 1 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);

        if(binaryPos + 8 * fileNameLen + 1 > binaryLength) {
            archive_corrupted = true;
            file.close();
            return;
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

        if(addresses.back().second) 
            len++;

        if(8 + binaryPos >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);

        if(binaryPos + 8 > binaryLength) {
            archive_corrupted = true;
            file.close();
            return;
        }
        
        fileNameLen = 0;
        for(int i = 0; i < 8; i++)
            fileNameLen = fileNameLen * 2 + (binary[binaryPos++] == '1');
    }

    bytesFromTheLastRead = binary.substr(binaryPos);

    for(auto i : addresses)
        if(i.first != "") {
            DecompressFile(i.first, i.second, file);

            *progress += 1.0 / len;
        }
    
    *progress = 1.0f;

    file.close();
}

//-------------------------------------------------- END OF DECOMPRESSING ALGORITHM ------------------------------------------------


//---------------------------------------------------- ARCHIVE OPERATIONS SECTION --------------------------------------------------

void TravelFile(ifstream &file, ofstream &outFile) {
    if(archive_corrupted)
        return;
    
    unsigned char byte;
    string binary = bytesFromTheLastRead;
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;

//------------------------------------------------ LENGTH -------------------------------------

    if(binaryPos + 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);
    
    if(binaryPos + 9 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    int codesSize = 0;
    for (int i = 0; i < 9; i++) {
        codesSize <<= 1;
        if (binary[binaryPos + i] == '1')
            codesSize |= 1;
    }
    binaryPos += 9;
    WriteToBufferBig(outFile, codesSize, 9);

    vector<pair<int, int>> codeLength(codesSize);
    int codeLengthIdx = 0;


    if(binaryPos + codesSize * 14 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 14 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 9; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        WriteToBufferBig(outFile, symbol, 9);
        for(int i = 9; i < 14; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        WriteToBuffer(outFile, symbolLength, 5);
        binaryPos += 14;

        codeLength[codeLengthIdx++] = {symbolLength, symbol};
    }

    sort(codeLength.begin(), codeLength.end());
    unordered_map<string, int> reverseCodes;
    reverseCodes.reserve(codesSize);
    ReverseCode(codeLength, reverseCodes);

//------------------------------------------------ OFFSET -------------------------------------

    if(binaryPos + 5 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + 5 > binaryLength) {
        archive_corrupted = true;
        return;
    }
    
    codesSize = 0;
    for (int i = 0; i < 5; i++) {
        codesSize <<= 1;
        if (binary[i + binaryPos] == '1')
            codesSize |= 1;
    }
    WriteToBuffer(outFile, codesSize, 5);
    binaryPos += 5;

    vector<pair<int, int>> offsetCodes(codesSize);
    int offsetCodesIdx = 0;

    if(binaryPos + codesSize * 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 9 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 5; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        WriteToBuffer(outFile, symbol, 5);
        for(int i = 5; i < 9; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        WriteToBuffer(outFile, symbolLength, 4);
        binaryPos += 9;

        offsetCodes[offsetCodesIdx++] = {symbolLength, symbol};
    }

    sort(offsetCodes.begin(), offsetCodes.end());
    unordered_map<string, int> reverseOffsetCodes;
    reverseOffsetCodes.reserve(codesSize);
    ReverseCode(offsetCodes, reverseOffsetCodes);

//------------------------------------------------- READ FILE -----------------------------------------------------------------

    unsigned char decompressedBytes[WINDOW_SIZE * 2];
    int decompressedBytesIdx = 0;
    auto getExtraBytes = [](int sizeLength, int &binaryPos, string &binary, string &value) {
        if(binaryPos + sizeLength > static_cast<int>(binary.length())) {
            archive_corrupted = true;

            return;
        }

        while(sizeLength--)
            value += binary[binaryPos++];
    };

    LZ77 token;
    bool readOffset = false;
    bool end_of_block = false;
    string value = "";
    int pos = 0;

    while(!end_of_block && !archive_corrupted) {
        if(binaryPos + 14 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);
        if(binaryPos > binaryLength) {
            cout << "ERROR - No data: " << binaryPos << ' ' << binaryLength << ' ' << binary.length() << endl;
            archive_corrupted = true;
            return;
        }
        value += binary[binaryPos++];

        if(readOffset) {
            auto it = reverseOffsetCodes.find(value);
            if(it != reverseOffsetCodes.end()) {
                int nr = it -> second;
                if(nr <= 3)
                    token.offset = nr + 1;
                else if(nr <= 5) {
                    getExtraBytes(1, binaryPos, binary, value);
                }
                else if(nr <= 7) {
                    getExtraBytes(2, binaryPos, binary, value);
                }
                else if(nr <= 9) {
                    getExtraBytes(3, binaryPos, binary, value);
                }
                else if(nr <= 11) {
                    getExtraBytes(4, binaryPos, binary, value);
                }
                else if(nr <= 13) {
                    getExtraBytes(5, binaryPos, binary, value);
                }
                else if(nr <= 15) {
                    getExtraBytes(6, binaryPos, binary, value);
                }
                else if(nr <= 17) {
                    getExtraBytes(7, binaryPos, binary, value);
                }
                else if(nr <= 19) {
                    getExtraBytes(8, binaryPos, binary, value);
                }
                else if(nr <= 21) {
                    getExtraBytes(9, binaryPos, binary, value);
                }
                else if(nr <= 23) {
                    getExtraBytes(10, binaryPos, binary, value);
                }
                else if(nr <= 25) {
                    getExtraBytes(11, binaryPos, binary, value);
                }
                else if(nr <= 27) {
                    getExtraBytes(12, binaryPos, binary, value);
                }
                else if(nr <= 29) {
                    getExtraBytes(13, binaryPos, binary, value);
                }
                else {
                    cerr << "Error at decompressing the offset of the code" << endl;
                    archive_corrupted = true;
                    return;
                }

                unsigned char tempByte = 0, tempByteIdx = 0;

                for(char c : value) {
                    tempByte = (tempByte << 1) + (c == '1');
                    tempByteIdx++;

                    if(tempByteIdx == 8) {
                        WriteToBuffer(outFile, tempByte);

                        tempByteIdx = 0;
                        tempByte = 0;
                    }
                }

                if(tempByteIdx > 0) {
                    WriteToBuffer(outFile, tempByte, tempByteIdx);
                }

                value = "";

                readOffset = false;
            }
        }
        else {
            auto it = reverseCodes.find(value);
            if(it != reverseCodes.end()) {
                if(it -> second <= 256) {
                    unsigned char tempByte = 0, tempByteIdx = 0;

                    for(char c : value) {
                        tempByte = (tempByte << 1) + (c == '1');
                        tempByteIdx++;

                        if(tempByteIdx == 8) {
                            WriteToBuffer(outFile, tempByte);

                            tempByteIdx = 0;
                            tempByte = 0;
                        }
                    }
                    if(tempByteIdx > 0) {
                        WriteToBuffer(outFile, tempByte, tempByteIdx);
                    }
                }

                if(it -> second < 256) {
                    value = "";
                }
                else if(it -> second == 256) {
                    bytesFromTheLastRead = binary.substr(binaryPos);
                    end_of_block = true;

                    break;
                }
                else {
                    int nr = it -> second;
                    if(nr <= 264)
                        nr = nr - 257 + 3;
                    else if(nr <= 268) {
                        getExtraBytes(1, binaryPos, binary, value);
                    }
                    else if(nr <= 272) {
                        getExtraBytes(2, binaryPos, binary, value);
                    }
                    else if(nr <= 276) {
                        getExtraBytes(3, binaryPos, binary, value);
                    }
                    else if(nr <= 280) {
                        getExtraBytes(4, binaryPos, binary, value);
                    }
                    else if(nr <= 284) {
                        getExtraBytes(5, binaryPos, binary, value);
                    }
                    else if(nr == 285)
                        nr = 258;
                    else {
                        cout << "Error at decompressing the length of the token" << endl;
                        archive_corrupted = true;
                        return;
                    }

                    unsigned char tempByte = 0, tempByteIdx = 0;

                    for(char c : value) {
                        tempByte = (tempByte << 1) + (c == '1');
                        tempByteIdx++;

                        if(tempByteIdx == 8) {
                            WriteToBuffer(outFile, tempByte);

                            tempByteIdx = 0;
                            tempByte = 0;
                        }
                    }

                    if(tempByteIdx > 0) {
                        WriteToBuffer(outFile, tempByte, tempByteIdx);
                    }
                    
                    readOffset = true;
                    value = "";
                }
            }
        }
    }
}

void InsertFile(const string &fileToCompress, const string &compressedFile, const int &index, float &prog) {
    archive_corrupted = false;
    prog = 0;
    progress = &prog;

    int temp_file_idx = 0;
    writeBufferIndex = 0;
    byteIndex = 0;
    bytesFromTheLastRead = "";
    string compressedFileName = "";

    for(int i = static_cast<int>(compressedFile.length()) - 1; i >= 0 && compressedFile[i] != '\\' && compressedFile[i] != '/'; i--)
        compressedFileName += compressedFile[i];
    reverse(compressedFileName.begin(), compressedFileName.end());

    while(temp_file_idx < INT_MAX && FileExists(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt"))
        temp_file_idx++;

    rename(compressedFile.c_str(), (filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
    ifstream oldFile(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt", ios::binary);
    if(!oldFile) {
        archive_corrupted = true;
        return;
    }
    vector<pair<string, bool>> addresses = GetCompressedFilesWithFile(oldFile);

    if(archive_corrupted)
        return;

    ofstream newFile(compressedFile, ios::binary);
    if(!newFile) {
        archive_corrupted = true;
        oldFile.close();
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
        return;
    }
    string folderPath = "";
    int real_index = 0;
    vector<string> addresses_newFile;

    for(int i = 0; i < addresses.size(); i++) {
        if(i == index)
            CompressNames(fileToCompress, newFile, addresses_newFile);
        
        if(addresses[i].first != "") {
            CompressFileName(addresses[i].first, newFile, addresses[i].second);
            real_index++;
        }
        else
            WriteToBuffer(newFile, 0);
    }

    WriteToBuffer(newFile, 0);

    *progress = 0.1f;
    
    int len = 0;
    for(auto i : addresses)
        len += i.second;
    for(auto i : addresses_newFile)
        if(!is_directory(i))
            len++;

    for(int i = 0; i < index; i++)
        if(addresses[i].second) {
            TravelFile(oldFile, newFile);

            *progress += 0.9f / len;
        }

    int fileNumber = 0;
    while(fileNumber < INT_MAX && FileExists(filesystem::temp_directory_path().string() + "tempFile_" + to_string(fileNumber) + ".txt"))
        fileNumber++;
    tempFileName = filesystem::temp_directory_path().string() + "tempFile_" + to_string(fileNumber) + ".txt";

    for(const auto &i : addresses_newFile)
        if(!is_directory(i)) {
            progress_ratio = 0.9f / len;

            Compress_help(i, newFile);
        }

    for(int i = index; i < addresses.size(); i++)
        if(addresses[i].second) {
            TravelFile(oldFile, newFile);

            *progress += 0.9f / len;
        }

    *progress = 1;

    //Write what is left
    writeBuffer[writeBufferIndex] <<= (8 - byteIndex);
    newFile.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex + 1);

    oldFile.close();
    newFile.close();

    remove(tempFileName.c_str());
    remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
}

void TravelFile(ifstream &file) {
    if(archive_corrupted)
        return;
    
    unsigned char byte;
    string binary = bytesFromTheLastRead;
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;

//------------------------------------------------ LENGTH -------------------------------------

    if(binaryPos + 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + 9 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    int codesSize = 0;
    for (int i = 0; i < 9; i++) {
        codesSize <<= 1;
        if (binary[binaryPos + i] == '1')
            codesSize |= 1;
    }
    binaryPos += 9;

    vector<pair<int, int>> codeLength(codesSize);
    int codeLengthIdx = 0;


    if(binaryPos + codesSize * 14 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 14 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 9; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        for(int i = 9; i < 14; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        binaryPos += 14;

        codeLength[codeLengthIdx++] = {symbolLength, symbol};
    }

    sort(codeLength.begin(), codeLength.end());
    unordered_map<string, int> reverseCodes;
    reverseCodes.reserve(codesSize);
    ReverseCode(codeLength, reverseCodes);

//------------------------------------------------ OFFSET -------------------------------------

    if(binaryPos + 5 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + 5 > binaryLength) {
        archive_corrupted = true;
        return;
    }
    
    codesSize = 0;
    for (int i = 0; i < 5; i++) {
        codesSize <<= 1;
        if (binary[i + binaryPos] == '1')
            codesSize |= 1;
    }
    binaryPos += 5;

    vector<pair<int, int>> offsetCodes(codesSize);
    int offsetCodesIdx = 0;

    if(binaryPos + codesSize * 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    if(binaryPos + codesSize * 9 > binaryLength) {
        archive_corrupted = true;
        return;
    }

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 5; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        for(int i = 5; i < 9; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        binaryPos += 9;

        offsetCodes[offsetCodesIdx++] = {symbolLength, symbol};
    }

    sort(offsetCodes.begin(), offsetCodes.end());
    unordered_map<string, int> reverseOffsetCodes;
    reverseOffsetCodes.reserve(codesSize);
    ReverseCode(offsetCodes, reverseOffsetCodes);

//------------------------------------------------- READ FILE -----------------------------------------------------------------

    unsigned char decompressedBytes[WINDOW_SIZE * 2];
    int decompressedBytesIdx = 0;
    auto getExtraBytes = [](int sizeLength, int &binaryPos, string &binary, string &value) {
        if(binaryPos + sizeLength > static_cast<int>(binary.length())) {
            archive_corrupted = true;
            return;
        }
        while(sizeLength--)
            value += binary[binaryPos++];
    };

    LZ77 token;
    bool readOffset = false;
    bool end_of_block = false;
    string value = "";
    int pos = 0;

    while(!end_of_block && !archive_corrupted) {
        if(binaryPos + 14 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);
        if(binaryPos > binaryLength) {
            cout << "ERROR - No data: " << binaryPos << ' ' << binaryLength << ' ' << binary.length() << endl;
            archive_corrupted = true;
            return;
        }
        value += binary[binaryPos++];

        if(readOffset) {
            auto it = reverseOffsetCodes.find(value);
            if(it != reverseOffsetCodes.end()) {
                int nr = it -> second;
                if(nr <= 3)
                    token.offset = nr + 1;
                else if(nr <= 5) {
                    getExtraBytes(1, binaryPos, binary, value);
                }
                else if(nr <= 7) {
                    getExtraBytes(2, binaryPos, binary, value);
                }
                else if(nr <= 9) {
                    getExtraBytes(3, binaryPos, binary, value);
                }
                else if(nr <= 11) {
                    getExtraBytes(4, binaryPos, binary, value);
                }
                else if(nr <= 13) {
                    getExtraBytes(5, binaryPos, binary, value);
                }
                else if(nr <= 15) {
                    getExtraBytes(6, binaryPos, binary, value);
                }
                else if(nr <= 17) {
                    getExtraBytes(7, binaryPos, binary, value);
                }
                else if(nr <= 19) {
                    getExtraBytes(8, binaryPos, binary, value);
                }
                else if(nr <= 21) {
                    getExtraBytes(9, binaryPos, binary, value);
                }
                else if(nr <= 23) {
                    getExtraBytes(10, binaryPos, binary, value);
                }
                else if(nr <= 25) {
                    getExtraBytes(11, binaryPos, binary, value);
                }
                else if(nr <= 27) {
                    getExtraBytes(12, binaryPos, binary, value);
                }
                else if(nr <= 29) {
                    getExtraBytes(13, binaryPos, binary, value);
                }
                else {
                    cerr << "Error at decompressing the offset of the code" << endl;
                    archive_corrupted = true;
                    return;
                }

                value = "";

                readOffset = false;
            }
        }
        else {
            auto it = reverseCodes.find(value);
            if(it != reverseCodes.end()) {
                if(it -> second < 256) {
                    value = "";
                }
                else if(it -> second == 256) {
                    bytesFromTheLastRead = binary.substr(binaryPos);
                    end_of_block = true;

                    break;
                }
                else {
                    int nr = it -> second;
                    if(nr <= 264)
                        nr = nr - 257 + 3;
                    else if(nr <= 268) {
                        getExtraBytes(1, binaryPos, binary, value);
                    }
                    else if(nr <= 272) {
                        getExtraBytes(2, binaryPos, binary, value);
                    }
                    else if(nr <= 276) {
                        getExtraBytes(3, binaryPos, binary, value);
                    }
                    else if(nr <= 280) {
                        getExtraBytes(4, binaryPos, binary, value);
                    }
                    else if(nr <= 284) {
                        getExtraBytes(5, binaryPos, binary, value);
                    }
                    else if(nr == 285)
                        nr = 258;
                    else {
                        cout << "Error at decompressing the length of the token" << endl;
                        archive_corrupted = true;
                        return;
                    }
                    
                    readOffset = true;
                    value = "";
                }
            }
        }
    }
}

void Decompress(const string &toDecompressFolderAddress, const string &compressedFileAddress, float &prog)
{
    archive_corrupted = false;
    prog = 0;
    progress = &prog;

    Decompress(toDecompressFolderAddress, compressedFileAddress);
}

void Decompress(const string &toDecompressFolderAddress, const string &compressedFileAddress, vector<int> indices)
{
    bytesFromTheLastRead = "";
    writeBufferIndex = 0;
    byteIndex = 0;

    sort(indices.begin(), indices.end());

    ifstream file(compressedFileAddress, ios::binary);
    if(!file) {
        archive_corrupted = true;
        return;
    }

    string binary = "";
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;
    vector<pair<string, bool>> addresses = GetCompressedFilesWithFile(file);

    int len = 0;
    for(int i = 0; i < indices.back(); i++)
        len += addresses[i].second;

    int lastIndex = 0;
    for(auto index : indices) {
        for(int i = lastIndex; i < index; i++)
            if(addresses[i].second && addresses[i].first != "") {
                TravelFile(file);

                *progress += 0.4f / len;
            }

        vector<pair<string, bool>> to_decompress_addresses;
        for(int i = (int) addresses[index].first.length() - 1; i >= 0; i--)
            if(addresses[index].first[i] == '/') {
                addresses[index].first = addresses[index].first.substr(i + 1);
                break;
            }
        
        to_decompress_addresses.push_back({toDecompressFolderAddress + "\\" + addresses[index].first, addresses[index].second});
        int i = index + 1, folders = !addresses[index].second;

        while(folders > 0) {
            int temp = folders;
            for(int j = (int) addresses[i].first.length(); j >= 0; j--)
                if(addresses[i].first[j] == '/')
                    if(temp > 0)
                        temp--;
                    else {
                        addresses[i].first = addresses[i].first.substr(j + 1);
                        break;
                    }
            
            to_decompress_addresses.push_back({toDecompressFolderAddress + "\\" + addresses[i].first, addresses[i].second});

            if(addresses[i].first == "")
                folders--;
            else if(addresses[i].second == 0)
                folders++;
            i++;
        }

        if(to_decompress_addresses.size() > 1)
            to_decompress_addresses.pop_back(); //to remove the last end_of_folder
        
        for(auto idx : to_decompress_addresses)
            if(idx.first != "") {
                writeBufferIndex = 0;
                byteIndex = 0;

                DecompressFile(idx.first, idx.second, file);

                *progress += 0.6f / (static_cast<float>(indices.size()) * static_cast<float>(to_decompress_addresses.size()));
            }
        
        lastIndex = i;
    }
}

void Decompress(const string &toDecompressFolderAddress, const string &compressedFileAddress, vector<int> indices, float &prog)
{
    archive_corrupted = false;
    prog = 0;
    progress = &prog;

    Decompress(toDecompressFolderAddress, compressedFileAddress, indices);

    prog = 1;
}

void DeleteFiles(const string &compressedFile, vector<int> indices, float &prog) {
    archive_corrupted = false;
    prog = 0;
    progress = &prog;

    int temp_file_idx = 0;
    writeBufferIndex = 0;
    byteIndex = 0;
    bytesFromTheLastRead = "";
    string compressedFileName = "";

    sort(indices.begin(), indices.end());

    for(int i = static_cast<int>(compressedFile.length()) - 1; i >= 0 && compressedFile[i] != '\\' && compressedFile[i] != '/'; i--)
        compressedFileName += compressedFile[i];
    reverse(compressedFileName.begin(), compressedFileName.end());

    while(temp_file_idx < INT_MAX && FileExists(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt"))
        temp_file_idx++;

    rename(compressedFile.c_str(), (filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
    ifstream oldFile(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt", ios::binary);
    if(!oldFile) {
        archive_corrupted = true;
        return;
    }
    vector<pair<string, bool>> addresses = GetCompressedFilesWithFile(oldFile);

    ofstream newFile(compressedFile, ios::binary);
    if(!newFile) {
        archive_corrupted = true;
        oldFile.close();
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
    }
    string folderPath = "";
    int real_index = 0;
    vector<string> addresses_newFile;

    int vector_idx = 0;
    for(int i = 0; i < addresses.size(); i++) {
        if(vector_idx < indices.size() && i == indices[vector_idx]) {
            if(addresses[i].second) {
                vector_idx++;
                continue;
            }
            int f = 1;
            i++;
            while(f > 0) {
                if(addresses[i].first == "")
                    f--;
                else {
                    if(addresses[i].second == 0)
                        f++;
                    real_index++;
                }
                i++;
            }
            vector_idx++;
            i--;
            continue;
        }
        
        if(addresses[i].first != "") {
            CompressFileName(addresses[i].first, newFile, addresses[i].second);
        }
        else {
            WriteToBuffer(newFile, 0);
        }
    }

    WriteToBuffer(newFile, 0);

    int len = 0;
    for(auto i : addresses)
        len += i.second;

    int last_index = 0;
    for(auto k : indices) {
        for(int i = last_index; i < k; i++)
            if(addresses[i].second) {
                TravelFile(oldFile, newFile);

                *progress += 1.0 / len;
            }

        if(addresses[k].second) {
            TravelFile(oldFile);
            last_index = k + 1;

            *progress += 1.0 / len;
        }
        else {
            int f = 1;
            last_index = k + 1;
            while(f > 0) {
                if(addresses[last_index].first == "")
                    f--;
                else {
                    if(addresses[last_index].second == 0)
                        f++;
                    else {
                        TravelFile(oldFile);

                        *progress += 1.0 / len;
                    }
                }
                last_index++;
            }
        }
    }

    for(int i = last_index; i < addresses.size(); i++)
            if(addresses[i].second == 1) {
                TravelFile(oldFile, newFile);

                *progress += 1.0 / len;
            }

    //Write what is left
    if(byteIndex > 0)
        writeBuffer[writeBufferIndex] <<= (8 - byteIndex);
    newFile.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex + (byteIndex > 0));

    oldFile.close();
    newFile.close();

    remove(tempFileName.c_str());
    remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());

    *progress = 1.0f;
}

void TravelFileToMove(ifstream &file, ofstream &outFile, string &saveBytes) {
    unsigned char byte;
    string binary = saveBytes;
    int binaryLength = static_cast<int>(binary.length()), binaryPos = 0;

//------------------------------------------------ LENGTH -------------------------------------

    if(binaryPos + 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    int codesSize = 0;
    for (int i = 0; i < 9; i++) {
        codesSize <<= 1;
        if (binary[binaryPos + i] == '1')
            codesSize |= 1;
    }
    binaryPos += 9;
    WriteToBufferBig(outFile, codesSize, 9);

    vector<pair<int, int>> codeLength(codesSize);
    int codeLengthIdx = 0;


    if(binaryPos + codesSize * 14 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 9; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        WriteToBufferBig(outFile, symbol, 9);
        for(int i = 9; i < 14; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        WriteToBuffer(outFile, symbolLength, 5);
        binaryPos += 14;

        codeLength[codeLengthIdx++] = {symbolLength, symbol};
    }

    sort(codeLength.begin(), codeLength.end());
    unordered_map<string, int> reverseCodes;
    reverseCodes.reserve(codesSize);
    ReverseCode(codeLength, reverseCodes);

//------------------------------------------------ OFFSET -------------------------------------

    if(binaryPos + 5 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);
    
    codesSize = 0;
    for (int i = 0; i < 5; i++) {
        codesSize <<= 1;
        if (binary[i + binaryPos] == '1')
            codesSize |= 1;
    }
    WriteToBuffer(outFile, codesSize, 5);
    binaryPos += 5;

    vector<pair<int, int>> offsetCodes(codesSize);
    int offsetCodesIdx = 0;

    if(binaryPos + codesSize * 9 >= binaryLength)
        ReadDataToDecompress(binary, file, binaryLength, binaryPos);

    while(codesSize--) {
        int symbol = 0, symbolLength = 0;
        for(int i = 0; i < 5; i++) {
            symbol <<= 1;
            if(binary[i + binaryPos] == '1')
                symbol |= 1;
        }
        WriteToBuffer(outFile, symbol, 5);
        for(int i = 5; i < 9; i++) {
            symbolLength <<= 1;
            if(binary[i + binaryPos] == '1')
                symbolLength |= 1;
        }
        WriteToBuffer(outFile, symbolLength, 4);
        binaryPos += 9;

        offsetCodes[offsetCodesIdx++] = {symbolLength, symbol};
    }

    sort(offsetCodes.begin(), offsetCodes.end());
    unordered_map<string, int> reverseOffsetCodes;
    reverseOffsetCodes.reserve(codesSize);
    ReverseCode(offsetCodes, reverseOffsetCodes);

//------------------------------------------------- READ FILE -----------------------------------------------------------------

    unsigned char decompressedBytes[WINDOW_SIZE * 2];
    int decompressedBytesIdx = 0;
    auto getExtraBytes = [](int sizeLength, int &binaryPos, string &binary, string &value) {
        while(sizeLength--)
            value += binary[binaryPos++];
    };

    LZ77 token;
    bool readOffset = false;
    bool end_of_block = false;
    string value = "";
    int pos = 0;

    while(!end_of_block) {
        if(binaryPos + 14 >= binaryLength)
            ReadDataToDecompress(binary, file, binaryLength, binaryPos);
        if(binaryPos + 1 >= binary.length()) {
            cout << "ERROR - No data: " << binaryPos << ' ' << binaryLength << ' ' << binary.length() << endl;
        }
        value += binary[binaryPos++];

        if(readOffset) {
            auto it = reverseOffsetCodes.find(value);
            if(it != reverseOffsetCodes.end()) {
                int nr = it -> second;
                if(nr <= 3)
                    token.offset = nr + 1;
                else if(nr <= 5) {
                    getExtraBytes(1, binaryPos, binary, value);
                }
                else if(nr <= 7) {
                    getExtraBytes(2, binaryPos, binary, value);
                }
                else if(nr <= 9) {
                    getExtraBytes(3, binaryPos, binary, value);
                }
                else if(nr <= 11) {
                    getExtraBytes(4, binaryPos, binary, value);
                }
                else if(nr <= 13) {
                    getExtraBytes(5, binaryPos, binary, value);
                }
                else if(nr <= 15) {
                    getExtraBytes(6, binaryPos, binary, value);
                }
                else if(nr <= 17) {
                    getExtraBytes(7, binaryPos, binary, value);
                }
                else if(nr <= 19) {
                    getExtraBytes(8, binaryPos, binary, value);
                }
                else if(nr <= 21) {
                    getExtraBytes(9, binaryPos, binary, value);
                }
                else if(nr <= 23) {
                    getExtraBytes(10, binaryPos, binary, value);
                }
                else if(nr <= 25) {
                    getExtraBytes(11, binaryPos, binary, value);
                }
                else if(nr <= 27) {
                    getExtraBytes(12, binaryPos, binary, value);
                }
                else if(nr <= 29) {
                    getExtraBytes(13, binaryPos, binary, value);
                }
                else {
                    cerr << "Error at decompressing the offset of the code" << endl;
                    exit(1);
                }

                unsigned char tempByte = 0, tempByteIdx = 0;

                for(char c : value) {
                    tempByte = (tempByte << 1) + (c == '1');
                    tempByteIdx++;

                    if(tempByteIdx == 8) {
                        WriteToBuffer(outFile, tempByte);

                        tempByteIdx = 0;
                        tempByte = 0;
                    }
                }

                if(tempByteIdx > 0) {
                    WriteToBuffer(outFile, tempByte, tempByteIdx);
                }

                value = "";

                readOffset = false;
            }
        }
        else {
            auto it = reverseCodes.find(value);
            if(it != reverseCodes.end()) {
                if(it -> second <= 256) {
                    unsigned char tempByte = 0, tempByteIdx = 0;

                    for(char c : value) {
                        tempByte = (tempByte << 1) + (c == '1');
                        tempByteIdx++;

                        if(tempByteIdx == 8) {
                            WriteToBuffer(outFile, tempByte);

                            tempByteIdx = 0;
                            tempByte = 0;
                        }
                    }
                    if(tempByteIdx > 0) {
                        WriteToBuffer(outFile, tempByte, tempByteIdx);
                    }
                }

                if(it -> second < 256) {
                    value = "";
                }
                else if(it -> second == 256) {
                    saveBytes = binary.substr(binaryPos);
                    end_of_block = true;

                    break;
                }
                else {
                    int nr = it -> second;
                    if(nr <= 264)
                        nr = nr - 257 + 3;
                    else if(nr <= 268) {
                        getExtraBytes(1, binaryPos, binary, value);
                    }
                    else if(nr <= 272) {
                        getExtraBytes(2, binaryPos, binary, value);
                    }
                    else if(nr <= 276) {
                        getExtraBytes(3, binaryPos, binary, value);
                    }
                    else if(nr <= 280) {
                        getExtraBytes(4, binaryPos, binary, value);
                    }
                    else if(nr <= 284) {
                        getExtraBytes(5, binaryPos, binary, value);
                    }
                    else if(nr == 285)
                        nr = 258;
                    else {
                        cout << "Error at decompressing the length of the token" << endl;
                        exit(1);
                    }

                    unsigned char tempByte = 0, tempByteIdx = 0;

                    for(char c : value) {
                        tempByte = (tempByte << 1) + (c == '1');
                        tempByteIdx++;

                        if(tempByteIdx == 8) {
                            WriteToBuffer(outFile, tempByte);

                            tempByteIdx = 0;
                            tempByte = 0;
                        }
                    }

                    if(tempByteIdx > 0) {
                        WriteToBuffer(outFile, tempByte, tempByteIdx);
                    }
                    
                    readOffset = true;
                    value = "";
                }
            }
        }
    }
}

void MoveFiles(const string &compressedFile, vector<int> indices, const int &index, float &prog) {
    archive_corrupted = false;
    
    if(find(indices.begin(), indices.end(), index) != indices.end()) {
        prog = 1.0f;
        return;
    }

    progress = &prog;
    
    sort(indices.begin(), indices.end());
    string compressedFileName = "";
    writeBufferIndex = 0;
    byteIndex = 0;
    bytesFromTheLastRead = "";

    for(int i = static_cast<int>(compressedFile.length()) - 1; i >= 0 && compressedFile[i] != '\\' && compressedFile[i] != '/'; i--)
        compressedFileName += compressedFile[i];
    reverse(compressedFileName.begin(), compressedFileName.end());

    int temp_file_idx = 0;
    while(temp_file_idx < INT_MAX && FileExists(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt"))
        temp_file_idx++;

    ofstream tempFile(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt", ios::binary);
    if(!tempFile) {
        archive_corrupted = true;
        return;
    }
    ifstream file(compressedFile, ios::binary);
    if(!file) {
        archive_corrupted = true;
        tempFile.close();
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
        return;
    }

    vector<pair<string, bool>> addresses = GetCompressedFilesWithFile(file);
    vector<pair<string, bool>> filesToMove;

    int len = 0;
    for(auto i : addresses)
        len += i.second;

    int last = 0;
    for(auto i : indices) {
        while(last < i) {
            if(addresses[last].second) {
                TravelFile(file);

                *progress += 0.45f / len;
            }
            last++;
        }
        filesToMove.push_back(addresses[i]);
        if(filesToMove.back().second) {
            TravelFile(file, tempFile);

            *progress += 0.45f / len;
        }
        
        int f = !filesToMove.back().second;
        i++;

        while(f > 0) {
            filesToMove.push_back(addresses[i]);
            if(filesToMove.back().second) {
                TravelFile(file, tempFile);

                *progress += 0.45f / len;
            }

            if(filesToMove.back().first == "")
                f--;
            else if(!filesToMove.back().second)
                f++;
            i++;
        }
        last = i;
    }

    *progress = 0.45f;

    if(byteIndex > 0)
        writeBuffer[writeBufferIndex] <<= (8 - byteIndex);
    tempFile.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex + (byteIndex > 0));

    writeBufferIndex = 0;
    byteIndex = 0;
    bytesFromTheLastRead = "";

    tempFile.close();
    file.close();

    int new_temp_file_idx = temp_file_idx;
    while(new_temp_file_idx < INT_MAX && FileExists(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt"))
        new_temp_file_idx++;

    rename(compressedFile.c_str(), (filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt").c_str());

    ifstream oldFile(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt", ios::binary);
    if(!oldFile) {
        archive_corrupted = true;
        return;
    }
    ifstream tempFileRead(filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt", ios::binary);
    if(!tempFileRead) {
        archive_corrupted = true;
        oldFile.close();
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt").c_str());
        return;
    }
    ofstream newFile(compressedFile, ios::binary);
    if(!newFile) {
        archive_corrupted = true;
        oldFile.close();
        tempFileRead.close();
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt").c_str());
        remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
        return;
    }

    addresses = GetCompressedFilesWithFile(oldFile);
    last = 0;
    for(auto i : indices) {
        while(last < i) {
            if(last == index) {
                for(auto j : filesToMove)
                    if(j.first != "")
                        CompressFileName(j.first, newFile, j.second);
                    else
                        WriteToBuffer(newFile, 0);
            }
            if(addresses[last].first != "")
                CompressFileName(addresses[last].first, newFile, addresses[last].second);
            else
                WriteToBuffer(newFile, 0);
            last++;
        }

        int f = !addresses[i].second;
        i++;

        while(f > 0) {
            if(addresses[i].first == "")
                f--;
            else if(!addresses[i].second)
                f++;
            i++;
        }
        last = i;
    }

    while(last < addresses.size()) {
        if(last == index) {
                for(auto j : filesToMove)
                    if(j.first != "")
                        CompressFileName(j.first, newFile, j.second);
                    else
                        WriteToBuffer(newFile, 0);
            }
            if(addresses[last].first != "")
                CompressFileName(addresses[last].first, newFile, addresses[last].second);
            else
                WriteToBuffer(newFile, 0);
            last++;
    }

    if(index == (int) addresses.size()) {
        for(auto j : filesToMove)
                    if(j.first != "")
                        CompressFileName(j.first, newFile, j.second);
                    else
                        WriteToBuffer(newFile, 0);
    }

    WriteToBuffer(newFile, 0);

    *progress = 0.55f;

    last = 0;
    string saveBytes = "";
    for(auto i : indices) {
        while(last < i) {
            if(last == index) {
                for(auto j : filesToMove)
                    if(j.second) {
                        TravelFileToMove(tempFileRead, newFile, saveBytes);

                        *progress += 0.45f / len;
                    }
            }
            if(addresses[last].second) {
                TravelFile(oldFile, newFile);

                *progress += 0.45f / len;
            }
            last++;
        }

        if(addresses[i].second) {
            TravelFile(oldFile);

            *progress += 0.45f / len;
        }
        
        int f = !addresses[i].second;
        i++;

        while(f > 0) {
            if(addresses[i].second) {
                TravelFile(oldFile);

                *progress += 0.45f / len;
            }

            if(addresses[i].first == "")
                f--;
            else if(!addresses[i].second)
                f++;
            i++;
        }
        last = i;
    }

     while(last < addresses.size()) {
        if(last == index) {
                for(auto j : filesToMove)
                    if(j.second) {
                        TravelFileToMove(tempFileRead, newFile, saveBytes);

                        *progress += 0.45f / len;
                    }
            }
            if(addresses[last].second) {
                TravelFile(oldFile, newFile);

                *progress += 0.45f / len;
            }
                
            last++;
    }

    if(index == (int) addresses.size()) {
        for(auto j : filesToMove)
                    if(j.second) {
                        TravelFileToMove(tempFileRead, newFile, saveBytes);

                        *progress += 0.45f / len;
                    }
    }

    *progress = 1;

    if(byteIndex > 0)
        writeBuffer[writeBufferIndex] <<= (8 - byteIndex);
    newFile.write(reinterpret_cast<char*>(writeBuffer), writeBufferIndex + (byteIndex > 0));

    oldFile.close();
    newFile.close();
    tempFileRead.close();

    remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(new_temp_file_idx) + ".txt").c_str());
    remove((filesystem::temp_directory_path().string() + compressedFileName + "_" + to_string(temp_file_idx) + ".txt").c_str());
}

//------------------------------------------------- END OF ARCHIVE OPERATIONS SECTION -----------------------------------------------