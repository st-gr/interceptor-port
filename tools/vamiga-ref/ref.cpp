// tools/vamiga-ref/ref.cpp - boot a disk in vAmiga headless and dump a frame.
#include "VAmiga.h"
#include "Constants.h"
#include "Memory.h"
#include "Agnus.h"
#include "Paula.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <vector>

using namespace vamiga;

static bool verbose = false;
static std::atomic<long> g_pauses{0};   // Msg::PAUSE count: run/stepInto return before the state changes, so waits key on this
static void on_message(const void *, Message m) {
    if (m.type == Msg::PAUSE) g_pauses++;
    if (verbose) std::fprintf(stderr, "msg %s(%ld)\n", MsgEnum::key(m.type), (long)m.value);
}

// vAmiga only accepts .rom/.bin suffixes from a path, so load via a memory buffer.
static std::vector<u8> slurp(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    return std::vector<u8>(std::istreambuf_iterator<char>(f), {});
}

// Write the current texture as a PPM. The texture is super-hires: 2 * HPIXELS u32 texels per
// row. Keep every second texel to get a hires-wide (HPIXELS) image.
static void write_frame(VAmiga &va, const std::string &out) {
    va.videoPort.lockTexture();
    isize nr = 0; bool lof = false, plof = false;
    const u32 *tex = va.videoPort.getTexture(&nr, &lof, &plof);
    if (verbose) std::fprintf(stderr, "texture frame nr=%ld\n", (long)nr);
    const int w = (int)HPIXELS, h = (int)VPIXELS, stride = 2 * (int)HPIXELS;
    std::ofstream f(out, std::ios::binary);
    if (!f) { va.videoPort.unlockTexture(); throw std::runtime_error("cannot open " + out + " for writing"); }
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; i++) {
        u32 p = tex[(i / w) * stride + (i % w) * 2];
        unsigned char rgb[3] = { (unsigned char)(p & 0xFF), (unsigned char)((p >> 8) & 0xFF),
                                 (unsigned char)((p >> 16) & 0xFF) };
        f.write((const char *)rgb, 3);
    }
    va.videoPort.unlockTexture();
    f.flush();
    if (!f.good()) throw std::runtime_error("writing " + out + " failed");
    std::printf("wrote %s (%dx%d)\n", out.c_str(), w, h);
}

// ---- Log, RAM access, flat-image address mapping ------------------------------------------------
static std::ofstream g_log;   // --log FILE (append-only)
static void log_line(const std::string &l) {
    std::printf("%s\n", l.c_str());
    if (g_log.is_open()) { g_log << l << "\n"; g_log.flush(); }
}
static std::string hx(u32 v, int w = 0) {
    char b[24]; std::snprintf(b, sizeof b, "%0*X", w, v); return b;
}
// Hunk table of the running game: flat-image address range -> RAM address (filled by `seglist`).
struct SegMap { u32 flat = 0, size = 0, ram = 0; bool found = false; };
static std::vector<SegMap> segmap;
static u32 flat_to_ram(u32 f) {
    for (const SegMap &s : segmap)
        if (s.found && f >= s.flat && f < s.flat + s.size) return s.ram + (f - s.flat);
    throw std::runtime_error("flat address 0x" + hx(f) + " is in no located hunk (run seglist first)");
}
static u8 peek8(VAmiga &va, u32 a) { return va.mem.debugger.spypeek8(Accessor::CPU, a); }
static u16 peek16(VAmiga &va, u32 a) { return va.mem.debugger.spypeek16(Accessor::CPU, a); }
static u32 peek32(VAmiga &va, u32 a) { return ((u32)peek16(va, a) << 16) | peek16(va, a + 2); }

// Minimal reader for the "hunks" array of interceptor.map.json: (addr, size) pairs in file order.
static std::vector<std::pair<u32, u32>> read_hunk_table(const std::string &path) {
    auto b = slurp(path);
    std::string t(b.begin(), b.end());
    size_t end = t.find("\"symbols\"");
    if (end != std::string::npos) t.resize(end);
    std::vector<std::pair<u32, u32>> r;
    size_t p = 0;
    while ((p = t.find("\"addr\":", p)) != std::string::npos) {
        u32 a = (u32)std::strtoul(t.c_str() + p + 8, nullptr, 10);
        size_t q = t.find("\"size\":", p);
        if (q == std::string::npos) break;
        r.push_back({a, (u32)std::strtoul(t.c_str() + q + 8, nullptr, 10)});
        p = q;
    }
    return r;
}

// seglist: LoadSeg leaves each hunk as [size long][BPTR to next segment's link long][data...].
// Find hunk 0 by scanning chip and slow RAM for the first 16 bytes of the flat image (the
// operands that the relocation turned into flat addresses are wildcards), then follow the BPTR
// chain and check every segment size against the hunk table. Logs one line per hunk.
static void do_seglist(VAmiga &va, const std::string &imgpath, const std::string &mappath) {
    auto img = slurp(imgpath);
    auto tab = read_hunk_table(mappath);
    if (img.size() < 16 || tab.empty()) throw std::runtime_error("bad image or map");
    const u32 lo = tab.front().first, hi = tab.back().first + tab.back().second;
    bool wild[16] = {};
    for (int o = 0; o + 4 <= 16; o++) {
        u32 v = ((u32)img[o] << 24) | (img[o + 1] << 16) | (img[o + 2] << 8) | img[o + 3];
        if (v >= lo && v < hi) for (int k = 0; k < 4; k++) wild[o + k] = true;
    }
    const std::pair<u32, u32> ranges[] = { {0x000400, 0x080000}, {0xC00000, 0xC80000} };
    u32 first = 0;
    for (auto r : ranges) {
        for (u32 a = r.first; a + 16 <= r.second && !first; a += 4) {
            if (!wild[0] && peek8(va, a) != img[0]) continue;
            bool ok = true;
            for (int k = 0; k < 16 && ok; k++) if (!wild[k] && peek8(va, a + k) != img[k]) ok = false;
            if (ok) first = a;
        }
    }
    if (!first) throw std::runtime_error("hunk 0 not found in RAM");
    segmap.assign(tab.size(), SegMap());
    u32 data = first;
    size_t n = 0;
    for (; n < tab.size(); n++) {
        u32 size = peek32(va, data - 8);
        u32 next = peek32(va, data - 4);
        segmap[n] = { tab[n].first, tab[n].second, data, true };
        log_line("seg " + std::to_string(n) + " flat " + hx(tab[n].first, 6) + " ram " + hx(data, 6) +
                 " size " + std::to_string(tab[n].second) + " segsize " + std::to_string(size) +
                 (size >= tab[n].second + 8 && size <= tab[n].second + 16 ? " ok" : " SIZE-MISMATCH"));
        // Tolerance: LoadSeg stores hunk size + 8 (observed exactly +8 for all 185); up to +16 is accepted for rounding.
        if (size < tab[n].second + 8 || size > tab[n].second + 16)
            throw std::runtime_error("segment " + std::to_string(n) + " size mismatch (wrong image or map?)");
        if (next == 0) { n++; break; }
        data = next * 4 + 4;
    }
    log_line("seglist hunks " + std::to_string(n) + " of " + std::to_string(tab.size()) + " hunk0 " + hx(first, 6));
    if (n != tab.size()) throw std::runtime_error("chain has " + std::to_string(n) + " segments, expected " + std::to_string(tab.size()));
}

