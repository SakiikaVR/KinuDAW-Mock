# KinuDAW-Mock

[KinuUI](https://github.com/SakiikaVR/KinuUI) を使ったWindows向けのネイティブDAW UIモックです。タイムライン、独立ミキサー、ピアノロール、音源選択、VST3風のGUI／MIDI／パラメーター画面を備えます。文字は同梱のLINE Seed JP Boldで統一しています。

**完成版DAWではありません。** 実際の音声処理、マイク／MIDI録音、VST3の検出・実行、プロジェクト保存は未実装です。波形・メーター・録音クリップ・音源リストはデモ表示です。

## ダウンロード・起動

[Windows x64版リリース](https://github.com/SakiikaVR/KinuDAW-Mock/releases/latest) から `KinuDAW-Mock-v0.1.0-windows-x64.zip` をダウンロードしてください。

1. ZIP全体を展開します。
2. `KinuDAW-Mock` 内の `rmlui_sample_daw_timeline.exe` を開きます。

Windows 10／11、Direct3D 11対応環境を想定しています。`Samples` フォルダーとDLLは実行ファイルの隣に置いたままにしてください。フォントのインストールやソースのビルドは不要です。

## 操作・機能

- タイムライン：小節表示、交互色グリッド、横スクロール、ズーム。BPMは数字を上下ドラッグして変更。
- クリップ：中央ドラッグで移動、左右端ドラッグで長さ変更、ダブルクリックでピアノロール。右クリックで切り取り・コピー・貼り付け・削除。
- コピー／貼り付け：instruments同士、Audio同士で可能。空き部分の右クリックからはクリック位置、キーボードからは選択トラックの再生バー位置に貼り付け。
- SNAP：初期ON。OFF、1/16、1/8、1/4、1/2、1、2、4、8を選択可能。1は1小節。
- LOOP：初期OFF、初期範囲1小節。小節ルーラーを右クリックして開始／終了位置を設定。
- トラック：MASTERは最上部固定。それ以外は名前部分をドラッグして並べ替え、右クリックで名前入力、色マーカーのクリックでプリセット色を選択。
- 追加：`+Audio`、`+instruments`、`+Automation`。Audioへの音声ファイルドロップはモッククリップを作成。instrumentsは検索・お気に入り付きデモ音源リストから選択。最大32トラック（MASTERを含む）。
- 録音モック：白丸をクリックして赤丸の録音待機状態にし、上部RECでモッククリップを作成、STOPで終了。録音ファイルは作成しない。
- ミキサー：「表示」メニューから別ウインドウで開く。トラック順・名前・色・音量・M/Sを同期。選択チャンネルのみハイライト。各チャンネル独立ラック、疑似L/Rメーター、横スクロール、最前面固定。
- VST3風ウインドウ：Automation以外のトラックヘッダーをダブルクリック。GUI、MIDI、パラメーターの3タブ。INPUT／OUTPUTのON/OFF、PORT、CHが一致するトラック間のMIDI接続をモック表示。
- パラメーター：検索付き2列一覧で値を変更し、既存Automationへ割り当て、または `+` で新規作成して割り当て。カーブ編集や実際のオートメーション再生は未実装。

| キー | 操作 |
| --- | --- |
| Space | 再生／一時停止 |
| Ctrl+Z / Ctrl+Y | クリップ編集の取り消し／やり直し（最大100回） |
| Ctrl+C / Ctrl+V | クリップをコピー／貼り付け |
| Ctrl+D | 選択クリップを削除 |

文字入力中は入力欄の操作を優先します。編集状態は実行中のみ保持されます。

## ソースからビルド

Visual Studio 2022のC++開発環境、CMake、FreeTypeが必要です。

```powershell
git clone --recursive https://github.com/SakiikaVR/KinuDAW-Mock.git
cd KinuDAW-Mock
cmake -S . -B build-daw -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_SHARED_LIBS=OFF -DRMLUI_SAMPLES=ON `
  -DRMLUI_BACKEND=Win32_DX11 -DFreetype_ROOT="path/to/freetype"
cmake --build build-daw --config Release --target rmlui_sample_daw_timeline
.\build-daw\Release\rmlui_sample_daw_timeline.exe
```

FreeTypeのパスは使用環境に合わせて置き換えてください。動的FreeTypeを使う場合は対応するDLLも必要です。KinuUIのメモリー管理を同一バイナリ内に保つため、Windowsでは `BUILD_SHARED_LIBS=OFF` を使用します。

実装は [Samples/basic/daw_timeline](Samples/basic/daw_timeline) にあります。単独ビルドに必要なKinuUI本体も含めています。元のUIライブラリーの説明は [KINUUI-UPSTREAM-README.md](KINUUI-UPSTREAM-README.md) を参照してください。

## ライセンス

KinuUI／RmlUiは [LICENSE.txt](LICENSE.txt)、LINE Seed JPは [SIL Open Font License 1.1](Samples/basic/daw_timeline/data/fonts/OFL.txt) を参照してください。リリースZIPには依存ライブラリー・フォントのライセンスも同梱しています。
