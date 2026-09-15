struct TrackColorPreset { const char* class_name; const char* preview_color; };
const TrackColorPreset color_presets[] = {
	{"coral", "#ffb6b0"}, {"amber", "#f7d58a"}, {"violet", "#d2bfff"},
	{"cyan", "#a8eff4"}, {"pink", "#f4b2d8"}, {"mint", "#a5e5c8"},
	{"blue", "#aecfff"}, {"orange", "#ffd0a6"}, {"slate", "#d3deec"}
};
int track_color_presets[kMaxTracks] = {0, 1, 2, 3, 4, 5};
int color_menu_track = -1;

void DismissTrackColors()
{
	if (Rml::Element* menu = Get("track-color-menu")) menu->SetClass("open", false);
	color_menu_track = -1;
}

void SetPresetClass(Rml::Element* element, int selected)
{
	for (int i = 0; i < 9; ++i) element->SetClass(color_presets[i].class_name, i == selected);
}

void ApplyTrackColor(int track, int preset)
{
	if (track < 0 || track >= track_count || preset < 0 || preset >= 9) return;
	track_color_presets[track] = preset;
	SetPresetClass(Get(Rml::CreateString("track-color-%d", track).c_str()), preset);
	Rml::ElementList elements;
	Get(Rml::CreateString("track-lane-%d", track).c_str())->QuerySelectorAll(elements, ".clip");
	for (Rml::Element* clip : elements)
	{
		SetPresetClass(clip, preset);
		Rml::ElementList previews;
		clip->QuerySelectorAll(previews, ".notes i, .waveform");
		for (Rml::Element* preview : previews)
			preview->SetProperty(preview->GetTagName() == "i" ? "background" : "color", color_presets[preset].preview_color);
	}
}

void ShowTrackColors(int track, Rml::Element* trigger, Rml::Event& event)
{
	if (track < 0 || track >= track_count) return;
	Rml::Element* menu = Get("track-color-menu");
	if (color_menu_track == track && menu->IsClassSet("open")) { DismissTrackColors(); event.StopPropagation(); return; }
	color_menu_track = track;
	SetText("color-track-name", track_names[track]);
	SetText("selection-name", track_names[track]);
	SetSelected(".track-header", Get(Rml::CreateString("track-header-%d", track).c_str()));
	const float density = context->GetDensityIndependentPixelRatio();
	const auto size = context->GetDimensions();
	menu->SetProperty("left", Rml::CreateString("%.0fpx", std::clamp(trigger->GetAbsoluteLeft() + 18.f * density, 4.f * density, std::max(4.f * density, size.x - 168.f * density))));
	menu->SetProperty("top", Rml::CreateString("%.0fpx", std::clamp(trigger->GetAbsoluteTop(), 4.f * density, std::max(4.f * density, size.y - 164.f * density))));
	for (int i = 0; i < 9; ++i) Get(Rml::CreateString("color-preset-%d", i).c_str())->SetClass("active", i == track_color_presets[track]);
	menu->SetClass("open", true);
	event.StopPropagation();
}
