# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20実装、C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー／バージョン、ファイル／メモリ読み込み
- ASCII文字列の単一ページPDF 1.4生成
- Classic xref、非stream間接オブジェクトの限定的な解決
- 字句解析器、辞書・配列・間接参照の構文木
- trailer /Rootを起点としたCatalog→Pages→Pageツリー
- 直接整数/Lengthを使うstream境界解析
- FlateDecode（zlib形式）の展開と展開後サイズ上限（既定64MiB）
- smoke、xref、lexer、value、streamテスト

## 制限
間接/Length、フィルター配列、Predictor、xref stream、object stream、増分更新、暗号化、CPU描画、フォントは未実装です。stream解析はまだxrefオブジェクトリゾルバーと接続されていません。現段階は一般的なPDF全体を扱える製品ではありません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

zlibの開発用ヘッダー・ライブラリが必要です。RinOSへの移植時にはtarget向けzlibと依存関係の整理が必要です。

## 次の実装
streamの間接/Length解決、フィルター配列とPredictor、オブジェクトリゾルバーとの統合、xref stream、CPU描画。
