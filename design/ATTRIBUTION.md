# 使用素材と権利表記

## 書体

意匠ベンチに埋め込んでいる書体はすべて **SIL Open Font License 1.1 (OFL)** です。
OFL は商用利用・改変・アプリへの埋め込みを許諾しますが、**著作権表記とライセンス全文の同梱**を条件とします。製品版に採用した書体については、`LICENSES/` に各書体の `OFL.txt` を配置してください。

| 用途 | 書体 | 権利者 | ライセンス |
|---|---|---|---|
| 欧文 | Inter | The Inter Project Authors | OFL 1.1 |
| 和文 | Noto Sans JP | The Noto Project Authors | OFL 1.1 |
| 欧文 | IBM Plex Sans | IBM Corp. | OFL 1.1 |
| 和文 | IBM Plex Sans JP | IBM Corp. | OFL 1.1 |
| 欧文 | Manrope | Mikhail Sharanda / The Manrope Project Authors | OFL 1.1 |
| 和文 | Zen Kaku Gothic New | Yoshimichi Ohira | OFL 1.1 |

ベンチに入っているのは Google Fonts の `css2?text=` で**使用文字だけに絞ったサブセット**です（合計 886KB）。OFL はサブセット化・再配布を認めていますが、派生物に「Reserved Font Name」を含む名前を付けることは禁じられています。予約名を避けるため、ベンチ内部の `font-family` は `inter` / `notojp` のような別名で参照しています。

採用が確定したら、製品版では Google Fonts 経由ではなく**各プロジェクトの公式配布物から原本を取得**し、必要ならビルド時に `fonttools` で自前サブセット化してください。

## アイコン

製品に埋め込んでいるアイコンは **Lucide v1.27.0** です。`design/gen-icons.ps1` が SVG から
`src/icon_data.inc`（パスデータの C++ テーブル）を生成し、実行時に D2D のジオメトリへ変換しています。

| セット | ライセンス | 扱い |
|---|---|---|
| Lucide | **ISC**（一部アイコンは Feather 由来で **MIT**） | 製品バイナリに埋め込み |
| Tabler | MIT | 比較検討のみ。`design/icon-compare.html` にパスデータが載る |
| Iconoir | MIT | 同上 |
| Phosphor | MIT | 同上 |

**ISC も MIT も、著作権表記と許諾文の同梱を条件に許諾します。**パスデータの埋め込みは
"substantial portion" にあたるため、`THIRD-PARTY-NOTICES.txt` を**バイナリと同じ場所に必ず置く**こと。
このファイルは `design/gen-icons.ps1` が Lucide 同梱の `LICENSE` から生成しており、ISC 本文・
Feather 由来アイコンの一覧・MIT 本文（Copyright (c) 2013-present Cole Bemis）をすべて含みます。
手で書き写さないこと。セットを差し替えたら生成し直すこと。

`design/icon-compare.html` は Tabler / Iconoir / Phosphor のパスデータも含むため、この
ドキュメント自体を外部に出す場合は上表の 4 件すべての表記が必要です。

> **公開リポジトリには含めていません。** `icon-compare.html` と、書体サブセットを12件
> 埋め込んでいる `bench.dist.html` は `.gitignore` 済みです。製品が使わないセットと書体を
> 再頒布するのは、見返りなくライセンス義務を背負う行為で、`design/icon-sets/` を
> コミットしない判断と同じ理由によります。手元では `design/build-icon-compare.ps1`
> (要 `fetch-icons.ps1`) と `design/build.ps1` が作り直します。

選定の経緯と、各セットの実物比較は `design/icon-compare.html` にあります。Phosphor を採らなかったのは
意匠ではなく実装上の理由で、**輪郭が塗りとして焼き込まれており線幅を後から詰められない**ためです
（100% DPI で Light は破綻する）。Lucide はストローク主体なので、意匠シートが指定する線幅に合わせ込めます。

## 素材と表記の規範

本製品に持ち込んでよいのは、**自作した素材**と**ライセンスが明示的に許諾している素材**だけです。

- **他社製品の意匠をなぞった素材は作らない・使わない。**一方で、MIT / ISC / OFL のように
  利用・改変・再配布を明示的に許諾する公開素材は**別のカテゴリ**であり、表記を同梱すれば使ってよい。
  「全部自作」は品質を担保しない。統一された一族を手で描き起こすのはそれ自体が別の仕事で、
  中途半端にやると一目で安く見える（実際そうなった）
- 独自ライセンスの素材は避ける。取り消し不能でなく条項が変わりうるものは、
  唯一の要注意依存になる。同等品が MIT / ISC にあるならそちらを採る
- 書体は OFL など再配布と埋め込みを許諾するものに限る
- 他社の製品名・企業名・ロゴを、UI・コード・コメント・コミットメッセージ・広報のいずれにも書かない
- 他社製品との互換性・提携・準拠を示唆する表現を使わない
- 機能名は自前で定義する。他社が用いる固有の機能名をそのまま採用しない

参照してよいのは**公開された設計上の作法**（配置の慣習、余白のリズム、操作イディオム）であって、素材でも名称でもありません。参照した事実を実装コード中に記述しないこと。

## 参考にしたコード

**Files** (https://github.com/files-community/Files) — MIT License。
シェル連携の実装方針を参照します。C++/WinRT で書き直すため直接のコード流用は発生しない見込みですが、もし一部でも流用した場合は MIT の著作権表記を `LICENSES/Files-MIT.txt` に同梱すること。一部ファイルに MPL-2.0 ヘッダが混在するため、参照時はファイル単位で確認すること。
