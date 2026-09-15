# KinuDAW implementation

実音声・WASAPI・MIDI・VST3を備えたWindows DAWの実装です。起動、操作、検証範囲、未対応機能は[プロジェクトREADME](../../../README.md)を参照してください。

- `src/AudioEngine.*`: PCMキュー、専用レンダラー、WASAPI再生／録音、合成、ミックス、音量カーブ、WAV書き出し。
- `src/VstHost.*`, `VstWorker.cpp`, `PluginIPC.*`: 個別プロセスのVST3ホスト、ネイティブ／汎用エディター、期限付きIPC、Job Object。
- `src/Project.h`, `PianoEditor.h`, `PluginCatalog.h`: 保存・回復・MIDI編集・入力・音源選択。
- `src/AudioTests.cpp`: 再生、制御、並行公開、書き出し、実機、録音、VSTワーカー終了の検証。
- `data/`: RML／RCSS、同梱フォント。

主ターゲットは`rmlui_sample_daw_timeline`、ワーカーは`kinu_vst_worker`です。旧UIの内部識別子にはMockという名前が残っています。