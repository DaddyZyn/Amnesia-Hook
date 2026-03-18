#include "../include/Amhk.h"
#include "../deps/hde64.h"
#include <tlhelp32.h>
#include <vector>
#include <atomic>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3d12.h>

namespace Amhook {

static std::atomic_flag s_vehLock = ATOMIC_FLAG_INIT;

HookManager::~HookManager() {
    DisableAll();
    if (m_vehHandler) {
        RemoveVectoredExceptionHandler(m_vehHandler);
    }
}

bool HookManager::Init() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_vehHandler) return true;
    m_vehHandler = AddVectoredExceptionHandler(1, VEHHandler);
    return m_vehHandler != nullptr;
}

static uint8_t* ResolveTarget(uint8_t* p) {
    if (p[0] == 0xE9) {
        int32_t rel = *(int32_t*)(p + 1);
        return ResolveTarget(p + 5 + rel);
    }
    if (p[0] == 0xFF && p[1] == 0x25) {
        int32_t rel = *(int32_t*)(p + 2);
        if (rel == 0) return p;
        return ResolveTarget(*(uint8_t**)(p + 6 + rel));
    }
    return p;
}

static void WriteAbsJmp(uint8_t* p, uint64_t target) {
    p[0] = 0xFF;
    p[1] = 0x25;
    *(uint32_t*)(p + 2) = 0;
    *(uint64_t*)(p + 6) = target;
}

static void WriteAbsCall(uint8_t* p, uint64_t target, uint64_t nextRip) {
    p[0] = 0x68;
    *(uint32_t*)(p + 1) = (uint32_t)(nextRip & 0xFFFFFFFF);
    p[5] = 0xC7;
    p[6] = 0x44;
    p[7] = 0x24;
    p[8] = 0x04;
    *(uint32_t*)(p + 9) = (uint32_t)(nextRip >> 32);
    WriteAbsJmp(p + 13, target);
}

static bool RelocateInstruction(uint8_t* src, uint8_t* dst, size_t& dstOffset, hde64s& hs, uint8_t* tg, size_t currentSrcOffset) {
    uint64_t srcAddr = (uint64_t)src;
    uint64_t dstAddr = (uint64_t)dst + dstOffset;
    memcpy(dst + dstOffset, src, hs.len);

    if (hs.flags & F_RELATIVE) {
        int64_t relOffset = 0;
        size_t immOffset = hs.len;
        size_t immSize = 0;

        if (hs.flags & F_IMM8) {
            relOffset = (int8_t)hs.imm.imm8;
            immSize = 1;
        } else if (hs.flags & F_IMM16) {
            relOffset = (int16_t)hs.imm.imm16;
            immSize = 2;
        } else if (hs.flags & F_IMM32) {
            relOffset = (int32_t)hs.imm.imm32;
            immSize = 4;
        }

        immOffset -= immSize;
        uint64_t absoluteDest = srcAddr + hs.len + relOffset;

        if (hs.opcode >= 0x70 && hs.opcode <= 0x7F) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + 6);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                dst[dstOffset] = 0x0F;
                dst[dstOffset + 1] = 0x80 | (hs.opcode & 0x0F);
                *(int32_t*)(dst + dstOffset + 2) = (int32_t)newRel32;
                dstOffset += 6;
                return true;
            } else {
                dst[dstOffset] = hs.opcode ^ 1;
                dst[dstOffset + 1] = 14;
                dstOffset += 2;
                WriteAbsJmp(dst + dstOffset, absoluteDest);
                dstOffset += 14;
                return true;
            }
        } else if (hs.opcode == 0x0F && (hs.opcode2 & 0xF0) == 0x80) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + 6);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                *(int32_t*)(dst + dstOffset + 2) = (int32_t)newRel32;
            } else {
                dst[dstOffset + 1] = hs.opcode2 ^ 1;
                *(int32_t*)(dst + dstOffset + 2) = 14;
                dstOffset += 6;
                WriteAbsJmp(dst + dstOffset, absoluteDest);
                dstOffset += 14;
                return true;
            }
        } else if (hs.opcode == 0xEB) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + 5);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                dst[dstOffset] = 0xE9;
                *(int32_t*)(dst + dstOffset + 1) = (int32_t)newRel32;
                dstOffset += 5;
                return true;
            } else {
                WriteAbsJmp(dst + dstOffset, absoluteDest);
                dstOffset += 14;
                return true;
            }
        } else if (hs.opcode == 0xE9) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + 5);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                *(int32_t*)(dst + dstOffset + 1) = (int32_t)newRel32;
            } else {
                WriteAbsJmp(dst + dstOffset, absoluteDest);
                dstOffset += 14;
                return true;
            }
        } else if (hs.opcode == 0xE8) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + 5);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                *(int32_t*)(dst + dstOffset + 1) = (int32_t)newRel32;
            } else {
                uint64_t nextRip = (uint64_t)tg + currentSrcOffset + hs.len;
                WriteAbsCall(dst + dstOffset, absoluteDest, nextRip);
                dstOffset += 27;
                return true;
            }
        } else if (hs.opcode >= 0xE0 && hs.opcode <= 0xE3) {
            dst[dstOffset + 1] = 2;
            dst[dstOffset + 2] = 0xEB;
            dst[dstOffset + 3] = 14;
            dstOffset += 4;
            WriteAbsJmp(dst + dstOffset, absoluteDest);
            dstOffset += 14;
            return true;
        } else if ((hs.modrm & 0xC7) == 0x05) {
            int64_t newRel32 = (int64_t)absoluteDest - (dstAddr + hs.len);
            if (newRel32 >= -2147483648LL && newRel32 <= 2147483647LL) {
                *(int32_t*)(dst + dstOffset + hs.len - 4) = (int32_t)newRel32;
            } else {
                return false;
            }
        }
    }

    dstOffset += hs.len;
    return true;
}