// ---- Scripting ----------------------------------------------------------------------------
struct KeyName { const char *name; u8 code; };
static const KeyName key_table[] = {
    {"1",0x01},{"2",0x02},{"3",0x03},{"4",0x04},{"5",0x05},{"6",0x06},{"7",0x07},{"8",0x08},{"9",0x09},{"0",0x0A},
    {"MINUS",0x0B},{"EQUALS",0x0C},{"BACKSPACE",0x41},
    {"Q",0x10},{"W",0x11},{"E",0x12},{"R",0x13},{"T",0x14},{"Y",0x15},{"U",0x16},{"I",0x17},{"O",0x18},{"P",0x19},
    {"LBRACKET",0x1A},{"RBRACKET",0x1B},
    {"A",0x20},{"S",0x21},{"D",0x22},{"F",0x23},{"G",0x24},{"H",0x25},{"J",0x26},{"K",0x27},{"L",0x28},
    {"Z",0x31},{"X",0x32},{"C",0x33},{"V",0x34},{"B",0x35},{"N",0x36},{"M",0x37},{"COMMA",0x38},{"PERIOD",0x39},
    {"SPACE",0x40},{"RETURN",0x44},{"ESC",0x45},
    {"F1",0x50},{"F2",0x51},{"F3",0x52},{"F4",0x53},{"F5",0x54},{"F6",0x55},{"F7",0x56},{"F8",0x57},{"F9",0x58},{"F10",0x59},
    {"UP",0x4C},{"DOWN",0x4D},{"RIGHT",0x4E},{"LEFT",0x4F},{"LSHIFT",0x60},{"RSHIFT",0x61},
    {"KP0",0x0F},{"KP1",0x1D},{"KP2",0x1E},{"KP3",0x1F},{"KP4",0x2D},{"KP5",0x2E},{"KP6",0x2F},
    {"KP7",0x3D},{"KP8",0x3E},{"KP9",0x3F},{"KPDOT",0x3C},{"KPENTER",0x43},
};
static bool lookup_key(const std::string &n, u8 &code) {
    if (n.size() > 2 && n[0] == '0' && (n[1] == 'x' || n[1] == 'X')) {          // a raw Amiga key code, 0x00-0x7F (census of the key handler, note 08 section 11)
        char *e = nullptr; long v = std::strtol(n.c_str(), &e, 16);
        if (*e == 0 && v >= 0 && v <= 0x7F) { code = (u8)v; return true; }
        return false;
    }
    for (const auto &k : key_table) if (n == k.name) { code = k.code; return true; }
    return false;
}
static bool parse_num(const std::string &s, double &v, double lo, double hi) {
    size_t used = 0;
    try { v = std::stod(s, &used); } catch (...) { return false; }
    return used == s.size() && v >= lo && v <= hi;
}
static void sleep_s(double s) { std::this_thread::sleep_for(std::chrono::milliseconds((long)(s * 1000))); }
[[noreturn]] static void script_fail(int line, const std::string &msg) {
    std::fprintf(stderr, "script line %d: %s\n", line, msg.c_str());
    std::exit(2);
}

// One parsed command; all arguments are converted once, at parse time.
struct ScriptCmd {
    int line = 0;
    std::string op, path;
    double num = 0, num2 = 0;        // wait seconds / key hold / mouse x, y
    std::vector<u8> keys;            // key: one entry; type: one per character
    std::string path2, label;        // seglist map file; peek / colors / log label or text
    u32 addr = 0, len = 0;           // peek
    bool flat = false;               // peek: address is a flat-image address
    u32 addr2 = 0; bool flat2 = false;   // copy: the destination
    std::string cmp;                 // holduntil: "lt" or "gt"
    long target = 0;                 // holduntil: signed target on the folded angle
};
static bool has_suffix(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static void check_parent_dir(int line, const std::string &path) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent))
        script_fail(line, "directory does not exist: " + parent.string());
}

