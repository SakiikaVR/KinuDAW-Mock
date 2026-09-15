int mixer_drag_channel = -1;
int mixer_selected_channel = 5;
std::string mixer_metadata;
double mixer_next_read = 0;
double mixer_next_meter = 0;
int mixer_meter_heights[kMaxTracks][2] = {};
struct MixerEffect { int id; int type; bool enabled = true; };
struct MixerRack { std::vector<MixerEffect> effects; int next_type = 0; float scroll_top = 0; };
MixerRack mixer_racks[kMaxTracks]; // Stable track IDs, independent of strip order/name.
int mixer_next_effect_id = 0;
const char* mixer_effect_names[] = {"EQ", "Compressor", "Reverb"};

void AppendMixerEffect(const MixerEffect& effect)
{
	auto row = document->CreateElement("div");
	row->SetClass("effect-row", true);
	row->SetId(Rml::CreateString("effect-%d", effect.id));
	row->SetAttribute("effect-id", effect.id);
	row->SetInnerRML(Rml::CreateString("<button class=\"effect-toggle%s\" onclick=\"toggle:effect\">%s</button><b>%s</b><button class=\"effect-remove\" onclick=\"mixer-remove-effect\">X</button>", effect.enabled ? " active" : "", effect.enabled ? "ON" : "OFF", mixer_effect_names[effect.type]));
	Get("mixer-effects")->AppendChild(std::move(row));
}

void UpdateMixerRackLabel()
{
	if (auto* name = Get(Rml::CreateString("mix-name-%d", mixer_selected_channel).c_str()))
		SetText("rack-channel-name", name->GetInnerRML());
}

void ShowMixerRack()
{
	UpdateMixerRackLabel();
	Get("mixer-effects")->SetInnerRML("");
	const auto& rack = mixer_racks[mixer_selected_channel];
	for (const auto& effect : rack.effects) AppendMixerEffect(effect);
	Get("effects-empty")->SetProperty("display", rack.effects.empty() ? "block" : "none");
	Get("rack-scroll")->SetScrollTop(rack.scroll_top);
}

void AddMixerEffect()
{
	auto& rack = mixer_racks[mixer_selected_channel];
	rack.effects.push_back({++mixer_next_effect_id, rack.next_type++ % 3, true});
	AppendMixerEffect(rack.effects.back());
	Get("effects-empty")->SetProperty("display", "none");
}

void RemoveMixerEffect(Rml::Element* row)
{
	if (!row || row->GetParentNode() != Get("mixer-effects")) return;
	auto& effects = mixer_racks[mixer_selected_channel].effects;
	const int id = row->GetAttribute<int>("effect-id", -1);
	effects.erase(std::remove_if(effects.begin(), effects.end(), [id](const MixerEffect& effect) { return effect.id == id; }), effects.end());
	Get("mixer-effects")->RemoveChild(row);
	Get("effects-empty")->SetProperty("display", effects.empty() ? "block" : "none");
}

void ToggleMixerEffect(Rml::Element* button)
{
	if (!button || !button->GetParentNode()) return;
	const int id = button->GetParentNode()->GetAttribute<int>("effect-id", -1);
	for (auto& effect : mixer_racks[mixer_selected_channel].effects)
		if (effect.id == id)
		{
			effect.enabled = !effect.enabled;
			button->SetClass("active", effect.enabled);
			button->SetInnerRML(effect.enabled ? "ON" : "OFF");
			break;
		}
}

// Illustrative UI-only levels, not measurements of an audio engine.
void UpdateMixerMeters(double time)
{
	if (time < mixer_next_meter) return;
	mixer_next_meter = time + 1.0 / 30.0;
	bool any_solo = false;
	for (int track = 0; track < track_count; ++track)
		if (track != 5 && !track_automation[track]) any_solo |= track_channels[track].solo;
	float levels[kMaxTracks][2] = {};
	int audible = 0;
	for (int track = 0; track < track_count; ++track)
	{
		const auto& channel = track_channels[track];
		if (track == 5 || track_automation[track] || channel.muted || (any_solo && !channel.solo)) continue;
		++audible;
		for (int side = 0; side < 2; ++side)
		{
			const float pulse = .55f + .18f * float(std::sin(time * 3.2 + track * 1.7 + side * .65));
			levels[track][side] = std::clamp(pulse * std::pow(10.f, channel.gain_db / 20.f), 0.f, 1.f);
			levels[5][side] += levels[track][side];
		}
	}
	for (int side = 0; side < 2; ++side)
		levels[5][side] = track_channels[5].muted || !audible ? 0.f : std::clamp(levels[5][side] / audible * std::pow(10.f, track_channels[5].gain_db / 20.f), 0.f, 1.f);
	for (int track = 0; track < track_count; ++track)
		for (int side = 0; side < 2; ++side)
		{
			const int height = int(std::round(levels[track][side] * 100));
			if (height == mixer_meter_heights[track][side]) continue;
			if (auto* bar = Get(Rml::CreateString("mix-meter-%d-%d", track, side).c_str()))
				bar->SetProperty("height", Rml::CreateString("%d%%", height));
			mixer_meter_heights[track][side] = height;
		}
}

