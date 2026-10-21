// Discord Rich Presence fuer CS:GO Legacy (Supremacy), als Proxy fuer bin/mss32.dll.
//
// Die DLL leitet alle Miles-Sound-Exporte an mss32_orig.dll weiter (mss32.def) und startet
// beim Laden einen Thread, der Spielzustand aus dem eigenen Prozess liest und per
// Discord-IPC (Unix-Socket, AF_UNIX unter Wine) als Presence sendet.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "assets_keys.h"

static const char* kClientId = "1562240534796439683";

// Offsets: hazedumper csgo.json, Stand 2023-09-05 (Windows CS:GO Legacy).
namespace off {
// engine.dll
constexpr uintptr_t dwClientState = 0x59F19C;
constexpr uintptr_t clientState_State = 0x108;          // dwClientState_State (SignonState)
constexpr uintptr_t clientState_Map = 0x28C;            // dwClientState_Map (char[])
// client.dll
constexpr uintptr_t dwEntityList = 0x4E0102C;
constexpr uintptr_t dwLocalPlayer = 0xDEB99C;
constexpr uintptr_t m_iTeamNum = 0xF4;  // netvar, hazedumper
constexpr uintptr_t m_iObserverMode = 0x3388;
}  // namespace off

constexpr int kSignonFull = 6;

// ---------------------------------------------------------------------------------------------
// Logging

static FILE* g_log = nullptr;
static char g_dir[MAX_PATH] = {};

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------------------------
// Speicherzugriff (ReadProcessMemory auf den eigenen Prozess, damit ungueltige Zeiger nicht crashen)

static bool ReadMem(uintptr_t addr, void* out, size_t len) {
    if (addr < 0x10000) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, len, &got) && got == len;
}

template <class T>
static T Rd(uintptr_t addr, T def = T()) {
    T v = def;
    return ReadMem(addr, &v, sizeof v) ? v : def;
}

static std::string RdStr(uintptr_t addr, size_t max = 260) {
    if (max > 260) max = 260;
    char buf[261] = {};
    if (ReadMem(addr, buf, max)) {
        buf[max] = 0;
        return buf;
    }
    size_t n = 0;  // Chunk ueber Seitengrenze nicht lesbar: byteweise
    for (; n < max; ++n) {
        char c;
        if (!ReadMem(addr + n, &c, 1) || !c) break;
        buf[n] = c;
    }
    return std::string(buf, n);
}

// ---------------------------------------------------------------------------------------------
// Netvars aus der ClientClass-Liste (VClient018::GetAllClasses)

typedef void* (*CreateInterfaceFn)(const char*, int*);
typedef uintptr_t(__thiscall* ThisCallFn)(void*);
typedef uintptr_t(__thiscall* FindVarFn)(void*, const char*);

// RecvTable: props@0, nProps@4, name@12. RecvProp (0x3C): name@0, type@4, dataTable@40, offset@44.
static bool FindProp(uintptr_t table, const char* name, uint32_t base, uint32_t& out, int depth = 0) {
    if (!table || depth > 6) return false;
    uintptr_t props = Rd<uintptr_t>(table);
    int n = Rd<int>(table + 4);
    if (!props || n <= 0 || n > 4096) return false;
    for (int i = 0; i < n; ++i) {
        uintptr_t p = props + static_cast<uintptr_t>(i) * 0x3C;
        int type = Rd<int>(p + 4);
        uint32_t o = Rd<uint32_t>(p + 44);
        if (type == 6) {  // DPT_DataTable
            if (FindProp(Rd<uintptr_t>(p + 40), name, base + o, out, depth + 1)) return true;
        } else if (RdStr(Rd<uintptr_t>(p), 64) == name) {
            out = base + o;
            return true;
        }
    }
    return false;
}

struct Netvars {
    bool ready = false;
    int teamClassId = -1;        // CCSTeam
    uint32_t teamScore = 0;      // CCSTeam::m_scoreTotal
    uint32_t teamNumOfTeam = 0;  // CCSTeam::m_iTeamNum
};

static Netvars g_nv;
static uintptr_t g_client = 0, g_engine = 0, g_vstdlib = 0;
static uintptr_t g_cvar = 0;
static int g_findVarIdx = -1;

