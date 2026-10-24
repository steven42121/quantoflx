// Brightness.dll - Rainmeter measure plugin.
// Reads/writes monitor brightness through dxva2 by *global* monitor index, so a
// skin running on a DDC-incapable screen can still drive the other monitors.
// Hardware access happens on one worker thread; the skin only reads a cache.
// Reads are demand-driven: measure load, "Read" (skin hover), and after a
// "Set"/"Bump" to confirm what the monitor actually accepted.
// The measure also pushes its value into a skin variable (option Var, default
// Bri<Index>) because Rainmeter's OnChangeAction does not fire for the value a
// measure has on its very first update.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")

typedef double(__stdcall* FnReadFormula)(void*, const wchar_t*, double);
typedef const wchar_t* (__stdcall* FnReadString)(void*, const wchar_t*, const wchar_t*, BOOL);
typedef void(__stdcall* FnExecute)(void*, const wchar_t*);
typedef void*(__stdcall* FnGet)(void*, int);

static FnReadFormula g_readFormula = nullptr;
static FnReadString g_readString = nullptr;
static FnExecute g_execute = nullptr;
static FnGet g_get = nullptr;

static const int MAXMON = 8;
static const int RMG_SKIN = 1;

static BOOL CALLBACK EnumMonProc(HMONITOR h, HDC, LPRECT, LPARAM p)
{
    reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(h);
    return TRUE;
}

static std::vector<HMONITOR> Monitors()
{
    std::vector<HMONITOR> v;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&v));
    return v;
}

// Opens the first physical monitor behind the index-th HMONITOR and runs fn on it.
template <typename Fn>
static bool WithPhysical(int idx, Fn fn)
{
    std::vector<HMONITOR> mons = Monitors();
    if (idx < 0 || idx >= static_cast<int>(mons.size()))
        return false;
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(mons[idx], &count) || count == 0)
        return false;
    std::vector<PHYSICAL_MONITOR> phys(count);
    if (!GetPhysicalMonitorsFromHMONITOR(mons[idx], count, phys.data()))
        return false;
    bool ok = fn(phys[0].hPhysicalMonitor);
    DestroyPhysicalMonitor(phys[0].hPhysicalMonitor);
    return ok;
}

static int ReadBri(int idx)
{
    int out = -1;
    WithPhysical(idx, [&](HANDLE h) {
        DWORD mn = 0, cur = 0, mx = 0;
        if (GetMonitorBrightness(h, &mn, &cur, &mx))
            out = static_cast<int>(cur);
        return true;
    });
    return out;
}

static bool WriteBri(int idx, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return WithPhysical(idx, [&](HANDLE h) {
        DWORD mn = 0, cur = 0, mx = 0;
        if (!GetMonitorBrightness(h, &mn, &cur, &mx))
            return false;
        DWORD span = (mx > mn) ? (mx - mn) : 1;
        DWORD raw = mn + static_cast<DWORD>(pct * span / 100 + 0.5);
        if (raw < mn) raw = mn;
        if (raw > mx) raw = mx;
        return SetMonitorBrightness(h, raw) != FALSE;
    });
}

