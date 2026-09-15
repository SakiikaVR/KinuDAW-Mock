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
	if(mock_recording) FinishRecording();
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
	if (track_count >= kMaxTracks) { MockNotice(u8"MASTERを含め32トラックまでです"); return -1; }
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
	if(!audio && !automation) lane->SetAttribute("ondblclick",Rml::CreateString("open-piano:%d",track));
	lane->SetInnerRML(Rml::CreateString("<span class=\"empty-track-hint\">%s</span>", automation ? "Automation" : audio ? u8"音声をドロップ / RECで録音" : Rml::StringUtilities::EncodeRml(instrument + u8" / ダブルクリックでMIDI編集").c_str()));
	Get("lane-canvas")->AppendChild(std::move(lane));
	ApplyTrackOrder();
	ApplyTrackColor(track, track_color_presets[track]);
	UpdateTrackKnobs(track);
	SetSelected(".track-header", Get(Rml::CreateString("track-header-%d", track).c_str()));
	SetText("selection-name", track_names[track]);
	if (audio) selected_audio_track = track;
	MockNotice(automation ? u8"Automationトラックを追加" : audio ? u8"Audioトラックを追加" : instrument+u8" を読み込み中");
	context->Update();
	Get("lane-canvas")->SetScrollTop(std::max(0.f, track_count * 46.f * context->GetDensityIndependentPixelRatio() - Get("lane-canvas")->GetClientHeight()));
	RefreshMockTrackExtent();
	return track;
}

ClipState* AddMockAudioClip(int track, const Rml::String& label, float beat, float length, bool recording = false)
{
	if(clips.size()>=Kinu::MaxClips) { MockNotice(u8"クリップ数の上限（256）に達しました"); return nullptr; }
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
	clip->SetInnerRML("<b>" + Rml::StringUtilities::EncodeRml(label) + "</b>");
	lane->AppendChild(std::move(clip));
	ApplyTrackColor(track, track_color_presets[track]); UpdateClipGeometry(state);
	return &state;
}

void ToggleMockRecording() { ToggleRecording(); }

void UpdateMockTracks()
{
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume)
	{
		int effectTarget=InterlockedExchange(shared_volume+kMixerVstRequest,0)-1;
		if(effectTarget>=0 && effectTarget<track_count) { int previous=track_fx_open; track_fx_open=effectTarget; AddVstEffect(); track_fx_open=previous; }
		const int choice = InterlockedExchange(shared_volume + kMaxTracks * 2, 0) - 1;
		if (choice >= 0 && choice < kMockInstrumentCount && audio_engine) {
			int target=InterlockedExchange(shared_volume+kMaxTracks*2+1,0)-1;
			int slot=std::clamp(int(InterlockedExchange(shared_volume+kMaxTracks*3+2,0)),0,3);
			const auto& info=installed_plugins[choice];
			if(target<0) target=AddMockTrack(info.category.find("Instrument")==std::string::npos,false,info.name);
			std::string error;
			if(target>=0 && audio_engine->loadPlugin(target,info,error,slot)) {
				if(!slot) project_plugins[target]=info; else project_effects[target][slot-1].info=info;
				try { if(!slot) cached_plugin_states[target]=audio_engine->pluginState(target); else project_effects[target][slot-1].state=audio_engine->pluginState(target,slot); } catch(...) {}
				MockNotice(info.name+u8" を別プロセスで読み込みました");
			} else MockNotice(error);
		}
	}
	for (const auto& drop : mock_audio_drops)
	{
		const auto& path = drop.first;
		Rml::String extension = Rml::StringUtilities::ToLower(path.extension().u8string());
		if (extension != ".wav" && extension != ".mp3" && extension != ".flac") { MockNotice(u8"WAV / MP3 / FLACのファイルを選んでください"); continue; }
		int target = -1;
		for (int track = 0; track < track_count; ++track)
		{
			auto* lane = Get(Rml::CreateString("track-lane-%d", track).c_str());
			if (drop.second.y >= lane->GetAbsoluteTop() && drop.second.y < lane->GetAbsoluteTop() + lane->GetOffsetHeight()) { if (track_audio[track]) target = track; break; }
		}
		if (target < 0) { MockNotice(u8"Audioトラックの行にドロップしてください"); continue; }
		auto* lane = Get(Rml::CreateString("track-lane-%d", target).c_str());
		ImportAudioFile(target,path,(drop.second.x-lane->GetAbsoluteLeft())/pixels_per_beat);
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
