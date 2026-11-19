#include "launchpad.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "aliases.h"
#include "fuzzy.h"
#include "launchpad_apps.h"
#include "paint.h"
#include "theme.h"
#include "window_list.h"
#include "../res/resource.h"

// The launchpad, laid out like the Windows 11 Start menu it stands in for:
// the header band with the search box, then the pinned apps as a grid under
// "Pinned". Typing replaces the grid with the best matches from the pinned
// apps and every installed one, which is where an app gets pinned from.

namespace {

constexpr wchar_t kClass[] = L"MonopaneLaunchpad";

// The pinned grid: eight columns of 80px. The width, the header band and the
// side and bottom spacing are the switcher's; see theme.h.
constexpr int COLS = 8;

// Base (96-dpi) metrics.
constexpr int BASE_GRID_LEFT = BASE_HIGHLIGHT_INSET - BASE_HIGHLIGHT_GAP;  // panel edge to the first tile
constexpr int BASE_TILE_W = (BASE_PANEL_WIDTH - 2 * BASE_GRID_LEFT) / COLS;
constexpr int BASE_TILE_H = 84;
constexpr int BASE_ICON = 32;          // a pinned app's icon
constexpr int BASE_LABEL_GAP = 8;      // the icon to its name
constexpr int BASE_LABEL_H = 20;
constexpr int BASE_EDIT_BUTTON_H = 24;
constexpr int BASE_HEADING_TOP = 12;   // the header band to the heading and Edit button
constexpr int BASE_HEADING_GAP = 8;    // the heading line to the grid
constexpr int BASE_GRID_TOP = BASE_HEADING_TOP + BASE_EDIT_BUTTON_H + BASE_HEADING_GAP;
constexpr int BASE_INSTALLED_ICON = 24;  // the installed apps' icons, for the picker
constexpr int BASE_BADGE = 18;         // the remove badge on a tile, in edit mode

static_assert(BASE_TILE_W * COLS + 2 * BASE_GRID_LEFT == BASE_PANEL_WIDTH,
              "the grid fills the panel exactly");
constexpr int MAX_RESULTS = COLS * 4;

constexpr UINT IDC_SEARCH = 100;
constexpr UINT IDC_RENAME = 101;
constexpr UINT IDM_ADD_INSTALLED = 1;
constexpr UINT IDM_ADD_FILE = 2;
constexpr UINT IDM_PIN = 3;
constexpr UINT IDM_UNPIN = 4;
constexpr UINT IDM_RENAME = 5;

// The installed apps, enumerated off the UI thread, arrive as this message.
constexpr UINT WM_APP_ALLAPPS = WM_APP + 40;

// Segoe Fluent Icons glyphs (the same code points in Segoe MDL2 Assets).
constexpr wchar_t kGlyphSearch[] = L"\xE721";
constexpr wchar_t kGlyphPencil[] = L"\xE70F";
constexpr wchar_t kGlyphPlus[] = L"\xE710";
constexpr wchar_t kGlyphCancel[] = L"\xE711";

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
HWND g_edit = nullptr;
HWND g_rename = nullptr;
WNDPROC g_editBase = nullptr;
WNDPROC g_renameBase = nullptr;

UINT g_dpi = 96;
HFONT g_fontName = nullptr;      // a tile's label
HFONT g_fontRow = nullptr;       // the grid's hints: nothing pinned, no matches
HFONT g_fontHeading = nullptr;   // "Pinned", "Best matches"
HFONT g_fontEdit = nullptr;
HFONT g_fontGlyph = nullptr;
HFONT g_fontGlyphSmall = nullptr;
HFONT g_fontGlyphBig = nullptr;
HBRUSH g_brushSearch = nullptr;
int g_editLineH = 0;   // one line of g_fontEdit, for centring the search box
COLORREF g_accent = CLR_ACCENT_FALLBACK;

std::vector<LaunchpadApp> g_apps;  // pinned, in grid order
int g_iconPx = 0;                  // the size g_apps' icons were resolved at
std::vector<int> g_filtered;       // with no search: indices into g_apps, in order
std::wstring g_query;

// Every installed app, from the shell's Applications folder, for searching
// and the picker. Replaced whole when a fresh enumeration differs.
std::vector<InstalledApp> g_all;
int g_allPx = 0;
std::atomic<bool> g_allLoading{ false };

// Installed apps resolved as launchpad entries on demand — to show one among
// the search results at grid size, or to open one — and kept.
std::vector<LaunchpadApp> g_resolved;

// While searching, the grid holds these: a pinned app, or an installed one.
struct Result {
    int pinned = -1;  // into g_apps
    int all = -1;     // into g_all
};
std::vector<Result> g_results;

// Content coordinates run down the body from its top, unscrolled.
int g_gridTop = 0;
int g_contentH = 0;
int g_scrollY = 0;

int g_selected = 0;  // a tile
bool g_visible = false;
bool g_editMode = false;
bool g_modal = false;  // a dialog or menu of ours is up; losing focus is expected
DWORD g_shownTick = 0;

// Edit mode: dragging a tile, and renaming one.
bool g_mouseDown = false;
bool g_dragging = false;
int g_dragIndex = -1;           // into g_apps
POINT g_downPt{};
POINT g_dragOffset{};           // from the tile's corner to where it was grabbed
int g_renaming = -1;            // into g_apps

// Where the cursor was when a mouse move was last acted on, in screen
// coordinates. The panel changes under a stationary cursor as a search
// filters it, and Windows reports that as a mouse move; acting on it would
// drag the selection off what was typed. See the switcher.
POINT g_lastMouseScreen{};

int Scale(int value)
{
    return MulDiv(value, g_dpi, 96);
}

bool MouseActuallyMoved(POINT client)
{
    POINT screen = client;
    ClientToScreen(g_hwnd, &screen);
    if (screen.x == g_lastMouseScreen.x && screen.y == g_lastMouseScreen.y)
        return false;
    g_lastMouseScreen = screen;
    return true;
}

void CreateFonts()
{
    for (HFONT* f : { &g_fontName, &g_fontRow, &g_fontHeading, &g_fontEdit, &g_fontGlyph,
                      &g_fontGlyphSmall, &g_fontGlyphBig })
        if (*f) DeleteObject(*f);

    static const bool variable = FontInstalled(L"Segoe UI Variable Text");
    static const bool fluent = FontInstalled(L"Segoe Fluent Icons");

    LOGFONTW lf{};
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, variable ? L"Segoe UI Variable Text" : L"Segoe UI");
    lf.lfHeight = -Scale(12);
    g_fontName = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(14);
    g_fontRow = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    g_fontHeading = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -Scale(15);
    g_fontEdit = CreateFontIndirectW(&lf);

    wcscpy_s(lf.lfFaceName, fluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets");
    lf.lfHeight = -Scale(16);
    g_fontGlyph = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(12);
    g_fontGlyphSmall = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(20);
    g_fontGlyphBig = CreateFontIndirectW(&lf);

    // A single-line edit control draws its text at the top of its client area,
    // not down the middle of it, so the only way to have the text sit centred
    // in the search box is to make the control exactly one line tall and
    // centre that. Which means knowing how tall a line is.
    if (HDC dc = GetDC(g_hwnd)) {
        HGDIOBJ old = SelectObject(dc, g_fontEdit);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        g_editLineH = tm.tmHeight;
        SelectObject(dc, old);
        ReleaseDC(g_hwnd, dc);
    }

    if (g_edit)
        SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontEdit), TRUE);
    if (g_rename)
        SendMessageW(g_rename, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontName), TRUE);
}