bool HookManager::CreateInlineHook(void* target, void* detour, void** original) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    if (m_hooks.find(actualTarget) != m_hooks.end()) return false;

    size_t totalLen = 0;
    hde64s hs;
    while (totalLen < 14) {
        unsigned int len = hde64_disasm(actualTarget + totalLen, &hs);
        if (len == 0 || (hs.flags & F_ERROR)) return false;
        totalLen += len;
    }

    void* trampoline = AllocateTrampoline(actualTarget, totalLen * 2 + 64);
    if (!trampoline) return false;

    HookInfo info = {};
    info.targetAddress = actualTarget;
    info.detourAddress = detour;
    info.trampolineAddress = trampoline;
    info.type = HookType::Inline;
    info.status = HookStatus::Disabled;
    info.patchSize = totalLen;
    info.drIndex = -1;
    memcpy(info.originalBytes, actualTarget, totalLen);

    uint8_t* tp = (uint8_t*)trampoline;
    uint8_t* tg = (uint8_t*)actualTarget;
    size_t currentSrcOffset = 0;
    size_t currentDstOffset = 0;

    while (currentSrcOffset < totalLen) {
        hde64_disasm(tg + currentSrcOffset, &hs);
        if (!RelocateInstruction(tg + currentSrcOffset, tp, currentDstOffset, hs, tg, currentSrcOffset)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        currentSrcOffset += hs.len;
    }

    WriteAbsJmp(tp + currentDstOffset, (uint64_t)actualTarget + totalLen);
    *original = trampoline;
    m_hooks[actualTarget] = info;
    return true;
}

bool HookManager::CreateVEHHook(void* target, void* detour, void** original) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    if (m_hooks.find(actualTarget) != m_hooks.end()) return false;

    size_t totalLen = 0;
    hde64s hs;
    unsigned int len = hde64_disasm(actualTarget, &hs);
    if (len == 0 || (hs.flags & F_ERROR)) return false;
    totalLen = len;

    void* trampoline = AllocateTrampoline(actualTarget, totalLen * 2 + 64);
    if (!trampoline) return false;

    HookInfo info = {};
    info.targetAddress = actualTarget;
    info.detourAddress = detour;
    info.trampolineAddress = trampoline;
    info.type = HookType::VEH;
    info.status = HookStatus::Disabled;
    info.patchSize = totalLen;
    info.drIndex = -1;
    memcpy(info.originalBytes, actualTarget, totalLen);

    uint8_t* tp = (uint8_t*)trampoline;
    uint8_t* tg = (uint8_t*)actualTarget;
    size_t currentSrcOffset = 0;
    size_t currentDstOffset = 0;

    while (currentSrcOffset < totalLen) {
        hde64_disasm(tg + currentSrcOffset, &hs);
        if (!RelocateInstruction(tg + currentSrcOffset, tp, currentDstOffset, hs, tg, currentSrcOffset)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        currentSrcOffset += hs.len;
    }

    WriteAbsJmp(tp + currentDstOffset, (uint64_t)actualTarget + totalLen);
    *original = trampoline;
    m_hooks[actualTarget] = info;
    return true;
}

