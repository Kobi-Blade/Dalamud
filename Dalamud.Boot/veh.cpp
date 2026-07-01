#include "pch.h"

#include "veh.h"

#include <shellapi.h>

#include "logging.h"
#include "utils.h"
#include "hooks.h"

#include "DalamudStartInfo.h"

#pragma comment(lib, "comctl32.lib")

#if defined _M_IX86
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

PVOID g_veh_handle = nullptr;
bool g_veh_do_full_dump = false;
std::optional<hooks::import_hook<decltype(SetUnhandledExceptionFilter)>> g_HookSetUnhandledExceptionFilter;

std::recursive_mutex g_exception_handler_mutex;

std::chrono::time_point<std::chrono::system_clock> g_time_start;

bool is_whitelist_exception(const DWORD code)
{
    switch (code)
    {
    case STATUS_ACCESS_VIOLATION:
    case STATUS_IN_PAGE_ERROR:
    case STATUS_INVALID_HANDLE:
    case STATUS_INVALID_PARAMETER:
    case STATUS_NO_MEMORY:
    case STATUS_ILLEGAL_INSTRUCTION:
    case STATUS_NONCONTINUABLE_EXCEPTION:
    case STATUS_INVALID_DISPOSITION:
    case STATUS_ARRAY_BOUNDS_EXCEEDED:
    case STATUS_FLOAT_DENORMAL_OPERAND:
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
    case STATUS_FLOAT_INEXACT_RESULT:
    case STATUS_FLOAT_INVALID_OPERATION:
    case STATUS_FLOAT_OVERFLOW:
    case STATUS_FLOAT_STACK_CHECK:
    case STATUS_FLOAT_UNDERFLOW:
    case STATUS_INTEGER_DIVIDE_BY_ZERO:
    case STATUS_INTEGER_OVERFLOW:
    case STATUS_PRIVILEGED_INSTRUCTION:
    case STATUS_STACK_OVERFLOW:
    case STATUS_DLL_NOT_FOUND:
    case STATUS_ORDINAL_NOT_FOUND:
    case STATUS_ENTRYPOINT_NOT_FOUND:
    case STATUS_DLL_INIT_FAILED:
    case STATUS_CONTROL_STACK_VIOLATION:
    case STATUS_FLOAT_MULTIPLE_FAULTS:
    case STATUS_FLOAT_MULTIPLE_TRAPS:
    case STATUS_HEAP_CORRUPTION:
    case STATUS_STACK_BUFFER_OVERRUN:
    case STATUS_INVALID_CRUNTIME_PARAMETER:
    case STATUS_THREAD_NOT_RUNNING:
    case STATUS_ALREADY_REGISTERED:
        return true;
    default:
        return false;
    }
}

bool get_module_file_and_base(const DWORD64 address, DWORD64& module_base, std::filesystem::path& module_file)
{
    HMODULE handle;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &handle))
    {
        std::wstring path(PATHCCH_MAX_CCH, L'\0');
        path.resize(GetModuleFileNameW(handle, &path[0], static_cast<DWORD>(path.size())));
        if (!path.empty())
        {
            module_base = reinterpret_cast<DWORD64>(handle);
            module_file = path;
            return true;
        }
    }
    return false;
}

bool is_ffxiv_address(const wchar_t* module_name, const DWORD64 address)
{
    DWORD64 module_base;
    if (std::filesystem::path module_path; get_module_file_and_base(address, module_base, module_path))
        return _wcsicmp(module_path.filename().c_str(), module_name) == 0;
    return false;
}

