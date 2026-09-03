#include "launchpad.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
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

namespace {

constexpr wchar_t kClass[] = L"MonopaneLaunchpad";

constexpr int COLS = 6;

// Base (96-dpi) metrics. The width, the search box and the padding are shared
// with the switcher; see theme.h. Tile width is measured out of that width
// rather than fixed, so the two panels are the same size to the pixel.
constexpr int BASE_TILE_H = 96;
constexpr int BASE_ICON = 48;
constexpr int BASE_BADGE_R = 9;
constexpr int BASE_PICK_ICON = 16;

constexpr UINT IDC_SEARCH = 100;
constexpr UINT IDC_RENAME = 101;
constexpr UINT IDM_ADD_INSTALLED = 1;
constexpr UINT IDM_ADD_FILE = 2;

// Segoe MDL2 Assets glyphs.
constexpr wchar_t kGlyphPencil[] = L"\xE70F";
constexpr wchar_t kGlyphPlus[] = L"\xE710";

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
HWND g_edit = nullptr;
HWND g_rename = nullptr;
WNDPROC g_editBase = nullptr;
WNDPROC g_renameBase = nullptr;

UINT g_dpi = 96;
HFONT g_fontName = nullptr;
HFONT g_fontEdit = nullptr;
HFONT g_fontGlyph = nullptr;
HFONT g_fontGlyphBig = nullptr;
HBRUSH g_brushSearch = nullptr;
int g_editLineH = 0;   // one line of g_fontEdit, for centring the search box

std::vector<LaunchpadApp> g_apps;
int g_iconPx = 0;               // the size g_apps' icons were resolved at
std::vector<int> g_filtered;    // indices into g_apps, best match first
std::wstring g_query;
int g_selected = 0;             // a tile index: into g_filtered, or the "+" tile past its end
int g_scrollRow = 0;
int g_rowsVisible = 1;
bool g_visible = false;
bool g_editMode = false;
bool g_modal = false;           // a dialog of ours is up; losing focus is expected
DWORD g_shownTick = 0;

// Edit mode: dragging a tile, and renaming one.
bool g_mouseDown = false;
bool g_dragging = false;
int g_dragIndex = -1;           // into g_apps
POINT g_downPt{};
POINT g_dragOffset{};           // from the tile's corner to where it was grabbed
int g_renaming = -1;            // into g_apps

// The installed-apps picker's list, built on first use and kept.
std::vector<InstalledApp> g_installed;
int g_installedPx = 0;

int Scale(int value)
{
    return MulDiv(value, g_dpi, 96);
}

void CreateFonts()
{
    if (g_fontName) DeleteObject(g_fontName);
    if (g_fontEdit) DeleteObject(g_fontEdit);
    if (g_fontGlyph) DeleteObject(g_fontGlyph);
    if (g_fontGlyphBig) DeleteObject(g_fontGlyphBig);

    LOGFONTW lf{};
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -Scale(12);
    g_fontName = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(17);
    g_fontEdit = CreateFontIndirectW(&lf);

    wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
    lf.lfHeight = -Scale(16);
    g_fontGlyph = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(22);
    g_fontGlyphBig = CreateFontIndirectW(&lf);

    // A single-line edit control draws its text at the top of its client area,
    // not down the middle of it, so the only way to have the text sit centred
    // in the search plate is to make the control exactly one line tall and
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

// ---------------------------------------------------------------------------
// Tiles

bool PlusTileShown()
{
    return g_editMode && g_query.empty();
}

int TileCount()
{
    return static_cast<int>(g_filtered.size()) + (PlusTileShown() ? 1 : 0);
}

bool IsPlusTile(int tile)
{
    return PlusTileShown() && tile == static_cast<int>(g_filtered.size());
}

int RowsNeeded()
{
    return std::max(1, (TileCount() + COLS - 1) / COLS);
}

int GridTop()
{
    return Scale(BASE_SEARCH_H);
}

int PanelWidth()
{
    return Scale(BASE_PANEL_WIDTH);
}

int TileWidth()
{
    return (PanelWidth() - Scale(BASE_PAD) * 2) / COLS;
}

RECT TileRect(int tile)
{
    const int tileW = TileWidth();
    const int tileH = Scale(BASE_TILE_H);
    const int pad = Scale(BASE_PAD);
    const int col = tile % COLS;
    const int row = tile / COLS - g_scrollRow;
    const int x = pad + col * tileW;
    const int y = GridTop() + row * tileH;
    return RECT{ x, y, x + tileW, y + tileH };
}

RECT IconRectIn(const RECT& tile)
{
    const int icon = Scale(BASE_ICON);
    const int left = tile.left + (tile.right - tile.left - icon) / 2;
    const int top = tile.top + Scale(10);
    return RECT{ left, top, left + icon, top + icon };
}

RECT LabelRectIn(const RECT& tile)
{
    const RECT icon = IconRectIn(tile);
    return RECT{ tile.left + Scale(4), icon.bottom + Scale(6), tile.right - Scale(4),
                 tile.bottom - Scale(2) };
}

RECT BadgeRectIn(const RECT& tile)
{
    const RECT icon = IconRectIn(tile);
    const int r = Scale(BASE_BADGE_R);
    const int cx = icon.left - Scale(2);
    const int cy = icon.top - Scale(2);
    return RECT{ cx - r, cy - r, cx + r, cy + r };
}

// The plate the search box and the pencil sit on.
RECT SearchPlateRect()
{
    const int pad = Scale(BASE_PAD);
    return RECT{ pad, pad, PanelWidth() - pad, Scale(BASE_SEARCH_H) - Scale(4) };
}

RECT PencilRect()
{
    const RECT plate = SearchPlateRect();
    const int size = plate.bottom - plate.top;
    return RECT{ plate.right - size, plate.top, plate.right, plate.bottom };
}

int TileFromPoint(POINT pt)
{
    const int pad = Scale(BASE_PAD);
    if (pt.y < GridTop() || pt.x < pad)
        return -1;
    const int col = (pt.x - pad) / TileWidth();
    const int row = (pt.y - GridTop()) / Scale(BASE_TILE_H);
    if (col >= COLS || row >= g_rowsVisible)
        return -1;
    const int tile = (row + g_scrollRow) * COLS + col;
    return tile < TileCount() ? tile : -1;
}

// Where a dragged tile would land: the nearest slot, however far off the grid
// the cursor has strayed.
int DragSlotFromPoint(POINT pt)
{
    const int pad = Scale(BASE_PAD);
    const int col = std::clamp(static_cast<int>(pt.x - pad) / TileWidth(), 0, COLS - 1);
    const int row = std::clamp(static_cast<int>(pt.y - GridTop()) / Scale(BASE_TILE_H), 0,
                               g_rowsVisible - 1) +
                    g_scrollRow;
    const int last = static_cast<int>(g_apps.size()) - 1;
    return std::clamp(row * COLS + col, 0, last);
}

// ---------------------------------------------------------------------------
// Filtering & selection

void ApplyFilter(bool resetSelection)
{
    g_query = GetSearchText();
    g_filtered.clear();

    if (g_query.empty()) {
        for (int i = 0; i < static_cast<int>(g_apps.size()); ++i)
            g_filtered.push_back(i);
    } else {
        const std::wstring aliasTarget = LookupAlias(g_query);
        constexpr int kAliasBoost = 1 << 20;

        std::vector<std::pair<int, int>> scored;
        for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
            int score = FuzzyScore(g_query, g_apps[i].Name());
            if (!aliasTarget.empty() && !g_apps[i].exePath.empty() &&
                _wcsicmp(g_apps[i].exePath.c_str(), aliasTarget.c_str()) == 0)
                score = (score < 0 ? 0 : score) + kAliasBoost;
            if (score >= 0)
                scored.emplace_back(score, i);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [score, index] : scored)
            g_filtered.push_back(index);
    }

    if (resetSelection) {
        g_selected = 0;
        g_scrollRow = 0;
    } else {
        g_selected = std::clamp(g_selected, 0, std::max(0, TileCount() - 1));
    }
}

void EnsureSelectionVisible()
{
    const int row = g_selected / COLS;
    if (row < g_scrollRow)
        g_scrollRow = row;
    else if (row >= g_scrollRow + g_rowsVisible)
        g_scrollRow = row - g_rowsVisible + 1;
}

void PositionRenameEdit()
{
    if (g_renaming < 0)
        return;
    for (int tile = 0; tile < static_cast<int>(g_filtered.size()); ++tile) {
        if (g_filtered[tile] != g_renaming)
            continue;
        const RECT label = LabelRectIn(TileRect(tile));
        MoveWindow(g_rename, label.left, label.top, label.right - label.left, Scale(18), TRUE);
        return;
    }
}

void Layout(bool reposition)
{
    const int width = PanelWidth();
    const int searchH = Scale(BASE_SEARCH_H);
    const int tileH = Scale(BASE_TILE_H);
    const int pad = Scale(BASE_PAD);

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

    // The search box sits on the middle of the work area, the same line the
    // switcher puts its own on, and the grid hangs below it. The top is fixed
    // whatever the grid is doing, so the box holds still as a search narrows
    // it, as apps are added, and when the switcher opens instead.
    const int top = PanelTop(mi.rcWork, SearchPlateRect());

    // As many rows as the apps need, up to what fits below that line; past
    // that the grid scrolls.
    const int maxRows = RowsThatFit(mi.rcWork, top, searchH, pad, tileH);
    const int needed = RowsNeeded();
    g_rowsVisible = std::min(needed, maxRows);
    g_scrollRow = std::clamp(g_scrollRow, 0, std::max(0, needed - g_rowsVisible));
    const int height = searchH + g_rowsVisible * tileH + pad;

    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, top, width, height, SWP_NOACTIVATE);

