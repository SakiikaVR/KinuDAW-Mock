// Session-only demonstration state. No VST loading, MIDI devices or audio processing.
constexpr int kPluginParams = 12;
const char* plugin_param_names[kPluginParams] = {"Depth", "Time", "In Gain", "Out Gain", "Clean XOV", "Thresh L", "Thresh M", "Thresh H", "Upward", "Downward", "Mix", "Release"};
struct PluginMockState { LONG route[kMaxTracks][6]; LONG value[kMaxTracks][kPluginParams]; LONG binding[kMaxTracks][kPluginParams]; LONG request; };
#if defined RMLUI_PLATFORM_WIN32
HANDLE plugin_mapping = nullptr;
PluginMockState* plugin_shared = nullptr;
#endif
Rml::String plugin_track_labels[kMaxTracks];
bool plugin_track_exists[kMaxTracks] = {}, plugin_track_is_auto[kMaxTracks] = {};
int plugin_tab = 0, plugin_drag_param = -1;
double plugin_next_poll = 0;
int plugin_last_values[kPluginParams], plugin_last_bindings[kPluginParams];
Rml::String plugin_last_metadata, plugin_last_connections;

LONG PluginRead(LONG* value) { return InterlockedCompareExchange(value, 0, 0); }
void InitialisePluginState()
{
	const auto name = L"Local\\KinuDawPluginMock-v1-" + std::to_wstring(mixer_session_id);
	plugin_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(PluginMockState), name.c_str());
	const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
	if (plugin_mapping) plugin_shared = static_cast<PluginMockState*>(MapViewOfFile(plugin_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PluginMockState)));
	if (plugin_shared && fresh)
		for (int track = 0; track < kMaxTracks; ++track)
			for (int param = 0; param < kPluginParams; ++param) plugin_shared->value[track][param] = param == 0 || param == 10 ? 1000 : param == 4 ? 0 : 500;
}

void ClosePluginState()
{
	if (plugin_shared) UnmapViewOfFile(plugin_shared);
	plugin_shared = nullptr;
	if (plugin_mapping) CloseHandle(plugin_mapping);
	plugin_mapping = nullptr;
}

void PluginSetTab(int tab)
{
	plugin_tab = tab;
	for (int i = 0; i < 3; ++i)
	{
		Get(Rml::CreateString("plugin-tab-%d", i).c_str())->SetClass("active", i == tab);
		Get(Rml::CreateString("plugin-page-%d", i).c_str())->SetProperty("display", i == tab ? "flex" : "none");
	}
}

void PluginSearch()
{
	const auto query = Rml::StringUtilities::ToLower(static_cast<Rml::ElementFormControlInput*>(Get("plugin-search"))->GetValue());
	int found = 0;
	for (int param = 0; param < kPluginParams; ++param)
	{
		const bool match = Rml::StringUtilities::ToLower(plugin_param_names[param]).find(query) != Rml::String::npos;
		Get(Rml::CreateString("plugin-param-%d", param).c_str())->SetProperty("display", match ? "block" : "none");
		found += match;
	}
	Get("plugin-param-empty")->SetProperty("display", found ? "none" : "block");
}

void PluginMoveParameter(Rml::Event& event)
{
	if (!plugin_shared || plugin_drag_param < 0) return;
	auto* rail = Get(Rml::CreateString("plugin-param-rail-%d", plugin_drag_param).c_str());
	const float ratio = (event.GetParameter("mouse_x", 0.f) - rail->GetAbsoluteLeft()) / std::max(1.f, rail->GetClientWidth());
	InterlockedExchange(&plugin_shared->value[plugin_track][plugin_drag_param], LONG(std::round(std::clamp(ratio, 0.f, 1.f) * 1000.f)));
}

void PluginReadMetadata()
{
	std::ifstream file(MixerSessionPath(), std::ios::binary);
	if (!file) return;
	std::ostringstream stream; stream << file.rdbuf();
	if (stream.str() == plugin_last_metadata) return;
	std::istringstream data(stream.str());
	int count; if (!(data >> count) || count < 6 || count > kMaxTracks) return;
	Rml::String labels[kMaxTracks]; bool exists[kMaxTracks] = {}, automation[kMaxTracks] = {};
	for (int row = 0; row < count; ++row)
	{
		int track, preset; std::string name; bool audio, is_auto;
		if (!(data >> track >> preset >> std::quoted(name) >> audio >> is_auto) || track < 0 || track >= count) return;
		labels[track] = name; exists[track] = true; automation[track] = is_auto;
	}
	for (int track = 0; track < kMaxTracks; ++track) { plugin_track_labels[track] = labels[track]; plugin_track_exists[track] = exists[track]; plugin_track_is_auto[track] = automation[track]; }
	SetText("plugin-track-title", labels[plugin_track]);
	for (int param = 0; param < kPluginParams; ++param)
	{
		Rml::String options = u8"<option value=\"0\">未接続</option>";
		for (int track = 0; track < count; ++track) if (automation[track])
			options += Rml::CreateString("<option value=\"%d\">%s</option>", track + 1, Rml::StringUtilities::EncodeRml(labels[track]).c_str());
		auto* select = Get(Rml::CreateString("plugin-binding-%d", param).c_str());
		select->SetInnerRML(options);
		plugin_last_bindings[param] = -1;
	}
	plugin_last_metadata = stream.str();
}

