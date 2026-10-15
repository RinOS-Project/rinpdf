# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20、C ABI、Meson。

## 現在の実装
- PDFヘッダー・バージョン検査、ファイル／メモリ読み込み
- ASCII文字列の1ページPDF生成
- classic xrefテーブル、非stream間接オブジェクト、PDF値構文木
- Catalog→Pages→Pageの参照解決、Kids巡回、Count照合、循環参照拒否
- ページ数APIを暫定スキャナーからページツリー方式に切り替え
- smoke、xref、lexer、valueテスト

## 制限
現在のCatalogはxref参照先を探索して検出しており、trailerの /Root をまだ検証していません。増分更新、xref stream、object stream、暗号化、stream解析、描画、実数は未実装です。ストリームを含むオブジェクトは限定リゾルバーが拒否します。正規のPDF仕様への完全準拠や悪意あるPDFの安全性は保証しません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

次はtrailer /Rootの検証、正規のstream処理、xref stream、増分更新、CPU描画。
