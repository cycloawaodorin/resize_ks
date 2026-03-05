# Lanczos3 / 画素平均法リサイズ
AviUtl ExEdit2 のためのリサイズフィルタープラグインです．

## インストール
[Releases](https://github.com/cycloawaodorin/resize_ks/releases) から最新の `resize_ks.VERSION.zip` をダウンロード，展開し，`resize_ks.auf2` を AviUtl ExEdit2 のプレビュー画面にドラッグ&ドロップしてください．

## 使い方
<dl>
 <dt>拡大率，X，Y</dt>
  <dd>横を (拡大率×X)/1e4 倍，縦を (拡大率×Y)/1e4 倍します．</dd>
 <dt>平均法</dt>
  <dd>チェックすると，Lanczos3法の代わりに画素平均法を使ってリサイズします．</dd>
 <dt>ピクセル数でサイズ指定</dt>
  <dd>出力サイズを X×Y にし，拡大率を無視します．</dd>
<dl>

## その他
- [更新履歴](./CHANGELOG.md)

## Contributing
バグ報告等は [Issues](https://github.com/cycloawaodorin/resize_ks/issues) にお願いします．
