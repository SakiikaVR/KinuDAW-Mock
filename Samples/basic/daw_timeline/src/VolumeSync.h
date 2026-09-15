// Session-local gain (slots 0..5, tenths of dB) and M/S flags (6..11).
#if defined RMLUI_PLATFORM_WIN32
HANDLE volume_mapping = nullptr;
volatile LONG* shared_volume = nullptr;
#endif

void InitialiseVolumeSync()
{
#if defined RMLUI_PLATFORM_WIN32
	const std::wstring name = L"Local\\KinuDawChannelState-v2-" + std::to_wstring(mixer_session_id);
	volume_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG) * (kMaxTracks * 2 + 1), name.c_str());
	if (volume_mapping) shared_volume = static_cast<volatile LONG*>(MapViewOfFile(volume_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG) * (kMaxTracks * 2 + 1)));
	if (!shared_volume) { Rml::Log::Message(Rml::Log::LT_WARNING, "Volume synchronization unavailable."); return; }
	for (int track = 0; track < track_count; ++track)
	{
		track_channels[track].gain_db = std::clamp(InterlockedCompareExchange(shared_volume + track, 0, 0) / 10.f, -48.f, 6.f);
		const LONG flags = InterlockedCompareExchange(shared_volume + kMaxTracks + track, 0, 0);
		track_channels[track].muted = (flags & 1) != 0;
		track_channels[track].solo = (flags & 2) != 0;
	}
#endif
}

void UpdateTrackSwitches(int track)
{
	if (track == 5) return;
	const char* prefix = mixer_window ? "mix" : "track";
	Get(Rml::CreateString("%s-mute-%d", prefix, track).c_str())->SetClass("active", track_channels[track].muted);
	Get(Rml::CreateString("%s-solo-%d", prefix, track).c_str())->SetClass("active", track_channels[track].solo);
}

void ToggleTrackSwitch(int track, bool mute)
{
	if (track < 0 || track >= track_count || track == 5) return;
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume)
	{
		const LONG bit = mute ? 1 : 2;
		const LONG flags = InterlockedXor(shared_volume + kMaxTracks + track, bit) ^ bit;
		track_channels[track].muted = (flags & 1) != 0;
		track_channels[track].solo = (flags & 2) != 0;
	}
	else
#endif
	{
		bool& flag = mute ? track_channels[track].muted : track_channels[track].solo;
		flag = !flag;
	}
	UpdateTrackSwitches(track);
}

void SetTrackGain(int track, float value)
{
	const float gain = std::round(std::clamp(value, -48.f, 6.f) * 10.f) / 10.f;
	track_channels[track].gain_db = gain;
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume) InterlockedExchange(shared_volume + track, LONG(std::lround(gain * 10.f)));
#endif
}

void CloseVolumeSync()
{
#if defined RMLUI_PLATFORM_WIN32
	if (shared_volume) UnmapViewOfFile(const_cast<LONG*>(shared_volume));
	shared_volume = nullptr;
	if (volume_mapping) CloseHandle(volume_mapping);
	volume_mapping = nullptr;
#endif
}
