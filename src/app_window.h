#pragma once
#include "i18n.h"
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_1.h>   // IDWriteTextLayout1, for optical tracking
#include <wrl/client.h>
#include "theme.h"
#include "column_model.h"
#include "shell_menu.h"
#include "thumbnail.h"
#include "dialog_server.h"
#include "anim.h"
#include "fade.h"
#include "icons.h"
#include "textfield.h"
#include "tags.h"
#include "settings.h"
#include <unordered_map>
#include <list>
#include <vector>

namespace oriel {

using Microsoft::WRL::ComPtr;

// What an application asked for, and what it gets back.
// One entry of the dialog's type filter, e.g. {T(L"画像"), L"*.png;*.jpg"}.
struct FileType {
    std::wstring label;
    std::wstring spec;
};
struct PickRequest {
    bool save = true;
    HWND owner = nullptr;
    std::wstring folder;
    std::wstring fileName;
    std::vector<FileType> types;
    int typeIndex = 0;
};
struct PickReply {
    bool cancelled = true;
    std::wstring path;
};

// Runs a picker on the calling (UI) thread and does not return until the user
// is done. Any failure leaves `reply->cancelled` alone, which the shim reads as
// "fall back to the genuine dialog".
bool runPicker(HINSTANCE, const PickRequest&, PickReply*);

// The shell: a borderless window we draw entirely ourselves, sitting on a
// DirectComposition surface so parts of it can be genuinely transparent and
// let the DWM backdrop through.
class AppWindow {
public:
    // `startPath` may be empty; anything else must be an existing directory.
    bool create(HINSTANCE hinst, int nCmdShow, const std::wstring& startPath = {});

    // Picker mode: the same window with an action bar, run for one answer.
    void configureAsPicker(const PickRequest& req, PickReply* reply) {
        picker_ = true; pickerReq_ = req; pickerReply_ = reply;
        // A save dialog opens with the suggested name already selected, so the
        // first keystroke replaces it rather than appending to it.
        nameField_.set(req.fileName);
        nameField_.selectAll();
        nameField_.focused = req.save;
    }
    bool pickerFinished() const { return pickerDone_; }
    HWND hwnd() const { return hwnd_; }
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);

private:
    LRESULT handle(HWND, UINT, WPARAM, LPARAM);

    bool createDevices();
    void resizeSwapChain(UINT w, UINT h);
    void render();

    // painting helpers, all in DIPs
    void paintTitleBar(const D2D1_RECT_F& r);
    void paintCaptionButtons(const D2D1_RECT_F& r);
    void paintSideBar(const D2D1_RECT_F& r);
    void paintToolBar(const D2D1_RECT_F& r);
    void paintColumns(const D2D1_RECT_F& r);
    void paintList(const D2D1_RECT_F& r);
    void paintIcons(const D2D1_RECT_F& r);
    void paintPreview(const D2D1_RECT_F& r);
    void paintPathBar(const D2D1_RECT_F& r);
    void paintViewSwitcher(const D2D1_RECT_F& r);

    // One source of truth for where each column sits, so painting and
    // hit-testing can never disagree.
    struct ColRect { size_t depth; float x, w; };
    std::vector<ColRect> columnLayout(const D2D1_RECT_F& area) const;
    bool  hitTestRow(float x, float y, size_t* depth, int* index) const;
    void  scrollSelectionIntoView();
    void  scrollColumnsToEnd(float areaWidth);
    D2D1_RECT_F contentArea() const;

    // Centres a shell bitmap on cx,cy inside a `side` box, letterboxed: shell
    // icons are not always square and stretching them is instantly visible.
    void drawIconBitmap(ID2D1Bitmap*, float cx, float cy, float side);
    void fill(const D2D1_RECT_F&, const D2D1_COLOR_F&);
    void hairline(float x0, float y0, float x1, float y1, const D2D1_COLOR_F&);
    void text(const wchar_t*, const D2D1_RECT_F&, const D2D1_COLOR_F&,
              float size, float trackingEm, float weight,
              DWRITE_TEXT_ALIGNMENT = DWRITE_TEXT_ALIGNMENT_LEADING);
    // Shared by drawing and measuring so the two cannot disagree.
    IDWriteTextFormat* formatFor(float size, float weight);
    // Measured, not guessed: breadcrumbs and carets have to sit where the
    // glyphs actually end.
    float textWidth(const wchar_t*, float size, float trackingEm, float weight,
                    int len = -1);
    // A field with a caret and selection. Returns the box it drew into.
    void paintField(const D2D1_RECT_F&, TextField&, const wchar_t* placeholder,
                    float size, float trackingEm, float weight);