void UpdatePluginWindow()
{
	const double time = Rml::GetSystemInterface()->GetElapsedTime();
	if (time < plugin_next_poll || !plugin_shared) return;
	plugin_next_poll = time + .1;
	PluginReadMetadata();
	Rml::String status;
	for (int side = 0; side < 2; ++side)
	{
		const int base = side * 3;
		const LONG enabled = PluginRead(&plugin_shared->route[plugin_track][base]);
		const LONG port = PluginRead(&plugin_shared->route[plugin_track][base + 1]);
		const LONG ch = PluginRead(&plugin_shared->route[plugin_track][base + 2]);
		auto* toggle = Get(Rml::CreateString("plugin-route-on-%d", side).c_str());
		toggle->SetClass("active", enabled != 0); SetText(toggle->GetId().c_str(), enabled ? "ON" : "OFF");
		SetText(Rml::CreateString("plugin-port-%d", side).c_str(), Rml::CreateString("%02d", int(port)));
		SetText(Rml::CreateString("plugin-ch-%d", side).c_str(), Rml::CreateString("%02d", int(ch + 1)));
		Rml::String peers;
		for (int track = 0; track < kMaxTracks; ++track)
		{
			const int opposite = (1 - side) * 3;
			if (track == plugin_track || !plugin_track_exists[track] || !enabled) continue;
			if (PluginRead(&plugin_shared->route[track][opposite]) && PluginRead(&plugin_shared->route[track][opposite + 1]) == port && PluginRead(&plugin_shared->route[track][opposite + 2]) == ch)
			{ if (!peers.empty()) peers += ", "; peers += plugin_track_labels[track]; }
		}
		status += (side ? "OUTPUT: " : "INPUT: ") + (peers.empty() ? enabled ? Rml::String(u8"接続先なし") : Rml::String("OFF") : peers) + "\n";
	}
	if (status != plugin_last_connections) { SetText("plugin-connections", status); plugin_last_connections = status; }
	for (int param = 0; param < kPluginParams; ++param)
	{
		const int value = PluginRead(&plugin_shared->value[plugin_track][param]);
		if (plugin_last_values[param] != value)
		{
			SetText(Rml::CreateString("plugin-param-value-%d", param).c_str(), Rml::CreateString("%.3f", value / 1000.f));
			Get(Rml::CreateString("plugin-param-fill-%d", param).c_str())->SetProperty("width", Rml::CreateString("%.1f%%", value / 10.f));
			Get(Rml::CreateString("plugin-param-thumb-%d", param).c_str())->SetProperty("left", Rml::CreateString("%.1f%%", value / 10.f));
			if (param < 4) SetText(Rml::CreateString("plugin-gui-value-%d", param).c_str(), Rml::CreateString("%.1f %%", value / 10.f));
			plugin_last_values[param] = value;
		}
		const int binding = PluginRead(&plugin_shared->binding[plugin_track][param]);
		if (plugin_last_bindings[param] != binding)
		{
			static_cast<Rml::ElementFormControl*>(Get(Rml::CreateString("plugin-binding-%d", param).c_str()))->SetValue(std::to_string(binding));
			plugin_last_bindings[param] = binding;
		}
	}
}

void InitialisePluginWindow()
{
	SetText("plugin-track-title", Rml::CreateString("TRACK%d", plugin_track + 1));
	for (int param = 0; param < kPluginParams; ++param)
	{
		auto card = document->CreateElement("div"); card->SetClass("parameter-card", true); card->SetId(Rml::CreateString("plugin-param-%d", param));
		card->SetInnerRML(Rml::CreateString("<div class=\"parameter-label\"><b>%s</b><span id=\"plugin-param-value-%d\">0.000</span></div><div id=\"plugin-param-rail-%d\" class=\"parameter-rail\" onmousedown=\"plugin-param-begin:%d\"><i id=\"plugin-param-fill-%d\" class=\"parameter-fill\"></i><i id=\"plugin-param-thumb-%d\" class=\"parameter-thumb\"></i></div><div class=\"binding-row\"><select id=\"plugin-binding-%d\" onchange=\"plugin-bind:%d\"><option value=\"0\">未接続</option></select><button onclick=\"plugin-auto:%d\" title=\"Automationトラックを作成して接続\">＋</button></div>", plugin_param_names[param], param, param, param, param, param, param, param, param));
		Get("plugin-parameters")->AppendChild(std::move(card));
		plugin_last_values[param] = plugin_last_bindings[param] = -1;
	}
	PluginSetTab(0); UpdatePluginWindow();
}
