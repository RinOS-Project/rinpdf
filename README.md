# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20実装、公開C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー・バージョン判定、ファイル／メモリ読み込み
- ASCII文字列の単一ページPDF 1.4生成
- Classic xref、非stream間接オブジェクトの限定的な位置解決
- 字句解析、辞書・配列・間接参照の構文木
- trailer /RootからのCatalog→Pages→Pageツリー巡回
- 直接整数/Lengthとxref経由の間接/Lengthによるstream境界取得
- 無圧縮・FlateDecodeストリーム展開、展開サイズ制限
- smoke、xref、lexer、value、stream、stream_objectテスト

## 制限
xref stream、object stream、増分更新、暗号化、フィルター配列、Predictor、CPU描画、フォントは未実装です。間接/Lengthはclassic xrefで参照可能な非stream整数オブジェクトのみ対応します。streamオブジェクト処理はページ描画APIにまだ接続されていません。信頼できないPDFに対する安全性・互換性は未検証です。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

zlibの開発用ヘッダー・ライブラリが必要です。

## 次の実装
streamの安全なobject resolverへの統合、Filter配列・Predictor、xref stream、ページコンテンツ解釈、CPUレンダラー。