static bool ResolveNetvars() {
    auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(reinterpret_cast<HMODULE>(g_client), "CreateInterface"));
    if (!createInterface) return false;
    void* vclient = createInterface("VClient018", nullptr);
    if (!vclient) return false;
    uintptr_t vt = Rd<uintptr_t>(reinterpret_cast<uintptr_t>(vclient));
    uintptr_t fn = Rd<uintptr_t>(vt + 4 * 8);  // GetAllClasses
    if (!fn) return false;
    uintptr_t cc = reinterpret_cast<ThisCallFn>(fn)(vclient);

    Netvars nv;
    uintptr_t teamTable = 0;
    for (int guard = 0; cc && guard < 1024; ++guard, cc = Rd<uintptr_t>(cc + 16)) {
        std::string name = RdStr(Rd<uintptr_t>(cc + 8), 64);
        if (name == "CCSTeam") {
            teamTable = Rd<uintptr_t>(cc + 12);
            nv.teamClassId = Rd<int>(cc + 20);
        }
    }
    if (!teamTable) return false;
    if (!FindProp(teamTable, "m_scoreTotal", 0, nv.teamScore)) return false;
    if (!FindProp(teamTable, "m_iTeamNum", 0, nv.teamNumOfTeam)) return false;
    nv.ready = true;
    g_nv = nv;
    Log("netvars: CCSTeam id=%d m_scoreTotal=0x%X m_iTeamNum=0x%X", nv.teamClassId, nv.teamScore,
        nv.teamNumOfTeam);
    return true;
}

// ---------------------------------------------------------------------------------------------
// ConVars (game_type / game_mode)

static uintptr_t CallFindVar(int idx, const char* name) {
    uintptr_t vt = Rd<uintptr_t>(g_cvar);
    uintptr_t fn = Rd<uintptr_t>(vt + 4 * idx);
    if (!fn) return 0;
    return reinterpret_cast<FindVarFn>(fn)(reinterpret_cast<void*>(g_cvar), name);
}

static bool InitCvar() {
    auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(reinterpret_cast<HMODULE>(g_vstdlib), "CreateInterface"));
    if (!createInterface) return false;
    void* cvar = createInterface("VEngineCvar007", nullptr);
    if (!cvar) return false;
    g_cvar = reinterpret_cast<uintptr_t>(cvar);
    for (int idx : {16, 15}) {  // FindVar-Index variiert; Ergebnis wird am ConVar-Namen (+0xC) geprueft
        uintptr_t cv = CallFindVar(idx, "game_type");
        if (cv && RdStr(Rd<uintptr_t>(cv + 0xC), 32) == "game_type") {
            g_findVarIdx = idx;
            Log("cvar: FindVar vtable index %d", idx);
            return true;
        }
    }
    g_cvar = 0;
    return false;
}

static bool CvarInt(const char* name, int& out) {
    if (!g_cvar || g_findVarIdx < 0) return false;
    uintptr_t cv = CallFindVar(g_findVarIdx, name);
    if (!cv) return false;
    uintptr_t parent = Rd<uintptr_t>(cv + 0x1C);  // ConVar::m_pParent
    uintptr_t str = Rd<uintptr_t>((parent ? parent : cv) + 0x24);  // m_pszString
    if (!str) return false;
    out = atoi(RdStr(str, 16).c_str());
    return true;
}

static std::string ModeFromCvars(const std::string& map) {
    int type = 0, mode = 0;
    if (CvarInt("game_type", type) && CvarInt("game_mode", mode)) {
        switch (type * 10 + mode) {
            case 0: return "Casual";
            case 1: return "Competitive";
            case 2: return "Wingman";
            case 3: return "Weapons Expert";
            case 10: return "Arms Race";
            case 11: return "Demolition";
            case 12: return "Deathmatch";
            case 20: return "Training";
            case 30: return "Custom";
            case 40: return "Cooperative";
            case 50: return "Skirmish";
            case 60: return "Danger Zone";
        }
    }
    if (map.rfind("ar_", 0) == 0) return "Arms Race";
    if (map.rfind("dz_", 0) == 0) return "Danger Zone";
    if (map.rfind("gd_", 0) == 0) return "Guardian";
    return "Match";
}

// ---------------------------------------------------------------------------------------------
// Spielzustand

struct Presence {
    std::string details;
    std::string state;
    time_t start = 0;
    std::string largeImage, largeText, smallImage, smallText;
    bool operator==(const Presence& o) const {
        return details == o.details && state == o.state && start == o.start && largeImage == o.largeImage &&
               largeText == o.largeText && smallImage == o.smallImage && smallText == o.smallText;
    }
};

static std::string CurrentMap() {
    uintptr_t cs = Rd<uintptr_t>(g_engine + off::dwClientState);
    if (!cs) return {};
    std::string map = RdStr(cs + off::clientState_Map, 64);
    size_t slash = map.find_last_of("/\\");
    if (slash != std::string::npos) map = map.substr(slash + 1);
    size_t dot = map.rfind(".bsp");
    if (dot != std::string::npos) map.resize(dot);
    return map;
}

