struct SavedClip { ClipState state; int track; Rml::String markup; Rml::String classes; };
struct ClipSnapshot { std::vector<SavedClip> clips; Rml::String selected; };
std::vector<ClipSnapshot> clip_undo, clip_redo;
SavedClip clip_clipboard;
bool clip_clipboard_valid = false;
int clip_paste_serial = 0;
int clip_paste_track = -1;

int ClipTrack(Rml::Element* element)
{
	if (!element || !element->GetParentNode()) return -1;
	const auto& id = element->GetParentNode()->GetId();
	return id.find("track-lane-") == 0 ? std::atoi(id.c_str() + 11) : -1;
}

ClipSnapshot CaptureClips()
{
	ClipSnapshot snapshot;
	for (const auto& state : clips)
		if (auto* element = Get(state.id.c_str()))
		{
			snapshot.clips.push_back({state, ClipTrack(element), element->GetInnerRML(), element->GetClassNames()});
			if (element->IsClassSet("selected")) snapshot.selected = state.id;
		}
	return snapshot;
}

void SaveClipUndo(ClipSnapshot snapshot)
{
	clip_undo.push_back(std::move(snapshot));
	if (clip_undo.size() > 100) clip_undo.erase(clip_undo.begin());
	clip_redo.clear();
}

void SaveClipUndo() { SaveClipUndo(CaptureClips()); }

Rml::Element* CreateSavedClip(const SavedClip& saved)
{
	auto element = document->CreateElement("div");
	element->SetId(saved.state.id);
	element->SetAttribute("class", saved.classes);
	element->SetClass("clip", true); element->SetClass("dragging", false); element->SetClass("selected", false);
	element->SetAttribute("onmousedown", "clip-drag:" + saved.state.id);
	element->SetAttribute("onclick", "clip:" + saved.state.label);
	if (!track_automation[saved.track]) element->SetAttribute("ondblclick", Rml::CreateString("open-piano:%d", saved.track));
	element->SetInnerRML(saved.markup);
	element->SetProperty("z-index", "6");
	auto* result = element.get();
	Get(Rml::CreateString("track-lane-%d", saved.track).c_str())->AppendChild(std::move(element));
	return result;
}

void RestoreClips(const ClipSnapshot& snapshot)
{
	EndClipDrag(); StopMockRecording();
	Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
	for (auto it = clips.begin(); it != clips.end();)
	{
		const auto found = std::find_if(snapshot.clips.begin(), snapshot.clips.end(), [&](const SavedClip& saved) { return saved.state.id == it->id; });
		if (found == snapshot.clips.end())
		{
			if (auto* element = Get(it->id.c_str())) element->GetParentNode()->RemoveChild(element);
			it = clips.erase(it);
		}
		else ++it;
	}
	for (const auto& saved : snapshot.clips)
	{
		auto* state = FindClip(saved.state.id);
		if (!state) { clips.push_back(saved.state); state = &clips.back(); CreateSavedClip(saved); }
		else {
			*state = saved.state;
			if(auto* element=Get(saved.state.id.c_str())) {
				if(ClipTrack(element)!=saved.track) { auto moved=element->GetParentNode()->RemoveChild(element); Get(Rml::CreateString("track-lane-%d",saved.track).c_str())->AppendChild(std::move(moved)); }
				element->SetInnerRML(saved.markup); element->SetAttribute("class",saved.classes); element->SetClass("dragging",false);
			}
		}
		UpdateClipGeometry(*state);
	}
	SetSelected(".clip", Get(snapshot.selected.c_str()));
	SetText("selection-name", snapshot.selected.empty() ? "—" : FindClip(snapshot.selected)->label);
	for (int track = 0; track < track_count; ++track)
	{
		ApplyTrackColor(track, track_color_presets[track]);
		auto* lane = Get(Rml::CreateString("track-lane-%d", track).c_str());
		if (auto* hint = lane->QuerySelector(".empty-track-hint")) hint->SetProperty("display", lane->QuerySelector(".clip") ? "none" : "block");
	}
}

void UndoClips(bool redo)
{
	EndClipDrag();
	auto& source = redo ? clip_redo : clip_undo;
	auto& destination = redo ? clip_undo : clip_redo;
	if (source.empty()) return;
	destination.push_back(CaptureClips());
	auto snapshot = std::move(source.back()); source.pop_back();
	RestoreClips(snapshot);
	for(int t=0;t<kMaxTracks;++t) if(piano_processes[t] && WaitForSingleObject(piano_processes[t],0)==WAIT_TIMEOUT) PreparePiano(t);
}

void CopyClip()
{
	const auto snapshot = CaptureClips();
	for (const auto& saved : snapshot.clips)
		if (saved.state.id == snapshot.selected) { clip_clipboard = saved; clip_clipboard_valid = true; clip_paste_track = saved.track; break; }
}

bool CanPasteClip(int target)
{
	if (!clip_clipboard_valid || target < 0 || target >= track_count) return false;
	const int source = clip_clipboard.track;
	if (source < 0 || source >= track_count) return false;
	if (source == 5 || target == 5) return source == target;
	return track_audio[source] == track_audio[target] && track_automation[source] == track_automation[target];
}

void PasteClip(float destination_beat = -1.f)
{
	if (!CanPasteClip(clip_paste_track)) return;
	EndClipDrag(); SaveClipUndo();
	auto saved = clip_clipboard;
	saved.track = clip_paste_track;
	saved.state.id = Rml::CreateString("pasted-clip-%d", ++clip_paste_serial);
	saved.state.start_beat = std::clamp(SnapBeat(destination_beat < 0.f ? playhead_beat : destination_beat), 0.f, kProjectBars * kBeatsPerBar - saved.state.length_beats);
	clips.push_back(saved.state);
	auto* element = CreateSavedClip(saved);
	ApplyTrackColor(saved.track, track_color_presets[saved.track]);
	UpdateClipGeometry(clips.back()); SetSelected(".clip", element); SetText("selection-name", saved.state.label);
	if (auto* hint = element->GetParentNode()->QuerySelector(".empty-track-hint")) hint->SetProperty("display", "none");
}
