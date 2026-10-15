# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20実装、公開C ABI、Meson。

## 実装済み
- PDFヘッダー判定、バージョン取得、ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF 1.4生成
- Classic xrefテーブル読み取り、限定的な非stream間接オブジェクトの位置解決
- PDF字句解析器
- 再帰的なPDF値パーサー（辞書、配列、整数、名前、文字列、真偽値、null、間接参照）
- smoke、xref、lexer、valueのテスト定義

## 現時点の制約
PDF値パーサーは内部モジュールとして追加した段階で、Catalog／Pagesツリーや公開ページ数APIとはまだ統合されていません。実数、stream、xref stream、object stream、増分更新、暗号化、レンダリングは未実装です。既存のページ数取得は暫定スキャナーを使っており、任意のPDFで正確とは限りません。未検証のPDFを安全に扱う製品用途には使用しないでください。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の開発
xrefからオブジェクト構文木を解決し、Catalog→Pages→Pageを検証して暫定スキャナーを廃止する。その後、ストリーム解析、描画、フォントへ進む。
