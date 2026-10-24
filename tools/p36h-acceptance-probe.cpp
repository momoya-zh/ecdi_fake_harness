// Phase 36 批三 · harness 侧库外验收探针（REQ-10 收口）
// 依据 D:\Project\ECDI\.workbuddy\spike\p36\DELIVERY.md 固定指纹复跑：
//   A2 = 五族墨迹比（方向判据 >1，预期 1.29–1.79x）——GDI 链沿用 REQ-10 取证仪器（raw lfWeight），
//        GL 链走生产代码 FontEngine::Glyph（GLRenderer 实际栅格化路径）。
//   A3 = 两链同 face 串宽，只在真字面格判（SimSun@700 GDI 合成格豁免，C36-4）。
//   四手段矩阵 = Font::weight / family+style / 复合族名 / ZH 族名 四路由 x 两链，
//        GL 侧 = 生产 FaceIdFor 身份矩阵（T36-4/T36-8 的库外复现），GDI 侧 = 映射器 tmWeight 读回。
//   A6 = 指纹（版本宏 + DPI + 字体文件清单）随读数落盘。
// 仪器纪律（REQ-10 教训）：墨迹只在链内比较；跨链绝对墨迹不可比。
#include "Platform/Win32/Win32FontSource.h"
#include "Render/FontEngine.h"
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif
#include <ECDI/Core/version.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

using namespace ECDI;

// ---------- UTF-8 helpers ----------
static std::wstring U82W(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}
static std::string W2U8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::u32string U832(const char* s) {
    std::u32string out;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        char32_t cp = 0;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p++ & 0x1F; cp = (cp << 6) | (*p++ & 0x3F); }
        else if ((*p & 0xF0) == 0xE0) { cp = *p++ & 0x0F; cp = (cp << 6) | (*p++ & 0x3F); cp = (cp << 6) | (*p++ & 0x3F); }
        else { cp = *p++ & 0x07; cp = (cp << 6) | (*p++ & 0x3F); cp = (cp << 6) | (*p++ & 0x3F); cp = (cp << 6) | (*p++ & 0x3F); }
        out.push_back(cp);
    }
    return out;
}

// ---------- A6 fingerprint ----------
static void PrintFingerprint(const Win32FontSource& src) {
    HDC dc = GetDC(nullptr);
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    std::printf("[A6 fingerprint]\n");
    std::printf("  ECDI_VERSION      = %s\n", ECDI_VERSION);
    std::printf("  LOGPIXELSX/Y      = %d/%d\n", dpiX, dpiY);
    const char* fams[] = { "Microsoft YaHei", "Microsoft YaHei UI", "Segoe UI",
                           "Consolas", "SimSun", "DengXian", "" };
    std::printf("  font file inventory (ResolveFileCandidates[0], ANSI paths per contract):\n");
    for (const char* f : fams) {
        auto cands = src.ResolveFileCandidates(f);
        if (cands.empty()) { std::printf("    %-20s -> (no candidate)\n", *f ? f : "(default)"); continue; }
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        char info[128] = "";
        if (GetFileAttributesExA(cands[0].filePath.c_str(), GetFileExInfoStandard, &fa)) {
            ULARGE_INTEGER sz; sz.HighPart = fa.nFileSizeHigh; sz.LowPart = fa.nFileSizeLow;
            FILETIME ft = fa.ftLastWriteTime; SYSTEMTIME st{};
            FileTimeToSystemTime(&ft, &st);
            std::snprintf(info, sizeof info, "%10llu bytes  %04u-%02u-%02u",
                          static_cast<unsigned long long>(sz.QuadPart),
                          st.wYear, st.wMonth, st.wDay);
        }
        std::printf("    %-20s -> %s (hint=%d direct=%d)  %s\n",
                    *f ? f : "(default)", cands[0].filePath.c_str(),
                    cands[0].faceIndexHint, cands[0].directFile ? 1 : 0, info);
        if (cands.size() > 1)
            std::printf("    %-20s    +%zu more candidates\n", "", cands.size() - 1);
    }
    std::printf("\n");
}