static uintptr_t EntityByIndex(int idx) { return Rd<uintptr_t>(g_client + off::dwEntityList + static_cast<uintptr_t>(idx) * 0x10); }

static int ClassIdOf(uintptr_t ent) {
    uintptr_t net = ent + 8;  // IClientNetworkable
    uintptr_t fn = Rd<uintptr_t>(Rd<uintptr_t>(net) + 4 * 2);  // GetClientClass
    if (!fn) return -1;
    uintptr_t cc = reinterpret_cast<ThisCallFn>(fn)(reinterpret_cast<void*>(net));
    return cc ? Rd<int>(cc + 20, -1) : -1;
}

// Team-Entities (CCSTeam) werden einmal pro Map gesucht und danach ueber ihren Listeneintrag validiert.
struct TeamEnt {
    int index = -1;
    uintptr_t ptr = 0;
};
static TeamEnt g_team[2];  // [0] = T (2), [1] = CT (3)
static DWORD g_lastTeamScan = 0;

static void ResetTeams() {
    g_team[0] = g_team[1] = TeamEnt();
    g_lastTeamScan = 0;
}

static bool FindTeams() {
    DWORD now = GetTickCount();
    if (g_lastTeamScan && now - g_lastTeamScan < 5000) return false;
    g_lastTeamScan = now;
    for (int i = 0; i < 2048; ++i) {
        uintptr_t ent = EntityByIndex(i);
        if (!ent || ClassIdOf(ent) != g_nv.teamClassId) continue;
        int team = Rd<int>(ent + g_nv.teamNumOfTeam, -1);
        if (team == 2 || team == 3) g_team[team - 2] = {i, ent};
    }
    Log("team scan: T idx=%d CT idx=%d", g_team[0].index, g_team[1].index);
    return g_team[0].ptr && g_team[1].ptr;
}

static bool ReadScores(int& t, int& ct) {
    for (auto& te : g_team) {
        if (te.ptr && EntityByIndex(te.index) != te.ptr) ResetTeams();
    }
    if (!g_team[0].ptr || !g_team[1].ptr) {
        if (!FindTeams()) return false;
    }
    t = Rd<int>(g_team[0].ptr + g_nv.teamScore, -1);
    ct = Rd<int>(g_team[1].ptr + g_nv.teamScore, -1);
    return t >= 0 && ct >= 0 && t < 100 && ct < 100;
}

// Spectator (Team 1) oder tot und einem Spieler zuschauend (Fixed/In-Eye/Chase/Roaming).
static bool IsSpectating() {
    uintptr_t local = Rd<uintptr_t>(g_client + off::dwLocalPlayer);
    if (!local) return false;
    int team = Rd<int>(local + off::m_iTeamNum, 0);
    int mode = Rd<int>(local + off::m_iObserverMode, 0);
    return team == 1 || (mode >= 3 && mode <= 6);
}

static int LocalTeam() {
    uintptr_t local = Rd<uintptr_t>(g_client + off::dwLocalPlayer);
    return local ? Rd<int>(local + off::m_iTeamNum, 0) : 0;
}

// Nur Bilder, die im Assets-Repo liegen; sonst gaebe Discord ein leeres Bild aus.
static const char* kAssetBaseUrl = "https://raw.githubusercontent.com/wildesPepega/csgo-rpc-assets/main/assets/";

// Liefert die Bild-URL fuer einen Key (Dateiname ohne Endung) oder "" wenn es kein solches Bild gibt.
static std::string AssetUrl(const std::string& key) {
    for (const char* f : kAssetFiles) {
        std::string file = f;
        if (file.compare(0, key.size() + 1, key + ".") == 0) return kAssetBaseUrl + file;
    }
    return {};
}

static const char* kProjectName = "CS:GO Supremacy";

static time_t g_sessionStart = 0;
static time_t g_matchStart = 0;
static std::string g_matchMap;

