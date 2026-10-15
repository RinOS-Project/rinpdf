# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20、C ABI、Meson。zlibが必要です。

## 実装済み
- PDFヘッダー／バージョン判定、ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF生成
- Classic xref、構文木、Catalog→Pages→Pageツリー
- stream境界、直接・間接/Length、FlateDecode
- ページ/Contents参照からのデータ抽出
- 実験的CPU矩形レンダラー：g、rg、re、f/F、n、q、Q（グレースケール・RGB・CMYK塗りつぶし、描画色の保存・復元）
- smoke、xref、lexer、value、stream、stream_object、rectanglesテスト

## 描画API
`rinpdf_render_page(document, index, width, height, rgba, stride)` は、classic xrefのページから/Contentsを読み取り、RGBAへ描画します。現状は `g`, `rg`, `k`, `re`, `f`, `F`, `n`, `q`, `Q` の限定的な矩形塗りつぶしに対応します。テキストや未対応命令を含む場合は `RINPDF_UNSUPPORTED` を返し、呼び出し元の出力バッファを変更しません。ページ座標のMediaBox変換に初期対応しました。回転・CropBox・ページリソースの継承は未対応です。

OS-CoreのLadybird/AquamarineにはPainterAquamarineのビットマップ・パス描画実装がありますが、rinpdfからの直接呼び出しはまだ未統合です。現段階では独立CPUラスタライズ結果をアプリ側で表示する構成です。

## 制限
矩形レンダラーは公開rinpdf_render_page APIに接続しました。座標変換、CropBox、フォント、画像、パス一般、グラフィックス状態、透明度、一般パス・画像・テキスト描画は未対応です。PDF数値やトークン処理は限定的です。xref stream、object stream、増分更新、暗号化なども未実装です。一般のPDF表示には使用できません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の実装
/Contentsとレンダラーの接続、MediaBoxと座標変換、グラフィックス状態、一般パス描画、フォント。

## リソース制限
公開レンダリングAPIは一時RGBAバッファを64MiB以内に制限します。CMYKは単純なDeviceCMYK→RGB変換であり、ICCプロファイルや色管理には未対応です。MediaBox、文字、一般パス、Aquamarine連携も引き続き未実装です。

## MediaBox座標変換 (experimental)
ページまたは親Pages辞書の整数配列MediaBoxを取得し、RGBA出力解像度に合わせて矩形を線形変換します。PDFの左下原点をRGBAの左上原点へ変換します。実数座標、CropBox、Rotate、UserUnit、一般CTMにはまだ対応していません。未対応の文書はRINPDF_UNSUPPORTEDを返します。
