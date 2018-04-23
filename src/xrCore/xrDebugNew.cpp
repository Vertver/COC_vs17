#include "stdafx.h"
#pragma hdrstop

#include "xrdebug.h"
#include "os_clipboard.h"

#include <sal.h>
#include <dxerr.h>

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#include <direct.h>
#pragma warning(pop)

#include "../build_config_defines.h"

extern bool shared_str_initialized;

#ifdef __BORLANDC__
# include "d3d9.h"
# include "d3dx9.h"
# include "D3DX_Wrapper.h"
# pragma comment(lib,"EToolsB.lib")
# define DEBUG_INVOKE DebugBreak()
static BOOL bException = TRUE;
# define USE_BUG_TRAP
#else
#ifndef NO_BUG_TRAP
# define USE_BUG_TRAP
#endif //-!NO_BUG_TRAP
# define DEBUG_INVOKE __asm int 3
static BOOL bException = FALSE;
#endif

#ifndef USE_BUG_TRAP
# include <exception>
#endif

#ifndef _M_AMD64
# ifndef __BORLANDC__
# pragma comment(lib,"dxerr.lib")
# endif
#endif

#include <dbghelp.h> // MiniDump flags

#include <new.h> // for _set_new_mode
#include <signal.h> // for signals

#ifdef NO_BUG_TRAP //DEBUG
# define USE_OWN_ERROR_MESSAGE_WINDOW
#else
# define USE_OWN_MINI_DUMP
#endif //-NO_BUG_TRAP //DEBUG

XRCORE_API xrDebug Debug;

static bool error_after_dialog = false;

extern void BuildStackTrace();
extern char g_stackTrace[100][4096];
extern int g_stackTraceCount;

void LogStackTrace(LPCSTR header)
{
    if (!shared_str_initialized)
        return;

    BuildStackTrace();

    Msg("%s", header);

    for (int i = 1; i < g_stackTraceCount; ++i)
        Msg("%s", g_stackTrace[i]);
}

void xrDebug::gather_info(const char* expression, const char* description, const char* argument0, const char* argument1, const char* file, int line, const char* function, LPSTR assertion_info, u32 const assertion_info_size)
{
    if (!expression)
        expression = "<no expression>";
    LPSTR buffer_base = assertion_info;
    LPSTR buffer = assertion_info;
    int assertion_size = (int) assertion_info_size;
    LPCSTR endline = "\n";
    LPCSTR prefix = "[error]";
    bool extended_description = (description && !argument0 && strchr(description, '\n'));
    for (int i = 0; i < 2; ++i)
    {
        if (!i)
            buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFATAL ERROR%s%s", endline, endline, endline);
        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sExpression    : %s%s", prefix, expression, endline);
        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFunction      : %s%s", prefix, function, endline);
        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sFile          : %s%s", prefix, file, endline);
        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sLine          : %d%s", prefix, line, endline);

        if (extended_description)
        {
            buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s%s", endline, description, endline);
            if (argument0)
            {
                if (argument1)
                {
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument0, endline);
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument1, endline);
                }
                else
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", argument0, endline);
            }
        }
        else
        {
            buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sDescription   : %s%s", prefix, description, endline);
            if (argument0)
            {
                if (argument1)
                {
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArgument 0    : %s%s", prefix, argument0, endline);
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArgument 1    : %s%s", prefix, argument1, endline);
                }
                else
                    buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%sArguments     : %s%s", prefix, argument0, endline);
            }
        }

        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s", endline);
        if (!i)
        {
            if (shared_str_initialized)
            {
                Msg("%s", assertion_info);
                FlushLog();
            }
            buffer = assertion_info;
            endline = "\r\n";
            prefix = "";
        }
    }

#ifdef USE_MEMORY_MONITOR
    memory_monitor::flush_each_time(true);
    memory_monitor::flush_each_time(false);
#endif //-USE_MEMORY_MONITOR

    if (!IsDebuggerPresent() && !strstr(GetCommandLine(), "-no_call_stack_assert"))
    {
        if (shared_str_initialized)
            Msg("stack trace:\n");

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
        buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "stack trace:%s%s", endline, endline);