std::wstring GetSearchText()
{
    wchar_t buffer[256];
    GetWindowTextW(g_edit, buffer, 256);
    return buffer;
}

std::wstring Trim(const std::wstring& s)
{
    const size_t b = s.find_first_not_of(L" \t");
    if (b == std::wstring::npos)
        return L"";
    const size_t e = s.find_last_not_of(L" \t");
    return s.substr(b, e - b + 1);
}

bool SameTarget(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

int PinnedIndexOf(const std::wstring& target)
{
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i)
        if (SameTarget(g_apps[i].target, target))
            return i;
    return -1;
}

// An installed app as a launchpad entry, resolved at grid size the first time
// it is asked for.
const LaunchpadApp& ResolvedApp(const InstalledApp& installed)
{
    for (const LaunchpadApp& app : g_resolved)
        if (SameTarget(app.target, installed.target))
            return app;
    LaunchpadApp app;
    app.target = installed.target;
    ResolveLaunchpadApp(app, g_iconPx);
    if (app.displayName.empty())
        app.displayName = installed.name;
    g_resolved.push_back(std::move(app));
    return g_resolved.back();
}

void ClearResolved()
{
    FreeLaunchpadIcons(g_resolved);
    g_resolved.clear();
}

// ---------------------------------------------------------------------------
// The installed apps, enumerated off the UI thread

struct AllAppsBatch {
    std::vector<InstalledApp> apps;
    int px = 0;
};

void FreeInstalled(std::vector<InstalledApp>& apps)
{
    for (InstalledApp& app : apps)
        if (app.icon) DeleteObject(app.icon);
    apps.clear();
}

DWORD WINAPI AllAppsThread(void* param)
{
    const int px = static_cast<int>(reinterpret_cast<INT_PTR>(param));
    // The shell's folders want COM, on a thread of their own.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto* batch = new AllAppsBatch{ EnumerateInstalledApps(px), px };
    if (SUCCEEDED(com))
        CoUninitialize();
    g_allLoading = false;
    if (!g_hwnd || !PostMessageW(g_hwnd, WM_APP_ALLAPPS, 0, reinterpret_cast<LPARAM>(batch))) {
        FreeInstalled(batch->apps);
        delete batch;
    }
    return 0;
}

// Walks the Applications folder again in the background. Enumerating it and
// pulling every icon takes long enough that the panel must never wait for it:
// what search finds is the last one that came back.
void RefreshAllApps()
{
    if (g_allLoading.exchange(true))
        return;
    const INT_PTR px = Scale(BASE_INSTALLED_ICON);
    if (HANDLE thread = CreateThread(nullptr, 0, AllAppsThread, reinterpret_cast<void*>(px), 0,
                                     nullptr))
        CloseHandle(thread);
    else
        g_allLoading = false;
}

// For the picker, which cannot show an empty list: the enumeration done here
// and now if none has come back yet.
void EnsureAllApps()
{
    const int px = Scale(BASE_INSTALLED_ICON);
    if (!g_all.empty() && g_allPx == px)
        return;
    HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    FreeInstalled(g_all);
    g_all = EnumerateInstalledApps(px);
    g_allPx = px;
    SetCursor(previous);
}

bool SameApps(const std::vector<InstalledApp>& a, const std::vector<InstalledApp>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || !SameTarget(a[i].target, b[i].target))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Geometry. Content coordinates run down the body from its top; the body
// starts under the header band and scrolls.

int PanelWidth()
{
    return Scale(BASE_PANEL_WIDTH);
}

int BodyTop()
{
    return Scale(BASE_HEADER_H);
}

int BodyHeight()
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    return std::max(0, static_cast<int>(rc.bottom) - BodyTop());
}

int ToClientY(int contentY)
{
    return BodyTop() + contentY - g_scrollY;
}

int TileWidth()
{
    return Scale(BASE_TILE_W);
}

int GridLeft()
{
    return Scale(BASE_GRID_LEFT);
}

// Where the heading and the hint text start: the tiles are centred blocks
// with no left edge of their own to line up with, so the heading lines up
// with the first tile's highlight instead, on the 16px line.
int ContentLeft()
{
    return Scale(BASE_HIGHLIGHT_INSET);
}

bool Searching()
{
    return !g_query.empty();
}

bool PlusTileShown()
{
    return g_editMode && !Searching();
}

int TileCount()
{
    if (Searching())
        return static_cast<int>(g_results.size());
    return static_cast<int>(g_filtered.size()) + (PlusTileShown() ? 1 : 0);
}

bool IsPlusTile(int tile)
{
    return PlusTileShown() && tile == static_cast<int>(g_filtered.size());
}

// The app a tile shows, or null for the "+" tile.
const LaunchpadApp* TileApp(int tile)
{
    if (tile < 0 || tile >= TileCount() || IsPlusTile(tile))
        return nullptr;
    if (!Searching())
        return &g_apps[g_filtered[tile]];
    const Result& r = g_results[tile];
    return r.pinned >= 0 ? &g_apps[r.pinned] : &ResolvedApp(g_all[r.all]);
}

// The pinned entry a tile is, or -1 (the "+" tile, or an unpinned result).
int TilePinnedIndex(int tile)
{
    if (tile < 0 || tile >= TileCount() || IsPlusTile(tile))
        return -1;
    return Searching() ? g_results[tile].pinned : g_filtered[tile];
}

int GridRows()
{
    return std::max(1, (TileCount() + COLS - 1) / COLS);
}

RECT TileRect(int tile)
{
    const int tileW = TileWidth();
    const int tileH = Scale(BASE_TILE_H);
    const int x = GridLeft() + (tile % COLS) * tileW;
    const int y = ToClientY(g_gridTop + (tile / COLS) * tileH);
    return RECT{ x, y, x + tileW, y + tileH };
}

// The icon and the name under it, as one block centred in the tile, so the
// space above the icon matches the space below the name.
RECT IconRectIn(const RECT& tile)
{
    const int icon = Scale(BASE_ICON);
    const int block = icon + Scale(BASE_LABEL_GAP) + Scale(BASE_LABEL_H);
    const int left = tile.left + (tile.right - tile.left - icon) / 2;
    const int top = tile.top + (tile.bottom - tile.top - block) / 2;
    return RECT{ left, top, left + icon, top + icon };
}

RECT LabelRectIn(const RECT& tile)
{
    const RECT icon = IconRectIn(tile);
    const int top = icon.bottom + Scale(BASE_LABEL_GAP);
    const int side = Scale(BASE_HIGHLIGHT_GAP) + Scale(4);
    return RECT{ tile.left + side, top, tile.right - side, top + Scale(BASE_LABEL_H) };
}

RECT BadgeRectIn(const RECT& tile)
{
    const RECT icon = IconRectIn(tile);
    const int size = Scale(BASE_BADGE);
    const int cx = icon.left - Scale(4);
    const int cy = icon.top - Scale(4);
    return RECT{ cx - size / 2, cy - size / 2, cx - size / 2 + size, cy - size / 2 + size };
}