// ---------- GL chain: production resolution matrix ----------
static void PrintGlMatrix(FontEngine& eng) {
    std::printf("[GL chain / production FaceIdFor identity matrix]\n");
    struct R { const char* family; std::uint16_t weight; };
    const R cells[] = {
        {"", 400},
        {"Microsoft YaHei", 400}, {"Microsoft YaHei", 700},
        {"Microsoft YaHei UI", 400}, {"Microsoft YaHei UI", 700},
        {"微软雅黑", 400}, {"微软雅黑 Bold", 400},
        {"Microsoft YaHei Bold", 400}, {"Microsoft YaHei Light", 400},
        {"Segoe UI", 300}, {"Segoe UI", 400}, {"Segoe UI", 600}, {"Segoe UI", 700},
        {"Consolas", 400}, {"Consolas", 700}, {"consola.ttf", 400},
        {"SimSun", 400}, {"SimSun", 700}, {"NSimSun", 400},
    };
    const size_t nCells = sizeof(cells) / sizeof(cells[0]);
    std::vector<int> ids;
    for (size_t i = 0; i < nCells; ++i) ids.push_back(eng.FaceIdFor(cells[i].family, cells[i].weight));
    for (size_t i = 0; i < nCells; ++i)
        std::printf("  FaceIdFor(\"%s\", %u) = %d\n", cells[i].family, cells[i].weight, ids[i]);

    auto idOf = [&](const char* f, std::uint16_t w) {
        for (size_t i = 0; i < nCells; ++i)
            if (std::string(cells[i].family) == f && cells[i].weight == w) return ids[i];
        return -1;
    };
    struct A { const char* desc; bool ok; };
    const A asserts[] = {
        {"A1 YaHeiUI != YaHei @400 (T36-4)",            idOf("Microsoft YaHei UI",400) != idOf("Microsoft YaHei",400)},
        {"A2 YaHeiUI != YaHei @700 (T36-4 同构)",        idOf("Microsoft YaHei UI",700) != idOf("Microsoft YaHei",700)},
        {"A3 YaHei @700 != @400 (bold face picked)",    idOf("Microsoft YaHei",700) != idOf("Microsoft YaHei",400)},
        {"A4 YaHeiUI @700 != @400",                     idOf("Microsoft YaHei UI",700) != idOf("Microsoft YaHei UI",400)},
        {"B1 ZH == default face (C36-2 回退)",           idOf("微软雅黑",400) == idOf("",400)},
        {"B2 ZH+style == default face (C36-2)",         idOf("微软雅黑 Bold",400) == idOf("",400)},
        {"C1 Segoe 600 == 700 (Semibold 滤除, T36-8)",  idOf("Segoe UI",600) == idOf("Segoe UI",700)},
        {"C2 Segoe 600/700 != 400 (T36-8)",             idOf("Segoe UI",600) != idOf("Segoe UI",400)},
        {"C3 Segoe 300 == 400 (Light 滤除, T36-8)",     idOf("Segoe UI",300) == idOf("Segoe UI",400)},
        {"D1 Consolas 700 != 400",                      idOf("Consolas",700) != idOf("Consolas",400)},
        {"E1 consola.ttf == Consolas (E36-1 直查豁免)", idOf("consola.ttf",400) == idOf("Consolas",400)},
        {"F1 YaHeiLight != YaHei (独立族直达, T36-8)",  idOf("Microsoft YaHei Light",400) != idOf("Microsoft YaHei",400)},
        {"G1 NSimSun != SimSun (T36-4)",                idOf("NSimSun",400) != idOf("SimSun",400)},
        {"H1 SimSun @700 == @400 (无 bold face, 不跨族)", idOf("SimSun",700) == idOf("SimSun",400)},
    };
    int pass = 0;
    for (const auto& a : asserts) { std::printf("  [%s] %s\n", a.ok ? "PASS" : "FAIL", a.desc); if (a.ok) ++pass; }
    std::printf("  => %d/%d assertions pass\n\n", pass, int(sizeof asserts / sizeof asserts[0]));
}

// ---------- GDI chain: mapper matrix (p36a instrument) ----------
static void GdiProbe(const char* familyUtf8, int reqWeight, const wchar_t* text,
                     int* tmWeightOut, std::string* faceOut, LONG* widthOut, int px = 14) {
    LOGFONTW lf{};
    lf.lfHeight = -px; lf.lfCharSet = DEFAULT_CHARSET; lf.lfWeight = reqWeight;
    const std::wstring w = U82W(familyUtf8);
    if (!w.empty()) wcsncpy(lf.lfFaceName, w.c_str(), LF_FACESIZE - 1);
    HFONT f = CreateFontIndirectW(&lf);
    HDC hdc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, f);
    TEXTMETRICW tm{}; GetTextMetricsW(hdc, &tm);
    wchar_t face[LF_FACESIZE]{}; GetTextFaceW(hdc, LF_FACESIZE, face);
    SIZE sz{}; GetTextExtentPoint32W(hdc, text, int(wcslen(text)), &sz);
    SelectObject(hdc, old); DeleteObject(f); ReleaseDC(nullptr, hdc);
    if (tmWeightOut) *tmWeightOut = tm.tmWeight;
    if (faceOut) *faceOut = W2U8(face);
    if (widthOut) *widthOut = sz.cx;
}

