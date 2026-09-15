// Demonstration library: no installed VST scanning or plug-in execution.
struct MockInstrument { const char* name; const char* category; };
const MockInstrument mock_instruments[] = {
	{"Kinu Keys", "Piano"}, {"Velvet Pad", "Synth"}, {"Orbit Synth", "Synth"},
	{"Pulse Bass", "Bass"}, {"Paper Drums", "Drums"}, {"Glass Bells", "Mallet"},
	{"Bloom Strings", "Strings"}, {"Cloud Sampler", "Sampler"}
};
constexpr int kMockInstrumentCount = sizeof(mock_instruments) / sizeof(mock_instruments[0]);
bool instrument_favorites[kMockInstrumentCount] = {};
int instrument_selected = -1;

std::filesystem::path InstrumentFavoritesPath()
{
	return std::filesystem::temp_directory_path() / ("kinu-daw-favorites-" + std::to_string(mixer_session_id) + ".txt");
}

void RefreshInstrumentList()
{
	auto* search = static_cast<Rml::ElementFormControlInput*>(Get("instrument-search"));
	Rml::String query = Rml::StringUtilities::ToLower(search->GetValue());
	int visible = 0;
	for (int favorite = 1; favorite >= 0; --favorite)
		for (int index = 0; index < kMockInstrumentCount; ++index)
		{
			if (instrument_favorites[index] != bool(favorite)) continue;
			auto* row = Get(Rml::CreateString("instrument-row-%d", index).c_str());
			const auto& item = mock_instruments[index];
			const bool match = Rml::StringUtilities::ToLower(Rml::String(item.name) + " " + item.category + " VST3").find(query) != Rml::String::npos;
			row->SetProperty("display", match ? "flex" : "none");
			row->SetClass("selected", index == instrument_selected);
			Get(Rml::CreateString("instrument-heart-%d", index).c_str())->SetClass("favorite", favorite != 0);
			SetText(Rml::CreateString("instrument-heart-%d", index).c_str(), favorite ? u8"♥" : u8"♡");
			Get("instrument-list")->AppendChild(Get("instrument-list")->RemoveChild(row));
			visible += match;
		}
	Get("instrument-empty")->SetProperty("display", visible ? "none" : "block");
	SetText("instrument-selection", instrument_selected < 0 ? u8"音源を選択してください" : mock_instruments[instrument_selected].name);
	Get("instrument-add")->SetClass("ready", instrument_selected >= 0);
}

void InitialiseInstrumentPicker()
{
	std::ifstream preferences(InstrumentFavoritesPath());
	for (bool& favorite : instrument_favorites) preferences >> favorite;
	for (int index = 0; index < kMockInstrumentCount; ++index)
	{
		auto row = document->CreateElement("div");
		row->SetClass("instrument-row", true);
		row->SetId(Rml::CreateString("instrument-row-%d", index));
		row->SetInnerRML(Rml::CreateString("<button class=\"instrument-pick\" onclick=\"instrument-select:%d\"><b>%s</b><small>%s</small></button><span class=\"vst-tag\">VST3</span><button id=\"instrument-heart-%d\" class=\"instrument-heart\" title=\"お気に入り・上に固定\" onclick=\"instrument-favorite:%d\"></button>", index, mock_instruments[index].name, mock_instruments[index].category, index, index));
		Get("instrument-list")->AppendChild(std::move(row));
	}
	RefreshInstrumentList();
}

void ToggleInstrumentFavorite(int index)
{
	if (index < 0 || index >= kMockInstrumentCount) return;
	instrument_favorites[index] = !instrument_favorites[index];
	std::ofstream preferences(InstrumentFavoritesPath());
	for (bool favorite : instrument_favorites) preferences << favorite << ' ';
	RefreshInstrumentList();
}

void ChooseMockInstrument()
{
	if (instrument_selected < 0) return;
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume) InterlockedExchange(shared_volume + kMaxTracks * 2, instrument_selected + 1);
#endif
	Backend::RequestExit();
}