// The highlight behind a tile: inset as a switcher row's is, so neighbours'
// highlights stand 4px apart and the outer ones meet the 16px line.
RECT TileHighlight(const RECT& tile)
{
    RECT rc = tile;
    InflateRect(&rc, -Scale(BASE_HIGHLIGHT_GAP), -Scale(BASE_HIGHLIGHT_GAP));
    return rc;
}

// The heading's line at the top of the body: as tall as the Edit button, which
// shares it, with the heading text centred on it.
RECT HeadingRect()
{
    const int y = ToClientY(Scale(BASE_HEADING_TOP));
    return RECT{ ContentLeft(), y, PanelWidth() - ContentLeft(), y + Scale(BASE_EDIT_BUTTON_H) };
}

// The Edit / Done button at the right of the "Pinned" heading, its right edge
// on the last tile's highlight edge.
RECT EditButtonRect()
{
    const RECT heading = HeadingRect();
    const int w = Scale(g_editMode ? 72 : 68);
    return RECT{ heading.right - w, heading.top, heading.right, heading.bottom };
}

RECT SearchPlateRect()
{
    const int inset = Scale(BASE_CONTENT_INSET);
    const int boxH = Scale(BASE_SEARCH_BOX_H);
    const int top = (Scale(BASE_HEADER_H) - boxH) / 2;
    return RECT{ inset, top, PanelWidth() - inset, top + boxH };
}

int SearchTextLeft()
{
    return SearchPlateRect().left + Scale(40);
}

bool InBody(POINT pt)
{
    return pt.y >= BodyTop();
}

int TileFromPoint(POINT pt)
{
    if (!InBody(pt) || pt.x < GridLeft())
        return -1;
    const int contentY = pt.y - BodyTop() + g_scrollY - g_gridTop;
    if (contentY < 0)
        return -1;
    const int col = (pt.x - GridLeft()) / TileWidth();
    const int row = contentY / Scale(BASE_TILE_H);
    if (col >= COLS || row >= GridRows())
        return -1;
    const int tile = row * COLS + col;
    return tile < TileCount() ? tile : -1;
}

// Where a dragged tile would land: the nearest slot, however far off the grid
// the cursor has strayed.
int DragSlotFromPoint(POINT pt)
{
    const int col = std::clamp(static_cast<int>(pt.x - GridLeft()) / TileWidth(), 0, COLS - 1);
    const int contentY = pt.y - BodyTop() + g_scrollY - g_gridTop;
    const int row = std::clamp(contentY / Scale(BASE_TILE_H), 0, GridRows() - 1);
    const int last = static_cast<int>(g_apps.size()) - 1;
    return std::clamp(row * COLS + col, 0, last);
}

// ---------------------------------------------------------------------------
// Filtering & layout

void ApplyFilter(bool resetSelection)
{
    g_query = Trim(GetSearchText());
    g_filtered.clear();
    g_results.clear();

    if (!Searching()) {
        for (int i = 0; i < static_cast<int>(g_apps.size()); ++i)
            g_filtered.push_back(i);
    } else {
        // Pinned and installed apps together, best first; an installed app
        // that is also pinned counts once, as the pinned one.
        const std::wstring aliasTarget = LookupAlias(g_query);
        constexpr int kAliasBoost = 1 << 20;
        struct Scored {
            int score;
            Result result;
        };
        std::vector<Scored> scored;
        for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
            int score = FuzzyScore(g_query, g_apps[i].Name());
            if (!aliasTarget.empty() && !g_apps[i].exePath.empty() &&
                SameTarget(g_apps[i].exePath, aliasTarget))
                score = (score < 0 ? 0 : score) + kAliasBoost;
            if (score >= 0)
                scored.push_back({ score, Result{ i, -1 } });
        }
        for (int i = 0; i < static_cast<int>(g_all.size()); ++i) {
            if (PinnedIndexOf(g_all[i].target) >= 0)
                continue;
            const int score = FuzzyScore(g_query, g_all[i].name);
            if (score >= 0)
                scored.push_back({ score, Result{ -1, i } });
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const Scored& a, const Scored& b) { return a.score > b.score; });
        for (const Scored& s : scored) {
            if (static_cast<int>(g_results.size()) >= MAX_RESULTS)
                break;
            g_results.push_back(s.result);
        }
    }

    if (resetSelection) {
        g_selected = 0;
        g_scrollY = 0;
    } else {
        g_selected = std::clamp(g_selected, 0, std::max(0, TileCount() - 1));
    }
}

// The heading, then the grid under it.
void BuildContent()
{
    g_gridTop = Scale(BASE_GRID_TOP);
    g_contentH = g_gridTop + GridRows() * Scale(BASE_TILE_H) + Scale(BASE_BOTTOM_PAD);
}

void ClampScroll()
{
    g_scrollY = std::clamp(g_scrollY, 0, std::max(0, g_contentH - BodyHeight()));
}

void PositionRenameEdit()
{
    if (g_renaming < 0)
        return;
    for (int tile = 0; tile < static_cast<int>(g_filtered.size()); ++tile) {
        if (g_filtered[tile] != g_renaming)
            continue;
        const RECT label = LabelRectIn(TileRect(tile));
        const bool shown = label.top >= BodyTop();
        MoveWindow(g_rename, label.left, label.top, label.right - label.left,
                   label.bottom - label.top, TRUE);
        ShowWindow(g_rename, shown ? SW_SHOW : SW_HIDE);
        return;
    }
}

// The content span of a tile's row, for scrolling it into view.
void TileContentSpan(int tile, int& top, int& bottom)
{
    top = g_gridTop + (tile / COLS) * Scale(BASE_TILE_H);
    bottom = top + Scale(BASE_TILE_H);
    // The first row brings its heading with it.
    if (tile < COLS)
        top = 0;
}

void EnsureSelectionVisible()
{
    int top = 0, bottom = 0;
    TileContentSpan(g_selected, top, bottom);
    const int bodyH = BodyHeight();
    if (top < g_scrollY)
        g_scrollY = top;
    else if (bottom > g_scrollY + bodyH)
        g_scrollY = bottom - bodyH + Scale(BASE_BOTTOM_PAD);
    ClampScroll();
}

