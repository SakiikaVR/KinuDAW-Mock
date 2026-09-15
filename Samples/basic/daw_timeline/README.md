# KinuUI DAW timeline mock

A native KinuUI arrange-view mock with a horizontally scrollable musical timeline and fixed track headers.

## DAW UIモック

Windows向けのDAW UIモックです。タイムライン、独立ミキサー、ピアノロール、VST3風のGUI／MIDI／パラメーター画面を備えます。実際の音声処理、音声・MIDI録音、VST3読み込みは行いません。ビルド手順は末尾を参照してください。

- Space: 再生／一時停止。Ctrl+Z／Y: クリップ編集の取り消し／やり直し（最大100回）。Ctrl+C／V: コピー／再生バー位置へ貼り付け。Ctrl+D: 選択クリップを削除。文字入力中は入力欄を優先します。
- クリップの両端をドラッグして長さを変更、中央をドラッグして移動。右クリックで切り取り・コピー・貼り付け・削除。同じカテゴリーのトラック間でコピー／貼り付けできます。Audio／instrumentsの空き部分を右クリックすると、クリックした位置にSNAPして貼り付けます。
- 録音待機ボタンは白丸OFF／赤丸ON。上部RECで待機中のトラックに録音モックのクリップを作成、STOPで終了します。MASTER以外に搭載し、追加トラックにも対応します。
- LOOPは初期OFF、初期範囲は1小節目〜2小節目の先頭。手動設定した範囲はON／OFFしても保持します。
- ミキサーのフェーダー横にL/R疑似メーターを表示。ラックの追加・削除・ON/OFFは各チャンネルで独立して保持します。

Double-click a non-Automation track header to open its independent VST3 **UI mock** window (`--plugin=<stable-track-id> --session=<owner-pid>`). GUI, MIDI and Parameters tabs use LINE Seed JP Bold. The GUI is a fictional demonstration, not a loaded plug-in. MIDI INPUT/OUTPUT have independent ON/OFF, PORT 00–15 and CH 01–16 controls; enabled output/input endpoints with identical PORT and CH show each other as connected, with no external MIDI or audio processing. Parameters are a searchable, scrollable two-column demonstration list with normalized sliders. Select an existing Automation track to assign a parameter, choose Unconnected to detach, or press + to create and assign a new Automation track. Its lane displays the source parameter and current mock value; automation curves/playback are not implemented. Shared-memory state lasts for the timeline session, survives closing/reopening the mock window, and uses stable IDs through track reorder/rename. Repeated header double-clicks reuse the open window, and owner exit closes it. Actual VST3 discovery, editors, parameters and MIDI transport are not implemented.

All arrange-view text uses the bundled LINE Seed JP Bold font, including Japanese menus and musical/numeric labels. Font sizes and timeline geometry are independent of the font family. The unmodified fonts are provided by LY Corporation under the SIL Open Font License 1.1; see `data/fonts/OFL.txt` and https://seed.line.me/index_jp.html. No system font installation is required.

The Japanese top menu bar provides File, Edit, Select, View headings. View > Mixer opens an independent native window (Windows launcher); the other headings remain mock controls. The mixer runs the same executable with `--mixer`, as the sample backend owns one window per process. A mixer opened from the timeline watches its owning process and closes when the main window exits. A directly launched `--mixer` has no owner. Reopening from the same timeline restores the existing mixer rather than spawning duplicates.

The mixer shows MASTER and all track channels in timeline order. Names and preset colors update live from the owning timeline using a session-specific atomic metadata snapshot in the system temp directory (read every 200ms). Renamed tracks keep their identity. Each channel fader supports -48 to +6 dB and double-click reset; only non-MASTER channels have M/S. On Windows, gain values synchronize bidirectionally with the timeline using session-local shared memory and stable track IDs, including MASTER. Only changed values update the UI; dragging does not write files. Only the selected channel has an accented border; clicking a channel or dragging its fader selects it. M/S values also synchronize bidirectionally through atomic shared flags, including initial state when reopening the mixer. There is no audio processing. Placeholder effects can be added, bypassed and removed. The static level meter is illustrative, not an audio signal.

## Included interactions

- `+Audio` appends an empty Audio track. On Windows, dropping a WAV/MP3/FLAC/OGG/AIFF file onto an Audio lane adds a draggable filename clip with a fixed four-bar mock length and illustrative waveform; file contents are not decoded. Select an Audio header and use REC to start/stop an expanding microphone-recording mock clip. No microphone is opened and no recording file is created. Stop also ends the recording mock.
- `+instruments` opens a separate Japanese instrument-picker window with an eight-item demonstration VST3 list, live name/category search, selection and Add/Cancel. Hearts pin favorites above other rows and retain them when the picker is reopened during the same session. Selected instruments append empty instruments tracks. The list is mock data, not an installed-plug-in scan, and no VST3 is loaded or run. The picker closes with its owning timeline.
- `+Automation` appends an empty Automation lane marked as unconnected; curve editing and parameter connections are not implemented. The mock supports up to 32 tracks including MASTER. Added tracks support existing reordering, renaming, preset colors and mixer synchronization; the track list follows vertical lane scrolling while the ruler and add buttons stay fixed.

