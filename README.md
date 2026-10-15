# rinpdf

RinOS向けPDFライブラリの初期実装。C++20、C ABI、Meson。

実装済み: PDFヘッダー判定、ファイルとメモリからの読み込み、バージョン取得、ASCII文字列を含む1ページPDF 1.4生成。

未実装: PDFオブジェクト解析、xref、ページ列挙、レンダリング、フォント、画像、暗号化。未実装APIは RINPDF_UNSUPPORTED を返します。PDFヘッダーの検出はPDFの完全な妥当性検証ではありません。

ビルド:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

今後はxref、object parser、page tree、content stream、font、CPU renderer、圧縮、セキュリティ検証を順に実装します。OS-Coreへのsubmodule導入は別途行います。