#endif //-USE_OWN_ERROR_MESSAGE_WINDOW

        BuildStackTrace();

        for (int i = 2; i < g_stackTraceCount; ++i)
        {
            if (shared_str_initialized)
                Msg("%s", g_stackTrace[i]);

#ifdef USE_OWN_ERROR_MESSAGE_WINDOW
            buffer += xr_sprintf(buffer, assertion_size - u32(buffer - buffer_base), "%s%s", g_stackTrace[i], endline);
#endif //-USE_OWN_ERROR_MESSAGE_WINDOW
        }

        if (shared_str_initialized)
            FlushLog();

        os_clipboard::copy_to_clipboard(assertion_info);
    }
}

void xrDebug::do_exit(const std::string& message)
{
    FlushLog();
    MessageBox(NULL, message.c_str(), "Error", MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
    TerminateProcess(GetCurrentProcess(), 1);
}

//AVO: simplified function
void xrDebug::backend(const char* expression, const char* description, const char* argument0, const char* argument1, const char* file, int line, const char* function, bool& ignore_always)
{
    static xrCriticalSection CS
#ifdef PROFILE_CRITICAL_SECTIONS
        (MUTEX_PROFILE_ID(xrDebug::backend))
#endif //-PROFILE_CRITICAL_SECTIONS
        ;

    CS.Enter();

    string4096 assertion_info;

    gather_info(expression, description, argument0, argument1, file, line, function, assertion_info, sizeof(assertion_info));

    LPCSTR endline = "\r\n";
    LPSTR buffer = assertion_info + xr_strlen(assertion_info);
    buffer += xr_sprintf(buffer, sizeof(assertion_info) - u32(buffer - &assertion_info[0]), "%sPress OK to abort execution%s", endline, endline);

    if (handler)
        handler();

    FlushLog();

    ShowCursor(true);
    ShowWindow(GetActiveWindow(), SW_FORCEMINIMIZE);
    MessageBox(
        GetTopWindow(NULL),
        assertion_info,
        "Fatal Error",
        MB_OK | MB_ICONERROR | MB_SYSTEMMODAL
        );

    CS.Leave();

    TerminateProcess(GetCurrentProcess(), 1);
}
//-AVO

LPCSTR xrDebug::error2string(long code)
{
    char* result = 0;
    static string1024 desc_storage;

#ifdef _M_AMD64
#else
	WCHAR err_result[1024];
    DXGetErrorDescription(code,err_result,sizeof(err_result));
	wcstombs(result, err_result, sizeof(err_result));
#endif
    if (0 == result)
    {
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, desc_storage, sizeof(desc_storage) - 1, 0);
        result = desc_storage;
    }
	return result;
}

void xrDebug::error(long hr, const char* expr, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(expr, error2string(hr), 0, 0, file, line, function, ignore_always);
}

void xrDebug::error(long hr, const char* expr, const char* e2, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(expr, error2string(hr), e2, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(e1, "assertion failed", 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const std::string& e2, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(e1, e2.c_str(), 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(e1, e2, 0, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(e1, e2, e3, 0, file, line, function, ignore_always);
}

void xrDebug::fail(const char* e1, const char* e2, const char* e3, const char* e4, const char* file, int line, const char* function, bool& ignore_always)
{
    backend(e1, e2, e3, e4, file, line, function, ignore_always);
}

//AVO: print, dont crash
void xrDebug::soft_fail(LPCSTR e1, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s", file, line, function, e1);
}
void xrDebug::soft_fail(LPCSTR e1, const std::string &e2, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s", file, line, function, e1, e2.c_str());
}
void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s", file, line, function, e1, e2);
}
void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s", file, line, function, e1, e2, e3);
}
void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR e4, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s %s", file, line, function, e1, e2, e3, e4);
}
void xrDebug::soft_fail(LPCSTR e1, LPCSTR e2, LPCSTR e3, LPCSTR e4, LPCSTR e5, LPCSTR file, int line, LPCSTR function)
{
    Msg("! VERIFY_FAILED: %s[%d] {%s}  %s %s %s %s %s", file, line, function, e1, e2, e3, e4, e5);
}
//-AVO

void __cdecl xrDebug::fatal(const char* file, int line, const char* function, const char* F, ...)
{
    string1024 buffer;

    va_list p;
    va_start(p, F);
    vsprintf(buffer, F, p);
    va_end(p);

    bool ignore_always = true;

    backend(nullptr, "fatal error", buffer, 0, file, line, function, ignore_always);
}

typedef void(*full_memory_stats_callback_type) ();
XRCORE_API full_memory_stats_callback_type g_full_memory_stats_callback = 0;

int out_of_memory_handler(size_t size)
{
    if (g_full_memory_stats_callback)
        g_full_memory_stats_callback();
    else
    {
        Memory.mem_compact();
        size_t process_heap = Memory.mem_usage();
        int eco_strings = (int) g_pStringContainer->stat_economy();
        int eco_smem = (int) g_pSharedMemoryContainer->stat_economy();
        Msg("* [x-ray]: process heap[%u K]", process_heap / 1024, process_heap / 1024);
        Msg("* [x-ray]: economy: strings[%d K], smem[%d K]", eco_strings / 1024, eco_smem);
    }

    Debug.fatal(DEBUG_INFO, "Out of memory. Memory request: %d K", size / 1024);
    return 1;
}

extern LPCSTR log_name();

XRCORE_API string_path g_bug_report_file;

void CALLBACK PreErrorHandler(INT_PTR)
{
}

extern void BuildStackTrace(struct _EXCEPTION_POINTERS* pExceptionInfo);
typedef LONG WINAPI UnhandledExceptionFilterType(struct _EXCEPTION_POINTERS* pExceptionInfo);
typedef LONG(__stdcall* PFNCHFILTFN) (EXCEPTION_POINTERS* pExPtrs);
extern "C" BOOL __stdcall SetCrashHandlerFilter(PFNCHFILTFN pFn);

static UnhandledExceptionFilterType* previous_filter = 0;

void format_message(LPSTR buffer, const u32& buffer_size)
{
    LPVOID message;
    DWORD error_code = GetLastError();

    if (!error_code)
    {
        *buffer = 0;
        return;
    }

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR) &message,
        0,
        NULL
        );

    xr_sprintf(buffer, buffer_size, "[error][%8d] : %s", error_code, message);
    LocalFree(message);
}

