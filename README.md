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

## CTM（実験的）
CPU矩形レンダラーは `cm` による軸平行な拡大縮小・平行移動と `q` / `Q` によるCTM保存復元に対応します。回転、せん断、任意のアフィン変換、一般パス、CropBox、Rotateは未対応です。対応できない行列は描画成功とせず拒否します。実機およびビルド・テストの動作確認は未実施です。

## CropBox表示領域 (experimental)
`read_page_box` はページまたは親Pages辞書のCropBoxを継承し、MediaBoxとの交差領域をRGBA描画のビューポートとして返します。CropBoxがなければMediaBoxを使用します。交差領域が空なら描画を拒否します。座標は現状整数配列のみです。/Rotate、/UserUnit、実数のページ境界、一般パスの描画には未対応です。CropBox用回帰テストは追加しましたが実行未確認です。

## ページ回転 (experimental)
ページ／親Pagesの整数/Rotateを継承し、0/90/180/270度（360度で正規化）に対応します。RGBA出力は時計回りの回転として処理します。90/270度では内部ラスタライズ寸法を入れ替えます。非90度単位は拒否します。ページ回転の統合テストを追加しましたが、ビルド・実行は未検証です。一般パス・テキスト・画像描画は引き続き未実装です。

## 一般パスの初期実装 (experimental)
CPUレンダラーは `m` (move)、`l` (line)、`h` (close) と `f` (fill) による直線ポリゴンのスキャンライン塗りつぶしに限定対応しました。既存の軸平行CTMとRGB/CMYK色を利用します。曲線 `c/v/y`、ストローク `S/s`、複合パスの正確なPDF非ゼロワインディング規則、アンチエイリアス、クリッピングは未対応です。現在の塗りつぶしは偶奇スキャンライン方式の暫定実装で、PDF標準の `f` と完全互換ではありません。ビルド・テスト実行は未検証です。

## Cubic path support (experimental)
`c`、`v`、`y` による3次ベジェ曲線を各16直線セグメントへ近似し、既存のスキャンライン塗りつぶしに接続しました。曲線の精度は固定分割であり、拡大表示時の品質やPDFのワインディング規則を保証しません。ストローク `S/s`、アンチエイリアス、クリッピングは未実装です。ビルド・テスト実行は未検証です。

## CPUパス描画の処理量制限
ポリゴン塗りつぶしではラスタライズ対象の出力領域を最大1,048,576ピクセルに制限し、クリップ後の負の座標をunsignedへ直接変換しないよう修正しました。これは暫定的な保守的制限であり、一般的な高解像度PDFを描画できることを意味しません。未対応のストローク、アンチエイリアス、非ゼロワインディング規則は引き続き未実装です。ビルド・テストの実行は未検証です。

## Basic stroke (experimental)
`w`、`S`、`s` による単純な線幅付きストローク描画を追加しました。CPUで線分からの距離を判定してRGBAを塗ります。描画要求は1024×1024以内に限定し、非正の線幅を拒否します。線端・線結合は暫定的な距離判定であり、PDFの`J`・`j`・`M`、破線、ストローク専用色、複雑なCTM、アンチエイリアスは未対応です。ビルド・テストは未検証です。

## Stroking color state (experimental)
非ストローク色（g/rg/k）とストローク色（G/RG/K）を分離しました。q/Qは線幅と両方の色を保存・復元します。線端J・線結合j・miter limit M・破線dは引き続き未実装です。現状の線分描画は丸い距離判定に基づく近似で、PDF仕様のcap/joinと一致しません。ビルド・テストは未検証です。