static void PrintGdiMatrix() {
    std::printf("[GDI chain / mapper matrix] (lfHeight=-14, one variable = family/lfWeight)\n");
    std::printf("  %-24s %5s -> %8s %-24s %s\n", "request family", "req", "tmWeight", "selected face", "route verdict");
    struct R { const char* family; int weight; const char* expect; };
    const R cells[] = {
        {"Microsoft YaHei",      400, "base"},
        {"Microsoft YaHei",      700, "weight route: expect 700 (real face)"},
        {"Microsoft YaHei UI",   700, "weight route: expect 700"},
        {"Segoe UI",             700, "weight route: expect 700"},
        {"Consolas",             700, "weight route: expect 700"},
        {"SimSun",               700, "no bold face: expect SYNTHESIS (tmWeight=700, same face)"},
        {"Microsoft YaHei Bold", 400, "style-embedded: GDI en face"},
        {"Microsoft YaHei UI Bold", 400, "compound+style: GDI en face"},
        {"微软雅黑 Bold",          400, "ZH+style: GDI en face (C36-2 registered asymmetry)"},
    };
    const wchar_t* kText = L"\u6574\u7406 API h1";
    for (const auto& c : cells) {
        int tm = 0; std::string face; LONG wd = 0;
        GdiProbe(c.family, c.weight, kText, &tm, &face, &wd);
        const char* note = "";
        if (std::string(c.expect).find("SYNTHESIS") != std::string::npos)
            note = (tm == 700) ? "[synthetic-bold cell -> A3 EXEMPT (C36-4)]" : "[unexpected]";
        std::printf("  %-24s %5d -> %8d %-24s %s %s\n", c.family, c.weight, tm, face.c_str(), c.expect, note);
    }
    std::printf("\n");
}

// ---------- A2 ink: five families ----------
struct InkCase { const char* family; const char* text8; int px; const char* label; };
static const InkCase kInk[] = {
    {"Microsoft YaHei",    "\u6574\u7406 API \u9519\u8bef\u5904\u7406", 14, "YaHei CJK 14px"},
    {"Microsoft YaHei",    "harness",                                   15, "YaHei lat 15px"},
    {"Microsoft YaHei UI", "\u6574\u7406 API \u9519\u8bef\u5904\u7406", 14, "YaHeiUI CJK 14px"},
    {"DengXian",           "\u6574\u7406 API \u9519\u8bef\u5904\u7406", 14, "DengXian CJK 14px"},
    {"Segoe UI",           "harness",                                   15, "Segoe lat 15px"},
};

static int GdiInk(const char* familyUtf8, int weight, const wchar_t* text, int px) {
    LOGFONTW lf{};
    lf.lfHeight = -px; lf.lfCharSet = DEFAULT_CHARSET; lf.lfWeight = weight;
    const std::wstring w = U82W(familyUtf8);
    if (!w.empty()) wcsncpy(lf.lfFaceName, w.c_str(), LF_FACESIZE - 1);
    HFONT hf = CreateFontIndirectW(&lf);
    if (!hf) return -1;
    const int W = 900, H = 80;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT rc{0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rc, bg); DeleteObject(bg);
    HGDIOBJ oldFont = SelectObject(dc, hf);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0, 0, 0));
    TextOutW(dc, 4, 4, text, int(wcslen(text)));
    int ink = 0;
    const auto* p = static_cast<const std::uint8_t*>(bits);
    for (int i = 0; i < W * H; ++i) {
        const int b = p[i*4+0], g = p[i*4+1], r = p[i*4+2];
        if ((r*299 + g*587 + b*114) / 1000 < 128) ++ink;
    }
    SelectObject(dc, oldFont); SelectObject(dc, oldBmp);
    DeleteObject(bmp); DeleteObject(hf); DeleteDC(dc); ReleaseDC(nullptr, screen);
    return ink;
}

// GL-chain ink through production code: FontEngine::Glyph == GLRenderer's rasterization path.
static int GlInk(FontEngine& eng, const char* familyUtf8, std::uint16_t weight,
                 const char* text8, int px) {
    Font f; f.size = float(px); f.family = familyUtf8; f.weight = weight;
    const std::u32string cps = U832(text8);
    int ink = 0; bool any = false;
    for (char32_t cp : cps) {
        GlyphBitmap gb;
        if (!eng.Glyph(f, cp, gb)) continue;
        any = true;
        for (std::uint8_t v : gb.a8) if (v > 127) ++ink;
    }
    return any ? ink : -1;
}

