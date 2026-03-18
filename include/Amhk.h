#pragma once
#include <windows.h>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <optional>
#include <map>
#include <thread>

#ifdef Amhook_EXPORTS
#define AMHOOK_API __declspec(dllexport)
#else
#define AMHOOK_API __declspec(dllimport)
#endif

namespace Amhook {

    enum class HookType {
        Inline,
        VEH
    };

    enum class HookStatus {
        Disabled,
        Enabled
    };

    struct HookInfo {
        void* targetAddress;
        void* detourAddress;
        void* trampolineAddress;
        HookType type;
        HookStatus status;
        size_t patchSize;
        uint8_t originalBytes[64]; 
        int drIndex; // For VEH hooks (0-3)
    };

    class HookManager {
    public:
        static AMHOOK_API HookManager& GetInstance() {
            static HookManager instance;
            return instance;
        }

        AMHOOK_API bool Init();

        AMHOOK_API bool CreateInlineHook(void* target, void* detour, void** original);

        AMHOOK_API bool CreateVEHHook(void* target, void* detour, void** original);

        AMHOOK_API bool Enable(void* target);

        AMHOOK_API bool Disable(void* target);

        AMHOOK_API bool RemoveHook(void* target);

        AMHOOK_API bool EnableAll();
        AMHOOK_API bool DisableAll();

        AMHOOK_API bool IsHooked(void* target);

        AMHOOK_API bool CreateOverlayHook(void* target, void* detour, void** original, HookType type);
        AMHOOK_API bool HookD3D9(void* detour, void** original);
        AMHOOK_API bool HookD3D11(void* detour, void** original);
        AMHOOK_API bool HookD3D12(void* detour, void** original);
        AMHOOK_API bool HookOpenGL(void* detour, void** original);

    private:
        HookManager() = default;
        ~HookManager();
        HookManager(const HookManager&) = delete;
        HookManager& operator=(const HookManager&) = delete;

        std::recursive_mutex m_mutex;
        std::map<void*, HookInfo> m_hooks;
        PVOID m_vehHandler = nullptr;
        std::vector<DWORD> m_suspendedThreads;
        
        struct VEHHookRecord {
            void* target;
            void* detour;
            int drIndex;
            std::atomic<bool> enabled;
        };
        VEHHookRecord m_vehHooks[4];
        
        bool m_drInUse[4] = { false, false, false, false };

        static LONG CALLBACK VEHHandler(PEXCEPTION_POINTERS exceptionInfo);
        
        // Internal helpers
        bool SuspendOtherThreads();
        void ResumeOtherThreads();
        void* AllocateTrampoline(void* target, size_t size);
        
        // VEH specific
        bool SetHardwareBreakpoint(void* address, int index, bool enable);
        int FindAvailableDR();
    };

} // namespace Amhook