    // One line tall, centred in the plate: see CreateFonts.
    const RECT plate = SearchPlateRect();
    const RECT pencil = PencilRect();
    const int editH = g_editLineH + Scale(2);
    const int editX = static_cast<int>(plate.left) + Scale(10);
    const int editW = std::max(0, static_cast<int>(pencil.left) - Scale(6) - editX);
    MoveWindow(g_edit, editX, static_cast<int>(plate.top) +
                                  (static_cast<int>(plate.bottom - plate.top) - editH) / 2,
               editW, editH, TRUE);
    PositionRenameEdit();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void MoveSelection(int delta)
{
    const int count = TileCount();
    if (count == 0)
        return;
    g_selected = ((g_selected + delta) % count + count) % count;
    EnsureSelectionVisible();
    InvalidateRect(g_hwnd, nullptr, TRUE);
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
    if (tile < 0 || tile >= static_cast<int>(g_filtered.size()))
        return;
    CommitRename();
    g_renaming = g_filtered[tile];
    SetWindowTextW(g_rename, g_apps[g_renaming].Name().c_str());
    PositionRenameEdit();
    ShowWindow(g_rename, SW_SHOW);
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

void RemoveTile(int tile)
{
    if (tile < 0 || tile >= static_cast<int>(g_filtered.size()))
        return;
    CommitRename();
    const int appIndex = g_filtered[tile];
    if (g_apps[appIndex].icon)
        DeleteObject(g_apps[appIndex].icon);
    g_apps.erase(g_apps.begin() + appIndex);
    SaveLaunchpad(g_apps);
    ApplyFilter(false);
    g_selected = std::clamp(tile, 0, std::max(0, TileCount() - 1));
    Layout(false);
}

void AddTarget(const std::wstring& target)
{
    if (target.empty())
        return;
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
        if (_wcsicmp(g_apps[i].target.c_str(), target.c_str()) == 0) {
            SelectAppTile(i);  // already there; just show where
            Layout(false);
            return;
        }
    }
    LaunchpadApp app;
    app.target = target;
    ResolveLaunchpadApp(app, g_iconPx);
    g_apps.push_back(std::move(app));
    SaveLaunchpad(g_apps);
    ApplyFilter(false);
    SelectAppTile(static_cast<int>(g_apps.size()) - 1);
    Layout(false);
}

// Brackets a dialog of ours: the launchpad must not take losing focus to it
// as a dismissal, and gets the focus back afterwards.
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
    std::vector<int> imageIndex;   // per g_installed entry, or -1
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
        for (int i = 0; i < static_cast<int>(g_installed.size()); ++i)
            shown.push_back(i);
    } else {
        std::vector<std::pair<int, int>> scored;
        for (int i = 0; i < static_cast<int>(g_installed.size()); ++i) {
            const int score = FuzzyScore(query, g_installed[i].name);
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
        item.pszText = const_cast<LPWSTR>(g_installed[shown[i]].name.c_str());
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
        st->images = ImageList_Create(g_installedPx, g_installedPx, ILC_COLOR32, 64, 32);
        st->imageIndex.assign(g_installed.size(), -1);
        for (size_t i = 0; i < g_installed.size(); ++i) {
            if (g_installed[i].icon)
                st->imageIndex[i] = ImageList_Add(st->images, g_installed[i].icon, nullptr);
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
            st->chosen = g_installed[static_cast<size_t>(item.lParam)].target;
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

    const int px = Scale(BASE_PICK_ICON);
    if (g_installed.empty() || g_installedPx != px) {
        HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        for (InstalledApp& app : g_installed)
            if (app.icon) DeleteObject(app.icon);
        g_installed = EnumerateInstalledApps(px);
        g_installedPx = px;
        SetCursor(previous);
    }

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
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_ADD_INSTALLED, L"Installed app…");
    AppendMenuW(menu, MF_STRING, IDM_ADD_FILE, L"Browse for a file…");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_NONOTIFY,
                                   screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);

    std::wstring target;
    if (cmd == IDM_ADD_INSTALLED && PickInstalledApp(target))
        AddTarget(target);
    else if (cmd == IDM_ADD_FILE && BrowseForFile(target))
        AddTarget(target);
}

