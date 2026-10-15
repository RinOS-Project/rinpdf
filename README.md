# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20、C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー／バージョン判定、ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF生成
- Classic xref、構文木、Catalog→Pages→Pageツリー
- stream境界、直接・間接/Length、FlateDecode
- ページ/Contents参照からのデータ抽出
- 実験的CPU矩形レンダラー：g、rg、re、f/F、n、q、Q（グレースケール・RGB塗りつぶし、描画色の保存・復元）
- smoke、xref、lexer、value、stream、stream_object、rectanglesテスト

## 描画API
`rinpdf_render_page(document, index, width, height, rgba, stride)` は、classic xrefのページから/Contentsを読み取り、RGBAへ描画します。現状は `g`, `rg`, `re`, `f`, `F`, `n`, `q`, `Q` の限定的な矩形塗りつぶしに対応します。テキストや未対応命令を含む場合は `RINPDF_UNSUPPORTED` を返し、呼び出し元の出力バッファを変更しません。ページ座標のMediaBox変換はまだ未実装です。

OS-CoreのLadybird/AquamarineにはPainterAquamarineのビットマップ・パス描画実装がありますが、rinpdfからの直接呼び出しはまだ未統合です。現段階では独立CPUラスタライズ結果をアプリ側で表示する構成です。

## 制限
矩形レンダラーは公開rinpdf_render_page APIに接続しました。座標変換、MediaBox、CropBox、フォント、画像、パス一般、グラフィックス状態、透明度、一般パス・画像・テキスト描画は未対応です。PDF数値やトークン処理は限定的です。xref stream、object stream、増分更新、暗号化なども未実装です。一般のPDF表示には使用できません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の実装
/Contentsとレンダラーの接続、MediaBoxと座標変換、グラフィックス状態、一般パス描画、フォント。