// Parse the whole script first (so errors surface before a long wait), then execute.
static std::vector<ScriptCmd> parse_script(const std::string &path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "error: cannot read script %s\n", path.c_str()); std::exit(2); }
    std::vector<ScriptCmd> cmds;
    std::string ln;
    for (int n = 1; std::getline(f, ln); n++) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        size_t b = ln.find_first_not_of(" \t");
        if (b == std::string::npos || ln[b] == '#') continue;
        ln = ln.substr(b);
        ScriptCmd c; c.line = n;
        size_t sp = ln.find(' ');
        c.op = ln.substr(0, sp);
        std::string rest = sp == std::string::npos ? "" : ln.substr(sp + 1);
        std::vector<std::string> a;
        { std::istringstream is(rest); std::string t; while (is >> t) a.push_back(t); }
        u8 code;
        if (c.op == "wait") {
            if (a.size() != 1 || !parse_num(a[0], c.num, 0, 86400)) script_fail(n, "wait needs <seconds> in 0..86400");
        } else if (c.op == "keydown" || c.op == "keyup") {
            // keydown NAME / keyup NAME: hold a key across other commands (SHIFT with a digit, a held key across a screen change)
            if (a.size() != 1) script_fail(n, c.op + " needs NAME");
            if (!lookup_key(a[0], code)) script_fail(n, "unknown key name '" + a[0] + "'");
            c.keys.push_back(code);
        } else if (c.op == "key") {
            if (a.empty() || a.size() > 2) script_fail(n, "key needs NAME [hold_seconds]");
            if (!lookup_key(a[0], code)) script_fail(n, "unknown key name '" + a[0] + "'");
            c.keys.push_back(code);
            c.num = 0.12;
            if (a.size() == 2 && !parse_num(a[1], c.num, 0, 600)) script_fail(n, "bad hold_seconds '" + a[1] + "'");
            if (c.num < 0.05) c.num = 0.05;
        } else if (c.op == "type") {
            if (rest.empty()) script_fail(n, "type needs text");
            if (rest.size() >= 2 && rest.compare(rest.size() - 2, 2, "\\n") == 0) rest.resize(rest.size() - 2), c.num = 1;
            for (char ch : rest) {
                std::string k = ch == ' ' ? "SPACE" : std::string(1, (char)std::toupper((unsigned char)ch));
                if (!lookup_key(k, code) || (k.size() != 1 && k != "SPACE")) script_fail(n, std::string("cannot type character '") + ch + "'");
                c.keys.push_back(code);
            }
            if (c.num == 1) c.keys.push_back(0x44);   // trailing \n = RETURN
        } else if (c.op == "code") {
            // code @FILE CHALLENGE: the answer comes from a git-ignored lookup file (lines "CHALLENGE ANSWER"),
            // so derived countercodes never appear in a committed script. Types the answer and RETURN.
            if (a.size() != 2 || a[0].size() < 2 || a[0][0] != '@') script_fail(n, "code needs @FILE CHALLENGE");
            std::ifstream lf(a[0].substr(1));
            if (!lf) script_fail(n, "cannot read lookup file " + a[0].substr(1));
            std::string l, ans;
            while (std::getline(lf, l)) {
                std::istringstream is(l); std::string ch, an;
                if (is >> ch >> an && ch[0] != '#' && ch == a[1]) ans = an;
            }
            if (ans.empty()) script_fail(n, "no answer for challenge " + a[1] + " in " + a[0].substr(1));
            for (char ch : ans) {
                std::string k(1, ch);
                if (!std::isdigit((unsigned char)ch) || !lookup_key(k, code)) script_fail(n, "answer must be digits");
                c.keys.push_back(code);
            }
            c.keys.push_back(0x44);
        } else if (c.op == "mouse") {
            if (a.size() != 2 || !parse_num(a[0], c.num, 0, HPIXELS) || !parse_num(a[1], c.num2, 0, VPIXELS))
                script_fail(n, "mouse needs <x> <y> (texture coordinates, x 0.." + std::to_string((int)HPIXELS) + ", y 0.." + std::to_string((int)VPIXELS) + ")");
        } else if (c.op == "click") {
            if (!a.empty()) script_fail(n, "click takes no arguments");
        } else if (c.op == "frame" || c.op == "snapshot" || c.op == "restore") {
            if (a.size() != 1) script_fail(n, c.op + " needs exactly one <path>");
            c.path = a[0];
            if (c.op != "restore") check_parent_dir(n, c.path);
            if (c.op != "frame" && !has_suffix(c.path, ".vasnap")) script_fail(n, c.op + " path must end in .vasnap (vAmiga rejects other suffixes)");
            if (c.op == "frame" && !has_suffix(c.path, ".ppm")) script_fail(n, "frame path must end in .ppm");
        } else if (c.op == "peek") {
            // peek <addr> <len> [label]; addr is hex: F:4B23C = flat-image address, R:1234 or bare = RAM address
            if (a.size() < 2 || a.size() > 3) script_fail(n, "peek needs <addr> <len> [label]");
            std::string ad = a[0];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr;
            unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad peek address '" + a[0] + "'");
            c.addr = (u32)v;
            double l; if (!parse_num(a[1], l, 1, 65536)) script_fail(n, "peek length must be 1..65536");
            c.len = (u32)l;
            if (!c.flat && (u64)c.addr + c.len > 0x1000000ull) script_fail(n, "peek range exceeds the 24-bit address space");
            c.label = a.size() == 3 ? a[2] : "peek";
        } else if (c.op == "seglist") {
            if (a.size() != 2) script_fail(n, "seglist needs <image.bin> <map.json>");
            for (const std::string &f : a) if (!std::filesystem::is_regular_file(f)) script_fail(n, "no such file: " + f);
            c.path = a[0]; c.path2 = a[1];
        } else if (c.op == "colors") {
            if (a.size() > 1) script_fail(n, "colors takes an optional label");
            c.label = a.empty() ? "colors" : a[0];
        } else if (c.op == "log") {
            if (rest.empty()) script_fail(n, "log needs text");
            c.label = rest;
        } else if (c.op == "holduntil") {
            // holduntil KEY ADDR lt|gt VALUE [max_seconds]: hold KEY until the angle word at ADDR (28800 units per circle,
            // folded to -14400..14400) is below (lt) or above (gt) VALUE; polls every 5 ms of wall time; the release lags the
            // target by one to four game frames, so the stop is that much past it (the logged value is exact). ADDR as for peek.
            if (a.size() < 4 || a.size() > 5) script_fail(n, "holduntil needs KEY ADDR lt|gt VALUE [max_seconds]");
            if (!lookup_key(a[0], code)) script_fail(n, "unknown key name '" + a[0] + "'");
            c.keys.push_back(code);
            std::string ad = a[1];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad address '" + a[1] + "'");
            c.addr = (u32)v;
            if (a[2] != "lt" && a[2] != "gt") script_fail(n, "comparison must be lt or gt");
            c.cmp = a[2];
            double t; if (!parse_num(a[3], t, -14400, 14400)) script_fail(n, "target must be -14400..14400");
            c.target = (long)t;
            c.num = 20;
            if (a.size() == 5 && !parse_num(a[4], c.num, 0.1, 600)) script_fail(n, "bad max_seconds");
        } else if (c.op == "step") {
            c.num = 1;
            if (a.size() > 1 || (a.size() == 1 && !parse_num(a[0], c.num, 1, 100000))) script_fail(n, "step needs an optional count 1..100000");
        } else if (c.op == "poke") {
            // poke <addr> <len 1|2|4> <value>: write RAM of a frozen machine (big-endian, same address forms as peek); a
            // capture aid for setting up a state (for example the crash allowance), never used to read the game.
            if (a.size() != 3) script_fail(n, "poke needs <addr> <len 1|2|4> <value>");
            std::string ad = a[0];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad poke address '" + a[0] + "'");
            c.addr = (u32)v;
            double l; if (!parse_num(a[1], l, 1, 4) || (l != 1 && l != 2 && l != 4)) script_fail(n, "poke length must be 1, 2 or 4");
            c.len = (u32)l;
            char *e2 = nullptr; long tv = std::strtol(a[2].c_str(), &e2, 0);
            if (a[2].empty() || *e2) script_fail(n, "bad poke value '" + a[2] + "'");
            c.target = tv;
            c.label = "poke";
        } else if (c.op == "copy") {
            // copy <src> <dst> <len>: copy LEN bytes inside the frozen machine's RAM (both addresses in the forms of peek; the source is read through spy reads). A capture aid that
            // stages a state from what the running game holds, so that a script names addresses only and carries no bytes of the game.
            if (a.size() != 3) script_fail(n, "copy needs <src> <dst> <len>");
            for (int k = 0; k < 2; k++) {
                std::string ad = a[k];
                bool flat = false;
                if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { flat = ad[0] == 'F'; ad = ad.substr(2); }
                char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
                if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad copy address '" + a[k] + "'");
                if (k == 0) { c.addr = (u32)v; c.flat = flat; } else { c.addr2 = (u32)v; c.flat2 = flat; }
            }
            double l; if (!parse_num(a[2], l, 1, 65536)) script_fail(n, "copy length must be 1..65536");
            c.len = (u32)l;
            c.label = "copy";
        } else if (c.op == "pokefill") {
            // pokefill <addr> <len> <hex bytes>: like poke, writes LEN bytes from ADDR with the byte pattern repeated (a capture aid:
            // the C-modulus probe of note 04 fills a bitplane with a non-periodic pattern before a blit)
            if (a.size() == 4 && a[3] == "ifblit") { c.cmp = "ifblit"; a.pop_back(); }   // only when the previous runto stopped at a blit
            if (a.size() != 3) script_fail(n, "pokefill needs <addr> <len> <hex bytes> [ifblit]");
            std::string ad = a[0];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad pokefill address '" + a[0] + "'");
            c.addr = (u32)v;
            double l; if (!parse_num(a[1], l, 1, 65536)) script_fail(n, "pokefill length must be 1..65536");
            c.len = (u32)l;
            if (a[2].empty() || a[2].size() % 2) script_fail(n, "pokefill pattern must be an even number of hex digits");
            for (size_t i = 0; i < a[2].size(); i += 2) {
                char *e2 = nullptr; std::string bt = a[2].substr(i, 2); unsigned long b = std::strtoul(bt.c_str(), &e2, 16);
                if (*e2) script_fail(n, "bad pokefill pattern '" + a[2] + "'");
                c.keys.push_back((u8)b);
            }
        } else if (c.op == "stepuntil") {
            // stepuntil ADDR LEN and|eq|ne|lt|gt VALUE [max_frames]: after `freeze`, run frame by frame until the LEN-byte
            // big-endian value at ADDR (as for peek) satisfies the test (and: value & VALUE nonzero; lt, gt signed); it is
            // tested before the first frame too. Frame-exact alignment with an event, whatever the wall-clock jitter before.
            if (a.size() < 4 || a.size() > 5) script_fail(n, "stepuntil needs ADDR LEN and|eq|ne|lt|gt VALUE [max_frames]");
            std::string ad = a[0];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad address '" + a[0] + "'");
            c.addr = (u32)v;
            double l; if (!parse_num(a[1], l, 1, 4) || (l != 1 && l != 2 && l != 4)) script_fail(n, "LEN must be 1, 2 or 4");
            c.len = (u32)l;
            if (a[2] != "and" && a[2] != "eq" && a[2] != "ne" && a[2] != "lt" && a[2] != "gt") script_fail(n, "test must be and, eq, ne, lt or gt");
            c.cmp = a[2];
            char *e2 = nullptr; long tv = std::strtol(a[3].c_str(), &e2, 0);
            if (a[3].empty() || *e2) script_fail(n, "bad VALUE '" + a[3] + "'");
            c.target = tv;
            c.num = 2000;
            if (a.size() == 5 && !parse_num(a[4], c.num, 1, 100000)) script_fail(n, "bad max_frames");
        } else if (c.op == "blittrace") {
            // blittrace on|off: while on, `runto` stops at every write to BLTSIZE (a CPU watchpoint at 0xDFF058) and logs the
            // blitter registers of that blit (see blit_line); a diagnostic of the blitter, never used to stage a state.
            if (a.size() != 1 || (a[0] != "on" && a[0] != "off")) script_fail(n, "blittrace needs on or off");
            c.num = a[0] == "on";
        } else if (c.op == "audtrace") {
            // audtrace on|off: while on, `runto`, `step` and `stepuntil` log every CPU write to AUD0..3 (LC, LEN, PER, VOL), DMACON and
            // ADKCON (a CPU watchpoint on each register; see aud_line); a diagnostic of the sound hardware use, never a staging aid.
            // `audtrace on nodmacon` leaves DMACON out: the game's blitter code writes it hundreds of times a frame in a 3D scene, and every stop costs time.
            // `audtrace on frame` sets no watchpoint: `step` and `stepuntil` log one line per video frame (the state at the end of the frame), which is fast (a watchpoint
            // stop costs the emulator thread a wake-up, a few hundred milliseconds of wall clock in all with the logging) and shows the registers as the vertical-blank
            // server and the game's passes left them; the per-write mode is for short windows.
            if (a.size() == 2 && a[0] == "on" && (a[1] == "nodmacon" || a[1] == "frame")) { c.cmp = a[1]; a.pop_back(); }
            if (a.size() != 1 || (a[0] != "on" && a[0] != "off")) script_fail(n, "audtrace needs on|off, on nodmacon or on frame");
            c.num = a[0] == "on";
        } else if (c.op == "wav") {
            // wav <path.wav> start|stop: record vAmiga's mixed stereo output (its audio port) as a 16-bit WAV at the host sample rate.
            // `wav <path> start vamiga` keeps vAmiga's own stereo separation and audio filter; without it the recording uses hard panning (voices 0 and 3 left, 1 and 2 right,
            // the hardware's pairing) and no filter, so that it shows the voices as the game wrote them.
            if (a.size() == 3 && a[1] == "start" && a[2] == "vamiga") { c.cmp = "vamiga"; a.pop_back(); }
            if (a.size() != 2 || (a[1] != "start" && a[1] != "stop")) script_fail(n, "wav needs <path.wav> start|stop");
            c.path = a[0]; c.num = a[1] == "start";
            check_parent_dir(n, c.path);
            if (!has_suffix(c.path, ".wav")) script_fail(n, "wav path must end in .wav");
        } else if (c.op == "blitstop") {
            // blitstop <bltcon0 hex>|off: `runto` also returns right after the BLTSIZE write of a traced blit with this BLTCON0
            // (the blit has not started yet), so that peek sees the planes before it.
            if (a.size() != 1) script_fail(n, "blitstop needs a BLTCON0 value (hex) or off");
            if (a[0] == "off") c.target = -1;
            else { char *e = nullptr; unsigned long v = std::strtoul(a[0].c_str(), &e, 16); if (*e || v > 0xFFFF) script_fail(n, "bad BLTCON0 '" + a[0] + "'"); c.target = (long)v; }
        } else if (c.op == "runto") {
            // runto <addr> [max_seconds]: after `freeze`, set a CPU breakpoint at ADDR (as for peek; the instruction there is
            // not executed yet when the machine stops), run, and stop there (or at a blitstop blit); logs `# runto ...`.
            // A third word `ifblit` skips the command unless the previous runto stopped at a blitstop blit.
            if (a.size() == 3 && a[2] == "ifblit") { c.cmp = "ifblit"; a.pop_back(); }
            if (a.size() == 3 && a[2] == "soft") { c.cmp = "soft"; a.pop_back(); }   // soft: reaching max_seconds first is not an error (a timed run under audtrace)
            if (a.empty() || a.size() > 2) script_fail(n, "runto needs ADDR [max_seconds] [ifblit|soft]");
            std::string ad = a[0];
            if (ad.size() > 2 && ad[1] == ':' && (ad[0] == 'F' || ad[0] == 'R')) { c.flat = ad[0] == 'F'; ad = ad.substr(2); }
            char *e = nullptr; unsigned long v = std::strtoul(ad.c_str(), &e, 16);
            if (ad.empty() || *e || v > 0xFFFFFFul) script_fail(n, "bad address '" + a[0] + "'");
            c.addr = (u32)v;
            c.num = 10;
            if (a.size() == 2 && !parse_num(a[1], c.num, 0.1, 600)) script_fail(n, "bad max_seconds");
        } else if (c.op == "blitregs") {
            // blitregs [label]: log the blitter registers now (after a finished blit the pointers hold their end values).
            if (a.size() > 1) script_fail(n, "blitregs takes an optional label");
            c.label = a.empty() ? c.op : a[0];
        } else if (c.op == "freeze" || c.op == "thaw" || c.op == "unprotect") {
            if (!a.empty()) script_fail(n, c.op + " takes no arguments");
        } else if (c.op == "savedisk") {
            // savedisk <path.adf>: export the disk in DF0 as an ADF (to see what the game wrote to it); unprotect: clear DF0's write protection
            if (a.size() != 1) script_fail(n, "savedisk needs exactly one <path>");
            c.path = a[0];
            check_parent_dir(n, c.path);
            if (!has_suffix(c.path, ".adf")) script_fail(n, "savedisk path must end in .adf");
        } else script_fail(n, "unknown command '" + c.op + "'");
        cmds.push_back(c);
    }
    return cmds;
}

