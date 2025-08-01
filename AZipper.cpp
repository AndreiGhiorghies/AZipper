#define SDL_MAIN_HANDLED

#include "Compresor/Compressor.h"

#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "SDL2/SDL_syswm.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include <iostream>
#include <vector>
#include <queue>
#include <stack>
#include <unordered_set>
#include <algorithm>

#include <thread>
#include <filesystem>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <random>

using namespace std;

unordered_set<int> selectedIndices;
vector<int> lastSelectedIndex;
vector<string> openedFiles;
string decompressedFileAddress, realCompressedFileAddress, tempFileAddress;
bool openPopup, processInProgress;
queue<string> filesToAdd;

struct fileTree {
    vector<pair<string, int>> files;
    fileTree *parent;
    string path;

    vector<pair<pair<string, int>, fileTree *>> folders;

    fileTree(fileTree *p) : parent(p) {}
};

struct ProgressStatus {
    float progress = 0.0f; // between 0.0f and 1.0f
    bool active = false;
};

ProgressStatus globalProgress;

void ShowProgressBar() {
    if (!globalProgress.active)
        return;

    ImGuiIO &io = ImGui::GetIO();

    ImGui::SetNextWindowFocus();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::Begin("##ProgressOverlay", nullptr,
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoInputs);

    ImGui::InvisibleButton("##Blocker", io.DisplaySize, ImGuiButtonFlags_None);

    ImGui::End();

    ImVec2 winSize = ImVec2(400, 80);
    ImVec2 winPos = ImVec2((io.DisplaySize.x - winSize.x) * 0.5f, io.DisplaySize.y - 250);

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));

    ImGui::Begin("Progress", nullptr,
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextWrapped("%s", "");
    ImGui::ProgressBar(globalProgress.progress, ImVec2(-1, 20));

    if (globalProgress.progress == 1.0f || archive_corrupted) {
        processInProgress = false;
        globalProgress.active = false;
    }

    ImGui::PopStyleColor();

    ImGui::End();
}

int ShowSaveProgressPopup() {
    static bool isOpen = true;
    static bool openPopup2 = true;

    int result = -1;

    if (openPopup2) {
        ImGui::OpenPopup("##PopupFaraText");
        openPopup2 = false;
        isOpen = true;
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));

    ImVec2 windowSize(250, 125);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    center.y -= 25.0f;
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.23f, 0.23f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    if (ImGui::BeginPopupModal("##PopupFaraText", &isOpen, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::SetCursorPosY(40);
        ImGui::SetWindowFontScale(1.15f);
        ImGui::SetCursorPosX((windowSize.x - ImGui::CalcTextSize("Unsaved Changes").x) / 2);
        ImGui::Text("Unsaved Changes");
        ImGui::SetCursorPosY(90);

        float buttonWidth = 100.0f;
        float spacing = 20.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        float startX = (windowSize.x - totalWidth) / 2;

        ImGui::SetCursorPosX(startX);
        if (ImGui::Button("Discard", ImVec2(buttonWidth, 0))) {
            result = 0;
            openPopup2 = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine(0, 20);
        if (ImGui::Button("Save", ImVec2(buttonWidth, 0))) {
            result = 1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SetWindowFontScale(1.0f);

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar(2);

    if (!isOpen) {
        openPopup2 = true;
        result = 2;

        ImGui::CloseCurrentPopup();
    }

    return result;
}

fileTree *buildFileTree(vector<pair<string, bool>> v, int &idx, fileTree *parent, string path) {
    fileTree *head = new fileTree(parent);
    head->path = path;

    while (idx < v.size() && v[idx].first != "") {
        string name = "";
        int i = (int)v[idx].first.length();
        while (i >= 0 && v[idx].first[i] != '/')
            i--;
        
        if (i < 0)
            name = v[idx].first;
        else
            name = v[idx].first.substr(i + 1);
        
        if (v[idx].second == 1) {
            head->files.push_back({name, idx});
            idx++;
        }
        else {
            idx++;
            string newPath = "";
            head->folders.push_back({{name, idx - 1}, buildFileTree(v, idx, head, path + (path.back() != '/' ? "/" : "") + name)});
        }
    }

    idx++;

    return head;
}

string openSaveFileDialog(bool withoutAllFiles = false, const string &defaultName = "file.azip") {
    wchar_t filePath[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    if(withoutAllFiles)
        ofn.lpstrFilter = L"AZip Files\0*.azip\0";
    else
        ofn.lpstrFilter = L"AZip Files\0*.azip\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"azip";

    // Converting string -> wstring
    wstring wDefaultName(defaultName.begin(), defaultName.end());
    wcscpy_s(ofn.lpstrFile, MAX_PATH, wDefaultName.c_str());

    if (GetSaveFileNameW(&ofn)) {
        wstring wstr(ofn.lpstrFile);
        string str(wstr.begin(), wstr.end());
        return str;
    }
    else
        return "";
}

string generateRandomString(size_t length) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    static mt19937 rng(random_device{}());
    static uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i)
        result += charset[dist(rng)];
    return result;
}

string getAvailableFilename(const string &folder, const string &extension = ".azip", size_t nameLength = 8) {
    namespace fs = filesystem;
    string filename;

    do
    {
        filename = generateRandomString(nameLength) + extension;
    } while (fs::exists(fs::path(folder) / filename));

    return (fs::path(folder) / filename).string();
}

string OpenFileDialog(bool allFiles = false, bool withoutAllFiles = false) {
    char fileName[MAX_PATH] = "";

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;

    if (allFiles)
        ofn.lpstrFilter = "All Files\0*.*\0";
    else if(withoutAllFiles)
        ofn.lpstrFilter = "AZip\0*.azip\0";
    else
        ofn.lpstrFilter = "AZip\0*.azip\0All Files\0*.*\0";
    
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "azip";

    if (GetOpenFileNameA(&ofn))
        return string(fileName);

    return "";
}

string OpenFolderDialog() {
    BROWSEINFOW bi = {0};
    bi.lpszTitle = L"Select a folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);

    if (pidl != nullptr) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            // Converting wide string -> UTF-8 string
            wstring_convert<codecvt_utf8<wchar_t>> converter;
            return converter.to_bytes(path);
        }
    }

    return "";
}