bool HookManager::Enable(void* target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    auto it = m_hooks.find(actualTarget);
    if (it == m_hooks.end()) return false;
    auto& info = it->second;
    if (info.status == HookStatus::Enabled) return true;

    if (info.type == HookType::Inline) {
        SuspendOtherThreads();
        DWORD oldProtect;
        if (VirtualProtect(actualTarget, info.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            WriteAbsJmp((uint8_t*)actualTarget, (uint64_t)info.detourAddress);
            if (info.patchSize > 14) {
                memset((uint8_t*)actualTarget + 14, 0x90, info.patchSize - 14);
            }
            VirtualProtect(actualTarget, info.patchSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), actualTarget, info.patchSize);
        }
        ResumeOtherThreads();
    } else if (info.type == HookType::VEH) {
        int dr = FindAvailableDR();
        if (dr == -1) return false;
        if (SetHardwareBreakpoint(actualTarget, dr, true)) {
            info.drIndex = dr;
            m_vehHooks[dr].target = actualTarget;
            m_vehHooks[dr].detour = info.detourAddress;
            m_vehHooks[dr].drIndex = dr;
            m_vehHooks[dr].enabled = true;
        } else {
            m_drInUse[dr] = false;
            return false;
        }
    }

    info.status = HookStatus::Enabled;
    return true;
}

bool HookManager::Disable(void* target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    auto it = m_hooks.find(actualTarget);
    if (it == m_hooks.end()) return false;
    auto& info = it->second;
    if (info.status == HookStatus::Disabled) return true;

    if (info.type == HookType::Inline) {
        SuspendOtherThreads();
        DWORD oldProtect;
        if (VirtualProtect(actualTarget, info.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(actualTarget, info.originalBytes, info.patchSize);
            VirtualProtect(actualTarget, info.patchSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), actualTarget, info.patchSize);
        }
        ResumeOtherThreads();
    } else if (info.type == HookType::VEH) {
        SetHardwareBreakpoint(actualTarget, info.drIndex, false);
        m_vehHooks[info.drIndex].enabled = false;
        m_drInUse[info.drIndex] = false;
        info.drIndex = -1;
    }

    info.status = HookStatus::Disabled;
    return true;
}

struct ThreadState {
    void* lastBreakpointAddr = nullptr;
    int lastDrIndex = -1;
    bool isSingleStepping = false;
};

static thread_local ThreadState s_threadState;

LONG CALLBACK HookManager::VEHHandler(PEXCEPTION_POINTERS exceptionInfo) {
    while (s_vehLock.test_and_set(std::memory_order_acquire));
    auto& manager = GetInstance();

    if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        if (s_threadState.isSingleStepping) {
            exceptionInfo->ContextRecord->Dr7 &= ~(1ULL << (s_threadState.lastDrIndex * 2));
            switch (s_threadState.lastDrIndex) {
                case 0: exceptionInfo->ContextRecord->Dr0 = (uint64_t)s_threadState.lastBreakpointAddr; break;
                case 1: exceptionInfo->ContextRecord->Dr1 = (uint64_t)s_threadState.lastBreakpointAddr; break;
                case 2: exceptionInfo->ContextRecord->Dr2 = (uint64_t)s_threadState.lastBreakpointAddr; break;
                case 3: exceptionInfo->ContextRecord->Dr3 = (uint64_t)s_threadState.lastBreakpointAddr; break;
            }
            exceptionInfo->ContextRecord->Dr7 |= (1ULL << (s_threadState.lastDrIndex * 2));
            s_threadState.isSingleStepping = false;
            s_vehLock.clear(std::memory_order_release);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        void* exceptionAddr = (void*)exceptionInfo->ExceptionRecord->ExceptionAddress;
        for (int i = 0; i < 4; ++i) {
            if (manager.m_vehHooks[i].enabled && manager.m_vehHooks[i].target == exceptionAddr) {
                exceptionInfo->ContextRecord->Dr6 = 0;
                exceptionInfo->ContextRecord->Dr7 &= ~(1ULL << (manager.m_vehHooks[i].drIndex * 2));
                s_threadState.lastBreakpointAddr = manager.m_vehHooks[i].target;
                s_threadState.lastDrIndex = manager.m_vehHooks[i].drIndex;
                s_threadState.isSingleStepping = true;
                exceptionInfo->ContextRecord->EFlags |= 0x100;
                exceptionInfo->ContextRecord->Rip = (uint64_t)manager.m_vehHooks[i].detour;
                s_vehLock.clear(std::memory_order_release);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    s_vehLock.clear(std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}

bool HookManager::SuspendOtherThreads() {
    m_suspendedThreads.clear();
    DWORD currentThreadId = GetCurrentThreadId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hSnapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != currentThreadId) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread) {
                    SuspendThread(hThread);
                    m_suspendedThreads.push_back(te.th32ThreadID);
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnapshot, &te));
    }
    CloseHandle(hSnapshot);
    return true;
}