// ---- Blitter diagnostics (blittrace, blitstop, runto, blitregs) ----------------------------------------------
static bool g_blittrace = false;
static long g_blitstop = -1;
static bool g_runto_blit = false;   // the last runto stopped at a blitstop blit (for `runto ... ifblit`)
// (vAmiga's stepInto did not stop in this driver, so there is no single-step command: to see a blit's result, stop at the
// next traced BLTSIZE write with blitstop; the game's blit routines wait for the blitter to be idle before they write it.)
// One log line with the blitter's registers as vAmiga holds them (Blitter::cacheInfo: BLTCON0/1, the four pointers, the
// masks, all four modulos, busy flag), the CPU's PC and D0, D4, D7 (BLTSIZE is write-only and the blitter keeps no copy
// that the API shows; the game's blit routines write it from one of these) and the configured accuracy level
// (Opt::BLITTER_ACCURACY: 0 = FastBlitter, 1 = fake timing with the fast path, 2 = SlowBlitter, the micro-coded one).
static std::string blit_line(VAmiga &va, const std::string &label) {
    const BlitterInfo &bi = va.agnus.blitter.getInfo();
    const CPUInfo &ci = va.cpu.getInfo();
    long acc = (long)va.get(Opt::BLITTER_ACCURACY);
    std::string l = label + " pc " + hx(ci.pc0, 6) + " con0 " + hx(bi.bltcon0, 4) + " con1 " + hx(bi.bltcon1, 4)
        + " apt " + hx(bi.bltapt, 6) + " bpt " + hx(bi.bltbpt, 6) + " cpt " + hx(bi.bltcpt, 6) + " dpt " + hx(bi.bltdpt, 6)
        + " fwm " + hx(bi.bltafwm, 4) + " lwm " + hx(bi.bltalwm, 4)
        + " amod " + std::to_string(bi.bltamod) + " bmod " + std::to_string(bi.bltbmod) + " cmod " + std::to_string(bi.bltcmod) + " dmod " + std::to_string(bi.bltdmod)
        + " d0 " + hx(ci.d[0], 8) + " d4 " + hx(ci.d[4], 8) + " d7 " + hx(ci.d[7], 8)
        + " bbusy " + std::to_string((int)bi.bbusy) + " accuracy " + std::to_string(acc) + (acc == 0 ? " fast" : acc == 1 ? " fake" : " slow");
    return l;
}