void ShowFileExplorerWindow(fileTree *&head) {
    ImGuiIO &io = ImGui::GetIO();

    float windowWidth = io.DisplaySize.x * 0.8f;
    float windowX = (io.DisplaySize.x - windowWidth) / 2.0f;
    ImVec2 winPos = ImVec2(windowX, 95);
    ImVec2 winSize = ImVec2(windowWidth, io.DisplaySize.y - 105);

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDecoration;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("File Explorer", nullptr, window_flags)) {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.23f, 0.23f, 0.23f, 1.0f)); // Hover
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));     // Clicked

        ImGui::BeginChild("##filelist", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        for (int i = 0, j = 0; i < (int)head->files.size() || j < head->folders.size();) {
            string name;
            bool isFolder;
            int id;

            if (i >= head->files.size() || (j < head->folders.size() && head->files[i].second > head->folders[j].first.second)) {
                name = head->folders[j].first.first;
                isFolder = true;
                id = head->folders[j].first.second;
                j++;
            }
            else {
                name = head->files[i].first;
                isFolder = false;
                id = head->files[i].second;
                i++;
            }

            ImGui::PushID(id);
            bool isSelected = selectedIndices.count(id) > 0;

            if (isSelected)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));

            if (isFolder)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.8f, 0.2f, 1.0f));

            if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                bool ctrl = ImGui::GetIO().KeyCtrl;
                bool shift = ImGui::GetIO().KeyShift;

                if (shift && lastSelectedIndex.size() > 0) {
                    int start = min(lastSelectedIndex.back(), id);
                    int end = max(lastSelectedIndex.back(), id);

                    if (!ctrl)
                        selectedIndices.clear();

                    for (auto k : head->files)
                        if (k.second >= start && k.second <= end)
                            selectedIndices.insert(k.second);
                    
                    for (auto k : head->folders)
                        if (k.first.second >= start && k.first.second <= end)
                            selectedIndices.insert(k.first.second);
                }
                else if (ctrl) {
                    if (isSelected) {
                        selectedIndices.erase(id);
                        auto it = find(lastSelectedIndex.begin(), lastSelectedIndex.end(), id);
                        if (it != lastSelectedIndex.end())
                            lastSelectedIndex.erase(it);
                    }
                    else {
                        selectedIndices.insert(id);
                        lastSelectedIndex.push_back(id);
                    }
                }
                else {
                    selectedIndices.clear();
                    selectedIndices.insert(id);
                    lastSelectedIndex.clear();
                    lastSelectedIndex.push_back(id);
                }

                if (ImGui::IsMouseDoubleClicked(0) && !globalProgress.active) {
                    if (isFolder) {
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                        ImGui::PopStyleColor(2);
                        ImGui::EndChild();
                        ImGui::PopStyleColor(2);
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();

                        ImGui::End();

                        selectedIndices.clear();
                        lastSelectedIndex.clear();

                        head = head->folders[j - 1].second;

                        ShowFileExplorerWindow(head);

                        return;
                    }
                    else
                    {
                        processInProgress = true;
                        thread t([id, name, &head]
                        {
                            string newPath = filesystem::temp_directory_path().string();

                            globalProgress.progress = 0;
                            globalProgress.active = true;

                            Decompress(newPath, tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, {id}, globalProgress.progress);
                            if(archive_corrupted) {
                                decompressedFileAddress = "ARCHIVE CORRUPTED";
                                realCompressedFileAddress = "";
                                head = nullptr;

                                return;
                            }

                            ShellExecuteA(nullptr, "open", (newPath + "\\" + name).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

                            openedFiles.push_back(newPath + "\\" + name); 
                        });

                        t.detach();
                    }
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FILE_ITEM")) {
                    bool ok = false;

                    for (auto i : head->folders)
                        if (i.first.second == id) {
                            ok = true;
                            break;
                        }
                    if (ok) {
                        thread t([&head, id]
                        {
                            MoveFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, vector<int>(selectedIndices.begin(), selectedIndices.end()), id + 1, globalProgress.progress);
                            if(archive_corrupted) {
                                decompressedFileAddress = "ARCHIVE CORRUPTED";
                                realCompressedFileAddress = "";
                                head = nullptr;
                                return;
                            }

                            stack<string> q;
                            string temp = "";

                            for (int i = (int)head->path.length() - 1; i > 0; i--) {
                                if (head->path[i] != '/')
                                    temp += head->path[i];
                                else {
                                    reverse(temp.begin(), temp.end());
                                    q.push(temp);
                                    temp = "";
                                }
                            }

                            vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                            if(archive_corrupted) {
                                decompressedFileAddress = "ARCHIVE CORRUPTED";
                                realCompressedFileAddress = "";
                                head = nullptr;
                                return;
                            }

                            int idx_fileTree = 0;
                            head = buildFileTree(v, idx_fileTree, nullptr, "./");
                            selectedIndices.clear();
                            lastSelectedIndex.clear();

                            while (!q.empty()) {
                                for (auto i : head->folders) {
                                    if (i.first.first == q.top()) {
                                        head = i.second;
                                        break;
                                    }
                                }

                                q.pop();
                            }
                        });

                        globalProgress.active = true;
                        processInProgress = true;
                        globalProgress.progress = 0;

                        t.detach();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (isFolder)
                ImGui::PopStyleColor();

            if (isSelected)
                ImGui::PopStyleColor();

            if (ImGui::BeginDragDropSource()) {
                if (!isSelected) {
                    selectedIndices.clear();
                    selectedIndices.insert(id);
                    lastSelectedIndex.clear();
                    lastSelectedIndex.push_back(id);
                }

                ImGui::SetDragDropPayload("FILE_ITEM", name.c_str(), name.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            selectedIndices.clear();
            lastSelectedIndex.clear();
        }

        ImGui::PopStyleColor(2);

        ImGui::EndChild();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::End();
}

void ShowPath(fileTree *&head) {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | 
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | 
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | 
                                    ImGuiWindowFlags_NoScrollbar;

    ImGuiIO &io = ImGui::GetIO();

    float windowWidth = io.DisplaySize.x * 0.8f;
    float windowX = (io.DisplaySize.x - windowWidth) / 2.0f;
    ImVec2 winPos = ImVec2(windowX + 10, 75);
    ImVec2 winSize = ImVec2(windowWidth, 30);

    string newPath = head->path;
    int pathLength = (int)head->path.length();

    if (ImGui::CalcTextSize((newPath).c_str()).x > windowWidth) {
        while (ImGui::CalcTextSize((".../" + newPath).c_str()).x > windowWidth) {
            int idx = 0;
            while (idx < newPath.length() && newPath[idx] != '/')
                idx++;
            if (idx == (int)newPath.length())
                break;
            
            newPath = newPath.substr(idx + 1);
        }

        newPath.insert(0, ".../");
    }

    ImGui::SetNextWindowPos(winPos);
    ImGui::SetNextWindowSize(winSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16, 0.16, 0.16, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));

    ImGui::Begin("##Path", nullptr, window_flags);

    ImGui::SetCursorPos(ImVec2(10, 8));
    ImVec2 textSize = ImGui::CalcTextSize(newPath.c_str());
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##DropTargetForPath", textSize);

    ImGui::SetCursorScreenPos(cursorPos);
    ImGui::TextUnformatted(newPath.c_str());

    if (head->parent != nullptr) {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FILE_ITEM")) {
                const char *droppedName = (const char *)payload->Data;

                thread t([&head]
                {
                    int mi = INT_MAX;
                    for (auto i : head->parent->files)
                        mi = min(mi, i.second);
                    for (auto i : head->parent->folders)
                        mi = min(mi, i.first.second);

                    MoveFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, vector<int>(selectedIndices.begin(), selectedIndices.end()), mi, globalProgress.progress);
                    if(archive_corrupted) {
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        return;
                    }

                    stack<string> q;
                    string temp = "";

                    for (int i = (int)head->path.length() - 1; i > 0; i--) {
                        if (head->path[i] != '/')
                            temp += head->path[i];
                        else {
                            reverse(temp.begin(), temp.end());
                            q.push(temp);
                            temp = "";
                        }
                    }

                    vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                    if(archive_corrupted) {
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        return;
                    }
                    int idx_fileTree = 0;
                    head = buildFileTree(v, idx_fileTree, nullptr, "./");
                    selectedIndices.clear();
                    lastSelectedIndex.clear();

                    while (q.size() > 1) {
                        for (auto i : head->folders) {
                            if (i.first.first == q.top()) {
                                head = i.second;
                                break;
                            }
                        }
                        q.pop();
                    } 
                });

                globalProgress.active = true;
                processInProgress = true;
                globalProgress.progress = 0;

                t.detach();
            }

            ImGui::EndDragDropTarget();
        }
    }

    ImGui::PopStyleColor(3);

    ImGui::End();
}