    float dips(int px) const { return px * 96.0f / static_cast<float>(dpi_); }
    float px(float d)  const { return d * static_cast<float>(dpi_) / 96.0f; }
    int   frameBorder() const;
    bool  maximized() const;

    // ── settings ───────────────────────────────────────────────────
    // Anything the panel can change is written back, or the panel is a demo.
    void loadSettings();
    void saveSettings();
    void settingsChanged();          // debounced: a slider drag is one write
    void applyTheme();               // rebuilds theme_ from the choices below

    Settings settings_;
    bool settingsDirty_ = false;
    int  setThemeMode_ = 0;          // 0 follow the system, 1 light, 2 dark
    int  setAccent_ = -1;            // -1 the system accent, else a swatch

    HWND hwnd_ = nullptr;
    UINT dpi_  = 96;
    Theme theme_{};
    int hotCaption_ = 0;    // HTMINBUTTON / HTMAXBUTTON / HTCLOSE, or 0

    // ── view modes ─────────────────────────────────────────────────
    enum class View { Column, List, Icon };
    void setView(View);
    // The folder list and item flat views operate on: the deepest loaded column.
    const ColumnModel::Column* currentFolder() const;
    D2D1_RECT_F switcherButton(int i) const;
    bool hitTestFlat(float x, float y, int* index) const;
    void tick();                       // drives animation frames

    View view_ = View::Column;
    Tween viewThumb_;                  // sliding indicator in the switcher
    float flatScrollY_ = 0;
    int flatSelected_ = -1;

    // ── inspector ──────────────────────────────────────────────────
    // Not a separate window: the frame grows to the right and the settings
    // occupy the new strip, so the columns being adjusted never reflow.
    void toggleInspector();
    void paintInspector(const D2D1_RECT_F&);
    bool inspectorHit(float x, float y, bool dragging);
    float inspectorWidth() const { return inspW_.value(); }

    struct Slider { float x, y, w; float lo, hi; float* target; const wchar_t* label;
                    const wchar_t* unit; };
    std::vector<Slider> inspSliders_;   // rebuilt each paint, read by hit-testing
    int inspDragSlider_ = -1;

    bool  inspOpen_ = false;
    Tween inspW_;
    Tween inspInk_;                    // contents fade in behind the widening frame
    int   inspBaseW_ = 0;              // window width with the panel closed
    int   inspTab_ = 1;                // 0 general, 1 appearance, 2 view, 3 keys

    // live-adjustable settings the inspector writes to
    float setRowHeight_ = metrics::kRow;
    float setColumnW_   = metrics::kColumn;
    float setBlur_      = 60.0f;       // toolbar wash, percent
    bool  setPreview_   = true;
    bool  setAppIcons_  = true;        // application marks for types they own
    // Building the shell menu ahead of every click made right-click instant,
    // but each build loads third-party handlers that never fully let go.
    // Measured over 135s of browsing: threads 114 -> 250 and still climbing
    // with it on, 81 -> 75 and flat with it off. Off by default; the cost it
    // was paying for is bought instead by warming the handlers once, below.
    bool  setPrebuildMenu_ = false;
    bool  menuWarmed_ = false;
    void warmShellHandlers();

    // ── tabs ───────────────────────────────────────────────────────
    // Each tab owns its own column stack; the rest of the window just reads
    // whichever one is in front.
    ColumnModel&       model()       { return tabs_[activeTab_]; }
    const ColumnModel& model() const { return tabs_[activeTab_]; }
    void paintTabs(const D2D1_RECT_F&);
    D2D1_RECT_F tabRect(int i) const;
    float tabWidth(int i) const;      // animated: a new tab opens to width
    void addTab(const Place&);
    void closeTab(int i);
    D2D1_RECT_F tabClose(int i) const;
    bool tabsAnimating() const;
    int hotTabClose_ = -1;