void HookManager::ResumeOtherThreads() {
    for (DWORD threadId : m_suspendedThreads) {
        HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadId);
        if (hThread) {
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
    }
    m_suspendedThreads.clear();
}

void* HookManager::AllocateTrampoline(void* target, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint64_t minAddr = (uint64_t)si.lpMinimumApplicationAddress;
    uint64_t maxAddr = (uint64_t)si.lpMaximumApplicationAddress;
    uint64_t targetAddr = (uint64_t)target;
    uint64_t start = (targetAddr > 0x7FFFFFFF) ? (targetAddr - 0x7FFFFFFF) : minAddr;
    uint64_t end = (targetAddr < maxAddr - 0x7FFFFFFF) ? (targetAddr + 0x7FFFFFFF) : maxAddr;
    start = (start + si.dwPageSize - 1) & ~(uint64_t)(si.dwPageSize - 1);

    for (uint64_t addr = start; addr < end; addr += si.dwPageSize) {
        void* allocated = VirtualAlloc((void*)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (allocated) return allocated;
    }
    return nullptr;
}

bool HookManager::SetHardwareBreakpoint(void* address, int index, bool enable) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hSnapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == GetCurrentProcessId()) {
                HANDLE hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread) {
                    if (te.th32ThreadID != GetCurrentThreadId()) SuspendThread(hThread);
                    CONTEXT ctx;
                    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(hThread, &ctx)) {
                        if (enable) {
                            switch (index) {
                                case 0: ctx.Dr0 = (uint64_t)address; break;
                                case 1: ctx.Dr1 = (uint64_t)address; break;
                                case 2: ctx.Dr2 = (uint64_t)address; break;
                                case 3: ctx.Dr3 = (uint64_t)address; break;
                            }
                            ctx.Dr7 |= (1ULL << (index * 2));
                            ctx.Dr7 &= ~(0xFULL << (16 + index * 4));
                        } else {
                            ctx.Dr7 &= ~(1ULL << (index * 2));
                        }
                        SetThreadContext(hThread, &ctx);
                    }
                    if (te.th32ThreadID != GetCurrentThreadId()) ResumeThread(hThread);
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnapshot, &te));
    }
    CloseHandle(hSnapshot);
    return true;
}

int HookManager::FindAvailableDR() {
    for (int i = 0; i < 4; i++) {
        if (!m_drInUse[i]) {
            m_drInUse[i] = true;
            return i;
        }
    }
    return -1;
}

bool HookManager::RemoveHook(void* target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Disable(target);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    auto it = m_hooks.find(actualTarget);
    if (it != m_hooks.end()) {
        auto& info = it->second;
        if (info.trampolineAddress) {
            VirtualFree(info.trampolineAddress, 0, MEM_RELEASE);
        }
        m_hooks.erase(actualTarget);
        return true;
    }
    return false;
}

bool HookManager::IsHooked(void* target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint8_t* actualTarget = ResolveTarget((uint8_t*)target);
    return m_hooks.find(actualTarget) != m_hooks.end();
}

bool HookManager::EnableAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& [target, info] : m_hooks) Enable(target);
    return true;
}

bool HookManager::DisableAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& [target, info] : m_hooks) Disable(target);
    return true;
}