// ---- Sound diagnostics (audtrace, wav) -----------------------------------------------------------------------
static bool g_audtrace = false, g_audframe = false;
static std::vector<u32> g_audwatch;   // the watchpoints audtrace installed
static std::atomic<bool> g_wav_on{false};
static const u32 AUD_REGS[] = { 0xDFF096, 0xDFF09E,
    0xDFF0A0, 0xDFF0A2, 0xDFF0A4, 0xDFF0A6, 0xDFF0A8, 0xDFF0B0, 0xDFF0B2, 0xDFF0B4, 0xDFF0B6, 0xDFF0B8,
    0xDFF0C0, 0xDFF0C2, 0xDFF0C4, 0xDFF0C6, 0xDFF0C8, 0xDFF0D0, 0xDFF0D2, 0xDFF0D4, 0xDFF0D6, 0xDFF0D8 };
struct AudState { u16 dmacon, adkcon; u32 lc[4]; u16 len[4], per[4], vol[4]; };
static AudState g_audprev;
// The state is read straight from the components (the machine is paused whenever the driver runs): the API's getInfo() calls suspend the emulator thread and made a
// frame cost about 90 ms of wall clock.
static i64 aud_frame(VAmiga &va) { return va.agnus.agnus->pos.frame; }
static AudState aud_read(VAmiga &va) {
    AudState s{};
    Agnus &ag = *va.agnus.agnus; Paula &pa = *va.paula.paula;
    s.dmacon = ag.dmacon; s.adkcon = pa.adkcon;
    for (int i = 0; i < 4; i++) s.lc[i] = ag.audlc[i];
    s.len[0] = pa.channel0.audlenLatch; s.per[0] = pa.channel0.audperLatch; s.vol[0] = pa.channel0.audvolLatch;
    s.len[1] = pa.channel1.audlenLatch; s.per[1] = pa.channel1.audperLatch; s.vol[1] = pa.channel1.audvolLatch;
    s.len[2] = pa.channel2.audlenLatch; s.per[2] = pa.channel2.audperLatch; s.vol[2] = pa.channel2.audvolLatch;
    s.len[3] = pa.channel3.audlenLatch; s.per[3] = pa.channel3.audperLatch; s.vol[3] = pa.channel3.audvolLatch;
    return s;
}
// One line per stop (watch mode) or per frame (frame mode): the beam position, the PC after the write (watch mode only), the registers that changed since the last line
// (a register rewritten with its old value shows as "same"); then every channel's LC, LEN, PER and VOL.
static void aud_line(VAmiga &va, bool watch) {
    Agnus &ag = *va.agnus.agnus;
    AudState s = aud_read(va);
    std::string ch;
    if (s.dmacon != g_audprev.dmacon) ch += " DMACON=" + hx(s.dmacon, 4);
    if (s.adkcon != g_audprev.adkcon) ch += " ADKCON=" + hx(s.adkcon, 4);
    for (int i = 0; i < 4; i++) {
        std::string n = std::to_string(i);
        if (s.lc[i] != g_audprev.lc[i]) ch += " LC" + n + "=" + hx(s.lc[i], 6);
        if (s.len[i] != g_audprev.len[i]) ch += " LEN" + n + "=" + hx(s.len[i], 4);
        if (s.per[i] != g_audprev.per[i]) ch += " PER" + n + "=" + std::to_string(s.per[i]);
        if (s.vol[i] != g_audprev.vol[i]) ch += " VOL" + n + "=" + std::to_string(s.vol[i]);
    }
    if (ch.empty()) ch = " same";
    std::string l = "aud frame " + std::to_string((long long)ag.pos.frame) + " line " + std::to_string((long)ag.pos.v) + " hpos " + std::to_string((long)ag.pos.h)
        + " pc " + (watch ? hx(va.cpu.getInfo().pc0, 6) : std::string("-")) + " dmacon " + hx(s.dmacon, 4) + " changed" + ch + " |";
    for (int i = 0; i < 4; i++)
        l += " " + std::to_string(i) + ": lc " + hx(s.lc[i], 6) + " len " + std::to_string(s.len[i]) + " per " + std::to_string(s.per[i]) + " vol " + std::to_string(s.vol[i]) + (i < 3 ? " /" : "");
    log_line(l);
    g_audprev = s;
}
// Run to the end of the current video frame; with audtrace on, every watchpoint stop on the way is logged and the run goes on.
static void finish_frame(VAmiga &va) {
    for (;;) {
        while (g_wav_on && va.audioPort.getStats().fillLevel > 0.4) sleep_s(0.001);   // let the recorder drain: a frame adds about 880 of 4,096 samples
        i64 f0 = aud_frame(va);
        long p0 = g_pauses;
        va.finishFrame();
        for (int k = 0; k < 2000 && g_pauses == p0; k++) sleep_s(0.001);
        for (int k = 0; k < 200 && !va.isPaused(); k++) sleep_s(0.005);
        if (!va.isPaused()) throw std::runtime_error("step did not finish");
        if (!g_audtrace || aud_frame(va) != f0) { if (g_audframe) aud_line(va, false); return; }
        aud_line(va, true);
    }
}
// WAV recorder: a thread drains the audio port's stereo ring buffer (about 4,096 samples) into memory every 2 ms; stop writes a 16-bit file.
static std::thread g_wav_thread;
static std::vector<int16_t> g_wav_pcm;      // interleaved L R
static std::string g_wav_path;
static double g_wav_rate = 44100;
static int16_t to_s16(float f) { f = f > 1.f ? 1.f : (f < -1.f ? -1.f : f); return (int16_t)(f * 32767.f); }
static isize wav_avail(VAmiga &va) {
    const AudioPortMetrics &m = va.audioPort.getStats();
    const AudioPortConfig &c = va.audioPort.getConfig();
    isize n = (isize)(m.fillLevel * (double)c.bufferSize);
    return n > 8 ? n - 8 : 0;   // a few samples of margin against the level being read while the emulator writes
}
static void wav_start(VAmiga &va, const std::string &path, bool vamiga_defaults) {
    if (g_wav_on) throw std::runtime_error("a recording is already running");
    if (!vamiga_defaults) {
        // pan = 0.5 (sin(value pi / 200) + 1): 300 is all left, 100 all right
        va.set(Opt::AUD_PAN0, (i64)300); va.set(Opt::AUD_PAN1, (i64)100); va.set(Opt::AUD_PAN2, (i64)100); va.set(Opt::AUD_PAN3, (i64)300);
        va.set(Opt::AUD_FILTER_TYPE, (i64)FilterType::NONE);
    }
    g_wav_path = path; g_wav_pcm.clear(); g_wav_rate = (double)va.get(Opt::HOST_SAMPLE_RATE);
    if (g_wav_rate < 8000) g_wav_rate = 44100;
    va.set(Opt::AUD_ASR, (i64)0);   // a fixed output rate: the adaptive rate stretches time by the buffer fill level
    { float l[1024], r[1024]; for (isize n; (n = wav_avail(va)) > 0; ) va.audioPort.copyStereo(l, r, n > 1024 ? 1024 : n); }   // drop what is buffered
    g_wav_on = true;
    g_wav_thread = std::thread([&va] {
        float l[1024], r[1024];
        while (g_wav_on) {
            isize n = wav_avail(va);
            if (n > 1024) n = 1024;
            if (n > 0) n = va.audioPort.copyStereo(l, r, n);   // never more than is buffered: an underflow would wipe the ring buffer
            for (isize i = 0; i < n; i++) { g_wav_pcm.push_back(to_s16(l[i])); g_wav_pcm.push_back(to_s16(r[i])); }
            if (n <= 0) sleep_s(0.002);
        }
    });
}
static void put32(std::ofstream &f, u32 v) { for (int i = 0; i < 4; i++) f.put((char)((v >> (8 * i)) & 0xFF)); }
static void put16(std::ofstream &f, u32 v) { for (int i = 0; i < 2; i++) f.put((char)((v >> (8 * i)) & 0xFF)); }
static void wav_stop(VAmiga &va) {
    if (!g_wav_on) throw std::runtime_error("no recording is running");
    sleep_s(0.05);
    g_wav_on = false; g_wav_thread.join();
    { float l[1024], r[1024]; isize n; while ((n = wav_avail(va)) > 0) { n = va.audioPort.copyStereo(l, r, n > 1024 ? 1024 : n); for (isize i = 0; i < n; i++) { g_wav_pcm.push_back(to_s16(l[i])); g_wav_pcm.push_back(to_s16(r[i])); } } }
    std::ofstream f(g_wav_path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + g_wav_path + " for writing");
    u32 bytes = (u32)(g_wav_pcm.size() * 2), rate = (u32)(g_wav_rate + 0.5);
    f << "RIFF"; put32(f, 36 + bytes); f << "WAVEfmt "; put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
    f << "data"; put32(f, bytes);
    for (int16_t v : g_wav_pcm) put16(f, (u16)v);
    f.flush();
    if (!f.good()) throw std::runtime_error("writing " + g_wav_path + " failed");
    std::printf("wrote %s (%zu stereo samples at %u Hz)\n", g_wav_path.c_str(), g_wav_pcm.size() / 2, rate);
    log_line("# wav " + g_wav_path + " samples " + std::to_string(g_wav_pcm.size() / 2) + " rate " + std::to_string(rate));
}

