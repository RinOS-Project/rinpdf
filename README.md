# rinpdf

RinOS向けPDFライブラリ。C++20内部実装、C ABI、Meson。

## 実装状況 (0.2.0 experimental)
- PDFヘッダーとバージョン取得
- ファイル・メモリ読み込み
- ASCII文字列の1ページPDF 1.4生成
- クラシックな間接オブジェクトの限定スキャンと /Type /Page 辞書の暫定カウント

**注意:** ページ数は暫定値です。xref、ページツリー、増分更新、圧縮オブジェクト、暗号化、stream長を解釈しないため、任意のPDFでは誤認識する可能性があります。信頼できないPDFに対して正確なページ数を保証しません。PDFヘッダーの検出も完全な妥当性検証ではありません。レンダリングAPIは RINPDF_UNSUPPORTED を返します。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

次はxref・object parser・page treeを実装し、暫定スキャナーを置き換えます。OS-Coreへのsubmodule追加は未実施です。
