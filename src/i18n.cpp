#include "i18n.h"
#include <unordered_map>
#include <string>

namespace oriel {
namespace {

Lang g_lang = Lang::Ja;

struct Pair { const wchar_t* ja; const wchar_t* en; };

// Ordered the way the UI is read rather than alphabetically, so a missing
// translation is easier to spot next to its neighbours.
const Pair kTable[] = {
    // sidebar and places
    { L"よく使う項目",            L"Favourites" },
    { L"場所",                    L"Locations" },
    { L"タグ",                    L"Tags" },
    { L"最近の項目",              L"Recents" },
    { L"デスクトップ",            L"Desktop" },
    { L"書類",                    L"Documents" },
    { L"ダウンロード",            L"Downloads" },
    { L"この PC",                 L"This PC" },
    { L"ローカル (C:)",           L"Local disk (C:)" },

    // default tags
    { L"至急",                    L"Urgent" },
    { L"確認中",                  L"Reviewing" },
    { L"納品済",                  L"Delivered" },
    { L"資料",                    L"Reference" },

    // columns and sorting
    { L"名前",                    L"Name" },
    { L"サイズ",                  L"Size" },
    { L"種類",                    L"Kind" },
    { L"変更日",                  L"Modified" },
    { L"名前で並べ替え",          L"Sort by name" },
    { L"サイズで並べ替え",        L"Sort by size" },
    { L"種類で並べ替え",          L"Sort by kind" },
    { L"変更日で並べ替え",        L"Sort by date modified" },
    { L"昇順",                    L"Ascending" },
    { L"降順",                    L"Descending" },

    // list states
    { L"項目がありません",        L"No items" },
    { L"一致する項目がありません", L"Nothing matches" },
    { L"読み込み中…",             L"Loading…" },
    { L"アクセスできません",      L"Not accessible" },
    { L"読み取れません",          L"Cannot be read" },
    { L"項目を選んでください",    L"Select an item" },
    { L"%zu 項目",                L"%zu items" },
    { L"%zu / %zu 項目",          L"%zu / %zu items" },
    { L"%zu 項目を選択",          L"%zu selected" },
    { L"検索",                    L"Search" },

    // context menu and actions
    { L"開く",                    L"Open" },
    { L"このアプリケーションで開く…", L"Open with…" },
    { L"そのアプリの書類に使う",  L"Use for documents of this type" },
    { L"エクスプローラーで表示",  L"Show in Explorer" },
    { L"名前を変更",              L"Rename" },
    { L"名前を変更\tF2",          L"Rename\tF2" },
    { L"複製",                    L"Duplicate" },
    { L"複製\tCtrl+D",            L"Duplicate\tCtrl+D" },
    { L"コピー",                  L"Copy" },
    { L"コピー\tCtrl+C",          L"Copy\tCtrl+C" },
    { L"切り取り",                L"Cut" },
    { L"切り取り\tCtrl+X",        L"Cut\tCtrl+X" },
    { L"貼り付け",                L"Paste" },
    { L"貼り付け\tCtrl+V",        L"Paste\tCtrl+V" },
    { L"名前をコピー",            L"Copy name" },
    { L"パスをコピー",            L"Copy path" },
    { L"パスをコピー\tCtrl+Shift+C", L"Copy path\tCtrl+Shift+C" },
    { L"ショートカットを作成",    L"Create shortcut" },
    { L"新規フォルダ",            L"New folder" },
    { L"新規フォルダ ",           L"New folder " },
    { L"新規フォルダ\tCtrl+Shift+N", L"New folder\tCtrl+Shift+N" },
    { L"削除",                    L"Delete" },
    { L"ごみ箱に入れる",          L"Move to Recycle Bin" },
    { L"ごみ箱に入れる\tDelete",  L"Move to Recycle Bin\tDelete" },
    { L"情報を見る\tCtrl+I",      L"Get info\tCtrl+I" },
    { L"プロパティ\tAlt+Enter",   L"Properties\tAlt+Enter" },
    { L"再読み込み\tF5",          L"Refresh\tF5" },
    { L"隠しファイルを表示",      L"Show hidden files" },
    { L"隠しファイルを表示\tCtrl+H", L"Show hidden files\tCtrl+H" },
    { L"タグを付け外しする",      L"Add or remove a tag" },
    { L"保存",                    L"Save" },
    { L"キャンセル",              L"Cancel" },
    { L"すべてのファイル",        L"All files" },

    // copies and shortcuts
    { L" のコピー",               L" copy" },
    { L" のコピー ",              L" copy " },
    { L" - ショートカット",       L" - Shortcut" },
    { L" - ショートカット (",     L" - Shortcut (" },

    // settings
    { L"設定",                    L"Settings" },
    { L"一般",                    L"General" },
    { L"外観",                    L"Appearance" },
    { L"表示",                    L"View" },
    { L"キー操作",                L"Keys" },
    { L"起動時",                  L"On launch" },
    { L"最後に見ていた場所",      L"Where you left off" },
    { L"配列",                    L"Layout" },
    { L"カラム幅",                L"Column width" },
    { L"行の高さ",                L"Row height" },
    { L"プレビュー欄",            L"Preview pane" },
    { L"カラムの右端に表示",      L"Show at the right edge of the column" },
    { L"ツールバーの濃度",        L"Toolbar density" },
    { L"アクセント",              L"Accent" },
    { L"ライト",                  L"Light" },
    { L"ダーク",                  L"Dark" },
    { L"アプリの記号",            L"App icons" },
    { L"Oriel 標準",              L"Oriel default" },
    { L"Windows 標準",            L"Windows default" },
    { L"隠しファイル",            L"Hidden files" },
    { L"一覧に表示する",          L"Show them in the list" },
    { L"ごみ箱へ移す前に確認する", L"Confirm before moving to the Recycle Bin" },
    { L"タグの保存先",            L"Where tags are stored" },
    { L"テキストなので手で直せます。", L"Plain text, so you can repair it by hand." },
    { L"別のアプリでファイルを移動すると対応が切れます。",
      L"Moving a file with another application breaks the link." },
    { L"この設定を開閉する",      L"Expand or collapse this setting" },

    // key help
    { L"検索を開く",              L"Open search" },
    { L"検索や入力を抜ける",      L"Leave search or editing" },
    { L"一段戻る / 一段進む",     L"Back / forward one level" },
    { L"同じ階層で選び変える",    L"Move within the same level" },
    { L"Enter で開きます。名前の変更は F2 です。",
      L"Enter opens it. F2 renames." },
    { L"Enter は名前の変更、Ctrl + ↓ で開きます。",
      L"Enter renames; Ctrl + ↓ opens." },

    // file kinds
    { L"フォルダ",                L"Folder" },
    { L"ファイル",                L"File" },
    { L" ファイル",               L" file" },
    { L"ファイル名",              L"File name" },
    { L"テキスト",                L"Text" },
    { L"文書",                    L"Document" },
    { L"表計算",                  L"Spreadsheet" },
    { L"プレゼン",                L"Presentation" },
    { L"PDF 書類",                L"PDF document" },
    { L"HTML 書類",               L"HTML document" },
    { L"スタイルシート",          L"Stylesheet" },
    { L"ソースコード",            L"Source code" },
    { L"画像",                    L"Image" },
    { L"PNG 画像",                L"PNG image" },
    { L"JPEG 画像",               L"JPEG image" },
    { L"GIF 画像",                L"GIF image" },
    { L"SVG 画像",                L"SVG image" },
    { L"WebP 画像",               L"WebP image" },
    { L"動画",                    L"Video" },
    { L"オーディオ",              L"Audio" },
    { L"圧縮フォルダ",            L"Archive" },
    { L"アプリケーション",        L"Application" },
    { L"ライブラリ",              L"Library" },
    { L"ログ",                    L"Log" },
};

const std::unordered_map<std::wstring, const wchar_t*>& map() {
    // Built on first use, which is after i18nInit() because nothing draws
    // before it.
    static const std::unordered_map<std::wstring, const wchar_t*> m = [] {
        std::unordered_map<std::wstring, const wchar_t*> t;
        t.reserve(std::size(kTable) * 2);
        for (const auto& p : kTable) t.emplace(p.ja, p.en);
        return t;
    }();
    return m;
}

} // namespace

void i18nInit(Lang preference) {
    if (preference == Lang::Ja || preference == Lang::En) { g_lang = preference; return; }
    const LANGID id = GetUserDefaultUILanguage();
    g_lang = (PRIMARYLANGID(id) == LANG_JAPANESE) ? Lang::Ja : Lang::En;
}

Lang i18nLang() { return g_lang; }

const wchar_t* T(const wchar_t* ja) {
    if (g_lang == Lang::Ja || !ja) return ja;
    const auto& m = map();
    const auto it = m.find(ja);
    return it == m.end() ? ja : it->second;   // untranslated stays readable
}

} // namespace oriel
