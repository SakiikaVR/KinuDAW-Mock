// Included in the application's view/controller namespace. Geometry is shared by
// keys, pitch rows, ruler, notes and velocity previews; only the grid owns scrolling.
constexpr int kPianoHighestPitch = 108;
constexpr int kPianoLowestPitch = 21;
float piano_density = 0.f;
struct PianoNote { int pitch; float beat; float duration; int velocity; };
PianoNote piano_notes[12];

Rml::String PianoPitchName(int pitch)
{
	const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return Rml::CreateString("%s%d", names[pitch % 12], pitch / 12 - 1);
}

void PianoAddElement(const char* parent, const Rml::String& id, const char* class_name, const Rml::String& label = "")
{
	auto element = document->CreateElement("div");
	element->SetId(id);
	element->SetClass(class_name, true);
	if (!label.empty()) element->SetInnerRML(label);
	Get(parent)->AppendChild(std::move(element));
}

void UpdatePianoRoll()
{
	const float density = context->GetDensityIndependentPixelRatio();
	if (density != piano_density)
	{
		piano_density = density;
		const float row_height = 18.f * density;
		const float beat_width = 64.f * density;
		const auto pixels = [](float value) { return Rml::CreateString("%.0fpx", value); };
		for (const char* id : {"piano-grid", "piano-ruler-content", "piano-velocity-content"}) Get(id)->SetProperty("width", pixels(16.f * beat_width));
		Get("piano-grid")->SetProperty("height", pixels((kPianoHighestPitch - kPianoLowestPitch + 1) * row_height));
		Get("piano-key-content")->SetProperty("height", pixels((kPianoHighestPitch - kPianoLowestPitch + 1) * row_height));
		for (int pitch = kPianoHighestPitch; pitch >= kPianoLowestPitch; --pitch)
		{
			for (const char* prefix : {"piano-key-", "piano-row-"})
			{
				auto* element = Get((Rml::String(prefix) + std::to_string(pitch)).c_str());
				element->SetProperty("top", pixels((kPianoHighestPitch - pitch) * row_height));
				element->SetProperty("height", pixels(row_height));
			}
		}
		for (int beat = 0; beat <= 16; ++beat)
		{
			Get(Rml::CreateString("piano-beat-%d", beat).c_str())->SetProperty("left", pixels(beat * beat_width));
			if (beat % 4 == 0) Get(Rml::CreateString("piano-bar-%d", beat / 4).c_str())->SetProperty("left", pixels(beat * beat_width));
		}
		for (int i = 0; i < 12; ++i)
		{
			const PianoNote& note = piano_notes[i];
			auto* element = Get(Rml::CreateString("piano-note-%d", i).c_str());
			element->SetProperty("left", pixels(note.beat * beat_width));
			element->SetProperty("top", pixels((kPianoHighestPitch - note.pitch) * row_height + 2.f * density));
			element->SetProperty("width", pixels(note.duration * beat_width - 2.f * density));
			element->SetProperty("height", pixels(row_height - 3.f * density));
			Get(Rml::CreateString("piano-velocity-%d", i).c_str())->SetProperty("left", pixels(note.beat * beat_width));
		}
	}
	Rml::Element* scroll = Get("piano-scroll");
	Get("piano-key-content")->SetProperty("top", Rml::CreateString("%.0fpx", -scroll->GetScrollTop()));
	for (const char* id : {"piano-ruler-content", "piano-velocity-content"}) Get(id)->SetProperty("left", Rml::CreateString("%.0fpx", -scroll->GetScrollLeft()));
}

void InitialisePianoRoll()
{
	SetText("piano-track-name", track_names[piano_track]);
	for (int pitch = kPianoHighestPitch; pitch >= kPianoLowestPitch; --pitch)
	{
		const int semitone = pitch % 12;
		const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
		PianoAddElement("piano-key-content", Rml::CreateString("piano-key-%d", pitch), black ? "piano-key-black" : "piano-key-white", PianoPitchName(pitch));
		PianoAddElement("piano-grid", Rml::CreateString("piano-row-%d", pitch), black ? "piano-row-black" : "piano-row-white");
	}
	for (int beat = 0; beat <= 16; ++beat)
	{
		PianoAddElement("piano-grid", Rml::CreateString("piano-beat-%d", beat), beat % 4 == 0 ? "piano-bar-line" : "piano-beat-line");
		if (beat % 4 == 0) PianoAddElement("piano-ruler-content", Rml::CreateString("piano-bar-%d", beat / 4), "piano-bar-label", std::to_string(beat / 4 + 1));
	}
	const int bases[] = {36, 48, 60, 64, 72, 60};
	const int intervals[] = {0, 7, 12, 3, 0, 10, 7, 3, 0, 7, 12, 10};
	for (int i = 0; i < 12; ++i)
	{
		piano_notes[i] = {bases[piano_track] + intervals[i], float(i) * 1.25f, i % 3 == 0 ? 1.f : .5f, 70 + (i * 13) % 55};
		PianoAddElement("piano-grid", Rml::CreateString("piano-note-%d", i), "piano-note", PianoPitchName(piano_notes[i].pitch));
		PianoAddElement("piano-velocity-content", Rml::CreateString("piano-velocity-%d", i), "piano-velocity-bar");
		Get(Rml::CreateString("piano-velocity-%d", i).c_str())->SetProperty("height", Rml::CreateString("%.0f%%", piano_notes[i].velocity / 127.f * 90.f));
	}
	UpdatePianoRoll();
	context->Update();
	Get("piano-scroll")->SetScrollTop((kPianoHighestPitch - bases[piano_track] - 18) * 18.f * piano_density);
	UpdatePianoRoll();
}