static void PrintInkTable(FontEngine& eng) {
    std::printf("[A2 five-family ink ratio, in-chain only (direction criterion >1, expect 1.29-1.79x)]\n");
    std::printf("  %-18s | GDI raw lfWeight 400->700 | GL Font::weight 400->700 (production Glyph)\n", "case");
    std::printf("  -----------------+---------------------------+----------------------------------\n");
    for (const auto& c : kInk) {
        const std::wstring wtext = U82W(c.text8);
        const int g4 = GdiInk(c.family, FW_NORMAL, wtext.c_str(), c.px);
        const int g7 = GdiInk(c.family, FW_BOLD,   wtext.c_str(), c.px);
        const int l4 = GlInk(eng, c.family, 400, c.text8, c.px);
        const int l7 = GlInk(eng, c.family, 700, c.text8, c.px);
        char gr[16] = "n/a", lr[16] = "n/a";
        if (g4 > 0 && g7 > 0) std::snprintf(gr, sizeof gr, "%.2fx", double(g7) / g4);
        if (l4 > 0 && l7 > 0) std::snprintf(lr, sizeof lr, "%.2fx", double(l7) / l4);
        const bool gDir = g4 > 0 && g7 > g4;
        const bool lDir = l4 > 0 && l7 > l4;
        std::printf("  %-18s | %5d -> %5d  %s %s | %5d -> %5d  %s %s\n",
                    c.label, g4, g7, gr, gDir ? "OK" : "FAIL",
                    l4, l7, lr, lDir ? "OK" : "FAIL");
    }
    std::printf("  (GDI = raw lfWeight instrument, same as REQ-10 baseline readings;\n");
    std::printf("   GL  = Font{family,weight} -> FontEngine::Glyph, the exact GLRenderer rasterization path)\n\n");
}

// ---------- A3 width: true-face cells only ----------
static void PrintWidthTable(FontEngine& eng) {
    std::printf("[A3 cross-chain string width, TRUE-FACE cells only (SimSun@700 GDI synthesis exempt, C36-4)]\n");
    std::printf("  %-22s %5s | %6s %6s | %6s %5s | %s\n", "cell", "wt", "GDIcx", "GLwpx", "delta", "ratio", "verdict");
    // Per-cell text: only glyphs the face itself covers (no font-linking confound).
    // CJK-bearing faces get the CJK string; latin-only faces (Consolas / Segoe UI) get a latin
    // string -- a mixed string against a latin-only face measures GDI font-LINKING vs GL no-fallback
    // (N6, registered), not face fidelity. Instrument lesson, same family as the REQ-10 cross-chain ink trap.
    struct W { const char* family; std::uint16_t weight; int px; bool trueFace; const char* text8; };
    const W cells[] = {
        {"Consolas",         400, 15, true,  "harness API 123"},
        {"Consolas",         700, 15, true,  "harness API 123"},
        {"Microsoft YaHei",  400, 14, true,  "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},
        {"Microsoft YaHei",  700, 14, true,  "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},
        {"Microsoft YaHei UI", 400, 14, true,  "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},
        {"Microsoft YaHei UI", 700, 14, true,  "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},
        {"Segoe UI",         400, 15, true,  "harness API 123"},
        {"Segoe UI",         700, 15, true,  "harness API 123"},
        {"SimSun",           400, 14, true,  "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},
        {"SimSun",           700, 14, false, "\u6574\u7406 API \u9519\u8bef\u5904\u7406 h1"},   // GDI synthesizes: exempt cell
    };
    int pass = 0, judged = 0;
    for (const auto& c : cells) {
        const std::wstring wtext = U82W(c.text8);
        LONG cx = 0;
        GdiProbe(c.family, c.weight, wtext.c_str(), nullptr, nullptr, &cx, c.px);
        Font f; f.size = float(c.px); f.family = c.family; f.weight = c.weight;
        const Size sz = eng.MeasureText(f, c.text8);   // DIP; SetDpi(96) => px 1:1
        const int glw = int(sz.width);
        const int delta = glw - int(cx);
        char verdict[64];
        if (!c.trueFace) {
            std::snprintf(verdict, sizeof verdict, "EXEMPT (C36-4 synthesis cell)");
        } else {
            ++judged;
            const bool ok = delta >= -2 && delta <= 2;
            if (ok) ++pass;
            std::snprintf(verdict, sizeof verdict, "%s (criterion |delta|<=2px, REQ-09 band)", ok ? "PASS" : "FAIL");
        }
        std::printf("  %-22s %5u | %6ld %6d | %+6d %5.2fx | %s\n",
                    c.family, c.weight, cx, glw, delta,
                    cx > 0 ? double(glw) / cx : 0.0, verdict);
    }
    std::printf("  => %d/%d true-face cells within band\n\n", pass, judged);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    Win32FontSource src;
    PrintFingerprint(src);

    FontEngine eng;
    eng.SetFontSource(std::make_unique<Win32FontSource>());
    eng.SetDpi(96);   // A6 pin: DIP == px, measurement and GDI px height share the formula

    PrintGlMatrix(eng);
    PrintGdiMatrix();
    PrintInkTable(eng);
    PrintWidthTable(eng);
    return 0;
}
