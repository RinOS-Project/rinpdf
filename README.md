# rinpdf

RinOS向けPDFライブラリ（開発中）。C++20内部実装、C ABI、Meson。

## 実装済み
- PDFヘッダーとバージョン識別、ファイル／メモリ読み込み
- ASCII文字列から単一ページPDF 1.4を生成
- Classic xrefテーブル読み取りと限定的な間接オブジェクト範囲解決
- PDF字句解析器（名前、整数、キーワード、配列・辞書の区切り、文字列、16進文字列、コメント）
- 暫定的なページ数スキャン
- smoke、xref、lexerの回帰テスト

## 制限
字句解析器はまだxref／間接オブジェクト／ページツリーの解析処理に接続していません。浮動小数点数、真偽値・nullの型付け、再帰的な辞書・配列構築、ストリーム、xref stream、増分更新、暗号化、描画は未実装です。現在のページ数取得は暫定スキャナーであり任意のPDFに対して正確ではありません。製品用途や信頼できないPDFの処理には使用しないでください。

## Build
```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## 次の段階
lexerを使ったオブジェクト構文木、xrefからの参照解決、Catalog／Pagesツリー検証、ストリーム解析、CPUレンダラー。OS-Coreへのsubmodule追加は未実施です。
