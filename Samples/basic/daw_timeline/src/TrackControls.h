struct TrackChannel { float gain_db = 0.f; float pan = 0.f; bool muted = false; bool solo = false; bool effects[3] = {}; };
TrackChannel track_channels[kMaxTracks];
#include "VolumeSync.h"
struct TrackKnobDrag { int track = -1; bool pan = false; float start_y = 0.f; float start_value = 0.f; };
TrackKnobDrag track_knob_drag;
int track_fx_open = -1;

void UpdateTrackKnobs(int track)
{
	UpdateTrackSwitches(track);
	const TrackChannel& channel = track_channels[track];
	const float volume_position = (channel.gain_db + 48.f) / 54.f * 52.f;
	Get(Rml::CreateString("track-vol-dial-%d", track).c_str())->SetProperty("left", Rml::CreateString("%.1fdp", volume_position));
	Get(Rml::CreateString("track-vol-fill-%d", track).c_str())->SetProperty("width", Rml::CreateString("%.1fdp", volume_position + 8.f));
	Get(Rml::CreateString("track-pan-dial-%d", track).c_str())->SetProperty("transform", Rml::CreateString("rotate(%.1fdeg)", channel.pan * 135.f));
	SetText(Rml::CreateString("track-vol-value-%d", track).c_str(), Rml::CreateString("%.1f", channel.gain_db));
	SetText(Rml::CreateString("track-pan-value-%d", track).c_str(), std::abs(channel.pan) < .01f ? "C" : Rml::CreateString("%s%.0f", channel.pan < 0 ? "L" : "R", std::abs(channel.pan) * 100.f));
	Get(Rml::CreateString("track-vol-%d", track).c_str())->SetAttribute("title", Rml::CreateString("VOL %.1f dB", channel.gain_db));
	Get(Rml::CreateString("track-pan-%d", track).c_str())->SetAttribute("title", Rml::CreateString("PAN %.0f%%", channel.pan * 100.f));
}

void MoveTrackKnob(Rml::Event& event)
{
	if (track_knob_drag.track < 0) return;
	const float delta = (track_knob_drag.pan ? track_knob_drag.start_y - event.GetParameter("mouse_y", track_knob_drag.start_y) : event.GetParameter("mouse_x", track_knob_drag.start_y) - track_knob_drag.start_y) / context->GetDensityIndependentPixelRatio();
	TrackChannel& channel = track_channels[track_knob_drag.track];
	if (track_knob_drag.pan) channel.pan = std::round(std::clamp(track_knob_drag.start_value + delta / 80.f, -1.f, 1.f) * 100.f) / 100.f;
	else SetTrackGain(track_knob_drag.track, track_knob_drag.start_value + delta * 54.f / 52.f);
	UpdateTrackKnobs(track_knob_drag.track);
}

void DismissTrackFx()
{
	if (Rml::Element* panel = Get("track-fx-menu")) panel->SetClass("open", false);
	track_fx_open = -1;
}

void ShowTrackFx(int track, Rml::Element* trigger)
{
	if (track_fx_open == track) { DismissTrackFx(); return; }
	track_fx_open = track;
	SetText("track-fx-name", track_names[track]);
	const float density = context->GetDensityIndependentPixelRatio();
	const auto dimensions = context->GetDimensions();
	auto* panel = Get("track-fx-menu");
	panel->SetProperty("left", Rml::CreateString("%.0fpx", std::min(trigger->GetAbsoluteLeft() + 20.f * density, std::max(4.f * density, dimensions.x - 190.f * density))));
	panel->SetProperty("top", Rml::CreateString("%.0fpx", std::min(trigger->GetAbsoluteTop(), std::max(4.f * density, dimensions.y - 180.f * density))));
	for (int i = 0; i < 3; ++i) Get(Rml::CreateString("track-effect-%d", i).c_str())->SetClass("active", track_channels[track].effects[i]);
	panel->SetClass("open", true);
}

void UpdateTrackMeters(float time)
{
	bool any_solo = false;
	for (int track = 0; track < track_count; ++track) any_solo |= track_channels[track].solo;
	for (int track = 0; track < track_count; ++track)
	{
		const auto& channel = track_channels[track];
		const float level = channel.muted || (any_solo && !channel.solo) ? 0.f : std::min(1.f, std::pow(10.f, channel.gain_db / 20.f) * (playing ? .55f + .3f * std::sin(time * 4.f + track) : .65f));
		for (int side = 0; side < 2; ++side)
		{
			const float balance = side == 0 ? std::min(1.f, 1.f - channel.pan) : std::min(1.f, 1.f + channel.pan);
			Get(Rml::CreateString("track-meter-%s-%d", side == 0 ? "l" : "r", track).c_str())->SetProperty("width", Rml::CreateString("%.0f%%", level * balance * (side == 0 ? 100.f : 87.f)));
		}
	}
}