void Layout(bool reposition)
{
    const int width = PanelWidth();

    HMONITOR monitor;
    if (reposition) {
        POINT cursor;
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    } else {
        monitor = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    const RECT& work = mi.rcWork;

    // The search box sits on the middle of the work area, the same line the
    // switcher puts its own on, and the grid hangs below it. The top is fixed
    // whatever the grid is doing, so the box holds still as a search narrows
    // it, and when the switcher opens instead. The panel is as tall as the
    // grid, up to what fits above the screen's bottom; past that it scrolls.
    BuildContent();
    const int top = PanelTop(work, SearchPlateRect());
    const int headerH = Scale(BASE_HEADER_H);
    const int roomH = static_cast<int>(work.bottom) - top - Scale(BASE_SCREEN_MARGIN) - headerH;
    const int bodyH = std::max(Scale(BASE_TILE_H), std::min(g_contentH, roomH));
    const int x = work.left + (work.right - work.left - width) / 2;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, top, width, headerH + bodyH, SWP_NOACTIVATE);
    ClampScroll();

    // One line tall, centred in the pill: see CreateFonts. It stops short of
    // the pill's rounded right end.
    const RECT plate = SearchPlateRect();
    const int editH = g_editLineH + Scale(2);
    const int editLeft = SearchTextLeft();
    MoveWindow(g_edit, editLeft, plate.top + ((plate.bottom - plate.top) - editH) / 2,
               (plate.right - Scale(18)) - editLeft, editH, TRUE);
    PositionRenameEdit();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void Select(int tile)
{
    const int count = TileCount();
    if (count == 0)
        return;
    g_selected = std::clamp(tile, 0, count - 1);
    EnsureSelectionVisible();
    PositionRenameEdit();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// The arrows walk the grid as a grid: Left and Right along the order, Up and
// Down a column at a time, Down into the short last row landing on its end.
void MoveSelection(int dx, int dy)
{
    const int tiles = TileCount();
    int s = g_selected;
    if (dx) {
        s += dx;
    } else if (dy < 0) {
        if (s - COLS >= 0)
            s -= COLS;
    } else if (dy > 0) {
        if (s + COLS < tiles)
            s += COLS;
        else if ((s / COLS) < (tiles - 1) / COLS)
            s = tiles - 1;
    }
    Select(s);
}

// ---------------------------------------------------------------------------
// Editing

void SelectAppTile(int appIndex)
{
    for (int tile = 0; tile < static_cast<int>(g_filtered.size()); ++tile) {
        if (g_filtered[tile] == appIndex) {
            g_selected = tile;
            break;
        }
    }
    EnsureSelectionVisible();
}

void CommitRename()
{
    if (g_renaming < 0)
        return;
    const int appIndex = g_renaming;
    g_renaming = -1;  // first, so the focus change below cannot re-enter

    wchar_t buffer[256];
    GetWindowTextW(g_rename, buffer, 256);
    const std::wstring text = Trim(buffer);
    if (appIndex < static_cast<int>(g_apps.size())) {
        LaunchpadApp& app = g_apps[appIndex];
        // Typing the app's own name back means "no custom name".
        app.customName = (text.empty() || text == app.displayName) ? L"" : text;
        SaveLaunchpad(g_apps);
    }
    ShowWindow(g_rename, SW_HIDE);
    ApplyFilter(false);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    if (g_visible)
        SetFocus(g_edit);
}

void CancelRename()
{
    if (g_renaming < 0)
        return;
    g_renaming = -1;
    ShowWindow(g_rename, SW_HIDE);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    if (g_visible)
        SetFocus(g_edit);
}

void BeginRename(int tile)
{
    if (Searching() || tile < 0 || tile >= static_cast<int>(g_filtered.size()))
        return;
    CommitRename();
    g_selected = tile;
    EnsureSelectionVisible();
    g_renaming = g_filtered[tile];
    SetWindowTextW(g_rename, g_apps[g_renaming].Name().c_str());
    PositionRenameEdit();
    SetFocus(g_rename);
    SendMessageW(g_rename, EM_SETSEL, 0, -1);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void EndDrag(bool save)
{
    if (!g_mouseDown && !g_dragging)
        return;
    const bool moved = g_dragging;
    g_mouseDown = false;
    g_dragging = false;
    g_dragIndex = -1;
    if (GetCapture() == g_hwnd)
        ReleaseCapture();
    if (moved && save)
        SaveLaunchpad(g_apps);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void SetEditMode(bool on)
{
    if (g_editMode == on)
        return;
    CommitRename();
    EndDrag(true);
    g_editMode = on;
    // A search would leave the grid in match order, where dragging means
    // nothing, so edit mode starts from the full list.
    SetWindowTextW(g_edit, L"");
    ApplyFilter(false);
    Layout(false);
}

void Unpin(int appIndex)
{
    if (appIndex < 0 || appIndex >= static_cast<int>(g_apps.size()))
        return;
    CommitRename();
    if (g_apps[appIndex].icon)
        DeleteObject(g_apps[appIndex].icon);
    g_apps.erase(g_apps.begin() + appIndex);
    SaveLaunchpad(g_apps);
    ApplyFilter(false);
    Layout(false);
}

void RemoveTile(int tile)
{
    const int appIndex = TilePinnedIndex(tile);
    if (appIndex < 0)
        return;
    Unpin(appIndex);
    g_selected = std::clamp(tile, 0, std::max(0, TileCount() - 1));
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void AddTarget(const std::wstring& target)
{
    if (target.empty())
        return;
    const int existing = PinnedIndexOf(target);
    if (existing >= 0) {
        if (!Searching())
            SelectAppTile(existing);  // already there; just show where
        Layout(false);
        return;
    }
    LaunchpadApp app;
    app.target = target;
    ResolveLaunchpadApp(app, g_iconPx);
    g_apps.push_back(std::move(app));
    SaveLaunchpad(g_apps);
    ApplyFilter(false);
    if (!Searching())
        SelectAppTile(static_cast<int>(g_apps.size()) - 1);
    Layout(false);
}

// Brackets a dialog or menu of ours: the launchpad must not take losing focus
// to it as a dismissal, and gets the focus back afterwards.
struct ModalScope {
    ModalScope() { g_modal = true; }
    ~ModalScope()
    {
        g_modal = false;
        if (g_visible) {
            ForceForeground(g_hwnd);
            SetFocus(g_edit);
        }
    }
};

bool BrowseForFile(std::wstring& pathOut)
{
    ModalScope modal;
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Programs and shortcuts (*.exe;*.lnk)\0*.exe;*.lnk\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Add to the launchpad";
    // NODEREFERENCELINKS keeps a picked shortcut as the shortcut, whose name
    // and icon are usually the better ones.
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NODEREFERENCELINKS |
                OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn))
        return false;
    pathOut = path;
    return true;
}

// ------------------------------------------------------------ the picker

struct PickerState {
    std::vector<int> imageIndex;   // per g_all entry, or -1
    HIMAGELIST images = nullptr;
    HWND list = nullptr;
    HWND search = nullptr;
    WNDPROC searchBase = nullptr;
    std::wstring chosen;
};

PickerState* g_picker = nullptr;

void FillPicker(PickerState& st)
{
    wchar_t buffer[256];
    GetWindowTextW(st.search, buffer, 256);
    const std::wstring query = buffer;

    std::vector<int> shown;
    if (query.empty()) {
        for (int i = 0; i < static_cast<int>(g_all.size()); ++i)
            shown.push_back(i);
    } else {
        std::vector<std::pair<int, int>> scored;
        for (int i = 0; i < static_cast<int>(g_all.size()); ++i) {
            const int score = FuzzyScore(query, g_all[i].name);
            if (score >= 0)
                scored.emplace_back(score, i);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [score, index] : scored)
            shown.push_back(index);
    }

    SendMessageW(st.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(st.list);
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(g_all[shown[i]].name.c_str());
        item.iImage = st.imageIndex[shown[i]];
        item.lParam = shown[i];
        ListView_InsertItem(st.list, &item);
    }
    SendMessageW(st.list, WM_SETREDRAW, TRUE, 0);
    if (!shown.empty()) {
        ListView_SetItemState(st.list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(st.list, 0, FALSE);
    }
}

void PickerMoveSelection(PickerState& st, int delta)
{
    const int count = ListView_GetItemCount(st.list);
    if (count == 0)
        return;
    int current = ListView_GetNextItem(st.list, -1, LVNI_SELECTED);
    if (current < 0)
        current = 0;
    const int next = std::clamp(current + delta, 0, count - 1);
    ListView_SetItemState(st.list, next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(st.list, next, FALSE);
}

// Up/Down in the search box move the list, so the keyboard never has to
// leave the box.
LRESULT CALLBACK PickerSearchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_picker && msg == WM_KEYDOWN) {
        if (wParam == VK_DOWN) { PickerMoveSelection(*g_picker, 1); return 0; }
        if (wParam == VK_UP) { PickerMoveSelection(*g_picker, -1); return 0; }
        if (wParam == VK_NEXT) { PickerMoveSelection(*g_picker, 10); return 0; }
        if (wParam == VK_PRIOR) { PickerMoveSelection(*g_picker, -10); return 0; }
    }
    return CallWindowProcW(g_picker ? g_picker->searchBase : DefWindowProcW, hwnd, msg, wParam,
                           lParam);
}

INT_PTR CALLBACK PickerDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PickerState* st = g_picker;
    switch (msg) {
    case WM_INITDIALOG: {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(
            LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
        st->list = GetDlgItem(dlg, IDC_PICK_LIST);
        st->search = GetDlgItem(dlg, IDC_PICK_SEARCH);
        st->searchBase = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            st->search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PickerSearchProc)));
        SendMessageW(st->search, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Search installed apps…"));

        // No ILC_MASK: these bitmaps carry their own alpha.
        st->images = ImageList_Create(g_allPx, g_allPx, ILC_COLOR32, 64, 32);
        st->imageIndex.assign(g_all.size(), -1);
        for (size_t i = 0; i < g_all.size(); ++i) {
            if (g_all[i].icon)
                st->imageIndex[i] = ImageList_Add(st->images, g_all[i].icon, nullptr);
        }
        ListView_SetImageList(st->list, st->images, LVSIL_SMALL);
        ListView_SetExtendedListViewStyle(st->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        RECT client;
        GetClientRect(st->list, &client);
        LVCOLUMNW column{};
        column.mask = LVCF_WIDTH;
        column.cx = client.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
        ListView_InsertColumn(st->list, 0, &column);

        FillPicker(*st);
        SetFocus(st->search);
        return FALSE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PICK_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            FillPicker(*st);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) {
            const int selected = ListView_GetNextItem(st->list, -1, LVNI_SELECTED);
            if (selected < 0)
                return TRUE;
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = selected;
            ListView_GetItem(st->list, &item);
            st->chosen = g_all[static_cast<size_t>(item.lParam)].target;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (hdr->idFrom == IDC_PICK_LIST && hdr->code == NM_DBLCLK) {
            PostMessageW(dlg, WM_COMMAND, IDOK, 0);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
        ListView_SetImageList(st->list, nullptr, LVSIL_SMALL);
        if (st->images)
            ImageList_Destroy(st->images);
        st->images = nullptr;
        return FALSE;
    }
    return FALSE;
}

bool PickInstalledApp(std::wstring& targetOut)
{
    ModalScope modal;
    EnsureAllApps();

    PickerState st;
    g_picker = &st;
    const INT_PTR result = DialogBoxParamW(g_inst, MAKEINTRESOURCEW(IDD_PICKER), g_hwnd,
                                           PickerDlgProc, 0);
    g_picker = nullptr;
    if (result != IDOK || st.chosen.empty())
        return false;
    targetOut = st.chosen;
    return true;
}

void ShowAddMenu(POINT screenPt)
{
    int cmd = 0;
    {
        ModalScope modal;
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_ADD_INSTALLED, L"Installed app…");
        AppendMenuW(menu, MF_STRING, IDM_ADD_FILE, L"Browse for a file…");
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_NONOTIFY, screenPt.x,
                             screenPt.y, 0, g_hwnd, nullptr);
        DestroyMenu(menu);
    }

    std::wstring target;
    if (cmd == IDM_ADD_INSTALLED && PickInstalledApp(target))
        AddTarget(target);
    else if (cmd == IDM_ADD_FILE && BrowseForFile(target))
        AddTarget(target);
}

// ---------------------------------------------------------------------------
// Opening apps

void Open(const LaunchpadApp& appRef, bool forceNew)
{
    const LaunchpadApp app = appRef;  // a copy: the lists may reload
    if (Searching() && !app.exePath.empty())
        SaveAlias(g_query, app.exePath);

    HideLaunchpad();

    // An app that is already running comes forward instead of starting over,
    // matched by the exe behind its window or the AppUserModelID it declares
    // (which is how Store apps, hosted by ApplicationFrameHost, are found).
    if (!forceNew && (!app.exePath.empty() || !app.aumid.empty())) {
        for (const WindowInfo& win : EnumerateAltTabWindows()) {
            const bool byExe = !app.exePath.empty() && !win.exePath.empty() &&
                               SameTarget(win.exePath, app.exePath);
            const bool byId = !app.aumid.empty() && !win.aumid.empty() &&
                              SameTarget(win.aumid, app.aumid);
            if (byExe || byId) {
                ActivateWindow(win.hwnd);
                return;
            }
        }
    }
    LaunchLaunchpadApp(app);
}

void Activate(int tile, bool forceNew)
{
    if (tile < 0 || tile >= TileCount())
        return;
    if (IsPlusTile(tile)) {
        const RECT rc = TileRect(tile);
        POINT pt{ rc.left + TileWidth() / 2, IconRectIn(rc).bottom };
        ClientToScreen(g_hwnd, &pt);
        ShowAddMenu(pt);
        return;
    }
    if (const LaunchpadApp* app = TileApp(tile))
        Open(*app, forceNew);
}

// Right-click: pin a search result, unpin or rename a pinned app.
void ShowItemMenu(int tile, POINT screenPt)
{
    if (tile < 0 || tile >= TileCount() || IsPlusTile(tile))
        return;
    const int item = tile;

    const int pinned = TilePinnedIndex(tile);
    const std::wstring target = pinned < 0 ? g_all[g_results[tile].all].target : L"";

    int cmd = 0;
    {
        ModalScope modal;
        HMENU menu = CreatePopupMenu();
        if (pinned >= 0) {
            AppendMenuW(menu, MF_STRING, IDM_UNPIN, L"Unpin from launchpad");
            if (item < TileCount() && !Searching())
                AppendMenuW(menu, MF_STRING, IDM_RENAME, L"Rename");
        } else {
            AppendMenuW(menu, MF_STRING, IDM_PIN, L"Pin to launchpad");
        }
        cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screenPt.x,
                             screenPt.y, 0, g_hwnd, nullptr);
        DestroyMenu(menu);
    }

    if (cmd == IDM_PIN)
        AddTarget(target);
    else if (cmd == IDM_UNPIN)
        Unpin(pinned);
    else if (cmd == IDM_RENAME)
        BeginRename(item);
}

// ---------------------------------------------------------------------------
// Painting

void DrawIcon(HDC dc, HBITMAP bitmap, const RECT& box)
{
    if (!bitmap)
        return;
    BITMAP bm{};
    GetObjectW(bitmap, sizeof(bm), &bm);
    const int w = bm.bmWidth;
    const int h = bm.bmHeight;
    const int x = box.left + (box.right - box.left - w) / 2;
    const int y = box.top + (box.bottom - box.top - h) / 2;

    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, bitmap);
    const BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, w, h, src, 0, 0, w, h, blend);
    SelectObject(src, old);
    DeleteDC(src);
}

