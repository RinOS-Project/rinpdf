# rinpdf

RinOS向けPDFライブラリ（実験的な開発版）。C++20実装、C ABI、Mesonビルド。

## 実装済み
- PDFヘッダーの検査、バージョン取得、メモリ・ファイル読み込み
- ASCII文字列を持つ単一ページPDF 1.4の生成
- 従来形式のxrefテーブルを `startxref` から読み取る内部パーサー
- 間接オブジェクトの限定スキャンによる暫定ページ数取得
- スモークテストとxref回帰テスト

## 制限事項
xrefパーサーはまだ公開APIやページ数取得処理に統合されていません。xref stream、増分更新、object stream、暗号化、ページツリー、ストリーム長、レンダリングは未対応です。現在のページ数取得は正式なPDFページツリーの解析ではなく、誤検出する場合があります。信頼できないPDFを安全かつ正確に解析できる段階ではありません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## Next
xrefエントリと間接オブジェクトの照合、CatalogとPagesツリーの解決、xref streamと増分更新、CPUレンダラーを順に実装します。OS-Coreへのsubmodule追加は未実施です。
