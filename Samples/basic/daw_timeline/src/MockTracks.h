int selected_audio_track = 3;
int mock_clip_serial = 0;
bool track_record_armed[kMaxTracks] = {};
ClipState* mock_record_clips[kMaxTracks] = {};
bool mock_recording = false;
double mock_record_start = 0;
float mock_extent_height = -1.f;

void InitialiseTrackRecordButton(int track)
{
	if (!track_automation[track]) Get(Rml::CreateString("track-header-%d", track).c_str())->SetAttribute("ondblclick", Rml::CreateString("open-plugin:%d", track));
	if (track == 5) return;
	auto* group = Get(Rml::CreateString("track-header-%d", track).c_str())->QuerySelector(".track-ms");
	if (!group) return;
	auto* button = Get(Rml::CreateString("track-record-%d", track).c_str());
	if (!button)
	{
		auto control = document->CreateElement("button");
		control->SetId(Rml::CreateString("track-record-%d", track));
		control->SetClass("track-record-arm", true);
		control->SetAttribute("onclick", Rml::CreateString("track-record-arm:%d", track));
		control->SetAttribute("ondblclick", "block-double-click");
		control->SetInnerRML("<i></i>");
		button = group->InsertBefore(std::move(control), group->GetFirstChild());
	}
	button->SetClass("track-record-arm", true);
	button->SetClass("armed", track_record_armed[track]);
	button->SetAttribute("title", track_record_armed[track] ? u8"録音待機 ON" : u8"録音待機 OFF");
}

void StopMockRecording()
{
	for (auto& clip : mock_record_clips) clip = nullptr;
	mock_recording = false;
	Get("mock-record")->SetClass("active", false);
}

void MockNotice(const Rml::String& message)
{
	SetText("mock-notice", message);
}

void RefreshMockTrackExtent()
{
	const float density = context->GetDensityIndependentPixelRatio();
	const float height = std::max(track_count * 46.f * density, Get("lane-canvas")->GetClientHeight());
	if (mock_extent_height != height)
	{
		mock_extent_height = height;
		Get("grid-overlay")->SetProperty("height", Rml::CreateString("%.0fpx", height));
		Get("playhead-line")->SetProperty("height", Rml::CreateString("%.0fpx", height));
	}
	Get("track-list")->SetScrollTop(Get("lane-canvas")->GetScrollTop());
}

int AddMockTrack(bool audio, bool automation = false, const Rml::String& instrument = "")
{
	if (track_count >= kMaxTracks) { MockNotice(u8"UIモックはMASTERを含め32トラックまでです"); return -1; }
	EndTrackRename(true); EndClipDrag();
	const int track = track_count++;
	track_audio[track] = audio;
	track_automation[track] = automation;
	track_order[track_count - 1] = track;
	track_color_presets[track] = automation ? 7 : audio ? 3 : 2;
	Rml::String markup = Get("track-header-4")->GetInnerRML();
	for (const char* token : {"-4", ":4"})
	{
		const Rml::String replacement = Rml::String(token).substr(0, 1) + std::to_string(track);
		size_t position = 0;
		while ((position = markup.find(token, position)) != Rml::String::npos) { markup.replace(position, 2, replacement); position += replacement.size(); }
	}
	auto header = document->CreateElement("div");
	header->SetClass("track-header", true);
	header->SetId(Rml::CreateString("track-header-%d", track));
	header->SetAttribute("onclick", Rml::CreateString("track:%d", track));
	header->SetAttribute("onmousedown", Rml::CreateString("track-reorder:%d", track));
	if (!automation) header->SetAttribute("ondblclick", Rml::CreateString("open-plugin:%d", track));
	header->SetInnerRML(markup);
	header->QuerySelector(".track-copy small")->SetInnerRML(automation ? "(Automation)" : audio ? "(Audio)" : "(instruments)");
	if (!instrument.empty()) header->SetAttribute("title", instrument);
	Get("track-list")->AppendChild(std::move(header));
	InitialiseTrackRecordButton(track);
	auto lane = document->CreateElement("div");
	lane->SetClass("lane", true);
	lane->SetId(Rml::CreateString("track-lane-%d", track));
	lane->SetInnerRML(Rml::CreateString("<span class=\"empty-track-hint\">%s</span>", automation ? u8"Automation / パラメーター未接続" : audio ? u8"WAVなどをドロップ / RECで録音モック" : Rml::StringUtilities::EncodeRml(instrument + " / VST3 UI mock").c_str()));
	Get("lane-canvas")->AppendChild(std::move(lane));
	ApplyTrackOrder();
	ApplyTrackColor(track, track_color_presets[track]);
	UpdateTrackKnobs(track);
	SetSelected(".track-header", Get(Rml::CreateString("track-header-%d", track).c_str()));
	SetText("selection-name", track_names[track]);
	if (audio) selected_audio_track = track;
	MockNotice(automation ? u8"Automationトラックを追加 / パラメーター未接続" : audio ? u8"Audioトラックを追加 / ファイル・録音はUIモック" : instrument + u8" を選択 / VST3は実行しません");
	context->Update();
	Get("lane-canvas")->SetScrollTop(std::max(0.f, track_count * 46.f * context->GetDensityIndependentPixelRatio() - Get("lane-canvas")->GetClientHeight()));
	RefreshMockTrackExtent();
	return track;
}

