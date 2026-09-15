# KinuDAW

Windows向けネイティブDAWです。KinuUI/RmlUiのタイムラインに実音声エンジン、WASAPI録音・再生、MIDI編集・入力、VST3ホスト、プロジェクト保存、WAV書き出しを接続しました。アプリケーションの実装はMITライセンスです。

**v0.2.1は実処理を備えた開発版です。** 基本的な制作機能を動作確認していますが、市販DAWと同等の機能・全プラグインの全プリセットでの互換性を保証するものではありません。未対応機能と検証範囲を下記に記載しています。

## 起動

1. [Windows x64開発版](https://github.com/SakiikaVR/KinuDAW-Mock/releases/tag/v0.2.1)のZIPを全体展開します。
2. `KinuDAW/KinuDAW.exe`を起動します。`Samples`、`Tools`、`kinu_vst_worker.exe`は隣に置いたままにします。
3. 初回はインストール済みVST3をバックグラウンド検出します。追加したプラグインは「VST3再検出」で更新します。

Direct3D 11対応のWindows x64環境が必要です。DLL不足で起動できない場合は、[Microsoft公式のVisual C++ v14 x64再頒布パッケージ](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)をインストールしてください。フォントのインストールは不要です。

## 制作機能

- 音声：WAV・FLAC・MP3を読み込み、Audioトラックで実再生。移動・トリム・コピー／貼り付け・ループ・BPM変更。
- MIDI：空のInstrumentレーンまたはMIDIクリップをダブルクリックしてピアノロールを開く。ダブルクリックでノート追加、ドラッグで移動、右端で長さ変更、右クリックで削除、ノート上のホイールでベロシティ変更。鍵盤で試聴。WinMM MIDI入力機器のノートも取り込みます。
- 録音：トラックの丸をクリックして赤い録音待機状態にし、RECで音声／MIDI録音、STOPで確定。音声は既定のWASAPI入力を使い、実行ファイル隣の`Recordings`にWAV保存。複数の音声トラックをアームした場合、同じステレオ入力を記録します。録音開始時はループを解除します。
- VST3：`+instruments`から検出したプラグインを選択。トラックヘッダーのダブルクリックでネイティブエディターを表示。ネイティブGUIがない場合はパラメーター一覧を表示。FXボタンから追加エフェクト3個まで接続・編集・削除できます。
- ミックス：ゲイン・パン・ミュート・ソロ・マスター出力。別ウインドウのミキサーと音量・M/S・内蔵FXを同期。メーター／波形は音声エンジンの実測値です。ステレオメーターの左右には共通のピーク値を使用しています。
- 内蔵FX：120 Hzハイパス、コンプレッサー、ステレオリバーブ。
- 音量オートメーション：対象トラックを選択して`+Automation`。クリック追加・ドラッグ移動・右クリック削除。最大64点、dBを線形補間して実再生に反映。
- 保存：「保存」で`.kinu`プロジェクトを保存。「開く」で音声参照・MIDI・チャンネル設定・VST3状態を復元。音声ファイルは埋め込まず元の場所を参照します。
- 書き出し：「WAV書き出し」で48 kHz／ステレオ／32-bit float WAVを生成。VST3と追加FXを含めてオフライン処理します。
- 最大32トラック（MASTERを含む）、256クリップ、128拍。ピアノロールの編集表示は16拍、1クリップ128ノート。

| キー | 操作 |
| --- | --- |
| Space | 再生／一時停止 |
| Ctrl+S / Ctrl+O | プロジェクト保存／読み込み |
| Ctrl+Z / Ctrl+Y | クリップ・MIDIノート編集の取り消し／やり直し |
| Ctrl+C / Ctrl+V | クリップのコピー／貼り付け |
| Ctrl+D | 選択クリップ削除（既存UIの割り当て） |
| Ctrl+＋ / Ctrl+－ / Ctrl+0 | UI表示倍率変更／既定に戻す |

文字入力中は入力欄を優先します。トラック名は右クリックで変更できます。

## WASAPIと安定性

既定はWASAPI共有、128フレーム要求、48 kHz float処理です。Windows／デバイスが対応する最短周期を使い、初期化に失敗した場合は共有512フレームへ戻します。「オーディオ設定」で共有128、排他128、共有512を選択できます。実際のデバイス周期はドライバー側が決めるため、要求値と一致しない場合があります。

WASAPIコールバックは固定容量PCMキューを取り出すだけです。VST3待機・DLL呼び出し・ファイル読み書き・UIロックを行いません。専用レンダラーと音声スレッドは優先度を上げ、MMCSSを使用します。先行レンダリング用キューと機器側バッファの分だけ実際の遅延が加わります。画面のdevice periodは入出力往復遅延ではありません。

プラグインの検出は個別プロセス、実行はインスタンスごとのワーカープロセスで隔離します。Windows Job Objectで親終了時にワーカーも終了。通常処理に20 msの期限を設け、停止・クラッシュしたワーカーは切り離します。FXはドライ音声へ戻し、音源は停止します。FXメニュー／ヘッダーから再読み込みできます。多数の重いプラグインでは音声キュー不足が起こる場合があり、XRUN表示で確認できます。

プロジェクト保存は一時ファイルへの書き込み後に置換し、既存の保存を保護します。30秒ごとに実行ファイル隣の`recovery.kinu`を保存し、「開く」で回復可能です。自動保存のVST3状態は直近の手動保存／ロード時のキャッシュです。状態取得に失敗しても編集データは保存し、以前のプラグイン状態を保持します。

## このPCでの検証

2026-09-15、Windows x64／Yamaha AG03MK2で確認しました。

- 検出40モジュール／43 VST3クラス、検出失敗0。全43クラスで512ブロックの処理、有限出力、状態保存・復元が成功。
- Vitalで実音声出力、状態復元、VST3込みWAV書き出し、ワーカー強制終了後の親処理継続を検証。
- 実WASAPI共有モードでVitalを30秒連続再生し、測定区間のキュー不足0回を確認。実機周期は共有441フレーム／44.1 kHz（10 ms）、排他128フレーム／44.1 kHz（約2.90 ms）。これは機器周期であり往復遅延ではありません。
- 音声再生・ゲイン／パン／ミュート・ループ／停止・WAV再読み込み・10,000回の並行Scene公開、音声とMIDIを含むプロジェクト往復、実タイムライン／ミキサー／ピアノロール画面とVitalのネイティブGUIを確認。
- 鍵盤右端の白鍵、黒鍵横の上下白鍵、離した後のNote Offを実マウスと共有状態で検証。WASAPI入力で48,128フレームを録音し、WAV確定・再読み込みを確認。

全プリセット・全GUI操作・外部MIDI機器・長時間の制作セッションまで検証済みという意味ではありません。Kontakt等の空の初期音源では、処理が成功してもサンプルを読み込むまで無音です。プラグインの認証・設定に通常ユーザー環境が必要なものがあります。

## 未対応・制約

VST3の遅延補償（PDC）、プラグインパラメーターのカーブ自動化、MIDI CC／ピッチベンド／サステイン、MIDIファイル入出力、サンプル精度の録音位置補正、複数入力機器／個別入力ルーティング、ASIO、タイムストレッチ、テンポマップ、トラックの削除、バス／センドは未実装です。VST3は主ステレオバスを使用し、サイドチェインと複数出力ルーティングは未対応です。再初期化を要求する一部プラグイン操作はワーカーの再読み込みが必要です。

音声ファイルはメモリーへデコードします（1ファイル最長1時間）。書き出し範囲はクリップ終端＋4拍で、長いFXテールは収まらない場合があります。書き出し・オーディオ設定変更・プラグイン交換中はデバイスを一時停止します。自動保存先への書き込み権限がある場所へZIPを展開してください。

## ビルド・検証

Visual Studio 2022の「C++によるデスクトップ開発」、CMake、Gitを用意します。FreeTypeとVST3 SDKは固定版の再帰サブモジュールに含まれます。

```powershell
git clone --recursive https://github.com/SakiikaVR/KinuDAW-Mock.git
cd KinuDAW-Mock
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Build-KinuDAW.ps1
.\build-daw\Release\rmlui_sample_daw_timeline.exe
```

既存のチェックアウトでは`git submodule update --init --recursive`を先に実行してください。手動設定時は`BUILD_SHARED_LIBS=OFF`、`RMLUI_BACKEND=Win32_DX11`、`RMLUI_TRACY_PROFILING=OFF`、`MI_DEBUG=OFF`、`MI_DEBUG_INTERNAL=OFF`を指定します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scan-Vst3.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Probe-Vst3.ps1
.\build-daw\Release\kinu_audio_tests.exe --device-exclusive
.\build-daw\Release\kinu_audio_tests.exe --capture
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Package-KinuDAW.ps1
```

プローブは各プラグインの初期状態で行い、タイムアウトしたプロセスを終了します。検出／互換性JSON、録音、回復ファイルはローカル生成物でありGitHubへ含めません。

## ソースとライセンス

実装は[Samples/basic/daw_timeline](Samples/basic/daw_timeline)です。MIT本文は[LICENSE.txt](LICENSE.txt)、依存関係・フォントのライセンスは[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)を参照してください。VST3 SDK 3.8.1はMITですが、利用者のVST3プラグインや音源ライブラリーのライセンスは別です。

元のUIライブラリーの説明は[KINUUI-UPSTREAM-README.md](KINUUI-UPSTREAM-README.md)を参照してください。