static void exec_cmd(VAmiga &va, const ScriptCmd &c, std::chrono::steady_clock::time_point t0) {
    if (c.op == "wait") sleep_s(c.num);
    else if (c.op == "keydown") { va.keyboard.press(c.keys[0], 0, 0); sleep_s(0.25); }
    else if (c.op == "keyup") { va.keyboard.release(c.keys[0]); sleep_s(0.25); }
    else if (c.op == "key") { va.keyboard.press(c.keys[0], 0, c.num); sleep_s(c.num + 0.25); }
    else if (c.op == "type" || c.op == "code") { for (u8 k : c.keys) { va.keyboard.press(k, 0, 0.1); sleep_s(0.3); } }
    else if (c.op == "mouse") { va.controlPort1.mouse.setXY(c.num, c.num2); sleep_s(0.1); }
    else if (c.op == "click") {
        va.controlPort1.mouse.trigger(GamePadAction::PRESS_LEFT); sleep_s(0.15);
        va.controlPort1.mouse.trigger(GamePadAction::RELEASE_LEFT); sleep_s(0.15);
    } else if (c.op == "frame") write_frame(va, c.path);
    else if (c.op == "freeze") {
        va.pause();
        for (int i = 0; i < 50 && !va.isPaused(); i++) sleep_s(0.1);   // wait until the emulator thread has stopped
        if (!va.isPaused()) throw std::runtime_error("machine did not pause");
        sleep_s(0.1);
    }
    else if (c.op == "thaw") va.run();
    else if (c.op == "unprotect") { va.df0.setFlag(DiskFlags::PROTECTED, false); }
    else if (c.op == "savedisk") { va.df0.writeToFile(std::filesystem::path(c.path)); std::printf("wrote disk %s\n", c.path.c_str()); }
    else if (c.op == "holduntil") {
        u32 ra = c.flat ? flat_to_ram(c.addr) : c.addr;
        auto folded = [&] { long v = peek16(va, ra); return v > 14400 ? v - 28800 : v; };
        va.keyboard.press(c.keys[0], 0, 0);
        auto t1 = std::chrono::steady_clock::now();
        bool hit = false;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < c.num) {
            long f = folded();
            if (c.cmp == "gt" ? f > c.target : f < c.target) { hit = true; break; }
            sleep_s(0.005);
        }
        va.keyboard.release(c.keys[0]);
        sleep_s(0.1);
        log_line(std::string("# holduntil ") + (hit ? "reached " : "TIMEOUT at ") + std::to_string(folded()));
        if (!hit) throw std::runtime_error("holduntil timed out");
    }
    else if (c.op == "step") {
        // run the paused machine for <count> video frames (one 50 Hz vertical blank each); must follow `freeze`
        if (!va.isPaused()) throw std::runtime_error("step needs a frozen machine");
        for (long i = 0; i < (long)c.num; i++) finish_frame(va);
    }
    else if (c.op == "poke") {
        if (!va.isPaused()) throw std::runtime_error("poke needs a frozen machine");
        for (u32 i = 0; i < c.len; i++) {
            u32 ra = c.flat ? flat_to_ram(c.addr + i) : c.addr + i;
            u8 b = (u8)((c.target >> (8 * (c.len - 1 - i))) & 0xFF);
            // slow RAM (0xC00000..) and chip RAM only; the game's hunks are in slow RAM on this machine
            if (ra >= 0xC00000 && ra < 0xC80000 && va.mem.mem->slow) va.mem.mem->slow[ra - 0xC00000] = b;
            else if (ra < 0x80000 && va.mem.mem->chip) va.mem.mem->chip[ra] = b;
            else throw std::runtime_error("poke: address outside slow and chip RAM");
        }
    }
    else if (c.op == "copy") {
        if (!va.isPaused()) throw std::runtime_error("copy needs a frozen machine");
        std::vector<u8> tmp(c.len);
        for (u32 i = 0; i < c.len; i++) tmp[i] = peek8(va, c.flat ? flat_to_ram(c.addr + i) : c.addr + i);
        for (u32 i = 0; i < c.len; i++) {
            u32 ra = c.flat2 ? flat_to_ram(c.addr2 + i) : c.addr2 + i;
            if (ra >= 0xC00000 && ra < 0xC80000 && va.mem.mem->slow) va.mem.mem->slow[ra - 0xC00000] = tmp[i];
            else if (ra < 0x80000 && va.mem.mem->chip) va.mem.mem->chip[ra] = tmp[i];
            else throw std::runtime_error("copy: destination outside slow and chip RAM");
        }
    }
    else if (c.op == "pokefill") {
        if (!va.isPaused()) throw std::runtime_error("pokefill needs a frozen machine");
        if (c.cmp == "ifblit" && !g_runto_blit) return;
        for (u32 i = 0; i < c.len; i++) {
            u32 ra = c.flat ? flat_to_ram(c.addr + i) : c.addr + i;
            u8 b = c.keys[i % c.keys.size()];
            if (ra >= 0xC00000 && ra < 0xC80000 && va.mem.mem->slow) va.mem.mem->slow[ra - 0xC00000] = b;
            else if (ra < 0x80000 && va.mem.mem->chip) va.mem.mem->chip[ra] = b;
            else throw std::runtime_error("pokefill: address outside slow and chip RAM");
        }
    }
    else if (c.op == "stepuntil") {
        if (!va.isPaused()) throw std::runtime_error("stepuntil needs a frozen machine");
        u32 ra = c.flat ? flat_to_ram(c.addr) : c.addr;
        const long mask = c.len == 1 ? 0xFF : c.len == 2 ? 0xFFFF : 0xFFFFFFFFl;
        auto test = [&]() {
            long v = c.len == 1 ? (long)(int8_t)peek8(va, ra) : c.len == 2 ? (long)(int16_t)peek16(va, ra) : (long)(int32_t)peek32(va, ra);
            if (c.cmp == "and") return (v & c.target) != 0;
            if (c.cmp == "eq") return (v & mask) == (c.target & mask);
            if (c.cmp == "ne") return (v & mask) != (c.target & mask);
            if (c.cmp == "lt") return v < c.target;
            return v > c.target;
        };
        long n = 0;
        while (!test() && n < (long)c.num) {
            finish_frame(va);
            n++;
        }
        log_line(std::string("# stepuntil ") + (test() ? "reached after " : "TIMEOUT after ") + std::to_string(n) + " frames");
        if (!test()) throw std::runtime_error("stepuntil timed out");
    }
    else if (c.op == "blittrace") {
        if (!va.isPaused()) throw std::runtime_error("blittrace needs a frozen machine");
        g_blittrace = c.num != 0;
        if (g_blittrace) va.cpu.watchpoints.setAt(0xDFF058); else va.cpu.watchpoints.removeAt(0xDFF058);
        sleep_s(0.05);
    }
    else if (c.op == "blitstop") g_blitstop = c.target;
    else if (c.op == "audtrace") {
        if (!va.isPaused()) throw std::runtime_error("audtrace needs a frozen machine");
        for (u32 a : g_audwatch) va.cpu.watchpoints.removeAt(a);   // only what is installed (removing a missing watchpoint throws)
        g_audwatch.clear();
        g_audtrace = c.num != 0 && c.cmp != "frame";
        g_audframe = c.num != 0 && c.cmp == "frame";
        if (g_audtrace)
            for (u32 a : AUD_REGS) {
                if (c.cmp == "nodmacon" && a == 0xDFF096) continue;
                va.cpu.watchpoints.setAt(a); g_audwatch.push_back(a);
            }
        sleep_s(0.05);
        if (g_audtrace || g_audframe) g_audprev = aud_read(va);
        sleep_s(0.05);
    }
    else if (c.op == "wav") { if (c.num != 0) wav_start(va, c.path, c.cmp == "vamiga"); else wav_stop(va); }
    else if (c.op == "runto") {
        if (!va.isPaused()) throw std::runtime_error("runto needs a frozen machine");
        if (c.cmp == "ifblit" && !g_runto_blit) { log_line("# runto skipped (the previous runto did not stop at a blit)"); return; }
        g_runto_blit = false;
        u32 ra = c.flat ? flat_to_ram(c.addr) : c.addr;
        va.cpu.breakpoints.setAt(ra);
        sleep_s(0.05);
        auto t1 = std::chrono::steady_clock::now();
        bool hit = false;
        for (;;) {
            long p0 = g_pauses;
            va.run();
            while (g_pauses == p0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() < c.num) sleep_s(0.0005);
            if (g_pauses == p0) { va.pause(); for (int i = 0; i < 50 && g_pauses == p0; i++) sleep_s(0.1); break; }
            for (int i = 0; i < 100 && !va.isPaused(); i++) sleep_s(0.001);
            u32 pc = va.cpu.getInfo().pc0;
            if (pc == ra) { hit = true; break; }
            if (g_audtrace) aud_line(va, true);
            if (g_blittrace && !g_audtrace) {
                const BlitterInfo &bi = va.agnus.blitter.getInfo();
                log_line(blit_line(va, "blit"));
                if (g_blitstop >= 0 && bi.bltcon0 == (u16)g_blitstop) { log_line("# runto stopped after the BLTSIZE write of BLTCON0 " + hx(bi.bltcon0, 4)); g_runto_blit = true; va.cpu.breakpoints.removeAt(ra); sleep_s(0.05); return; }
            }
        }
        va.cpu.breakpoints.removeAt(ra);
        sleep_s(0.05);
        log_line(std::string("# runto ") + (hit ? "reached " : c.cmp == "soft" ? "timeout (soft) before " : "TIMEOUT before ") + hx(ra));
        if (!hit && c.cmp != "soft") throw std::runtime_error("runto timed out");
    }
    else if (c.op == "blitregs") log_line(blit_line(va, c.label));
    else if (c.op == "log") log_line("# " + c.label);
    else if (c.op == "seglist") do_seglist(va, c.path, c.path2);
    else if (c.op == "peek") {
        // Spy reads have no side effects; put freeze/thaw around a group of peeks for one consistent instant.
        std::string line = c.label + " " + (c.flat ? "F:" : "R:") + hx(c.addr);
        for (u32 i = 0; i < c.len; i++) {
            u32 ra = c.flat ? flat_to_ram(c.addr + i) : c.addr + i;
            line += (i % 32 == 0 ? " " : "") + hx(peek8(va, ra), 2);
        }
        log_line(line);
    } else if (c.op == "colors") {
        // OCS colour registers COLOR00..COLOR31 as held by Denise (12-bit RGB); not readable over the bus
        const DeniseInfo &di = va.denise.getInfo();
        std::string line = c.label;
        for (int i = 0; i < 32; i++) line += " " + hx(di.colorReg[i] & 0xFFF, 3);
        log_line(line);
    }
    else if (c.op == "snapshot") { va.amiga.saveSnapshot(std::filesystem::path(c.path)); std::printf("wrote snapshot %s\n", c.path.c_str()); }
    else if (c.op == "restore") {
        if (!std::filesystem::is_regular_file(c.path)) throw std::runtime_error("no such snapshot: " + c.path);
        // Loading in the first moments after power-on fails the snapshot hash check (the
        // emulator thread has not settled yet); make sure the machine has run for 3 s.
        double up = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (up < 3.0) sleep_s(3.0 - up);
        va.amiga.loadSnapshot(std::filesystem::path(c.path));
        sleep_s(0.5);
    }
}