void DeleteContent(fileTree *&head) {
    if (selectedIndices.size() > 0) {
        if (globalProgress.active)
            return;

        globalProgress.progress = 0;
        globalProgress.active = true;
        processInProgress = true;

        thread t([&head]()
        { 
            DeleteFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, vector<int>(selectedIndices.begin(), selectedIndices.end()), globalProgress.progress);
            if(archive_corrupted) {
                decompressedFileAddress = "ARCHIVE CORRUPTED";
                realCompressedFileAddress = "";
                head = nullptr;
                return;
            }
            
            stack<string> q;
            string temp = "";

            for (int i = (int)head->path.length() - 1; i > 0; i--) {
                if (head->path[i] != '/')
                    temp += head->path[i];
                else {
                    reverse(temp.begin(), temp.end());
                    q.push(temp);
                    temp = "";
                }
            }

            vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
            if(archive_corrupted) {
                decompressedFileAddress = "ARCHIVE CORRUPTED";
                realCompressedFileAddress = "";
                head = nullptr;
                return;
            }

            int idx_fileTree = 0;
            head = buildFileTree(v, idx_fileTree, nullptr, "./");
            selectedIndices.clear();
            lastSelectedIndex.clear();

            while (!q.empty()) {
                for (auto i : head->folders) {
                    if (i.first.first == q.top()) {
                        head = i.second;
                        break;
                    }
                }
                q.pop();
            } 
        });

        t.detach();
    }
}