    std::vector<ColumnModel> tabs_;
    std::vector<ULONGLONG>   tabBorn_;   // parallel to tabs_, for the open animation
    int activeTab_ = 0;
    int hotTab_ = -1;

    // ── hover and selection feedback ───────────────────────────────
    // The design sheet asks for 60ms on hover, 180ms for the sidebar pill and
    // 260ms for a column arriving. All of it runs off tick().
    void  setHover(size_t depth, int index);
    bool  columnsAnimating() const;
    void  sidebarClick(float x, float y);
    float sidebarPillY(int index) const;

    // Which chrome control the pointer is over, as a HotId. Painting reads it
    // through fades_, so leaving a control fades out instead of cutting.
    int   chromeHit(float x, float y) const;
    int   hotChrome_ = 0;
    bool  tracking_ = false;         // WM_MOUSELEAVE armed
    FadeSet fades_;

    size_t hoverDepth_ = 0;
    int    hoverIndex_ = -1;

    int   sideSelected_ = 2;          // 書類
    Tween sidePill_;
    int   sideHover_ = -1;

    float scrollX_ = 0;     // horizontal offset of the column strip

    // ── column dividers ────────────────────────────────────────────
    int dragDivider_ = -1;
    float dragStartX_ = 0, dragStartW_ = 0;

    // ── toolbar ────────────────────────────────────────────────────
    D2D1_RECT_F capsuleButton(int i) const;   // 0 sort, 1 share, 2 more
    D2D1_RECT_F searchRect() const;
    D2D1_RECT_F navButton(int i) const;       // 0 sidebar, 1 back, 2 forward
    void paintNavButtons();
    std::wstring pendingRenameName_;          // rename this once it appears

    // Built as soon as the selection settles, so the right-click has nothing
    // left to do but show it.
    void scheduleMenuPrepare(std::wstring path);
    ShellMenu shellMenu_;
    std::wstring pendingMenuPath_;   // what we are betting the next click wants

    // Thumbnails and shell icons: decoded off-thread, cached here as device
    // bitmaps. `key` separates the caches - a type icon is filed under its
    // extension so a folder of 500 project files costs one lookup, not 500.
    // Bounded by bytes, not by count: a row icon is a few KB and a preview is a
    // quarter of a megabyte, so "keep 256 of them" means anywhere between 1MB
    // and 64MB depending only on what the user happened to look at.
    struct CachedThumb { ComPtr<ID2D1Bitmap> bmp; bool isIcon = false; size_t bytes = 0; };
    static constexpr size_t kThumbBudget = 24u * 1024 * 1024;
    size_t thumbBytes_ = 0;
    ID2D1Bitmap* thumbFor(const std::wstring& path, int dipBox, bool* isIcon = nullptr);
    ID2D1Bitmap* shellImage(const std::wstring& key, const std::wstring& path,
                            int dipBox, bool iconOnly, bool* isIcon = nullptr);
    // The application's own icon for a row, or null to draw our line mark.
    ID2D1Bitmap* rowIcon(const Entry&, const Place&, int dipBox);
    void onThumbReady(ThumbBits*);

    // ── picker mode ────────────────────────────────────────────────
    void paintActionBar(const D2D1_RECT_F&);
    void finishPicker(bool cancelled);
    D2D1_RECT_F actionButton(int which) const;   // 0 = confirm, 1 = cancel
    D2D1_RECT_F nameFieldRect() const;
    D2D1_RECT_F typeFieldRect() const;
    float actionBarHeight() const;
    // What the caller gets: the typed name resolved against the browsed folder,
    // or the selected item when nothing was typed.
    std::wstring pickerResult() const;
    bool passesTypeFilter(const std::wstring& name) const;
    void cycleType();

    bool picker_ = false;
    bool pickerDone_ = false;
    PickRequest pickerReq_;
    PickReply* pickerReply_ = nullptr;
    int hotAction_ = -1;
    TextField nameField_;
    bool typeOpen_ = false;      // the type list is showing

