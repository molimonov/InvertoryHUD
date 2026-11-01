#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "installer/payload.h"
#include "installer/resource.h"

namespace {

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring TrimPath(std::wstring path)
{
    while (!path.empty() && (path.back() == L' ' || path.back() == L'\t' || path.back() == L'\r' ||
                             path.back() == L'\n')) {
        path.pop_back();
    }
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

bool WritePayload(const std::wstring& path, const unsigned char* data, size_t size)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < size) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1u << 20));
        if (!WriteFile(file, data + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

bool CreateShortcut(const std::wstring& exe_path, const std::wstring& working_directory,
                    const std::wstring& shortcut_path)
{
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void**>(&link)))) {
        return false;
    }
    link->SetPath(exe_path.c_str());
    link->SetWorkingDirectory(working_directory.c_str());
    link->SetIconLocation(exe_path.c_str(), 0);
    link->SetDescription(L"The Settlers II - InvertoryHUD");

    IPersistFile* file = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        ok = SUCCEEDED(file->Save(shortcut_path.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

std::wstring DesktopDirectory()
{
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return path;
    }
    return {};
}

std::wstring CommandLinePathArgument()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring result;
    if (argv != nullptr) {
        if (argc > 1) {
            result = argv[1];
        }
        LocalFree(argv);
    }
    return result;
}

bool Install(HWND hwnd, const std::wstring& selected_path, bool create_shortcut, std::wstring& message)
{
    const std::wstring root = TrimPath(selected_path);
    if (root.empty()) {
        message = L"Select the game folder first.";
        return false;
    }
    if (!DirectoryExists(root)) {
        message = L"The selected folder does not exist.";
        return false;
    }

    std::wstring target;
    if (FileExists(root + L"\\bin\\s2dng_addon.exe")) {
        target = root + L"\\bin";
    } else if (FileExists(root + L"\\s2dng_addon.exe")) {
        target = root;
    } else {
        message = L"s2dng_addon.exe was not found.\n\n"
                  L"Select the game folder that contains bin\\s2dng_addon.exe.";
        return false;
    }

    if (!WritePayload(target + L"\\InvertoryHUD.dll", kPayloadDll, kPayloadDllSize)) {
        message = L"Could not write InvertoryHUD.dll.\n\n"
                  L"Close the game and run the installer as administrator if the game is in Program Files.";
        return false;
    }
    if (!WritePayload(target + L"\\S2_HUD.exe", kPayloadLauncher, kPayloadLauncherSize)) {
        message = L"Could not write S2_HUD.exe.";
        return false;
    }
    if (!FileExists(target + L"\\InvertoryHUD.ini")) {
        WritePayload(target + L"\\InvertoryHUD.ini", kPayloadIni, kPayloadIniSize);
    }

    std::wstring shortcut_note;
    if (create_shortcut) {
        const std::wstring desktop = DesktopDirectory();
        if (!desktop.empty() && CreateShortcut(target + L"\\S2_HUD.exe", target,
                                               desktop + L"\\The Settlers II HUD.lnk")) {
            shortcut_note = L"\n\nA desktop shortcut was created.";
        } else {
            shortcut_note = L"\n\nCould not create the desktop shortcut.";
        }
    }

    message = L"InvertoryHUD installed successfully:\n" + target +
              L"\n\nRun S2_HUD.exe to start the game with the HUD." + shortcut_note;
    (void)hwnd;
    return true;
}

INT_PTR CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG: {
        const HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                                        MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0,
                                                        LR_DEFAULTSIZE));
        if (icon != nullptr) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
        CheckDlgButton(hwnd, IDC_SHORTCUT, BST_CHECKED);
        const std::wstring argument = CommandLinePathArgument();
        if (!argument.empty()) {
            SetDlgItemTextW(hwnd, IDC_PATH, argument.c_str());
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_BROWSE: {
            BROWSEINFOW browse = {};
            browse.hwndOwner = hwnd;
            browse.lpszTitle = L"Select The Settlers II game folder";
            browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
            if (item != nullptr) {
                wchar_t path[MAX_PATH] = {};
                if (SHGetPathFromIDListW(item, path)) {
                    SetDlgItemTextW(hwnd, IDC_PATH, path);
                }
                CoTaskMemFree(item);
            }
            return TRUE;
        }
        case IDOK: {
            wchar_t path[MAX_PATH * 2] = {};
            GetDlgItemTextW(hwnd, IDC_PATH, path, static_cast<int>(std::size(path)));
            std::wstring message_text;
            const bool create_shortcut = IsDlgButtonChecked(hwnd, IDC_SHORTCUT) == BST_CHECKED;
            const bool ok = Install(hwnd, path, create_shortcut, message_text);
            MessageBoxW(hwnd, message_text.c_str(), L"InvertoryHUD Setup",
                        MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            if (ok) {
                EndDialog(hwnd, IDOK);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INSTALLER),
                                           nullptr, DialogProc, 0);
    CoUninitialize();
    return result == -1 ? 1 : 0;
}
