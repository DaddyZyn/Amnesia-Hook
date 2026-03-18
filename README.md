<div align="center">

  <img src="https://readme-typing-svg.demolab.com?font=Fira+Code&weight=500&size=32&pause=1000&color=FFFFFF&center=true&vCenter=true&width=500&lines=amnesia_hook;x64+stealth+hooking;directx11+overlay+base" alt="Typing SVG" />

</div>

---

<div align="center">

  <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue?style=for-the-badge&color=0D1117" alt="C++20">

  <img src="https://img.shields.io/badge/Platform-Windows_x64-blue?style=for-the-badge&color=0D1117" alt="Windows x64">

  <img src="https://img.shields.io/badge/Status-Beta-red?style=for-the-badge&color=0D1117" alt="Beta">

</div>

### /// system_status: beta

sup. **Amnesia** is a stealth-focused, production-ready x64 hooking library and DirectX 11 overlay base built for game reverse engineering and internal modifications.

> **⚠️ Note:** This is currently in active development. While the core engine works flawlessly in standalone tests and isolated environments, it hasn't been heavily battle-tested in the wild against modern anti-cheats just yet. Expect updates, optimizations, and heavy improvements in the near future.

### // core features

* `Advanced Inline Hooking`: Custom memory allocator ensures trampolines are built within a 2GB bounds. Fully calculates and rewrites **RIP-relative instructions** (LEA, CALL, Jcc) into absolute 32-bit/64-bit jumps so your target process doesn't crash.

* `Lock-Free VEH Hooking`: Hardware breakpoint (Dr0-Dr3) execution interception. Uses atomic spinlocks and Thread Local Storage (TLS) to handle single-stepping trap flags without deadlocking game threads.

* `DirectX 11 VMT Hooking`: Dynamically creates a temporary swapchain to steal the `IDXGISwapChain::Present` (Index 8) pointer. Cleanly sets up an ImGui rendering context without crashing the game engine.

* `Thread-Safe`: Automatically suspends and resumes other threads during inline patching to prevent mid-instruction execution crashes.

### // api reference

#### Singleton Access

```cpp
static HookManager& GetInstance()
```

Returns the singleton instance of the HookManager. Thread-safe initialization on first call.

---

#### Initialization

```cpp
bool Init()
```

Registers the Vectored Exception Handler (VEH) for hardware breakpoint hooks. Must be called before using VEH hooks. Automatically called on DLL_PROCESS_ATTACH if using as a DLL.

**Returns:** `true` if VEH registered successfully or already registered.

---

#### Hook Creation

##### Inline Hook (Detour)

```cpp
bool CreateInlineHook(void* target, void* detour, void** original)
```

Creates a standard code detour hook by overwriting the target function's prologue with a jump instruction.

**Parameters:**
- `target` - Address of the function to hook
- `detour` - Address of your replacement function
- `original` - Output pointer that receives the original function address (trampoline)

**Returns:** `true` if hook created successfully.

**Details:**
- Resolves jump chains (E9, FF25) to find the actual target
- Disassembles minimum 14 bytes to ensure space for jump
- Builds a trampoline that preserves original instruction execution
- Relocates all RIP-relative instructions (Jcc expansion, CALL relocation, RIP-relative ModRM)

---

##### VEH Hook (Hardware Breakpoint)

```cpp
bool CreateVEHHook(void* target, void* detour, void** original)
```

Creates a hook using hardware breakpoints via Vectored Exception Handling.

**Parameters:**
- `target` - Address of the function to hook
- `detour` - Address of your replacement function
- `original` - Output pointer that receives the original function address (trampoline)

**Returns:** `true` if hook created successfully.

**Details:**
- Uses DR0-DR3 debug registers for execution breakpoint
- Only hooks single instruction (hardware breakpoint limitation)
- Trampoline preserves original code for seamless re-execution
- Lock-free design prevents thread deadlocks

---

##### Overlay Hook (Generic)

```cpp
bool CreateOverlayHook(void* target, void* detour, void** original, HookType type)
```

Generic wrapper that creates either an inline or VEH hook based on type parameter.

**Parameters:**
- `target` - Address of the function to hook
- `detour` - Address of your replacement function
- `original` - Output pointer for original function address
- `type` - `HookType::Inline` or `HookType::VEH`

**Returns:** `true` if hook created successfully.

---

#### Hook Management

##### Enable

```cpp
bool Enable(void* target)
```

Activates a previously created hook.

**Parameters:**
- `target` - Address that was hooked

**Returns:** `true` if hook enabled successfully.

**Details:**
- For Inline hooks: patches target with jump to detour, suspends other threads during write
- For VEH hooks: sets hardware breakpoint in debug register

---

##### Disable

```cpp
bool Disable(void* target)
```

Deactivates a hook without removing it.

**Parameters:**
- `target` - Address that was hooked

**Returns:** `true` if hook disabled successfully.

**Details:**
- For Inline hooks: restores original bytes, resumes normal execution
- For VEH hooks: clears hardware breakpoint

---

##### Remove Hook

```cpp
bool RemoveHook(void* target)
```

Completely removes a hook and frees associated resources.

**Parameters:**
- `target` - Address that was hooked

**Returns:** `true` if hook removed successfully.

---

##### Enable All

```cpp
bool EnableAll()
```

Activates all registered hooks simultaneously.

**Returns:** `true` if all hooks enabled.

---

##### Disable All