ClipState* AddMockAudioClip(int track, const Rml::String& label, float beat, float length, bool recording = false)
{
	if (track < 0 || track >= track_count || track == 5 || (!recording && !track_audio[track])) return nullptr;
	auto* lane = Get(Rml::CreateString("track-lane-%d", track).c_str());
	if (auto* hint = lane->QuerySelector(".empty-track-hint")) hint->SetProperty("display", "none");
	clips.push_back({Rml::CreateString("mock-audio-%d", ++mock_clip_serial), label, std::clamp(SnapBeat(beat), 0.f, kProjectBars * kBeatsPerBar - length), length});
	auto& state = clips.back();
	auto clip = document->CreateElement("div");
	clip->SetId(state.id); clip->SetClass("clip", true); clip->SetClass("audio", track_audio[track]);
	if (recording) clip->SetProperty("z-index", "6");
	if (!track_automation[track]) clip->SetAttribute("ondblclick", Rml::CreateString("open-piano:%d", track));
	clip->SetAttribute("onmousedown", "clip-drag:" + state.id);
	clip->SetAttribute("onclick", "clip:" + label);
	clip->SetInnerRML("<b>" + Rml::StringUtilities::EncodeRml(label) + u8"</b><span class=\"waveform\">▁▃▆▂▄▇▃▅▂▆▁▃▅▇▃▂▅▁▃▆▂▄▇▃▅▂</span>");
	lane->AppendChild(std::move(clip));
	ApplyTrackColor(track, track_color_presets[track]); UpdateClipGeometry(state);
	return &state;
}

void ToggleMockRecording()
{
	if (mock_recording)
	{
		StopMockRecording(); playing = false;
		Get("play-button")->SetClass("active", false); SetText("play-label", "PLAY");
		MockNotice(u8"録音モックを終了 / 音声ファイルは生成しません");
		return;
	}
	int armed = 0;
	for (int track = 0; track < track_count; ++track) if (track != 5 && track_record_armed[track]) ++armed;
	if (!armed) { MockNotice(u8"録音するトラックの白丸をクリックして赤くしてください"); return; }
	SaveClipUndo(); armed = 0;
	for (int track = 0; track < track_count; ++track)
	{
		if (track == 5 || !track_record_armed[track]) continue;
		mock_record_clips[track] = AddMockAudioClip(track, track_automation[track] ? u8"Automation録音（モック）" : track_audio[track] ? u8"マイク録音（モック）" : u8"MIDI録音（モック）", playhead_beat, .25f, true);
		if (mock_record_clips[track]) ++armed;
	}
	if (!armed) { MockNotice(u8"録音するトラックの白丸をクリックして赤くしてください"); return; }
	mock_recording = true;
	mock_record_start = Rml::GetSystemInterface()->GetElapsedTime();
	playing = true; Get("mock-record")->SetClass("active", true);
	Get("play-button")->SetClass("active", true); SetText("play-label", "PAUSE");
	MockNotice(u8"録音モック中 / マイクにはアクセスしていません / RECで終了");
}

void UpdateMockTracks()
{
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume)
	{
		const int choice = InterlockedExchange(shared_volume + kMaxTracks * 2, 0) - 1;
		if (choice >= 0 && choice < kMockInstrumentCount) AddMockTrack(false, false, mock_instruments[choice].name);
	}
	for (const auto& drop : mock_audio_drops)
	{
		const auto& path = drop.first;
		Rml::String extension = Rml::StringUtilities::ToLower(path.extension().u8string());
		if (extension != ".wav" && extension != ".mp3" && extension != ".flac" && extension != ".ogg" && extension != ".aif" && extension != ".aiff") { MockNotice(u8"WAV / MP3 / FLAC / OGG / AIFFのファイルを選んでください"); continue; }
		int target = -1;
		for (int track = 0; track < track_count; ++track)
		{
			auto* lane = Get(Rml::CreateString("track-lane-%d", track).c_str());
			if (drop.second.y >= lane->GetAbsoluteTop() && drop.second.y < lane->GetAbsoluteTop() + lane->GetOffsetHeight()) { if (track_audio[track]) target = track; break; }
		}
		if (target < 0) { MockNotice(u8"Audioトラックの行にドロップしてください"); continue; }
		auto* lane = Get(Rml::CreateString("track-lane-%d", target).c_str());
		SaveClipUndo();
		AddMockAudioClip(target, path.filename().u8string(), (drop.second.x - lane->GetAbsoluteLeft()) / pixels_per_beat, 16.f);
		MockNotice(u8"ファイル名をクリップ表示 / 長さ・波形はモックです");
	}
	mock_audio_drops.clear();
	if (instruments_process && WaitForSingleObject(instruments_process, 0) == WAIT_OBJECT_0)
	{
		CloseHandle(instruments_process); instruments_process = nullptr;
	}
#endif
	for (auto* clip : mock_record_clips)
	{
		if (!clip) continue;
		clip->length_beats = std::clamp(float((Rml::GetSystemInterface()->GetElapsedTime() - mock_record_start) * bpm / 60.f), .25f, kProjectBars * kBeatsPerBar - clip->start_beat);
		UpdateClipGeometry(*clip);
	}
	RefreshMockTrackExtent();
}
