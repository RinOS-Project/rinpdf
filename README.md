# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20実装、公開C ABI、Meson。

## 実装済み
- PDFヘッダー・バージョン、ファイル／メモリ読み込み
- ASCII文字列の単一ページPDF 1.4生成
- Classic xrefテーブル、非stream間接オブジェクトの限定的な位置解決
- PDF字句解析器と辞書・配列・間接参照の構文木
- **trailer /Rootを起点とするCatalog→Pages→Pageツリー解析**
- ページ数・Count照合、循環参照の検出
- smoke、xref、lexer、valueのテスト

## 未対応と制限
xref stream、object stream、増分更新（/Prev）、ハイブリッドxref（/XRefStm）、暗号化（/Encrypt）、ストリーム解析、描画、実数は未実装です。現状の間接オブジェクト解決は非streamに限定され、文字列中のendobj等を完全には扱えません。trailerの完全な妥当性検証も未実装です。信頼できないPDFに対して安全性や互換性を保証する段階ではありません。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の実装
間接オブジェクトの字句境界に基づく安全な解決、streamの/Length解決とFlateDecode、xref stream、増分更新、CPU描画。OS-Coreへのsubmodule導入は未実施です。
