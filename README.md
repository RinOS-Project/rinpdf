# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20、公開C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー／バージョン、ファイル／メモリ読み込み
- ASCII文字列の単一ページPDF生成
- Classic xref、PDF構文木、Catalog→Pages→Pageツリー
- stream境界と直接・間接/Length解決
- FlateDecodeと展開サイズ制限
- ページ/Contents参照をたどる内部抽出器（単一stream・配列）
- smoke、xref、lexer、value、stream、stream_objectテスト

## 制限
/Contents抽出器は内部APIであり、公開描画APIにはまだ接続されていません。xref stream、object stream、増分更新、暗号化、Predictor、Filter配列、フォント、描画は未実装です。ページツリーの全不変条件と複雑なPDFへの互換性は未検証です。現段階で製品用途のPDF処理は推奨しません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の実装
/Contents抽出の回帰テスト、描画命令のトークン化、基本グラフィックス状態、CPUレンダラー、フォント。