static void run_script(VAmiga &va, const std::vector<ScriptCmd> &cmds) {
    const auto t0 = std::chrono::steady_clock::now();
    for (const ScriptCmd &c : cmds) {
        if (verbose) std::fprintf(stderr, "script line %d: %s\n", c.line, c.op.c_str());
        try { exec_cmd(va, c, t0); }
        catch (std::exception &e) { script_fail(c.line, c.op + ": " + e.what()); }
        catch (...) { script_fail(c.line, c.op + ": unknown failure"); }
    }
}

int main(int argc, char **argv) {
    std::string rom, ext, adf, script, out = "frame.ppm";
    int seconds = 30;
    bool warp = false, parse_only = false, disk_rw = false;
    std::string logpath;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        if (k == "--warp") { warp = true; continue; }
        if (k == "-v") { verbose = true; continue; }
        if (k == "--parse-only") { parse_only = true; continue; }
        if (k == "--rw") { disk_rw = true; continue; }
        if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", k.c_str()); return 2; }
        std::string v = argv[++i];
        if (k == "--rom") rom = v; else if (k == "--ext") ext = v; else if (k == "--adf") adf = v;
        else if (k == "--seconds") {
            size_t used = 0;
            try { seconds = std::stoi(v, &used); } catch (...) { used = 0; }
            if (used != v.size() || seconds < 1) { std::fprintf(stderr, "--seconds needs an integer >= 1, got '%s'\n", v.c_str()); return 2; }
        } else if (k == "--out") out = v;
        else if (k == "--script") script = v;
        else if (k == "--log") logpath = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    if (rom.empty()) { std::fprintf(stderr, "usage: ref --rom R [--ext E] [--adf D] [--seconds N] [--warp] [--rw] [--script FILE] [--log FILE] [--parse-only] --out F\n"); return 2; }

    for (const std::string *p : { &rom, &ext, &adf, &script })
        if (!p->empty() && !std::filesystem::is_regular_file(*p)) {
            std::fprintf(stderr, "error: no such file: %s\n", p->c_str());
            return 1;
        }

    std::vector<ScriptCmd> cmds;
    if (!script.empty()) cmds = parse_script(script);   // fail fast on script errors, before booting
    if (parse_only) { std::printf("%zu commands parsed\n", cmds.size()); return 0; }
    if (!logpath.empty()) {
        g_log.open(logpath, std::ios::app);
        if (!g_log) { std::fprintf(stderr, "error: cannot open log %s\n", logpath.c_str()); return 1; }
    }

    try {
        VAmiga va;
        va.launch(&g_pauses, on_message);   // a non-null listener: vAmiga only calls the callback when one is set (otherwise it queues)
        va.set(ConfigScheme::A500_OCS_1MB);
        { auto b = slurp(rom); va.mem.loadRom(b.data(), (isize)b.size()); }
        if (!ext.empty()) { auto b = slurp(ext); va.mem.loadExt(b.data(), (isize)b.size()); }
        if (warp) va.set(Opt::AMIGA_WARP_MODE, (i64)Warp::ALWAYS);
        if (!adf.empty()) va.df0.insert(std::filesystem::path(adf), !disk_rw);
        va.isReady();
        va.powerOn();
        va.run();
        if (verbose) std::fprintf(stderr, "powered=%d running=%d\n", (int)va.isPoweredOn(), (int)va.isRunning());
        if (!script.empty()) { run_script(va, cmds); return 0; }
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        if (verbose) std::fprintf(stderr, "running=%d\n", (int)va.isRunning());
        va.pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (verbose) {
            va.retroShell.setStream(std::cout);
            va.retroShell.execScript(std::string("debugger\nstate\ncpu\n"));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto &ci = va.cpu.getInfo();
            std::fprintf(stderr, "rom@f80002=%04x execbase=%04x%04x cpuclock=%ld\n",
                         va.mem.debugger.spypeek16(Accessor::CPU, 0xf80002),
                         va.mem.debugger.spypeek16(Accessor::CPU, 4), va.mem.debugger.spypeek16(Accessor::CPU, 6), (long)va.amiga.getInfo().cpuClock);
            for (u32 a : {0xdff002u, 0xdff01cu, 0xdff01eu, 0xdff180u, 0xdff182u, 0xdff100u, 0xdff00au})
                std::fprintf(stderr, "reg %06x = %04x\n", a, va.mem.debugger.spypeek16(Accessor::CPU, a));
            std::fprintf(stderr, "cpu pc=%08x sr=%04x halt=%d\n", ci.pc0, ci.sr, (int)ci.halt);
        }

        write_frame(va, out);
    } catch (std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "error: unknown failure (ROM or disk rejected?)\n");
        return 1;
    }
    return 0;
}