std::filesystem::path MixerSessionPath()
{
	return std::filesystem::temp_directory_path() / ("kinu-daw-tracks-" + std::to_string(mixer_session_id) + ".txt");
}

void PublishMixerTracks()
{
	std::ostringstream data;
	data << track_count << '\n';
	for (int row = 0; row < track_count; ++row)
	{
		const int track = track_order[row];
		data << track << ' ' << track_color_presets[track] << ' ' << std::quoted(track_names[track]) << ' ' << track_audio[track] << ' ' << track_automation[track] << '\n';
	}
	const std::string value = data.str();
	if (value == mixer_metadata) return;
	try
	{
		const auto path = MixerSessionPath();
		auto pending = path; pending += ".new";
		{ std::ofstream file(pending, std::ios::binary); file << value; if (!file) return; }
#if defined RMLUI_PLATFORM_WIN32
		if (!MoveFileExW(pending.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
#else
		std::filesystem::rename(pending, path);
#endif
		mixer_metadata = value;
	}
	catch (const std::filesystem::filesystem_error&) {}
}

void UpdateMixerChannel(int track)
{
	UpdateTrackSwitches(track);
	SetText(Rml::CreateString("mix-gain-%d", track).c_str(), Rml::CreateString("%.1f dB", track_channels[track].gain_db));
	Get(Rml::CreateString("mix-thumb-%d", track).c_str())->SetProperty("top", Rml::CreateString("%.2f%%", (6.f - track_channels[track].gain_db) / 54.f * 100.f));
}

void SelectMixerChannel(int track)
{
	if (track < 0 || track >= track_count) return;
	const bool changed = mixer_selected_channel != track;
	if (changed) mixer_racks[mixer_selected_channel].scroll_top = Get("rack-scroll")->GetScrollTop();
	mixer_selected_channel = track;
	for (int channel = 0; channel < track_count; ++channel)
	{
		auto* strip = Get(Rml::CreateString("mix-strip-%d", channel).c_str());
		if (!strip) continue;
		strip->SetClass("selected", channel == track);
		strip->SetProperty("border-color", channel == track ? color_presets[track_color_presets[channel]].preview_color : "#303840");
	}
	if (changed) ShowMixerRack();
	else UpdateMixerRackLabel();
}

void MoveMixerChannel(Rml::Event& event)
{
	if (mixer_drag_channel < 0) return;
	auto* rail = Get(Rml::CreateString("mix-rail-%d", mixer_drag_channel).c_str());
	const float ratio = (event.GetParameter("mouse_y", rail->GetAbsoluteTop()) - rail->GetAbsoluteTop()) / std::max(1.f, rail->GetClientHeight());
	SetTrackGain(mixer_drag_channel, 6.f - ratio * 54.f);
	UpdateMixerChannel(mixer_drag_channel);
}

void CreateMixerStrip(int track);

void ReadMixerTracks()
{
	const double time = Rml::GetSystemInterface()->GetElapsedTime();
	if (time < mixer_next_read) return;
	mixer_next_read = time + .2;
	try
	{
		std::ifstream file(MixerSessionPath(), std::ios::binary);
		if (!file) return;
		std::ostringstream stream; stream << file.rdbuf();
		const std::string value = stream.str();
		if (value == mixer_metadata) return;
		std::istringstream data(value);
		int count = 0;
		if (!(data >> count) || count < 6 || count > kMaxTracks) return;
		int order[kMaxTracks], presets[kMaxTracks]; std::string names[kMaxTracks]; bool seen[kMaxTracks] = {}, audio[kMaxTracks] = {}, automation[kMaxTracks] = {};
		for (int row = 0; row < count; ++row)
		{
			if (!(data >> order[row] >> presets[row] >> std::quoted(names[row]) >> audio[row] >> automation[row])) return;
			if (order[row] < 0 || order[row] >= count || seen[order[row]] || presets[row] < 0 || presets[row] >= 9 || names[row].size() > 256) return;
			seen[order[row]] = true;
		}
		if (order[0] != 5) return;
		track_count = count;
		for (int row = 0; row < track_count; ++row)
		{
			const int track = order[row];
			track_order[row] = track;
			track_audio[track] = audio[row];
			track_automation[track] = automation[row];
			if (!Get(Rml::CreateString("mix-strip-%d", track).c_str())) { track_names[track] = "TRACK"; CreateMixerStrip(track); }
			auto* strip = Get(Rml::CreateString("mix-strip-%d", track).c_str());
			SetText(Rml::CreateString("mix-name-%d", track).c_str(), names[row]);
			track_color_presets[track] = presets[row];
			strip->SetProperty("border-color", track == mixer_selected_channel ? color_presets[presets[row]].preview_color : "#303840");
			Get(Rml::CreateString("mix-thumb-%d", track).c_str())->SetProperty("background", color_presets[presets[row]].preview_color);
			Get("mixer-channels")->AppendChild(Get("mixer-channels")->RemoveChild(strip));
		}
		Get("mixer-channels")->SetProperty("width", Rml::CreateString("%ddp", track_count * 84 - 8));
		UpdateMixerRackLabel();
		mixer_metadata = value;
	}
	catch (const std::filesystem::filesystem_error&) {}
}

void CreateMixerStrip(int track)
{
	auto strip = document->CreateElement("section");
	strip->SetClass("master-strip", true);
	strip->SetId(Rml::CreateString("mix-strip-%d", track));
	strip->SetAttribute("onclick", Rml::CreateString("mixer-select:%d", track));
	const Rml::String controls = track == 5 ? "<div class=\"channel-controls\"></div>" : Rml::CreateString("<div class=\"channel-controls\"><button id=\"mix-mute-%d\" onclick=\"mixer-channel-mute:%d\">M</button><button id=\"mix-solo-%d\" onclick=\"mixer-channel-solo:%d\">S</button></div>", track, track, track, track);
	strip->SetInnerRML(Rml::CreateString("<b id=\"mix-name-%d\" class=\"channel-name\">%s</b><b id=\"mix-gain-%d\" class=\"gain-value\">0.0 dB</b>%s<div class=\"channel-fader-area\"><div id=\"mix-rail-%d\" class=\"fader-rail\" onmousedown=\"mixer-channel-begin:%d\" ondblclick=\"mixer-channel-reset:%d\"><div class=\"fader-groove\"></div><div class=\"unity-line\"></div><div id=\"mix-thumb-%d\" class=\"fader-thumb\"><i></i></div></div><div class=\"channel-meter\" title=\"L/R meter (UI mock)\"><div class=\"channel-meter-column\"><i id=\"mix-meter-%d-0\" class=\"channel-meter-fill\"></i></div><div class=\"channel-meter-column\"><i id=\"mix-meter-%d-1\" class=\"channel-meter-fill\"></i></div></div></div><small>%s</small>", track, track_names[track], track, controls.c_str(), track, track, track, track, track, track, track == 5 ? "Stereo Out" : track_automation[track] ? "Automation" : track_audio[track] ? "Audio" : "instruments"));
	strip->SetProperty("border-color", color_presets[track_color_presets[track]].preview_color);
	Get("mixer-channels")->AppendChild(std::move(strip));
	UpdateMixerChannel(track);
}

void InitialiseMixerTracks()
{
	for (int row = 0; row < track_count; ++row) CreateMixerStrip(track_order[row]);
	ReadMixerTracks();
	SelectMixerChannel(mixer_selected_channel);
}

void ReadVolumeSync()
{
#if defined RMLUI_PLATFORM_WIN32
	if (!shared_volume) return;
	for (int track = 0; track < track_count; ++track)
	{
		const float gain = std::clamp(InterlockedCompareExchange(shared_volume + track, 0, 0) / 10.f, -48.f, 6.f);
		if (track_channels[track].gain_db != gain)
		{
			track_channels[track].gain_db = gain;
			if (mixer_window) UpdateMixerChannel(track);
			else UpdateTrackKnobs(track);
		}
		const LONG flags = InterlockedCompareExchange(shared_volume + kMaxTracks + track, 0, 0);
		const bool mute = (flags & 1) != 0, solo = (flags & 2) != 0;
		if (track_channels[track].muted != mute || track_channels[track].solo != solo)
		{
			track_channels[track].muted = mute;
			track_channels[track].solo = solo;
			UpdateTrackSwitches(track);
		}
	}
#endif
}