static DalamudExpected<void> append_injector_launch_args(std::vector<std::wstring>& args)
{
    if (auto path = utils::loaded_module::current_process().path())
        args.emplace_back(L"--game=\"" + path->wstring() + L"\"");
    else
        return DalamudUnexpected(std::in_place, std::move(path.error()));

    switch (g_startInfo.DalamudLoadMethod) {
    case DalamudStartInfo::LoadMethod::Entrypoint:
        args.emplace_back(L"--mode=entrypoint");
        break;
    case DalamudStartInfo::LoadMethod::DllInject:
        args.emplace_back(L"--mode=inject");
    }
    args.emplace_back(L"--dalamud-working-directory=\"" + unicode::convert<std::wstring>(g_startInfo.WorkingDirectory) + L"\"");
    args.emplace_back(L"--dalamud-configuration-path=\"" + unicode::convert<std::wstring>(g_startInfo.ConfigurationPath) + L"\"");
    args.emplace_back(L"--logpath=\"" + unicode::convert<std::wstring>(g_startInfo.LogPath) + L"\"");
    args.emplace_back(L"--logname=\"" + unicode::convert<std::wstring>(g_startInfo.LogName) + L"\"");
    args.emplace_back(L"--dalamud-plugin-directory=\"" + unicode::convert<std::wstring>(g_startInfo.PluginDirectory) + L"\"");
    args.emplace_back(L"--dalamud-asset-directory=\"" + unicode::convert<std::wstring>(g_startInfo.AssetDirectory) + L"\"");
    args.emplace_back(L"--dalamud-temp-directory=\"" + unicode::convert<std::wstring>(g_startInfo.TempDirectory) + L"\"");
    args.emplace_back(std::format(L"--dalamud-client-language={}", static_cast<int>(g_startInfo.Language)));
    args.emplace_back(std::format(L"--dalamud-delay-initialize={}", g_startInfo.DelayInitializeMs));

    if (g_startInfo.BootShowConsole)
        args.emplace_back(L"--console");
    if (g_startInfo.BootEnableEtw)
        args.emplace_back(L"--etw");
    if (g_startInfo.BootVehEnabled)
        args.emplace_back(L"--veh");
    if (g_startInfo.BootVehFull)
        args.emplace_back(L"--veh-full");
    if ((g_startInfo.BootWaitMessageBox & DalamudStartInfo::WaitMessageboxFlags::BeforeInitialize) != DalamudStartInfo::WaitMessageboxFlags::None)
        args.emplace_back(L"--msgbox1");
    if ((g_startInfo.BootWaitMessageBox & DalamudStartInfo::WaitMessageboxFlags::BeforeDalamudEntrypoint) != DalamudStartInfo::WaitMessageboxFlags::None)
        args.emplace_back(L"--msgbox2");
    if ((g_startInfo.BootWaitMessageBox & DalamudStartInfo::WaitMessageboxFlags::BeforeDalamudConstruct) != DalamudStartInfo::WaitMessageboxFlags::None)
        args.emplace_back(L"--msgbox3");
    switch (g_startInfo.UnhandledException) {
        case DalamudStartInfo::UnhandledExceptionHandlingMode::Default:
            args.emplace_back(L"--unhandled-exception=default");
            break;
        case DalamudStartInfo::UnhandledExceptionHandlingMode::StallDebug:
            args.emplace_back(L"--unhandled-exception=stalldebug");
            break;
        case DalamudStartInfo::UnhandledExceptionHandlingMode::None:
            args.emplace_back(L"--unhandled-exception=none");
            break;
    }

    args.emplace_back(L"--");

    if (int nArgs; LPWSTR * szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs)) {
        for (auto i = 1; i < nArgs; i++)
            args.emplace_back(szArgList[i]);
        LocalFree(szArgList);
    }

    return {};
}

LONG exception_handler(EXCEPTION_POINTERS* ex)
{
    if (g_startInfo.UnhandledException == DalamudStartInfo::UnhandledExceptionHandlingMode::StallDebug) {
        while (!IsDebuggerPresent())
            Sleep(100);

        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto lock = std::lock_guard(g_exception_handler_mutex);
    (void)ex;

    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI structured_exception_handler(EXCEPTION_POINTERS* ex)
{
    return exception_handler(ex);
}

LONG WINAPI vectored_exception_handler(EXCEPTION_POINTERS* ex)
{
    if (ex->ExceptionRecord->ExceptionCode == 0x12345678)
    {
        // pass
    }
    else if (ex->ExceptionRecord->ExceptionCode == 0x406D1388)
    {
        // VS thread namer - just let these run; they aren't a problem.
        // https://learn.microsoft.com/en-us/visualstudio/debugger/tips-for-debugging-threads
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    else
    {
        if (!is_whitelist_exception(ex->ExceptionRecord->ExceptionCode))
            return EXCEPTION_CONTINUE_SEARCH;

        if (!is_ffxiv_address(L"ffxiv_dx11.exe", ex->ContextRecord->Rip) &&
            !is_ffxiv_address(L"cimgui.dll", ex->ContextRecord->Rip))
            return EXCEPTION_CONTINUE_SEARCH;
    }

    return exception_handler(ex);
}

bool veh::add_handler(bool doFullDump, const std::string& workingDirectory, bool bootConsole)
{
	if (g_veh_handle)
		return false;

	(void)workingDirectory;
	(void)bootConsole;

	g_veh_handle = AddVectoredExceptionHandler(TRUE, vectored_exception_handler);

	g_HookSetUnhandledExceptionFilter.emplace("kernel32.dll!SetUnhandledExceptionFilter (lpTopLevelExceptionFilter)", "kernel32.dll", "SetUnhandledExceptionFilter", 0);
	g_HookSetUnhandledExceptionFilter->set_detour([](LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) -> LPTOP_LEVEL_EXCEPTION_FILTER
	{
		logging::I("Overwriting UnhandledExceptionFilter from {} to {}", reinterpret_cast<ULONG_PTR>(lpTopLevelExceptionFilter), reinterpret_cast<ULONG_PTR>(structured_exception_handler));
		return g_HookSetUnhandledExceptionFilter->call_original(structured_exception_handler);
	});
	SetUnhandledExceptionFilter(structured_exception_handler);

	g_veh_do_full_dump = doFullDump;
	g_time_start = std::chrono::system_clock::now();

	return true;
}

bool veh::remove_handler()
{
    if (g_veh_handle && RemoveVectoredExceptionHandler(g_veh_handle) != 0)
    {
        g_veh_handle = nullptr;
        g_HookSetUnhandledExceptionFilter.reset();
        SetUnhandledExceptionFilter(nullptr);
        return true;
    }
    return false;
}
