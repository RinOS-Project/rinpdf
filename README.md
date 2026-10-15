# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20、C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー／バージョン判定、ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF生成
- Classic xref、構文木、Catalog→Pages→Pageツリー
- stream境界、直接・間接/Length、FlateDecode
- ページ/Contents参照からのデータ抽出
- 実験的CPU矩形レンダラー：g、re、f/F、n（グレースケール塗りつぶし）
- smoke、xref、lexer、value、stream、stream_object、rectanglesテスト

## 制限
矩形レンダラーは内部実装で、公開rinpdf_render_page APIにはまだ接続されていません。座標変換、MediaBox、CropBox、フォント、画像、パス一般、グラフィックス状態、透明度、カラー描画は未対応です。PDF数値やトークン処理は限定的です。xref stream、object stream、増分更新、暗号化なども未実装です。一般のPDF表示には使用できません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の実装
/Contentsとレンダラーの接続、MediaBoxと座標変換、グラフィックス状態、一般パス描画、フォント。