```cpp
bool DisableAll()
```

Deactivates all registered hooks.

**Returns:** `true` if all hooks disabled.

---

##### Check Hook Status

```cpp
bool IsHooked(void* target)
```

Checks if an address has an active hook.

**Parameters:**
- `target` - Address to check

**Returns:** `true` if hook exists and is enabled.

---

### // graphics api hooks

The library provides convenience functions for hooking common graphics API presentation functions.

#### D3D9

```cpp
bool HookD3D9(void* detour, void** original)
```

Hooks `IDirect3DDevice9::Present` (VMT index 17) for Direct3D 9 applications.

**Parameters:**
- `detour` - Your Present replacement function
- `original` - Output for original Present function pointer

**Returns:** `true` if hook successful.

**Details:**
- Creates a dummy device to resolve the VMT pointer
- Hooks the Present function at index 17
- Release device after resolving address

---

#### D3D11

```cpp
bool HookD3D11(void* detour, void** original)
```

Hooks `IDXGISwapChain::Present` (VMT index 8) for Direct3D 11 applications.

**Parameters:**
- `detour` - Your Present replacement function
- `original` - Output for original Present function pointer

**Returns:** `true` if hook successful.

**Details:**
- Creates a temporary swapchain via `D3D11CreateDeviceAndSwapChain`
- Resolves `pVMT[8]` for the Present function
- Automatically releases temporary resources after resolving address
- This is the recommended hook point for D3D11 overlay rendering

---

#### D3D12

```cpp
bool HookD3D12(void* detour, void** original)
```

Hooks `IDXGISwapChain::Present` for Direct3D 12 applications.

**Parameters:**
- `detour` - Your Present replacement function
- `original` - Output for original Present function pointer

**Returns:** `true` if hook successful.

---

#### OpenGL

```cpp
bool HookOpenGL(void* detour, void** original)
```

Hooks `wglSwapBuffers` for OpenGL applications.

**Parameters:**
- `detour` - Your SwapBuffers replacement function
- `original` - Output for original SwapBuffers function pointer

**Returns:** `true` if hook successful.

---

### // usage example

```cpp
#include "Amhk.h"

void* g_originalPresent = nullptr;

HRESULThk DetourPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    static bool initialized = false;
    static ID3D11Device* pDevice = nullptr;
    static ID3D11DeviceContext* pContext = nullptr;
    static ID3D11RenderTargetView* pRTView = nullptr;

    if (!initialized) {
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
        pDevice->GetImmediateContext(&pContext);

        ID3D11Texture2D* pBackBuffer;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTView);
        pBackBuffer->Release();

        ImGui_ImplWin32_Init(/* hwnd */);
        ImGui_ImplDX11_Init(pDevice, pContext);
        initialized = true;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Amnesia Hook");
    ImGui::Text("Hello, world!");
    ImGui::End();

    ImGui::Render();
    pContext->OMSetRenderTargets(1, &pRTView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    using Present_t = HRESULT(IDXGISwapChain*, UINT, UINT);
    return ((Present_t*)g_originalPresent)(pSwapChain, SyncInterval, Flags);
}

int main() {
    Amhook::HookManager::GetInstance().HookD3D11((void*)DetourPresent, &g_originalPresent);
    Amhook::HookManager::GetInstance().Enable((void*)/* resolved target */);

    // Game loop...
    return 0;
}
```

---

### // quick usage

```cmake
cmake -B build
cmake --build build
```

Link against `Amhook.dll` and include `Amhk.h` in your project.

---

### // hook type comparison

| Feature | Inline Hook | VEH Hook |
|---------|-------------|----------|
| **Mechanism** | Code patching | Hardware breakpoints |
| **Debug Registers** | Not used | DR0-DR3 |
| **Hook Size** | Minimum 14 bytes | Single instruction |
| **Thread Safety** | Thread suspension | Atomic spinlock |
| **Stealth** | Lower | Higher |
| **Compatibility** | Universal | Windows only |
| **Performance** | Faster | Slightly slower |

---

### // architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        HookManager                          │
├─────────────────────────────────────────────────────────────┤
│  Singleton: GetInstance()                                   │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │Inline Hook  │  │  VEH Hook   │  │  Graphics API Hooks │ │
│  │             │  │             │  │                     │ │
│  │- ResolveTarget │- SetHWBP   │  │- HookD3D9()        │ │
│  │- RelocateInst │- VEHHandler │  │- HookD3D11()       │ │
│  │- AllocTramp   │- TLS State  │  │- HookD3D12()       │ │
│  │- SuspendThread│             │  │- HookOpenGL()      │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Thread Safety: std::recursive_mutex                 │   │
│  │  VEH Lock: std::atomic_flag (spinlock)              │   │
│  │  TLS: thread_local ThreadState                      │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

### // memory layout

```
Target Function:
┌────────────────────┬────────────────────┐
│   Original Bytes   │                    │
│   (patchSize)      │   Jump to Detour   │
│                    │   (14 bytes FF25)  │
└────────────────────┴────────────────────┘
         │                    ▲
         │                    │
         └────────────────────┘
              trampoline

Trampoline:
┌─────────────────────────────────────────────────────────────┐
│  Relocated Instructions    │  Jump back to Target + 14   │
│  (with fixed RIP-relative)  │  (14 bytes)                 │
└─────────────────────────────────────────────────────────────┘
```

---

### // license

MIT License - Free for personal and commercial use.