#ifndef _EDITOR
#include <errorrep.h>
#pragma comment( lib, "faultrep.lib" )
#endif //-!_EDITOR

//AVO: simplify function
LONG WINAPI UnhandledFilter(_EXCEPTION_POINTERS* pExceptionInfo)
{
    string256 error_message;
    format_message(error_message, sizeof(error_message));

    CONTEXT save = *pExceptionInfo->ContextRecord;
    BuildStackTrace(pExceptionInfo);
    *pExceptionInfo->ContextRecord = save;

    if (shared_str_initialized)
        Msg("stack trace:\n");

    if (!IsDebuggerPresent())
    {
        os_clipboard::copy_to_clipboard("stack trace:\r\n\r\n");
    }

    string4096 buffer;
    for (int i = 0; i < g_stackTraceCount; ++i)
    {
        if (shared_str_initialized)
            Msg("%s", g_stackTrace[i]);
        xr_sprintf(buffer, sizeof(buffer), "%s\r\n", g_stackTrace[i]);
#ifdef DEBUG
        if (!IsDebuggerPresent())
            os_clipboard::update_clipboard(buffer);
#endif //-DEBUG
    }

    if (*error_message)
    {
        if (shared_str_initialized)
            Msg("\n%s", error_message);

        xr_strcat(error_message, sizeof(error_message), "\r\n");
#ifdef DEBUG
        if (!IsDebuggerPresent())
            os_clipboard::update_clipboard(buffer);
#endif //-DEBUG
    }

    FlushLog();

    ShowCursor(true);
    ShowWindow(GetActiveWindow(), SW_FORCEMINIMIZE);
    MessageBox(
        GetTopWindow(NULL),
        "Unhandled exception occured. See log for details",
        "Fatal Error",
        MB_OK | MB_ICONERROR | MB_SYSTEMMODAL
        );
    TerminateProcess(GetCurrentProcess(), 1);

    return (EXCEPTION_CONTINUE_SEARCH);
}
//-AVO

//////////////////////////////////////////////////////////////////////
#ifdef M_BORLAND
namespace std
{
    extern new_handler _RTLENTRY _EXPFUNC set_new_handler(new_handler new_p);
};

static void __cdecl def_new_handler()
{
    FATAL ("Out of memory.");
}

void xrDebug::_initialize (const bool& dedicated)
{
    handler = 0;
    m_on_dialog = 0;
    std::set_new_handler (def_new_handler); // exception-handler for 'out of memory' condition
    // ::SetUnhandledExceptionFilter (UnhandledFilter); // exception handler to all "unhandled" exceptions
}
#else