// ---------------------------------------------------------------------------
// Opening apps

void Activate(int tile, bool forceNew)
{
    if (tile < 0 || tile >= TileCount())
        return;
    if (IsPlusTile(tile)) {
        const RECT rc = TileRect(tile);
        POINT pt{ rc.left + TileWidth() / 2, rc.top + Scale(BASE_ICON) + Scale(14) };
        ClientToScreen(g_hwnd, &pt);
        ShowAddMenu(pt);
        return;
    }

    const LaunchpadApp app = g_apps[g_filtered[tile]];  // a copy: the list may reload
    if (!g_query.empty() && !app.exePath.empty())
        SaveAlias(g_query, app.exePath);

    HideLaunchpad();

    // An app that is already running comes forward instead of starting over,
    // matched by the exe behind its window or the AppUserModelID it declares
    // (which is how Store apps, hosted by ApplicationFrameHost, are found).
    if (!forceNew && (!app.exePath.empty() || !app.aumid.empty())) {
        for (const WindowInfo& win : EnumerateAltTabWindows()) {
            const bool byExe = !app.exePath.empty() && !win.exePath.empty() &&
                               _wcsicmp(win.exePath.c_str(), app.exePath.c_str()) == 0;
            const bool byId = !app.aumid.empty() && !win.aumid.empty() &&
                              _wcsicmp(win.aumid.c_str(), app.aumid.c_str()) == 0;
            if (byExe || byId) {
                ActivateWindow(win.hwnd);
                return;
            }
        }
    }
    LaunchLaunchpadApp(app);
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

void DrawBadge(HDC dc, const RECT& rc)
{
    HBRUSH brush = CreateSolidBrush(RGB(80, 80, 80));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, border);
    Ellipse(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    HPEN cross = CreatePen(PS_SOLID, Scale(2), CLR_TEXT);
    SelectObject(dc, cross);
    const int inset = Scale(5);
    MoveToEx(dc, rc.left + inset, rc.top + inset, nullptr);
    LineTo(dc, rc.right - inset, rc.bottom - inset);
    MoveToEx(dc, rc.right - inset, rc.top + inset, nullptr);
    LineTo(dc, rc.left + inset, rc.bottom - inset);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(cross);
    DeleteObject(brush);
}

// One app tile, at `rc`, however it is being shown: in its slot, or floating
// under the cursor while dragged.
void DrawAppTile(HDC dc, const RECT& rc, const LaunchpadApp& app, bool selected, bool badge,
                 bool label)
{
    DrawIcon(dc, app.icon, IconRectIn(rc));
    if (label) {
        RECT text = LabelRectIn(rc);
        SelectObject(dc, g_fontName);
        SetTextColor(dc, selected ? RGB(255, 255, 255) : CLR_TEXT);
        DrawTextW(dc, app.Name().c_str(), -1, &text,
                  DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
    }
    if (badge)
        DrawBadge(dc, BadgeRectIn(rc));
}

void Paint(HDC hdc, const RECT& client)
{
    const int width = client.right;
    const int height = client.bottom;
    const int searchH = Scale(BASE_SEARCH_H);
    const int pad = Scale(BASE_PAD);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    HBRUSH bgBrush = CreateSolidBrush(CLR_BG);
    RECT full{ 0, 0, width, height };
    FillRect(mem, &full, bgBrush);
    DeleteObject(bgBrush);

    // Rounded to the same radius the compositor rounds the window itself by,
    // so the search box echoes the frame around it rather than cutting a
    // square out of it.
    FillRoundRectAA(mem, SearchPlateRect(), Scale(BASE_CORNER), CLR_SEARCH_BG, CLR_BG);
    SetBkMode(mem, TRANSPARENT);

    // The pencil, lit while editing.
    const RECT pencil = PencilRect();
    if (g_editMode) {
        RECT plate = pencil;
        InflateRect(&plate, -Scale(4), -Scale(4));
        FillRoundRectAA(mem, plate, Scale(4), CLR_SELECTION, CLR_SEARCH_BG);
    }
    DrawGlyph(mem, kGlyphPencil, pencil, g_fontGlyph, g_editMode ? RGB(255, 255, 255) : CLR_TEXT_DIM);

    if (g_filtered.empty() && !PlusTileShown()) {
        RECT row{ pad, searchH, width - pad, searchH + Scale(BASE_TILE_H) };
        SelectObject(mem, g_fontName);
        SetTextColor(mem, CLR_TEXT_DIM);
        DrawTextW(mem, L"No matching apps", -1, &row, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const int first = g_scrollRow * COLS;
    const int last = std::min(TileCount(), (g_scrollRow + g_rowsVisible) * COLS);
    for (int tile = first; tile < last; ++tile) {
        const RECT rc = TileRect(tile);
        const bool selected = tile == g_selected && !g_dragging;

        if (selected) {
            RECT sel = rc;
            InflateRect(&sel, -Scale(2), -Scale(2));
            FillRoundRectAA(mem, sel, Scale(6), CLR_SELECTION, CLR_BG);
        }

        if (IsPlusTile(tile)) {
            const RECT icon = IconRectIn(rc);
            HPEN dashed = CreatePen(PS_DASH, 1, CLR_TEXT_DIM);
            HGDIOBJ oldPen = SelectObject(mem, dashed);
            HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
            RoundRect(mem, icon.left, icon.top, icon.right, icon.bottom, Scale(10), Scale(10));
            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(dashed);
            DrawGlyph(mem, kGlyphPlus, icon, g_fontGlyphBig, CLR_TEXT_DIM);

            RECT text = LabelRectIn(rc);
            SelectObject(mem, g_fontName);
            SetTextColor(mem, selected ? RGB(255, 255, 255) : CLR_TEXT_DIM);
            DrawTextW(mem, L"Add", -1, &text, DT_CENTER | DT_NOPREFIX);

            if (g_apps.empty()) {
                RECT hint{ rc.right + pad, rc.top, width - pad * 2, rc.bottom };
                SetTextColor(mem, CLR_TEXT_DIM);
                DrawTextW(mem, L"Nothing pinned yet. Add an installed app, or browse for one.",
                          -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            continue;
        }

        const int appIndex = g_filtered[tile];
        if (g_dragging && appIndex == g_dragIndex) {
            // Its slot, waiting for it.
            RECT slot = IconRectIn(rc);
            InflateRect(&slot, Scale(4), Scale(4));
            HPEN outline = CreatePen(PS_DASH, 1, CLR_TEXT_DIM);
            HGDIOBJ oldPen = SelectObject(mem, outline);
            HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
            RoundRect(mem, slot.left, slot.top, slot.right, slot.bottom, Scale(10), Scale(10));
            SelectObject(mem, oldBrush);
            SelectObject(mem, oldPen);
            DeleteObject(outline);
            continue;
        }
        DrawAppTile(mem, rc, g_apps[appIndex], selected, g_editMode, appIndex != g_renaming);
    }

    if (g_dragging && g_dragIndex >= 0 && g_dragIndex < static_cast<int>(g_apps.size())) {
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(g_hwnd, &cursor);
        RECT rc{ cursor.x - g_dragOffset.x, cursor.y - g_dragOffset.y, 0, 0 };
        rc.right = rc.left + TileWidth();
        rc.bottom = rc.top + Scale(BASE_TILE_H);
        DrawAppTile(mem, rc, g_apps[g_dragIndex], false, false, true);
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
        MoveSelection(-1);
        return true;
    case VK_RIGHT:
        MoveSelection(1);
        return true;
    case VK_UP:
        MoveSelection(-COLS);
        return true;
    case VK_DOWN:
        MoveSelection(COLS);
        return true;
    case VK_TAB:
        MoveSelection(ShiftHeld() ? -1 : 1);
        return true;
    case VK_PRIOR:
        MoveSelection(-COLS * g_rowsVisible);
        return true;
    case VK_NEXT:
        MoveSelection(COLS * g_rowsVisible);
        return true;
    case VK_RETURN:
        Activate(g_selected, ShiftHeld());
        return true;
    case VK_ESCAPE:
        if (g_editMode)
            SetEditMode(false);
        else
            HideLaunchpad();
        return true;
    case VK_DELETE:
        // Only once there is nothing in the search box for it to delete.
        if (g_editMode && g_query.empty() && !IsPlusTile(g_selected)) {
            RemoveTile(g_selected);
            return true;
        }
        return false;
    case VK_F2:
        if (g_editMode && !IsPlusTile(g_selected)) {
            BeginRename(g_selected);
            return true;
        }
        return false;
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
    const RECT pencil = PencilRect();
    if (PtInRect(&pencil, pt)) {
        SetEditMode(!g_editMode);
        return;
    }
    const int tile = TileFromPoint(pt);
    if (tile < 0) {
        if (g_renaming >= 0)
            CommitRename();
        return;
    }

    if (!g_editMode) {
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
    // Dragging only means something in the unfiltered order.
    if (g_query.empty()) {
        g_mouseDown = true;
        g_downPt = pt;
        g_dragIndex = g_filtered[tile];
        g_dragOffset = POINT{ pt.x - rc.left, pt.y - rc.top };
        SetCapture(g_hwnd);
    }
}

void OnMouseMove(POINT pt)
{
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
    const int tile = TileFromPoint(pt);
    if (tile >= 0 && tile != g_selected) {
        g_selected = tile;
        InvalidateRect(g_hwnd, nullptr, TRUE);
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
        SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search apps…"));

        g_rename = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RENAME)), g_inst, nullptr);
        g_renameBase = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            g_rename, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditProc)));
        SendMessageW(g_rename, EM_SETLIMITTEXT, 64, 0);

        CreateFonts();
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE) {
            if (g_visible) {
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

    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd)
            EndDrag(true);
        return 0;

    case WM_MOUSEWHEEL: {
        const int steps = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        const int maxRow = std::max(0, RowsNeeded() - g_rowsVisible);
        const int row = std::clamp(g_scrollRow + steps, 0, maxRow);
        if (row != g_scrollRow) {
            g_scrollRow = row;
            PositionRenameEdit();
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
        if (g_visible)
            Layout(true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

// ---------------------------------------------------------------------------

HWND CreateLaunchpadWindow(HINSTANCE instance)
{
    g_inst = instance;

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

    DWORD cornerPref = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &cornerPref,
                          sizeof(cornerPref));
    return hwnd;
}

void ShowLaunchpad()
{
    const int px = Scale(BASE_ICON);
    const bool sizeChanged = px != g_iconPx;
    g_iconPx = px;
    LoadLaunchpad(g_apps, px, sizeChanged);

    SetWindowTextW(g_edit, L"");
    g_editMode = g_apps.empty();  // nothing to see yet: straight to adding
    ApplyFilter(true);
    Layout(true);

    g_visible = true;
    g_shownTick = GetTickCount();
    ShowWindow(g_hwnd, SW_SHOW);
    ForceForeground(g_hwnd);
    SetFocus(g_edit);
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
    for (InstalledApp& app : g_installed)
        if (app.icon) DeleteObject(app.icon);
    g_installed.clear();
    if (g_fontName) DeleteObject(g_fontName);
    if (g_fontEdit) DeleteObject(g_fontEdit);
    if (g_fontGlyph) DeleteObject(g_fontGlyph);
    if (g_fontGlyphBig) DeleteObject(g_fontGlyphBig);
    if (g_brushSearch) DeleteObject(g_brushSearch);
}
