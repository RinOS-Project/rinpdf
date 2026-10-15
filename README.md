# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20実装、C ABI、Meson。

## 実装済み
- PDFヘッダー・バージョン判定、ファイル／メモリ読み込み
- ASCII文字列からの単一ページPDF 1.4生成
- Classic xrefテーブル、非stream間接オブジェクトの限定的な位置解決
- PDF字句解析と辞書・配列・間接参照の構文木
- trailer /RootからのCatalog→Pages→Pageツリー巡回
- 直接整数/Lengthを持つstreamデータ領域の境界解析
- smoke、xref、lexer、value、streamテスト

## 未対応
間接参照の/Length、FlateDecode、xref stream、object stream、増分更新、暗号化、CPU描画、フォントは未実装です。stream境界解析はまだオブジェクトリゾルバーと接続されていません。PDF仕様への完全準拠、信頼できない入力に対する安全性は未検証です。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の段階
streamの間接/Length解決、FlateDecodeと展開サイズ上限、streamを含む間接オブジェクトの安全な処理、xref stream、描画。