#ifdef LEGACY_CODE
#ifndef USE_BUG_TRAP
void _terminate()
{
    if (strstr(GetCommandLine(),"-silent_error_mode"))
        exit (-1);

    string4096 assertion_info;

    Debug.gather_info (
        nullptr,
        "Unexpected application termination",
        0,
        0,
#ifdef ANONYMOUS_BUILD
        "",
        0,
#else
        __FILE__,
        __LINE__,
#endif
#ifndef _EDITOR
        __FUNCTION__,
#else // _EDITOR
        "",
#endif // _EDITOR
        assertion_info
        );

    LPCSTR endline = "\r\n";
    LPSTR buffer = assertion_info + xr_strlen(assertion_info);
    buffer += xr_sprintf(buffer, sizeof(buffer), "Press OK to abort execution%s",endline);

    MessageBox (
        GetTopWindow(NULL),
        assertion_info,
        "Fatal Error",
        MB_OK|MB_ICONERROR|MB_SYSTEMMODAL
        );

    exit (-1);
    // FATAL ("Unexpected application termination");
}
#endif //-!USE_BUG_TRAP
#endif //-LEGACY_CODE

static void handler_base(LPCSTR reason_string)
{
    bool ignore_always = false;
    Debug.backend(
        nullptr,
        reason_string,
        0,
        0,
        DEBUG_INFO,
        ignore_always
        );
}

static void invalid_parameter_handler(
    const wchar_t* expression,
    const wchar_t* function,
    const wchar_t* file,
    unsigned int line,
    uintptr_t reserved
    )
{
    bool ignore_always = false;

    string4096 expression_;
    string4096 function_;
    string4096 file_;
    size_t converted_chars = 0;
    // errno_t err =
    if (expression)
        wcstombs_s(
        &converted_chars,
        expression_,
        sizeof(expression_),
        expression,
        (wcslen(expression) + 1) * 2 * sizeof(char)
        );
    else
        xr_strcpy(expression_, "");

    if (function)
        wcstombs_s(
        &converted_chars,
        function_,
        sizeof(function_),
        function,
        (wcslen(function) + 1) * 2 * sizeof(char)
        );
    else
        xr_strcpy(function_, __FUNCTION__);

    if (file)
        wcstombs_s(
        &converted_chars,
        file_,
        sizeof(file_),
        file,
        (wcslen(file) + 1) * 2 * sizeof(char)
        );
    else
    {
        line = __LINE__;
        xr_strcpy(file_, __FILE__);
    }

    Debug.backend(
        expression_,
		"invalid parameter",
        0,
        0,
        file_,
        line,
        function_,
        ignore_always
        );
}

static void pure_call_handler()
{
    handler_base("pure virtual function call");
}

#ifdef XRAY_USE_EXCEPTIONS
static void unexpected_handler()
{
    handler_base("unexpected program termination");
}
#endif // XRAY_USE_EXCEPTIONS

static void abort_handler(int signal)
{
    handler_base("application is aborting");
}

static void floating_point_handler(int signal)
{
    handler_base("floating point error");
}

static void illegal_instruction_handler(int signal)
{
    handler_base("illegal instruction");
}

static void termination_handler(int signal)
{
    handler_base("termination with exit code 3");
}

void debug_on_thread_spawn()
{
#ifdef USE_BUG_TRAP
    BT_SetTerminate();
#else // USE_BUG_TRAP
    //std::set_terminate (_terminate);
#endif // USE_BUG_TRAP

    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, abort_handler);
    signal(SIGABRT_COMPAT, abort_handler);
    signal(SIGFPE, floating_point_handler);
    signal(SIGILL, illegal_instruction_handler);
    signal(SIGINT, 0);
    // signal (SIGSEGV, storage_access_handler);
    signal(SIGTERM, termination_handler);

    _set_invalid_parameter_handler(&invalid_parameter_handler);

    _set_new_mode(1);
    _set_new_handler(&out_of_memory_handler);
    // std::set_new_handler (&std_out_of_memory_handler);

    _set_purecall_handler(&pure_call_handler);
}

void xrDebug::_initialize(const bool& dedicated)
{
    static bool is_dedicated = dedicated;

    *g_bug_report_file = 0;

    debug_on_thread_spawn();

#ifdef USE_BUG_TRAP
    SetupExceptionHandler(is_dedicated);
#endif // USE_BUG_TRAP
    previous_filter = ::SetUnhandledExceptionFilter(UnhandledFilter); // exception handler to all "unhandled" exceptions
}
#endif