void AddFile(fileTree *&head) {
    static bool dropThreadActive = false;

    if (!dropThreadActive) {
        dropThreadActive = true;

        if (realCompressedFileAddress != "" || tempFileAddress != "") {
            int mi = INT_MAX;
            if (head->files.size() > 0 || head->folders.size() > 0) {
                for (auto k : head->files)
                    mi = min(mi, k.second);
                for (auto k : head->folders)
                    mi = min(mi, k.first.second);

                thread t([&head, mi]
                {
                    while(!filesToAdd.empty()) {
                        if(archive_corrupted) {
                            filesToAdd.pop();
                            continue;
                        }
                        globalProgress.progress = 0;
                        globalProgress.active = true;

                        InsertFile(filesToAdd.front(), tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, mi, globalProgress.progress);
                        if(archive_corrupted) {
                            decompressedFileAddress = "ARCHIVE CORRUPTED";
                            realCompressedFileAddress = "";
                            head = nullptr;
                            continue;
                        }

                        stack<string> q;
                        string temp = "";

                        for (int i = (int)head->path.length() - 1; i > 0; i--) {
                            if (head->path[i] != '/')
                                temp += head->path[i];
                            else {
                                reverse(temp.begin(), temp.end());
                                q.push(temp);
                                temp = "";
                            }
                        }

                        if(!archive_corrupted) {
                            vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                            if(archive_corrupted) {
                                decompressedFileAddress = "ARCHIVE CORRUPTED";
                                realCompressedFileAddress = "";
                                head = nullptr;
                                continue;
                            }

                            int idx_fileTree = 0;
                            head = buildFileTree(v, idx_fileTree, nullptr, "./");
                            selectedIndices.clear();
                            lastSelectedIndex.clear();

                            while (!q.empty()) {
                                for (auto i : head->folders) {
                                    if (i.first.first == q.top()) {
                                        head = i.second;
                                        break;
                                    }
                                }
                                q.pop();
                            }
                        }

                        filesToAdd.pop();
                    } 
                    
                    dropThreadActive = false; 
                });

                t.detach();
            }
            else
            {
                thread t([&head]
                {
                    globalProgress.progress = 0;
                    globalProgress.active = true;
                    Compress({filesToAdd.front()}, tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, globalProgress.progress);

                    if(archive_corrupted) {
                        while(!filesToAdd.empty())
                            filesToAdd.pop();
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        dropThreadActive = false;

                        return;
                    }

                    filesToAdd.pop();
                                
                    stack<string> q;
                    string temp = "";

                    for (int i = (int)head->path.length() - 1; i > 0; i--) {
                        if (head->path[i] != '/')
                            temp += head->path[i];
                        else {
                            reverse(temp.begin(), temp.end());
                            q.push(temp);
                            temp = "";
                        }
                    }

                    vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                    if(archive_corrupted) {
                        while(!filesToAdd.empty())
                            filesToAdd.pop();
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        dropThreadActive = false;

                        return;
                    }

                    int idx_fileTree = 0;
                    head = buildFileTree(v, idx_fileTree, nullptr, "./");
                    selectedIndices.clear();
                    lastSelectedIndex.clear();

                    while (!q.empty()) {
                        for (auto i : head->folders) {
                            if (i.first.first == q.top()) {
                                head = i.second;
                                break;
                            }
                        }
                        q.pop();
                    }

                    while(!filesToAdd.empty()) {
                        globalProgress.progress = 0;
                        globalProgress.active = true;

                        InsertFile(filesToAdd.front(), tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, 0, globalProgress.progress);
                        if(archive_corrupted) {
                            while(!filesToAdd.empty())
                                filesToAdd.pop();
                            decompressedFileAddress = "ARCHIVE CORRUPTED";
                            realCompressedFileAddress = "";
                            head = nullptr;
                            dropThreadActive = false;

                            return;
                        }

                        stack<string> q;
                        string temp = "";

                        for (int i = (int)head->path.length() - 1; i > 0; i--) {
                            if (head->path[i] != '/')
                                temp += head->path[i];
                            else {
                                reverse(temp.begin(), temp.end());
                                q.push(temp);
                                temp = "";
                            }
                        }

                        vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                        if(archive_corrupted) {
                            while(!filesToAdd.empty())
                                filesToAdd.pop();
                            decompressedFileAddress = "ARCHIVE CORRUPTED";
                            realCompressedFileAddress = "";
                            head = nullptr;
                            dropThreadActive = false;

                            return;
                        }

                        int idx_fileTree = 0;
                        head = buildFileTree(v, idx_fileTree, nullptr, "./");
                        selectedIndices.clear();
                        lastSelectedIndex.clear();

                        while (!q.empty()) {
                            for (auto i : head->folders) {
                                if (i.first.first == q.top()) {
                                    head = i.second;
                                    break;
                                }
                            }
                            q.pop();


                        }

                        filesToAdd.pop();
                    } 
                    
                    dropThreadActive = false; 
                });

                t.detach();
            }
        }
        else
        {
            tempFileAddress = getAvailableFilename(filesystem::temp_directory_path().string());

            thread t([&head]
            {
                globalProgress.progress = 0;
                globalProgress.active = true;

                Compress({filesToAdd.front()}, tempFileAddress, globalProgress.progress);
                if(archive_corrupted) {
                    while(!filesToAdd.empty())
                        filesToAdd.pop();
                    decompressedFileAddress = "ARCHIVE CORRUPTED";
                    realCompressedFileAddress = "";
                    head = nullptr;
                    dropThreadActive = false;

                    return;
                }
                
                filesToAdd.pop();
                decompressedFileAddress = "Temporary file";
                int idx_fileTree = 0;

                vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress);
                if(archive_corrupted) {
                    while(!filesToAdd.empty())
                        filesToAdd.pop();
                    decompressedFileAddress = "ARCHIVE CORRUPTED";
                    realCompressedFileAddress = "";
                    head = nullptr;
                    dropThreadActive = false;

                    return;
                }

                head = buildFileTree(v, idx_fileTree, nullptr, "./");

                selectedIndices.clear();
                lastSelectedIndex.clear();

                while(!filesToAdd.empty()) {
                    globalProgress.progress = 0;
                    globalProgress.active = true;

                    InsertFile(filesToAdd.front(), tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, 0, globalProgress.progress);
                    if(archive_corrupted) {
                        while(!filesToAdd.empty())
                            filesToAdd.pop();
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        dropThreadActive = false;

                        return;
                    }

                    globalProgress.progress = 0;
                    globalProgress.active = false;
                    stack<string> q;
                    string temp = "";

                    for (int i = (int)head->path.length() - 1; i > 0; i--) {
                        if (head->path[i] != '/')
                            temp += head->path[i];
                        else {
                            reverse(temp.begin(), temp.end());
                            q.push(temp);
                            temp = "";
                        }
                    }

                    vector<pair<string, bool>> v = GetCompressedFiles(tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress);
                    if(archive_corrupted) {
                        while(!filesToAdd.empty())
                            filesToAdd.pop();
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                        dropThreadActive = false;

                        return;
                    }

                    int idx_fileTree = 0;
                    head = buildFileTree(v, idx_fileTree, nullptr, "./");

                    while (!q.empty()) {
                        for (auto i : head->folders) {
                            if (i.first.first == q.top()) {
                                head = i.second;
                                break;
                            }
                        }
                        q.pop();
                    }

                    filesToAdd.pop();
                } 
                
                dropThreadActive = false; 
            });

            t.detach();
        }
    }
}