void DrawGlyph(HDC dc, const wchar_t* glyph, const RECT& rc, HFONT font, COLORREF color)
{
    SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT r = rc;
    DrawTextW(dc, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// The round remove badge on a tile in edit mode.
void DrawBadge(HDC dc, const RECT& rc, COLORREF back)
{
    OutlineRoundRectAA(dc, rc, (rc.right - rc.left) / 2, RGB(62, 62, 62), RGB(90, 90, 90), back);
    DrawGlyph(dc, kGlyphCancel, rc, g_fontGlyphSmall, CLR_TEXT);
}

// One app tile, at `rc`, however it is being shown: in its slot, or floating
// under the cursor while dragged.
void DrawAppTile(HDC dc, const RECT& rc, const LaunchpadApp& app, bool badge, bool label,
                 COLORREF back)
{
    DrawIcon(dc, app.icon, IconRectIn(rc));
    if (label) {
        RECT text = LabelRectIn(rc);
        SelectObject(dc, g_fontName);
        SetTextColor(dc, CLR_TEXT);
        DrawTextW(dc, app.Name().c_str(), -1, &text,
                  DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (badge)
        DrawBadge(dc, BadgeRectIn(rc), back);
}

void DrawHeading(HDC dc, const RECT& rc, const wchar_t* text)
{
    SelectObject(dc, g_fontHeading);
    SetTextColor(dc, CLR_TEXT);
    RECT r = rc;
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// The pinned grid's Edit button: a quiet plate with a pencil, or, while
// editing, Done in the accent, the way a primary button looks.
void DrawEditButton(HDC dc)
{
    const RECT rc = EditButtonRect();
    const COLORREF fill = g_editMode ? g_accent : CLR_ROW_SELECTED;
    const COLORREF text = g_editMode ? RGB(0, 0, 0) : CLR_TEXT;
    FillRoundRectAA(dc, rc, Scale(BASE_ROW_RADIUS), fill, CLR_BG);
    if (g_editMode) {
        SelectObject(dc, g_fontName);
        SetTextColor(dc, text);
        RECT r = rc;
        DrawTextW(dc, L"Done", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    const RECT glyph{ rc.left + Scale(10), rc.top, rc.left + Scale(26), rc.bottom };
    DrawGlyph(dc, kGlyphPencil, glyph, g_fontGlyphSmall, text);
    SelectObject(dc, g_fontName);
    SetTextColor(dc, text);
    RECT label{ glyph.right + Scale(6), rc.top, rc.right - Scale(8), rc.bottom };
    DrawTextW(dc, L"Edit", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawGrid(HDC mem, int width)
{
    const RECT body{ 0, BodyTop(), width, BodyTop() + BodyHeight() };
    const int tiles = TileCount();

    if (tiles == 0) {
        RECT row{ ContentLeft(), ToClientY(g_gridTop), width - ContentLeft(),
                  ToClientY(g_gridTop) + Scale(BASE_TILE_H) };
        SelectObject(mem, g_fontRow);
        SetTextColor(mem, CLR_TEXT_DIM);
        const wchar_t* text = Searching()
            ? L"No matching apps"
            : L"Nothing pinned yet. Search for an app and right-click it to pin it, or press Edit.";
        DrawTextW(mem, text, -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    for (int tile = 0; tile < tiles; ++tile) {
        const RECT rc = TileRect(tile);
        RECT clip;
        if (!IntersectRect(&clip, &rc, &body))
            continue;
        const bool selected = tile == g_selected && !g_dragging;
        if (selected)
            FillRoundRectAA(mem, TileHighlight(rc), Scale(BASE_ROW_RADIUS), CLR_ROW_SELECTED, CLR_BG);
        const COLORREF back = selected ? CLR_ROW_SELECTED : CLR_BG;

        if (IsPlusTile(tile)) {
            RECT plate = IconRectIn(rc);
            InflateRect(&plate, Scale(4), Scale(4));
            OutlineRoundRectAA(mem, plate, Scale(BASE_ROW_RADIUS), back, RGB(90, 90, 90), back);
            DrawGlyph(mem, kGlyphPlus, plate, g_fontGlyph, CLR_TEXT_DIM);
            RECT text = LabelRectIn(rc);
            SelectObject(mem, g_fontName);
            SetTextColor(mem, CLR_TEXT_DIM);
            DrawTextW(mem, L"Add", -1, &text, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            continue;
        }

        const int pinned = TilePinnedIndex(tile);
        if (g_dragging && pinned >= 0 && pinned == g_dragIndex) {
            // Its slot, waiting for it.
            RECT slot = IconRectIn(rc);
            InflateRect(&slot, Scale(8), Scale(8));
            FillRoundRectAA(mem, slot, Scale(BASE_ROW_RADIUS), CLR_ROW_SELECTED, CLR_BG);
            continue;
        }
        if (const LaunchpadApp* app = TileApp(tile))
            DrawAppTile(mem, rc, *app, g_editMode && !Searching(),
                        pinned < 0 || pinned != g_renaming, back);
    }
}

void Paint(HDC hdc, const RECT& client)
{
    const int width = client.right;
    const int height = client.bottom;
    const int headerH = BodyTop();

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    // The header band, the hairline under it, and the body: the switcher's.
    const auto fill = [mem](const RECT& rc, COLORREF colour) {
        HBRUSH brush = CreateSolidBrush(colour);
        FillRect(mem, &rc, brush);
        DeleteObject(brush);
    };
    fill(RECT{ 0, 0, width, headerH - 1 }, CLR_HEADER_BG);
    fill(RECT{ 0, headerH - 1, width, headerH }, CLR_DIVIDER);
    fill(RECT{ 0, headerH, width, height }, CLR_BG);

    // The search box: a pill with a 1px outline and the magnifier in the
    // accent, the Start menu's own.
    const RECT plate = SearchPlateRect();
    OutlineRoundRectAA(mem, plate, (plate.bottom - plate.top) / 2, CLR_SEARCH_BG,
                       CLR_SEARCH_BORDER, CLR_HEADER_BG);
    SetBkMode(mem, TRANSPARENT);
    const RECT glyph{ plate.left + Scale(10), plate.top, SearchTextLeft() - Scale(6), plate.bottom };
    DrawGlyph(mem, kGlyphSearch, glyph, g_fontGlyph, g_accent);

    // The body, clipped so nothing scrolls up over the header.
    const int saved = SaveDC(mem);
    IntersectClipRect(mem, 0, headerH, width, height);

    DrawHeading(mem, HeadingRect(), Searching() ? L"Best matches" : L"Pinned");
    if (!Searching())
        DrawEditButton(mem);
    DrawGrid(mem, width);

    if (g_dragging && g_dragIndex >= 0 && g_dragIndex < static_cast<int>(g_apps.size())) {
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(g_hwnd, &cursor);
        RECT rc{ cursor.x - g_dragOffset.x, cursor.y - g_dragOffset.y, 0, 0 };
        rc.right = rc.left + TileWidth();
        rc.bottom = rc.top + Scale(BASE_TILE_H);
        DrawAppTile(mem, rc, g_apps[g_dragIndex], false, true, CLR_BG);
    }
    RestoreDC(mem, saved);

    // A thin scroll thumb at the right edge, as Windows 11 shows one at rest.
    const int bodyH = height - headerH;
    if (g_contentH > bodyH && bodyH > 0) {
        const int trackTop = headerH + Scale(4);
        const int trackH = bodyH - Scale(8);
        const int thumbH = std::max(Scale(24), trackH * bodyH / g_contentH);
        const int range = std::max(1, g_contentH - bodyH);
        const int thumbTop = trackTop + (trackH - thumbH) * g_scrollY / range;
        const RECT thumb{ width - Scale(7), thumbTop, width - Scale(4), thumbTop + thumbH };
        FillRoundRectAA(mem, thumb, Scale(1), RGB(120, 120, 120), CLR_BG);
    }

    BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

// ---------------------------------------------------------------------------
// Input

bool ShiftHeld()
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

// Navigation keys, from either the search box or the window itself.
bool HandleKey(WPARAM vk)
{
    switch (vk) {
    case VK_LEFT:
        MoveSelection(-1, 0);
        return true;
    case VK_RIGHT:
        MoveSelection(1, 0);
        return true;
    case VK_UP:
        MoveSelection(0, -1);
        return true;
    case VK_DOWN:
        MoveSelection(0, 1);
        return true;
    case VK_TAB:
        Select(ShiftHeld() ? g_selected - 1 : g_selected + 1);
        return true;
    case VK_PRIOR:
    case VK_NEXT: {
        g_scrollY += (vk == VK_NEXT ? 1 : -1) * (BodyHeight() - Scale(BASE_TILE_H));
        ClampScroll();
        // Take the selection along to whatever is now at the top.
        POINT pt{ ContentLeft() + Scale(4), BodyTop() };
        int item = -1;
        for (int y = pt.y; item < 0 && y < BodyTop() + BodyHeight(); y += Scale(8))
            item = TileFromPoint(POINT{ pt.x, y });
        if (item >= 0)
            g_selected = item;
        PositionRenameEdit();
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return true;
    }
    case VK_HOME:
    case VK_END:
        // Only once there is nothing in the search box for them to move in.
        if (Searching())
            return false;
        Select(vk == VK_HOME ? 0 : TileCount() - 1);
        return true;
    case VK_RETURN:
        Activate(g_selected, ShiftHeld());
        return true;
    case VK_ESCAPE:
        if (g_editMode)
            SetEditMode(false);
        else if (Searching())
            SetWindowTextW(g_edit, L"");
        else
            HideLaunchpad();
        return true;
    case VK_DELETE:
        if (g_editMode && !Searching() && g_selected < TileCount() && !IsPlusTile(g_selected)) {
            RemoveTile(g_selected);
            return true;
        }
        return false;
    case VK_F2:
        if (!Searching() && g_selected < TileCount() && !IsPlusTile(g_selected)) {
            BeginRename(g_selected);
            return true;
        }
        return false;
    case VK_APPS: {
        int top = 0, bottom = 0;
        TileContentSpan(g_selected, top, bottom);
        POINT pt{ ContentLeft(), ToClientY(bottom) };
        ClientToScreen(g_hwnd, &pt);
        ShowItemMenu(g_selected, pt);
        return true;
    }
    default:
        return false;
    }
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (HandleKey(wParam))
            return 0;
        break;
    case WM_CHAR:
        if (wParam == L'\r' || wParam == L'\t' || wParam == 27)
            return 0;
        break;
    case WM_PAINT: {
        // The placeholder is drawn here rather than set as a cue banner: an
        // edit control draws its cue banner in the system's grey text colour,
        // which on this dark fill is all but invisible. As the switcher does.
        const LRESULT result = CallWindowProcW(g_editBase, hwnd, msg, wParam, lParam);
        if (GetWindowTextLengthW(hwnd) == 0) {
            HDC dc = GetDC(hwnd);
            HideCaret(hwnd);
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.left += LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0)) + 1;
            HGDIOBJ oldFont = SelectObject(dc, g_fontEdit);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, CLR_TEXT_HINT);
            DrawTextW(dc, L"Search for apps", -1, &rc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, oldFont);
            ShowCaret(hwnd);
            ReleaseDC(hwnd, dc);
        }
        return result;
    }
    }
    return CallWindowProcW(g_editBase, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK RenameEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            CommitRename();
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            CancelRename();
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == L'\r' || wParam == 27)
            return 0;
        break;
    case WM_KILLFOCUS:
        CommitRename();
        break;
    }
    return CallWindowProcW(g_renameBase, hwnd, msg, wParam, lParam);
}

void OnMouseDown(POINT pt)
{
    if (!Searching()) {
        const RECT button = EditButtonRect();
        if (InBody(pt) && PtInRect(&button, pt)) {
            SetEditMode(!g_editMode);
            return;
        }
    }

    const int tile = TileFromPoint(pt);
    if (tile < 0) {
        if (g_renaming >= 0)
            CommitRename();
        return;
    }

    if (!g_editMode || Searching()) {
        g_selected = tile;
        Activate(tile, ShiftHeld());
        return;
    }

    if (g_renaming >= 0)
        CommitRename();
    g_selected = tile;
    InvalidateRect(g_hwnd, nullptr, TRUE);

    if (IsPlusTile(tile)) {
        POINT screen = pt;
        ClientToScreen(g_hwnd, &screen);
        ShowAddMenu(screen);
        return;
    }
    const RECT rc = TileRect(tile);
    const RECT badge = BadgeRectIn(rc);
    if (PtInRect(&badge, pt)) {
        RemoveTile(tile);
        return;
    }
    const RECT label = LabelRectIn(rc);
    if (PtInRect(&label, pt)) {
        BeginRename(tile);
        return;
    }
    g_mouseDown = true;
    g_downPt = pt;
    g_dragIndex = g_filtered[tile];
    g_dragOffset = POINT{ pt.x - rc.left, pt.y - rc.top };
    SetCapture(g_hwnd);
}

void OnRightClick(POINT pt)
{
    const int item = TileFromPoint(pt);
    if (item < 0)
        return;
    CommitRename();
    g_selected = item;
    InvalidateRect(g_hwnd, nullptr, TRUE);
    POINT screen = pt;
    ClientToScreen(g_hwnd, &screen);
    ShowItemMenu(item, screen);
}

void OnMouseMove(POINT pt)
{
    if (!MouseActuallyMoved(pt))
        return;
    if (g_mouseDown && !g_dragging) {
        if (abs(pt.x - g_downPt.x) > Scale(4) || abs(pt.y - g_downPt.y) > Scale(4))
            g_dragging = true;
    }
    if (g_dragging) {
        const int slot = DragSlotFromPoint(pt);
        if (slot != g_dragIndex && slot >= 0 && slot < static_cast<int>(g_apps.size())) {
            // Slide the tile to its new slot; the ones in between shift over.
            if (slot > g_dragIndex)
                std::rotate(g_apps.begin() + g_dragIndex, g_apps.begin() + g_dragIndex + 1,
                            g_apps.begin() + slot + 1);
            else
                std::rotate(g_apps.begin() + slot, g_apps.begin() + g_dragIndex,
                            g_apps.begin() + g_dragIndex + 1);
            g_dragIndex = slot;
            ApplyFilter(false);
            g_selected = slot;
        }
        InvalidateRect(g_hwnd, nullptr, TRUE);
        return;
    }
    const int item = TileFromPoint(pt);
    if (item >= 0 && item != g_selected) {
        g_selected = item;
        InvalidateRect(g_hwnd, nullptr, TRUE);
    }
}

void OnAllApps(AllAppsBatch* batch)
{
    if (batch->px != Scale(BASE_INSTALLED_ICON)) {
        // Enumerated for a DPI the panel has since left; go again.
        FreeInstalled(batch->apps);
        delete batch;
        RefreshAllApps();
        return;
    }
    // Nothing changed — or the picker is up, holding indices into the list it
    // was given; the next opening fetches a fresh one anyway.
    if (g_modal || (g_allPx == batch->px && SameApps(g_all, batch->apps))) {
        FreeInstalled(batch->apps);
        delete batch;
        return;
    }
    FreeInstalled(g_all);
    g_all = std::move(batch->apps);
    g_allPx = batch->px;
    delete batch;
    // The results and the rows both index into g_all.
    if (g_visible) {
        ApplyFilter(false);
        Layout(false);
    }
}

// ---------------------------------------------------------------------------
// Window procedure

LRESULT CALLBACK LaunchpadWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_dpi = GetDpiForWindow(hwnd);
        g_brushSearch = CreateSolidBrush(CLR_SEARCH_BG);

        g_edit = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_SEARCH)), g_inst, nullptr);
        g_editBase = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchEditProc)));

        g_rename = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RENAME)), g_inst, nullptr);
        g_renameBase = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_rename, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditProc)));
        SendMessageW(g_rename, EM_SETLIMITTEXT, 64, 0);

        CreateFonts();
        return 0;
    }

    case WM_APP_ALLAPPS:
        OnAllApps(reinterpret_cast<AllAppsBatch*>(lParam));
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            if (g_visible) {
                // Typing is searching, which edit mode has no use for.
                if (g_editMode && GetWindowTextLengthW(g_edit) > 0) {
                    CommitRename();
                    EndDrag(true);
                    g_editMode = false;
                }
                ApplyFilter(true);
                Layout(false);
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (HandleKey(wParam))
            return 0;
        break;

    case WM_SETFOCUS:
        SetFocus(g_renaming >= 0 ? g_rename : g_edit);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client;
        GetClientRect(hwnd, &client);
        Paint(hdc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, CLR_SEARCH_BG);
        SetTextColor(hdc, CLR_TEXT);
        return reinterpret_cast<LRESULT>(g_brushSearch);
    }

    case WM_MOUSEMOVE:
        OnMouseMove(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_LBUTTONDOWN:
        OnMouseDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_LBUTTONUP:
        EndDrag(true);
        return 0;

    case WM_RBUTTONUP:
        OnRightClick(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd)
            EndDrag(true);
        return 0;

    case WM_MOUSEWHEEL: {
        const int before = g_scrollY;
        g_scrollY -= GET_WHEEL_DELTA_WPARAM(wParam) * Scale(BASE_TILE_H) / WHEEL_DELTA;
        ClampScroll();
        if (g_scrollY != before) {
            PositionRenameEdit();
            // The row now under a pointer that has not moved is hovered all
            // the same; the wheel is a deliberate move.
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const int item = TileFromPoint(pt);
            if (item >= 0)
                g_selected = item;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && g_visible && !g_modal) {
            // Same grace period as the switcher: a window finishing its own
            // activation just after we opened must not dismiss us.
            if (GetTickCount() - g_shownTick < 400) {
                ForceForeground(hwnd);
                SetFocus(g_edit);
            } else {
                HideLaunchpad();
            }
        }
        return 0;

    case WM_SYSCOMMAND:
        // Releasing Alt alone must not enter the window-menu loop — and the
        // hotkey's own modifier may well arrive as Alt.
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DPICHANGED:
        g_dpi = HIWORD(wParam);
        CreateFonts();
        g_iconPx = Scale(BASE_ICON);
        for (LaunchpadApp& app : g_apps)
            ResolveLaunchpadApp(app, g_iconPx);
        ClearResolved();
        RefreshAllApps();
        if (g_visible)
            Layout(true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Lets the popup menus follow the system's dark mode. uxtheme exports this
// by ordinal only (135, SetPreferredAppMode, Windows 10 1903 and later); where
// it is missing the menus simply stay light.
void AllowDarkMenus()
{
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme)
        return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    if (auto set = reinterpret_cast<SetPreferredAppMode>(
            GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))))
        set(1 /* AllowDark: follow the system setting */);
}

} // namespace

