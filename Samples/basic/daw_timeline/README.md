# KinuDAW-Mock — DAW UI実装

このディレクトリにはタイムライン、ミキサー、ピアノロール、音源選択、VST3風ウインドウのモック実装があります。

ダウンロード・起動方法、操作一覧、Windowsビルド手順、未実装事項は [プロジェクトREADME](../../../README.md) を参照してください。

- `src/`：C++の状態管理、入力処理、ウインドウ連携。
- `data/`：RML／RCSSによるUIとLINE Seed JPフォント。
- `CMakeLists.txt`：`rmlui_sample_daw_timeline` ターゲット。

実際の音声処理、録音、VST3実行は行いません。メーター・波形・音源リストはモックです。