int main(int argc, char *argv[])
{
    int idx_fileTree = 0;
    fileTree *head = nullptr;

    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("AZipper", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    int fb_width, fb_height;
    SDL_GL_GetDrawableSize(window, &fb_width, &fb_height);
    int win_width, win_height;
    SDL_GetWindowSize(window, &win_width, &win_height);
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    io.DisplayFramebufferScale = ImVec2(
        (float)fb_width / (float)win_width,
        (float)fb_height / (float)win_height);
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init();

    ImGui::GetIO().IniFilename = nullptr;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
    SDL_GL_SetSwapInterval(1); // vsync on

    bool show_demo = false;
    char input1[128] = "";
    char input2[128] = "";

    decompressedFileAddress.resize(1000);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

    bool done = false;
    bool close_window = false;
    bool showPopup = true;

    while (!done) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                if (tempFileAddress != "" && showPopup) {
                    openPopup = true;
                    close_window = true;
                }
                else
                    done = true;

            if (event.type == SDL_DROPFILE) {
                showPopup = true;
                char *dropped_filedir = event.drop.file;
                string filePath(dropped_filedir);
                SDL_free(dropped_filedir);

                if (processInProgress)
                    continue;

                filesToAdd.push(filePath);

                AddFile(head);
            }
        }

        // Start frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 25));
        glViewport(0, 0, fb_width, fb_height);

        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !globalProgress.active) {
            if (selectedIndices.size() > 0)
                showPopup = true;
            
            DeleteContent(head);
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16, 0.16, 0.16, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));
        ImGui::Begin("##TopBar", nullptr, window_flags);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.23f, 0.23f, 0.23f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.5f);

        if (ImGui::Button("Open")) {
            if (tempFileAddress != "" && showPopup)
                openPopup = true;
            else {
                archive_corrupted = false;

                string address = OpenFileDialog(false, true);
                if (address != "") {
                    if (tempFileAddress != "")
                        remove(tempFileAddress.c_str());
                    
                    tempFileAddress = "";
                    showPopup = true;
                    idx_fileTree = 0;

                    vector<pair<string, bool>> v = GetCompressedFiles(address);
                    if (!archive_corrupted)
                    {
                        head = buildFileTree(v, idx_fileTree, nullptr, "./");
                        decompressedFileAddress = address;
                        realCompressedFileAddress = address;
                    }
                    else
                    {
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                    }

                    selectedIndices.clear();
                    lastSelectedIndex.clear();

                    if (tempFileAddress != "")
                        remove(tempFileAddress.c_str());
                    tempFileAddress = "";
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Save") && tempFileAddress != "") {
            string address = openSaveFileDialog(true);
            if (address != "") {
                try
                {
                    if (filesystem::exists(address))
                        filesystem::remove(address);

                    filesystem::copy_file(tempFileAddress, address, filesystem::copy_options::overwrite_existing);

                    remove(tempFileAddress.c_str());

                    idx_fileTree = 0;
                    vector<pair<string, bool>> v = GetCompressedFiles(address);
                    if (!archive_corrupted)
                    {
                        head = buildFileTree(v, idx_fileTree, nullptr, "./");
                        decompressedFileAddress = address;
                        realCompressedFileAddress = address;
                    }
                    else
                    {
                        head = nullptr;
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                    }

                    tempFileAddress = "";
                    selectedIndices.clear();
                    lastSelectedIndex.clear();
                }
                catch (const filesystem::filesystem_error &e)
                {
                    cerr << "Error at saving the file: " << e.what() << endl;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("New")) {
            if (tempFileAddress != "" && showPopup)
                openPopup = true;
            else {
                string newAddress = openSaveFileDialog(true);

                if (newAddress != "") {
                    showPopup = true;
                    if (tempFileAddress != "")
                        remove(tempFileAddress.c_str());
                    tempFileAddress = "";

                    Compress({}, newAddress, globalProgress.progress);

                    decompressedFileAddress = newAddress;
                    realCompressedFileAddress = newAddress;

                    vector<pair<string, bool>> v;

                    idx_fileTree = 0;
                    head = buildFileTree(v, idx_fileTree, nullptr, "./");

                    selectedIndices.clear();
                    lastSelectedIndex.clear();
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Compress File")) {
            showPopup = true;
            string filePath = OpenFileDialog(true);

            if (filePath != "") {
                processInProgress = true;
                filesToAdd.push(filePath);

                AddFile(head);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Compress folder")) {
            showPopup = true;
            string filePath = OpenFolderDialog();

            if (filePath != "") {
                processInProgress = true;
                filesToAdd.push(filePath);

                AddFile(head);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Decompress") && (!selectedIndices.empty() || head != nullptr)) {
            string address = OpenFolderDialog();
            if (address != "") {
                globalProgress.active = true;
                processInProgress = true;
                globalProgress.progress = 0;

                if (!selectedIndices.empty())
                {
                    thread t([&address, &head]()
                    { 
                        Decompress(address, tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, vector<int>(selectedIndices.begin(), selectedIndices.end()), globalProgress.progress);
                        if(archive_corrupted) {
                            decompressedFileAddress = "ARCHIVE CORRUPTED";
                            realCompressedFileAddress = "";
                            head = nullptr;
                        }
                    });

                    t.detach();
                }
                else
                {
                    thread t([&address, &head]()
                    { 
                        Decompress(address, tempFileAddress != "" ? tempFileAddress : realCompressedFileAddress, globalProgress.progress);
                        if(archive_corrupted) {
                            decompressedFileAddress = "ARCHIVE CORRUPTED";
                            realCompressedFileAddress = "";
                            head = nullptr;
                        }
                    });

                    t.detach();
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            if (selectedIndices.size() > 0)
                showPopup = true;
            
            DeleteContent(head);
        }

        ImGui::SameLine();
        if (ImGui::Button("Deselect")) {
            selectedIndices.clear();
            lastSelectedIndex.clear();
        }

        ImGui::SameLine();
        if (ImGui::Button("Exit")) {
            if (tempFileAddress != "" && showPopup) {
                openPopup = true;
                close_window = true;
            }
            else
                done = true;
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);

        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

        ImGui::SetNextWindowPos(ImVec2(0, 35));
        ImGui::SetNextWindowSize(ImVec2(50, 45));

        ImGuiWindowFlags backBtnFlags = ImGuiWindowFlags_NoTitleBar | 
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | 
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | 
                                        ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoDecoration;

        ImGui::Begin("##BackButtonWindow", nullptr, backBtnFlags);

        float buttonSize = 28.0f;
        ImGui::SetCursorPos(ImVec2(12, 8.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, buttonSize * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        if (ImGui::Button("<", ImVec2(buttonSize, buttonSize))) {
            if (head && head->parent)
                head = head->parent;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGuiWindowFlags input_flags = ImGuiWindowFlags_NoTitleBar | 
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | 
                                       ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | 
                                       ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoDecoration;

        static char filePath[512] = "";

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16, 0.16, 0.16, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));

        float window_width = io.DisplaySize.x;
        float input_width = window_width * 0.6f;
        float input_pos_x = (window_width - input_width) * 0.5f;

        ImGui::SetNextWindowPos(ImVec2(input_pos_x, 35));
        ImGui::SetNextWindowSize(ImVec2(input_width, 45));

        ImGui::Begin("##FilePathBox", nullptr, input_flags);

        ImGui::SetCursorPos(ImVec2(10, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 8));

        ImGui::SetWindowFontScale(1.15f);

        ImGui::PushItemWidth(input_width - 20);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

        if (ImGui::InputText("##input", &decompressedFileAddress[0], 1000, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (tempFileAddress != "" && showPopup)
                openPopup = true;
            else {
                if (decompressedFileAddress != "") {
                    archive_corrupted = false;
                    decompressedFileAddress = string(decompressedFileAddress.c_str());
                    idx_fileTree = 0;
                    if (tempFileAddress != "")
                        remove(tempFileAddress.c_str());
                    tempFileAddress = "";

                    vector<pair<string, bool>> v = GetCompressedFiles(decompressedFileAddress);
                    if (!archive_corrupted) {
                        head = buildFileTree(v, idx_fileTree, nullptr, "./");
                        realCompressedFileAddress = decompressedFileAddress;
                    }
                    else {
                        decompressedFileAddress = "ARCHIVE CORRUPTED";
                        realCompressedFileAddress = "";
                        head = nullptr;
                    }

                    selectedIndices.clear();
                    lastSelectedIndex.clear();
                }
            }
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();

        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::End();

        if (head) {
            ShowFileExplorerWindow(head);
            ShowPath(head);
        }

        if (openPopup) {
            int result = ShowSaveProgressPopup();

            if (result != -1) {
                if (result == 1) {
                    string address = openSaveFileDialog();
                    if (address != "") {
                        try
                        {
                            if (filesystem::exists(address))
                                filesystem::remove(address);

                            filesystem::copy_file(tempFileAddress, address, filesystem::copy_options::overwrite_existing);

                            remove(tempFileAddress.c_str());

                            idx_fileTree = 0;
                            vector<pair<string, bool>> v = GetCompressedFiles(address);
                            if(archive_corrupted) {
                                decompressedFileAddress = "ARCHIVE CORRUPTED";
                                realCompressedFileAddress = "";
                                head = nullptr;
                            }
                            else {
                                head = buildFileTree(v, idx_fileTree, nullptr, "./");
                                decompressedFileAddress = address;
                                realCompressedFileAddress = address;
                            }

                            if (tempFileAddress != "")
                                remove(tempFileAddress.c_str());
                            tempFileAddress = "";

                            selectedIndices.clear();
                            lastSelectedIndex.clear();
                        }
                        catch (const filesystem::filesystem_error &e)
                        {
                            cerr << "Error at saving the file: " << e.what() << endl;
                        }
                    }
                }

                showPopup = false;
                openPopup = false;

                if (close_window)
                    done = true;
            }
        }

        ShowProgressBar();

        // Render
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    for (auto i : openedFiles)
        remove(i.c_str());

    if (tempFileAddress != "")
        remove(tempFileAddress.c_str());

    return 0;
}

//To compile use:
// g++ testUI2.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp  imgui/backends/imgui_impl_sdl2.cpp imgui/backends/imgui_impl_opengl3.cpp Compressor.cpp Utils.cpp Globals.cpp -Iimgui -Iimgui/backends -lmingw32 -lSDL2main -lSDL2 -lcomdlg32 -lopengl32 -lole32 -luuid -lcomctl32 -lcomdlg32 -o  app.exe; ./app.exe