// One worker for the whole DLL: DDC calls take 50-150 ms and must never block
// Rainmeter's UI thread.
class Worker
{
public:
    void start()
    {
        m_stop = false;
        if (!m_thread.joinable())
            m_thread = std::thread(&Worker::loop, this);
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> g(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    int value(int idx)
    {
        std::lock_guard<std::mutex> g(m_mutex);
        return (idx >= 0 && idx < MAXMON) ? m_value[idx] : -1;
    }

    void requestRead(int idx)
    {
        if (idx < 0 || idx >= MAXMON) return;
        {
            std::lock_guard<std::mutex> g(m_mutex);
            m_readMask |= (1u << idx);
        }
        m_cv.notify_all();
    }

    // Optimistic: publish the requested value at once so the slider tracks the
    // pointer, then let the confirm-read pull it back if the monitor clamps.
    void requestWrite(int idx, int val)
    {
        if (idx < 0 || idx >= MAXMON) return;
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        {
            std::lock_guard<std::mutex> g(m_mutex);
            m_value[idx] = val;
            m_write[idx] = val;
            m_writeMask |= (1u << idx);
            m_readMask |= (1u << idx);
        }
        m_cv.notify_all();
    }

private:
    void loop()
    {
        for (;;)
        {
            unsigned readMask = 0, writeMask = 0;
            int writeVal[MAXMON] = { 0 };
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this] { return m_stop || m_readMask || m_writeMask; });
                if (m_stop)
                    return;
                readMask = m_readMask;
                writeMask = m_writeMask;
                m_readMask = 0;
                m_writeMask = 0;
                for (int i = 0; i < MAXMON; ++i)
                    writeVal[i] = m_write[i];
            }
            for (int i = 0; i < MAXMON; ++i)
                if (writeMask & (1u << i))
                    WriteBri(i, writeVal[i]);
            for (int i = 0; i < MAXMON; ++i)
            {
                if (!(readMask & (1u << i)))
                    continue;
                int v = ReadBri(i);
                if (v >= 0)
                {
                    std::lock_guard<std::mutex> g(m_mutex);
                    m_value[i] = v;
                }
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    bool m_stop = false;
    unsigned m_readMask = 0;
    unsigned m_writeMask = 0;
    int m_write[MAXMON] = { 0 };
    int m_value[MAXMON] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};

static Worker g_worker;
static std::mutex g_instMutex;
static int g_instCount = 0;

struct Instance
{
    int index = -1;
    int pushed = -999;
    void* skin = nullptr;
    std::wstring var;
    wchar_t text[32] = { 0 };
};

static void ResolveApi()
{
    if (g_readFormula)
        return;
    // The API lives in Rainmeter.dll, not in the Rainmeter.exe launcher.
    HMODULE rm = GetModuleHandleW(L"Rainmeter.dll");
    if (!rm)
        rm = GetModuleHandleW(nullptr);
    g_readFormula = reinterpret_cast<FnReadFormula>(GetProcAddress(rm, "RmReadFormula"));
    g_readString = reinterpret_cast<FnReadString>(GetProcAddress(rm, "RmReadString"));
    g_execute = reinterpret_cast<FnExecute>(GetProcAddress(rm, "RmExecute"));
    g_get = reinterpret_cast<FnGet>(GetProcAddress(rm, "RmGet"));
}

extern "C" __declspec(dllexport) void Initialize(void** data, void* measure)
{
    ResolveApi();
    g_worker.start();
    { std::lock_guard<std::mutex> g(g_instMutex); ++g_instCount; }
    *data = new Instance();
    (void)measure;
}

extern "C" __declspec(dllexport) void Reload(void* data, void* measure, double* maxValue)
{
    Instance* inst = static_cast<Instance*>(data);
    if (maxValue)
        *maxValue = 100.0;
    if (!inst)
        return;
    int idx = 0;
    if (g_readFormula)
        idx = static_cast<int>(g_readFormula(measure, L"Index", 0.0));
    inst->index = idx;
    if (g_get)
        inst->skin = g_get(measure, RMG_SKIN);
    inst->var = L"Bri" + std::to_wstring(idx);
    if (g_readString)
    {
        const wchar_t* custom = g_readString(measure, L"Var", nullptr, FALSE);
        if (custom && *custom)
            inst->var = custom;
    }
    g_worker.requestRead(idx);
}

extern "C" __declspec(dllexport) double Update(void* data)
{
    Instance* inst = static_cast<Instance*>(data);
    if (!inst)
        return -1.0;
    int v = g_worker.value(inst->index);
    if (v != inst->pushed)
    {
        inst->pushed = v;
        if (g_execute && inst->skin)
        {
            std::wstring cmd = L"!SetVariable \"" + inst->var + L"\" \"" +
                               std::to_wstring(v) + L"\"";
            g_execute(inst->skin, cmd.c_str());
            g_execute(inst->skin, L"!Redraw");
        }
    }
    return v;
}

extern "C" __declspec(dllexport) LPCWSTR GetString(void* data)
{
    Instance* inst = static_cast<Instance*>(data);
    if (!inst)
        return L"";
    wsprintfW(inst->text, L"%d", g_worker.value(inst->index));
    return inst->text;
}

extern "C" __declspec(dllexport) void ExecuteBang(void* data, LPCWSTR args)
{
    Instance* inst = static_cast<Instance*>(data);
    if (!inst || !args)
        return;
    const wchar_t* s = args;
    while (*s == L' ' || *s == L'\t') ++s;

    if (_wcsnicmp(s, L"Read", 4) == 0)
    {
        g_worker.requestRead(inst->index);
        return;
    }
    if (_wcsnicmp(s, L"Set", 3) == 0)
    {
        g_worker.requestWrite(inst->index, _wtoi(s + 3));
        return;
    }
    if (_wcsnicmp(s, L"Bump", 4) == 0)
    {
        int cur = g_worker.value(inst->index);
        if (cur < 0)
            return;
        g_worker.requestWrite(inst->index, cur + _wtoi(s + 4));
        return;
    }
}

extern "C" __declspec(dllexport) void Finalize(void* data)
{
    delete static_cast<Instance*>(data);
    bool last = false;
    { std::lock_guard<std::mutex> g(g_instMutex); last = (--g_instCount <= 0); }
    if (last) g_worker.stop();
}