static Presence BuildPresence() {
    Presence p;
    uintptr_t cs = Rd<uintptr_t>(g_engine + off::dwClientState);
    int signon = cs ? Rd<int>(cs + off::clientState_State, -1) : -1;
    std::string map = signon > 0 ? CurrentMap() : std::string();

    static int lastSignon = -2;
    static std::string lastMap;
    if (signon != lastSignon || map != lastMap) {
        Log("state: signon=%d map='%s'", signon, map.c_str());
        lastSignon = signon;
        lastMap = map;
    }

    bool menuMap = map.empty() || map.rfind("ui/", 0) == 0 || map.find("background") != std::string::npos;
    if (signon == kSignonFull && !menuMap) {
        if (map != g_matchMap) {
            g_matchMap = map;
            g_matchStart = time(nullptr);
            ResetTeams();
        }
        if (!g_nv.ready) ResolveNetvars();
        p.details = ModeFromCvars(map) + " - " + map;
        int t = 0, ct = 0;
        if (g_nv.ready && ReadScores(t, ct)) {
            char buf[64];
            snprintf(buf, sizeof buf, "CT %d : %d T", ct, t);
            p.state = buf;
        } else {
            p.state = "In game";
        }
        p.start = g_matchStart;
        std::string key = map;
        for (char& c : key) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        p.largeImage = AssetUrl(key.empty() ? "logo" : key);
        if (p.largeImage.empty()) p.largeImage = AssetUrl("logo");
        p.largeText = map;
        int team = LocalTeam();
        if (IsSpectating()) {
            p.smallImage = AssetUrl("spectator");
            p.smallText = "Spectating";
        } else if (team == 2 || team == 3) {
            p.smallImage = AssetUrl(team == 3 ? "ct" : "t");
            p.smallText = team == 3 ? "Counter-Terrorists" : "Terrorists";
        } else {
            p.smallImage = AssetUrl("logo");
            p.smallText = kProjectName;
        }
        return p;
    }
    g_matchMap.clear();
    if (signon > 0 && signon < kSignonFull && !map.empty()) {
        p.details = "Loading map";
        p.state = map;
        p.largeImage = AssetUrl("logo");
        p.largeText = kProjectName;
        return p;
    }
    p.details = "In Main Menu";
    p.start = g_sessionStart;
    p.largeImage = AssetUrl("logo");
    p.largeText = kProjectName;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Discord IPC

static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += static_cast<char>(c);
        } else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else {
            o += static_cast<char>(c);
        }
    }
    return o;
}

struct SockAddrUn {
    USHORT family;
    char path[108];
};

class DiscordIpc {
public:
    bool Connected() const { return s_ != INVALID_SOCKET; }

    void Close() {
        if (s_ != INVALID_SOCKET) closesocket(s_);
        s_ = INVALID_SOCKET;
    }

    bool Connect() {
        std::string bases[2];
        char env[MAX_PATH] = {};
        if (GetEnvironmentVariableA("XDG_RUNTIME_DIR", env, sizeof env)) bases[0] = env;
        bases[1] = "/run/user/1000";
        static const char* subdirs[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};
        for (const auto& base : bases) {
            if (base.empty()) continue;
            for (const char* sub : subdirs) {
                for (int n = 0; n < 10; ++n) {
                    std::string unixPath = base + sub + "/discord-ipc-" + std::to_string(n);
                    std::string winPath = "Z:" + unixPath;
                    for (char& c : winPath) if (c == '/') c = '\\';
                    if (TryConnect(winPath) || TryConnect(unixPath)) {
                        Log("discord: verbunden ueber %s", unixPath.c_str());
                        if (Handshake()) return true;
                        Close();
                    }
                }
            }
        }
        return false;
    }

    bool SetActivity(const Presence& p) {
        if (!Connected()) return false;
        Pump();
        if (!Connected()) return false;
        std::string act = "{\"details\":\"" + JsonEscape(p.details) + "\"";
        if (!p.state.empty()) act += ",\"state\":\"" + JsonEscape(p.state) + "\"";
        if (p.start) act += ",\"timestamps\":{\"start\":" + std::to_string(static_cast<long long>(p.start)) + "}";
        std::string assets;
        auto addAsset = [&](const char* k, const std::string& v) {
            if (!v.empty()) assets += std::string(assets.empty() ? "" : ",") + "\"" + k + "\":\"" + JsonEscape(v) + "\"";
        };
        addAsset("large_image", p.largeImage);
        addAsset("large_text", p.largeText);
        addAsset("small_image", p.smallImage);
        addAsset("small_text", p.smallText);
        if (!assets.empty()) act += ",\"assets\":{" + assets + "}";
        act += "}";
        std::string msg = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"activity\":" + act +
                          "},\"nonce\":\"" + std::to_string(++nonce_) + "\"}";
        Log("discord: SET_ACTIVITY %s", act.c_str());
        if (!Send(1, msg)) {
            Close();
            return false;
        }
        return true;
    }