- The mixer's upper-right pin button toggles native always-on-top on Windows. It starts off, turns yellow when enabled, and toggles off on the next click without moving or resizing the window.
- A horizontal scrollbar remains visible below mixer channels. Overflowing channels scroll without moving the Current meter or rack; faders and labels keep their fixed readable width.
- Native move/resize loops keep rendering on the UI thread, including while the mouse button remains held, rather than stretching the previous frame.

- Right-click a non-MASTER header's name/empty area for immediate inline renaming with all text selected. Enter or focus loss commits, Escape cancels, and blank names retain the previous name. Custom names follow their stable track identity when reordered and last for the running session.

- MASTER stays fixed at the top. Other headers are numbered TRACK1 onward from top to bottom, with (instruments), (Audio) or (Automation) categories. Drag a header's name/empty area vertically to reorder it with its entire lane; controls and color markers do not initiate reordering. Numbers refresh after dropping; channel state and clips stay with their stable track identity. Ordering currently lasts only for the running session.

- Compact track controls keep the 46dp row height: stereo mock meters beside a horizontal volume slider (-48 to +6 dB), a pan knob. Drag volume horizontally and pan vertically; double-click resets volume to 0 dB or pan to center. M/S, gain and pan affect the illustrative meters. Track FX buttons have been removed; the independent mixer's effects remain available. These controls are session-only UI state, not audio processing. Volume and M/S synchronize with the owning mixer on Windows; pan does not.

- Click a track's color marker to choose from nine preset colors. Marker, arrangement clips and their preview accents update together; the palette marks the current choice and dismisses on selection or an outside click. Color choices currently last for the running session only.

- Double-click an arrangement clip to open its track's independent piano-roll window. Track headers open the separate VST3 UI mock instead. Repeated opens reuse that track's existing window. The piano roll shows illustrative MIDI notes, keyboard, bar ruler and velocity values; editing, audio and synchronization with arrangement clips are not implemented yet. Run with `--piano=0` through `--piano=5` to open directly.

- Play/pause and stop transport controls
- The transport's former L/R meters are now two waveform displays, animated from playback position and flat when stopped. They are illustrative mock waveforms, not audio samples; track and mixer meters are unchanged.
- Waveforms use two custom RmlUi elements with cached GPU geometry instead of 128 per-frame layout updates. Geometry is rebuilt only when the canvas dimensions change; playback changes draw translation only.
- Drag the tempo number vertically to change BPM (20–300, 0.1 precision); upward is faster. Playback uses the current tempo and release ends the gesture.
- Ruler click-and-drag scrubbing
- On Windows, scrubbing ends on native mouse release, capture loss, cancellation or focus loss, even when release occurs outside the timeline. Subsequent hover cannot move the playhead.
- Timeline zoom from 60% to 180%
- A visible horizontal scrollbar stays at the bottom of the timeline viewport. Drag its thumb to scroll the ruler, grid, clips and playhead together while the track headers remain fixed. Its thumb follows timeline zoom and viewport resizing.
- Loop enable/disable, with a pair of opposing arrows and active-state color. Loop starts off; its range bar is visible only while enabled. Disabling loop retains the chosen range.
- Track and clip selection
- Horizontal clip dragging with 1/16-note snapping enabled by default; choose OFF, 1/16, 1/8, 1/4, 1/2, 1, 2, 4 or 8 from the SNAP menu (1 is one bar).
- Snap on/off toggle to the right of transport zoom, alongside selected-track, sample-rate and illustrative CPU readouts. There is no bottom status bar.
- Per-track mute and solo toggles

## Windows build

KinuUI's global mimalloc override must share one allocation boundary with the sample executable. Use a static KinuUI build on Windows:

```powershell
cmake -S . -B build-daw -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_SHARED_LIBS=OFF `
  -DRMLUI_SAMPLES=ON `
  -DRMLUI_BACKEND=Win32_DX11 `
  -DFreetype_ROOT="path/to/freetype"
cmake --build build-daw --config Release --target rmlui_sample_daw_timeline
./build-daw/Release/rmlui_sample_daw_timeline.exe
```

Run the executable with the repository root as its working directory so the RML, RCSS, and font assets resolve correctly.