    // ── search ─────────────────────────────────────────────────────
    // Filters the folder in view rather than crawling the disk: the column the
    // user is looking at is the thing they are looking through.
    TextField search_;
    bool searchOn_ = false;
    // Whichever field owns the keyboard, or null. One at a time by construction.
    TextField* focusedField() {
        if (renaming_)          return &renameField_;
        if (nameField_.focused) return &nameField_;
        if (search_.focused)    return &search_;
        return nullptr;
    }
    std::vector<int> visibleRows(const ColumnModel::Column&) const;
    void setSearch(bool on);
    void moveSelectionVisible(int delta, bool extend = false);

    // ── path bar ───────────────────────────────────────────────────
    struct Crumb { float x, w; size_t depth; };
    std::vector<Crumb> crumbs_;      // rebuilt each paint, read by hit-testing
    int hotCrumb_ = -1;
    bool crumbClick(float x, float y);

    // ── tags ───────────────────────────────────────────────────────
    TagStore tags_;
    int tagFilter_ = -1;             // sidebar tag row in view, or -1
    void showTagFolder(int tag);
    Listing buildTagListing(int tag) const;

    // ── key scheme ─────────────────────────────────────────────────
    bool setOrielKeys_ = true;       // Enter renames; Ctrl+Down opens
    bool setConfirmDelete_ = true;

    // ── navigation history ─────────────────────────────────────────
    // One list with a cursor, not two stacks: going back then somewhere new
    // must discard the forward branch, and a cursor makes that a truncation.
    std::vector<Place> history_;
    size_t historyAt_ = 0;
    void pushHistory(const Place&);
    void goBack();
    void goForward();
    bool canGoBack() const    { return historyAt_ > 0; }
    bool canGoForward() const { return historyAt_ + 1 < history_.size(); }
    void navigate(const Place&, bool record = true);

    // ── sidebar collapse ───────────────────────────────────────────
    void  toggleSidebar();
    float sideWidth() const { return sideW_.value(); }
    Tween sideW_;
    bool  sideOpen_ = true;

    // ── menus ──────────────────────────────────────────────────────
    // Our own commands live above the shell's, in one popup, so a right-click
    // and the toolbar offer the same vocabulary.
    void showSortMenu();
    void showMoreMenu();
    void showFolderMenu(POINT screenPt);
    void runCommand(int id);
    // What the commands act on: the deepest selection, or the folder in view.
    std::wstring targetPath() const;
    std::vector<std::wstring> targetPaths() const;
    std::wstring targetFolder() const;
    void refreshVisible();

    // ── inline rename ──────────────────────────────────────────────
    // Editing happens on the row, not in a dialog: the name is where the eye
    // already is, and a modal box would hide the neighbours you are naming
    // against.
    bool   renaming_ = false;
    size_t renameDepth_ = 0;
    int    renameIndex_ = -1;
    TextField renameField_;
    void beginRename();
    void commitRename();
    void cancelRename() { renaming_ = false; renameField_.focused = false; tick(); }
    void openSelected();

    DialogServer dialogServer_;   // only the resident window runs one
    HINSTANCE hinst_ = nullptr;

    ThumbnailSource thumbs_;
    std::unordered_map<std::wstring, CachedThumb> thumbCache_;
    std::list<std::wstring> thumbLru_;          // front = most recently used
    std::unordered_map<std::wstring, bool> thumbPending_;

    ComPtr<ID3D11Device>          d3d_;
    ComPtr<IDXGISwapChain1>       swap_;
    ComPtr<IDCompositionDevice>   dcomp_;
    ComPtr<IDCompositionTarget>   dtarget_;
    ComPtr<IDCompositionVisual>   dvisual_;
    ComPtr<ID2D1Factory1>         d2dFactory_;
    ComPtr<ID2D1Device>           d2dDevice_;
    ComPtr<ID2D1DeviceContext>    dc_;
    ComPtr<ID2D1SolidColorBrush>  brush_;
    ComPtr<IDWriteFactory>        dwrite_;
    IconSet                       icons_;
    // keyed by size+weight; rebuilding these per row per frame was measurable
    std::unordered_map<uint64_t, ComPtr<IDWriteTextFormat>> formats_;
};

} // namespace oriel
