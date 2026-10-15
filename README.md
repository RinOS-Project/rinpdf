# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20内部実装、C ABI、Meson。

## 実装済み
- PDFヘッダーとバージョンの識別
- ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF 1.4を生成
- 従来形式xrefテーブルの読み取り
- xrefのオフセットから非stream間接オブジェクトの本文範囲を取得し、番号・世代を照合
- 限定的なオブジェクトスキャンによる暫定ページ数取得
- スモークテストとxref／間接オブジェクトの回帰テスト

## 重要な制限
このリゾルバーはstreamを含むオブジェクトを拒否します。文字列中のendobj、増分更新、xref stream、object stream、暗号化、正規のPDFトークナイザー、ページツリーは未対応です。現時点のxref解析は公開ページ数APIに未接続です。現在のページ数は暫定スキャナーの値であり、任意のPDFで正確とは限りません。PDF描画は未実装です。信頼できないPDFを処理する製品にはまだ使用しないでください。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の開発
正規のトークナイザーと辞書／配列／間接参照の解析、CatalogとPagesツリーの検証、xref stream、増分更新、ストリームの長さ解決、CPUレンダラー。