static void* GetD3D9Device() {
    void* pDevice = nullptr;
    typedef IDirect3D9* (WINAPI* PFN_D3D_CREATE9)(UINT);
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) return nullptr;
    PFN_D3D_CREATE9 D3DCreate9 = (PFN_D3D_CREATE9)GetProcAddress(d3d9, "Direct3DCreate9");
    if (!D3DCreate9) return nullptr;
    IDirect3D9* pD3D = D3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return nullptr;
    
    HWND hWnd = GetDesktopWindow();
    UINT Adapter = D3DADAPTER_DEFAULT;
    D3DDISPLAYMODE d3ddm;
    if (FAILED(pD3D->GetAdapterDisplayMode(Adapter, &d3ddm))) {
        pD3D->Release();
        return nullptr;
    }
    
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hWnd;
    
    IDirect3DDevice9* pTempDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(Adapter, D3DDEVTYPE_HAL, hWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_PRINTSCREEN,
        &pp, &pTempDevice);
    
    if (SUCCEEDED(hr) && pTempDevice) {
        pDevice = (void*)pTempDevice;
        pTempDevice->Release();
    }
    pD3D->Release();
    return pDevice;
}

bool HookManager::HookD3D9(void* detour, void** original) {
    void* pDevice = GetD3D9Device();
    if (!pDevice) return false;
    
    void** pVMT = *(void***)pDevice;
    void* pPresent = pVMT[17]; // IDirect3DDevice9::Present is Index 17
    
    return CreateInlineHook(pPresent, detour, original);
}

bool HookManager::CreateOverlayHook(void* target, void* detour, void** original, HookType type) {
    if (type == HookType::Inline) {
        return CreateInlineHook(target, detour, original);
    } else if (type == HookType::VEH) {
        return CreateVEHHook(target, detour, original);
    }
    return false;
}

bool HookManager::HookD3D11(void* detour, void** original) {
    HWND hWnd = GetDesktopWindow();
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11Device* pDevice = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &sd, &pSwapChain, &pDevice, nullptr, nullptr))) {
        return false;
    }

    void** pVMT = *(void***)pSwapChain;
    void* pPresent = pVMT[8]; // IDXGISwapChain::Present is strictly Index 8

    bool result = CreateInlineHook(pPresent, detour, original);

    pSwapChain->Release();
    pDevice->Release();

    return result;
}

bool HookManager::HookD3D12(void* detour, void** original) {
    HMODULE d3d12 = GetModuleHandleA("d3d12.dll");
    if (!d3d12) return false;

    typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    PFN_D3D12CreateDevice D3D12CreateDevice = (PFN_D3D12CreateDevice)GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!D3D12CreateDevice) return false;

    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) return false;

    typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
    PFN_CreateDXGIFactory1 CreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!CreateDXGIFactory1) return false;

    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) return false;

    IDXGIAdapter1* pAdapter = nullptr;
    if (FAILED(pFactory->EnumAdapters1(0, &pAdapter))) {
        pFactory->Release();
        return false;
    }

    ID3D12Device* pDevice = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D12CreateDevice(pAdapter, featureLevel, IID_PPV_ARGS(&pDevice)))) {
        pAdapter->Release();
        pFactory->Release();
        return false;
    }
    pAdapter->Release();

    ID3D12CommandQueue* pCommandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
    if (FAILED(pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue)))) {
        pDevice->Release();
        pFactory->Release();
        return false;
    }

    IDXGISwapChain* pSwapChain = nullptr;
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferDesc.Width = 1;
    scDesc.BufferDesc.Height = 1;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.OutputWindow = GetDesktopWindow();
    scDesc.Windowed = TRUE;
    scDesc.BufferCount = 2;

    if (FAILED(pFactory->CreateSwapChain(pCommandQueue, &scDesc, &pSwapChain))) {
        pCommandQueue->Release();
        pDevice->Release();
        pFactory->Release();
        return false;
    }

    void* pVT = *(void**)pSwapChain;
    void* pPresent = *(void**)((uint8_t*)pVT + 0x28);

    bool result = CreateInlineHook(pPresent, detour, original);

    pSwapChain->Release();
    pCommandQueue->Release();
    pDevice->Release();
    pFactory->Release();

    return result;
}

bool HookManager::HookOpenGL(void* detour, void** original) {
    HMODULE opengl32 = GetModuleHandleA("opengl32.dll");
    if (!opengl32) return false;

    void* pSwapBuffers = GetProcAddress(opengl32, "wglSwapBuffers");
    if (!pSwapBuffers) return false;

    return CreateInlineHook(pSwapBuffers, detour, original);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        Amhook::HookManager::GetInstance().Init();
    }
    return TRUE;
}

} // namespace Amhook