    // Eingehende Frames abholen (Antworten, Ping).
    void Pump() {
        while (Connected()) {
            u_long avail = 0;
            if (ioctlsocket(s_, FIONREAD, &avail) != 0 || avail < 8) return;
            uint32_t op = 0;
            std::string payload;
            if (!RecvFrame(op, payload)) {
                Close();
                return;
            }
            Log("discord: op=%u %.200s", op, payload.c_str());
            if (op == 3) Send(4, payload);
            if (op == 2) Close();
        }
    }

private:
    SOCKET s_ = INVALID_SOCKET;
    int nonce_ = 0;
    int lastErr_ = 0;

    bool TryConnect(const std::string& path) {
        SOCKET t = socket(1 /*AF_UNIX*/, SOCK_STREAM, 0);
        if (t == INVALID_SOCKET) {
            lastErr_ = WSAGetLastError();
            return false;
        }
        SockAddrUn a = {};
        a.family = 1;
        strncpy(a.path, path.c_str(), sizeof a.path - 1);
        if (connect(t, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            lastErr_ = WSAGetLastError();
            closesocket(t);
            return false;
        }
        DWORD timeout = 2000;
        setsockopt(t, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
        s_ = t;
        return true;
    }

    bool Handshake() {
        if (!Send(0, std::string("{\"v\":1,\"client_id\":\"") + kClientId + "\"}")) return false;
        uint32_t op = 0;
        std::string payload;
        if (!RecvFrame(op, payload)) return false;
        Log("discord: handshake op=%u %.200s", op, payload.c_str());
        return op == 1;
    }

    bool SendAll(const char* d, size_t n) {
        while (n) {
            int r = send(s_, d, static_cast<int>(n), 0);
            if (r <= 0) return false;
            d += r;
            n -= static_cast<size_t>(r);
        }
        return true;
    }

    bool RecvAll(char* d, size_t n) {
        while (n) {
            int r = recv(s_, d, static_cast<int>(n), 0);
            if (r <= 0) return false;
            d += r;
            n -= static_cast<size_t>(r);
        }
        return true;
    }

    bool Send(uint32_t op, const std::string& json) {
        uint32_t hdr[2] = {op, static_cast<uint32_t>(json.size())};
        return SendAll(reinterpret_cast<const char*>(hdr), 8) && SendAll(json.data(), json.size());
    }

    bool RecvFrame(uint32_t& op, std::string& payload) {
        uint32_t hdr[2];
        if (!RecvAll(reinterpret_cast<char*>(hdr), 8) || hdr[1] > (1u << 20)) return false;
        op = hdr[0];
        payload.assign(hdr[1], '\0');
        return hdr[1] == 0 || RecvAll(&payload[0], hdr[1]);
    }
};

// ---------------------------------------------------------------------------------------------
// Worker

static DWORD WINAPI Worker(LPVOID) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\DiscordRPC.log", g_dir);
    g_log = fopen(path, "w");
    Log("start (pid %lu)", GetCurrentProcessId());

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_sessionStart = time(nullptr);

    while (!(g_client = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"))) ||
           !(g_engine = reinterpret_cast<uintptr_t>(GetModuleHandleA("engine.dll"))) ||
           !(g_vstdlib = reinterpret_cast<uintptr_t>(GetModuleHandleA("vstdlib.dll")))) {
        Sleep(1000);
    }
    Log("module: client=%p engine=%p vstdlib=%p", reinterpret_cast<void*>(g_client), reinterpret_cast<void*>(g_engine),
        reinterpret_cast<void*>(g_vstdlib));

    DiscordIpc discord;
    Presence last;
    bool haveLast = false;
    DWORD lastConnect = 0;

    for (;; Sleep(1000)) {
        if (!g_cvar) InitCvar();
        Presence now = BuildPresence();

        if (!discord.Connected()) {
            DWORD t = GetTickCount();
            if (lastConnect && t - lastConnect < 5000) continue;
            lastConnect = t;
            if (!discord.Connect()) {
                Log("discord: keine Verbindung (WSA-Fehler %d)", WSAGetLastError());
                continue;
            }
            haveLast = false;
        }
        discord.Pump();
        if (!haveLast || !(now == last)) {
            if (discord.SetActivity(now)) {
                last = now;
                haveLast = true;
            }
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, sizeof exe);
    const char* base = strrchr(exe, '\\');
    base = base ? base + 1 : exe;
    if (_stricmp(base, "csgo.exe") != 0) return TRUE;

    GetModuleFileNameA(inst, g_dir, sizeof g_dir);
    *strrchr(g_dir, '\\') = 0;

    // Original zuerst laden, damit die Export-Weiterleitungen aufloesen.
    char orig[MAX_PATH];
    snprintf(orig, sizeof orig, "%s\\mss32_orig.dll", g_dir);
    LoadLibraryA(orig);

    CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    return TRUE;
}
