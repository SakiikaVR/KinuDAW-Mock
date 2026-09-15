int track_order[kMaxTracks] = {5, 0, 1, 2, 3, 4};
int reorder_track = -1;
int reorder_row = -1;
float reorder_start_y = 0.f;
bool reorder_moved = false;
Rml::String custom_track_names[kMaxTracks];
int rename_track = -1;
bool rename_focus_pending = false;

void EndTrackRename(bool commit)
{
	if (rename_track < 0) return;
	const int track = rename_track;
	rename_track = -1; rename_focus_pending = false;
	auto* input = static_cast<Rml::ElementFormControlInput*>(Get(Rml::CreateString("track-name-input-%d", track).c_str()));
	const Rml::String value = Rml::StringUtilities::StripWhitespace(input->GetValue());
	if (commit && !value.empty())
	{
		custom_track_names[track] = value;
		track_names[track] = custom_track_names[track].c_str();
		SetText(Rml::CreateString("track-name-%d", track).c_str(), value);
		SetText("selection-name", value);
	}
	input->GetParentNode()->SetClass("renaming", false);
	input->Blur();
}

void BeginTrackRename(int track)
{
	EndTrackRename(true);
	DismissTrackColors(); DismissTrackFx();
	reorder_track = reorder_row = -1; reorder_moved = false;
	rename_track = track;
	auto* input = static_cast<Rml::ElementFormControlInput*>(Get(Rml::CreateString("track-name-input-%d", track).c_str()));
	input->SetValue(track_names[track]);
	input->GetParentNode()->SetClass("renaming", true);
	SetSelected(".track-header", Get(Rml::CreateString("track-header-%d", track).c_str()));
	rename_focus_pending = true;
}

void ApplyTrackOrder()
{
	static Rml::String labels[kMaxTracks];
	labels[0] = "MASTER";
	for (int row = 1; row < track_count; ++row) labels[row] = Rml::CreateString("TRACK%d", row);
	for (int row = 0; row < track_count; ++row)
	{
		const int track = track_order[row];
		track_names[track] = custom_track_names[track].empty() ? labels[row].c_str() : custom_track_names[track].c_str();
		SetText(Rml::CreateString("track-name-%d", track).c_str(), track_names[track]);
		for (const char* prefix : {"track-header", "track-lane"})
		{
			auto* node = Get(Rml::CreateString("%s-%d", prefix, track).c_str());
			auto* parent = node->GetParentNode();
			parent->AppendChild(parent->RemoveChild(node));
		}
	}
}

void MoveTrackOrder(Rml::Event& event)
{
	if (reorder_track < 0) return;
	const float y = event.GetParameter("mouse_y", reorder_start_y);
	if (std::abs(y - reorder_start_y) < 4.f * context->GetDensityIndependentPixelRatio() && !reorder_moved) return;
	reorder_moved = true;
	auto* first = Get("track-header-5");
	reorder_row = std::clamp(int((y - first->GetAbsoluteTop()) / first->GetOffsetHeight()), 1, track_count - 1);
	for (int row = 0; row < track_count; ++row)
		Get(Rml::CreateString("track-header-%d", track_order[row]).c_str())->SetClass("drop-target", row == reorder_row);
	Get(Rml::CreateString("track-header-%d", reorder_track).c_str())->SetClass("reordering", true);
}

void EndTrackOrder(bool commit)
{
	if (reorder_track < 0) return;
	if (commit && reorder_moved && reorder_row >= 1)
	{
		int from = 1;
		while (from < track_count && track_order[from] != reorder_track) ++from;
		if (from < track_count)
		{
			if (from < reorder_row) for (int row = from; row < reorder_row; ++row) track_order[row] = track_order[row + 1];
			else for (int row = from; row > reorder_row; --row) track_order[row] = track_order[row - 1];
			track_order[reorder_row] = reorder_track;
			ApplyTrackOrder();
			SetText("selection-name", track_names[reorder_track]);
		}
	}
	for (int track = 0; track < track_count; ++track)
	{
		auto* header = Get(Rml::CreateString("track-header-%d", track).c_str());
		header->SetClass("drop-target", false);
		header->SetClass("reordering", false);
	}
	reorder_track = reorder_row = -1;
	reorder_moved = false;
}