// ---------------------------------------------------------------------------

HWND CreateLaunchpadWindow(HINSTANCE instance)
{
    g_inst = instance;
    AllowDarkMenus();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = LaunchpadWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"Monopane launchpad",
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    if (!hwnd)
        return nullptr;

    // Rounded, with the dark-mode border and the Start menu's thin outline.
    DWORD cornerPref = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &cornerPref,
                          sizeof(cornerPref));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    const COLORREF border = RGB(62, 62, 62);
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));

    BeginLaunchpadSeed();
    // The installed apps are searchable from the first press, then kept
    // fresh on each.
    RefreshAllApps();
    return hwnd;
}

void ShowLaunchpad()
{
    const int px = Scale(BASE_ICON);
    const bool sizeChanged = px != g_iconPx;
    g_iconPx = px;
    LoadLaunchpad(g_apps, px, sizeChanged);
    g_accent = SystemAccentLight();

    SetWindowTextW(g_edit, L"");
    g_editMode = false;
    ApplyFilter(true);
    Layout(true);

    // Opening under a cursor that is already sitting there is not a hover.
    GetCursorPos(&g_lastMouseScreen);
    g_visible = true;
    g_shownTick = GetTickCount();
    ShowWindow(g_hwnd, SW_SHOW);
    ForceForeground(g_hwnd);
    SetFocus(g_edit);

    RefreshAllApps();
}

void HideLaunchpad()
{
    if (!g_visible)
        return;
    CommitRename();
    EndDrag(true);
    g_visible = false;
    g_editMode = false;
    ShowWindow(g_hwnd, SW_HIDE);
}

bool LaunchpadVisible()
{
    return g_visible;
}

void OpenLaunchpadConfigFolder()
{
    if (g_iconPx == 0) {
        g_iconPx = Scale(BASE_ICON);
        LoadLaunchpad(g_apps, g_iconPx, true);
    }
    OpenLaunchpadFolder(g_apps);
}

void DestroyLaunchpadResources()
{
    FreeLaunchpadIcons(g_apps);
    ClearResolved();
    FreeInstalled(g_all);
    for (HFONT f : { g_fontName, g_fontRow, g_fontHeading, g_fontEdit, g_fontGlyph,
                     g_fontGlyphSmall, g_fontGlyphBig })
        if (f) DeleteObject(f);
    if (g_brushSearch) DeleteObject(g_brushSearch);
}
