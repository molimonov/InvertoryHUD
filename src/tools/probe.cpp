#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

DWORD FindProcess(const wchar_t* name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

class Reader {
public:
    explicit Reader(HANDLE process) : process_(process) {}

    bool Read(uintptr_t address, void* out, size_t size) const
    {
        SIZE_T read = 0;
        return ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), out, size, &read) &&
               read == size;
    }

    template <typename T>
    bool ReadValue(uintptr_t address, T& out) const
    {
        return Read(address, &out, sizeof(T));
    }

    bool Readable(uintptr_t address, size_t size) const
    {
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQueryEx(process_, reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
            return false;
        }
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
            return false;
        }
        const uintptr_t start = reinterpret_cast<uintptr_t>(info.BaseAddress);
        return address >= start && address + size <= start + info.RegionSize;
    }

private:
    HANDLE process_ = nullptr;
};

void DumpMapNode(const Reader& reader, uintptr_t node, int depth, int& printed, int& visited)
{
    if (depth > 32 || printed >= 64 || visited >= 256 || node < 0x10000 || !reader.Readable(node, 0x18)) {
        return;
    }
    ++visited;
    uintptr_t left = 0;
    uintptr_t right = 0;
    int key = 0;
    int value = 0;
    unsigned char nil = 0;
    reader.ReadValue(node, left);
    reader.ReadValue(node + 8, right);
    reader.ReadValue(node + 0xC, key);
    reader.ReadValue(node + 0x10, value);
    reader.ReadValue(node + 0x15, nil);
    if (nil != 0) {
        return;
    }
    DumpMapNode(reader, left, depth + 1, printed, visited);
    wprintf(L"        map[%02d] key=0x%08X (%d) value=%d\n", printed, static_cast<unsigned>(key), key, value);
    ++printed;
    DumpMapNode(reader, right, depth + 1, printed, visited);
}

void DumpPlayerMap(const Reader& reader, uintptr_t player)
{
    const uintptr_t stock = player + 0x114;
    uintptr_t raw0 = 0;
    uintptr_t raw1 = 0;
    uintptr_t raw2 = 0;
    uintptr_t raw3 = 0;
    reader.ReadValue(stock + 0x00, raw0);
    reader.ReadValue(stock + 0x04, raw1);
    reader.ReadValue(stock + 0x08, raw2);
    reader.ReadValue(stock + 0x0C, raw3);
    wprintf(L"    stock@0x%08X: %08X %08X %08X %08X\n", static_cast<unsigned>(stock),
            static_cast<unsigned>(raw0), static_cast<unsigned>(raw1), static_cast<unsigned>(raw2),
            static_cast<unsigned>(raw3));

    // FUN_00461FB0 passes this+8 to std::map::find; map header is at map+4.
    const uintptr_t map = stock + 8;
    uintptr_t header = 0;
    reader.ReadValue(map + 4, header);
    uintptr_t root = 0;
    if (header != 0) {
        reader.ReadValue(header + 4, root);
    }
    wprintf(L"    map=0x%08X header=0x%08X root=0x%08X\n", static_cast<unsigned>(map),
            static_cast<unsigned>(header), static_cast<unsigned>(root));
    int printed = 0;
    int visited = 0;
    DumpMapNode(reader, root, 0, printed, visited);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* name = argc > 1 ? argv[1] : L"s2dng_addon.exe";
    DWORD pid = FindProcess(name);
    if (pid == 0) {
        pid = FindProcess(L"s2dng.exe");
    }
    if (pid == 0) {
        wprintf(L"Process not found.\n");
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr) {
        wprintf(L"OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }

    uintptr_t base = 0x400000;
    HMODULE modules[1024] = {};
    DWORD needed = 0;
    if (EnumProcessModules(process, modules, sizeof(modules), &needed) && needed >= sizeof(HMODULE)) {
        base = reinterpret_cast<uintptr_t>(modules[0]);
    }

    Reader reader(process);
    wprintf(L"PID=%lu base=0x%08X\n", pid, static_cast<unsigned>(base));

    uintptr_t graphic = 0;
    const uintptr_t graphic_global = base + (0x006D0854 - 0x00400000);
    if (!reader.ReadValue(graphic_global, graphic)) {
        wprintf(L"CGraphicDevice global 0x%08X unreadable\n", static_cast<unsigned>(graphic_global));
    } else {
        wprintf(L"CGraphicDevice global 0x%08X = 0x%08X\n", static_cast<unsigned>(graphic_global),
                static_cast<unsigned>(graphic));
        if (graphic != 0) {
            unsigned char ready = 0;
            uintptr_t d3d9 = 0;
            uintptr_t device = 0;
            reader.ReadValue(graphic + 0x60, ready);
            reader.ReadValue(graphic + 0xAC, d3d9);
            reader.ReadValue(graphic + 0xB0, device);
            wprintf(L"  +0x60 ready=%u  +0xAC d3d9=0x%08X  +0xB0 device=0x%08X\n", ready,
                    static_cast<unsigned>(d3d9), static_cast<unsigned>(device));
            if (device != 0) {
                uintptr_t vtable = 0;
                if (reader.ReadValue(device, vtable)) {
                    uintptr_t present = 0;
                    uintptr_t reset = 0;
                    reader.ReadValue(vtable + 0x40, reset);
                    reader.ReadValue(vtable + 0x44, present);
                    wprintf(L"  vtable=0x%08X  Reset=0x%08X  Present=0x%08X\n",
                            static_cast<unsigned>(vtable), static_cast<unsigned>(reset),
                            static_cast<unsigned>(present));
                    MEMORY_BASIC_INFORMATION info = {};
                    if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(vtable), &info, sizeof(info)) != 0) {
                        wprintf(L"  vtable state=0x%X protect=0x%X size=0x%X\n", info.State, info.Protect,
                                static_cast<unsigned>(info.RegionSize));
                    }
                }
            }
        }
    }

    uintptr_t manager = 0;
    const uintptr_t manager_global = base + (0x006CAC60 - 0x00400000);
    if (!reader.ReadValue(manager_global, manager)) {
        wprintf(L"GameManager global 0x%08X unreadable\n", static_cast<unsigned>(manager_global));
    } else {
        wprintf(L"GameManager global 0x%08X = 0x%08X\n", static_cast<unsigned>(manager_global),
                static_cast<unsigned>(manager));
        if (manager != 0) {
            uintptr_t players = 0;
            reader.ReadValue(manager + 0x14, players);
            wprintf(L"  players=0x%08X\n", static_cast<unsigned>(players));
            if (players != 0) {
                for (int i = 0; i < 8; ++i) {
                    const uintptr_t player = players + 4 + static_cast<uintptr_t>(i) * 500;
                    int index = -1;
                    int type = -1;
                    unsigned char active = 0;
                    uintptr_t stock_end = 0;
                    uintptr_t stock_root = 0;
                    reader.ReadValue(player + 4, index);
                    reader.ReadValue(player + 0x0C, type);
                    reader.ReadValue(player + 0x164, active);
                    reader.ReadValue(player + 0x114 + 0x0C, stock_end);
                    reader.ReadValue(player + 0x114 + 0x08, stock_root);
                    wprintf(L"  player[%d] ptr=0x%08X index=%d type=%d active=%u stock_root=0x%08X stock_end=0x%08X\n",
                            i, static_cast<unsigned>(player), index, type, active,
                            static_cast<unsigned>(stock_root), static_cast<unsigned>(stock_end));
                    if (active != 0 && type == 2) {
                        DumpPlayerMap(reader, player);
                    }
                }
            }
        }
    }

    CloseHandle(process);
    return 0;
}
