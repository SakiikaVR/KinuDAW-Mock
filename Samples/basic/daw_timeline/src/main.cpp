#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Debugger.h>
#include <RmlUi_Backend.h>
#include <Shell.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <deque>
#include <list>
#include <vector>
#include "AudioEngine.h"
#include "json.hpp"
#include <cstring>

#if defined RMLUI_PLATFORM_WIN32
	#include <RmlUi_Include_Windows.h>
	#include <shellapi.h>
	#include <dwmapi.h>
	#include <commdlg.h>
	#include <mmsystem.h>
	#pragma comment(lib, "dwmapi.lib")
#endif

namespace {
class DawSystem final : public Rml::SystemInterface {
public:
    Rml::SystemInterface* base=nullptr; std::ofstream log;
    double GetElapsedTime() override { return base->GetElapsedTime(); }
    void JoinPath(Rml::String& translated,const Rml::String& document,const Rml::String& path) override { base->JoinPath(translated,document,path); }
    bool LogMessage(Rml::Log::Type type,const Rml::String& message) override {
        if(!log.is_open()) { wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768); log.open(std::filesystem::path(executable).parent_path()/("kinu-ui-"+std::to_string(GetCurrentProcessId())+".log")); }
        if(log) { log<<int(type)<<": "<<message<<"\n"; log.flush(); }
        return base->LogMessage(type,message);
    }
    void SetMouseCursor(const Rml::String& cursor) override { base->SetMouseCursor(cursor); }
    void SetClipboardText(const Rml::String& text) override { base->SetClipboardText(text); }
    void GetClipboardText(Rml::String& text) override { base->GetClipboardText(text); }
    void ActivateKeyboard(Rml::Vector2f caret,float height) override { base->ActivateKeyboard(caret,height); }
    void DeactivateKeyboard() override { base->DeactivateKeyboard(); }
};
constexpr int kMaxTracks = 32;
std::unique_ptr<Kinu::AudioEngine> audio_engine;
std::array<std::vector<unsigned char>,kMaxTracks> cached_plugin_states;
std::array<Kinu::PluginInfo,kMaxTracks> project_plugins;
struct EffectCache { Kinu::PluginInfo info; std::vector<unsigned char> state; };
std::array<std::array<EffectCache,3>,kMaxTracks> project_effects;
int instrument_target = -1;
int instrument_slot=0;
bool ui_test=false;
float ui_density=0;
void AddVstEffect();
std::string PluginSessionMetadata();
void MockNotice(const Rml::String& text);
void SyncAudio();
void SaveProject(bool recovery = false);
void LoadProject();
void ImportAudioDialog();
void ExportAudio();
void ImportAudioFile(int track,const std::filesystem::path& path,float beat);
void PreparePiano(int track);
void ToggleRecording();
void FinishRecording();
void RecordMidi(int track,int pitch,int velocity,bool on,int channel=0);
void ExternalMidi(DWORD packed);
void PianoRelease();
void AddVolumeAutomation();
void AutomationClick(int track,Rml::Event& event);
void MoveAutomation(Rml::Event& event);
void EndAutomationGesture();
void AudioSettings(Rml::Element* trigger);
int track_count = 6;
bool track_audio[kMaxTracks] = {false, false, false, true, true, false};
bool track_automation[kMaxTracks] = {};
bool instrument_window = false;
int plugin_track = -1;
constexpr float kBeatsPerBar = 4.f;
constexpr float kProjectBars = 32.f;
float bpm = 124.f;
bool tempo_dragging = false;
float tempo_start_y = 0.f;
float tempo_start_bpm = 124.f;
constexpr float kSnapStepBeats = 0.25f;
const float snap_steps[] = {.25f, .5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
const char* snap_labels[] = {"1/16", "1/8", "1/4", "1/2", "1", "2", "4", "8"};
int snap_index = 0;
unsigned long mixer_session_id = 0;

struct ClipState {
	Rml::String id;
	Rml::String label;
	float start_beat;
	float length_beats;
	const Kinu::AudioFile* audio = nullptr;
	std::vector<Kinu::Note> notes;
	std::vector<Kinu::MidiControl> controls;
	double source_offset = 0;
	double pattern_length = 16;
};

std::list<ClipState> clips = {
	{"clip-drums-a", "VERSE DRUMS", 0, 32}, {"clip-drums-b", "CHORUS DRUMS", 32, 32},
	{"clip-drums-c", "VERSE DRUMS 2", 64, 32}, {"clip-drums-d", "OUTRO DRUMS", 96, 32},
	{"clip-bass-a", "BASS A", 8, 24}, {"clip-bass-b", "BASS B", 40, 24},
	{"clip-bass-c", "BASS C", 72, 24}, {"clip-bass-d", "BASS END", 104, 16},
	{"clip-keys-a", "NEON CHORDS", 16, 40}, {"clip-keys-b", "GLASS ARP", 64, 32},
	{"clip-vocal-a", "LEAD VOCAL 01", 36, 20}, {"clip-vocal-b", "LEAD VOCAL 02", 68, 28},
	{"clip-fx-a", "REVERSE BLOOM", 12, 12}, {"clip-fx-b", "NOISE LIFT", 84, 16},
	{"clip-master-a", "ARRANGEMENT", 0, 120},
};

struct ClipDragSession {
	ClipState* clip = nullptr;
	Rml::Element* element = nullptr;
	float pointer_start_x = 0.f;
	float clip_start_beat = 0.f;
	float clip_length_beats = 0.f;
	int resize_edge = 0; // -1: left, +1: right, 0: move
	double source_offset = 0;
};

Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
bool playing = false;
bool looping = false;
bool scrubbing = false;
bool snap_enabled = true;
bool mixer_window = false;
bool mixer_topmost = false;
int piano_track = -1;
const char* track_names[kMaxTracks] = {"TRACK1", "TRACK2", "TRACK3", "TRACK4", "TRACK5", "MASTER"};
bool mixer_fader_dragging = false;
float mixer_gain_db = 0.f;
#if defined RMLUI_PLATFORM_WIN32
HANDLE instruments_process = nullptr;
DWORD instruments_process_id = 0;
HANDLE mixer_process = nullptr;
HWND daw_native_window = nullptr;
DWORD mixer_process_id = 0;
HANDLE piano_processes[kMaxTracks] = {};
DWORD piano_process_ids[kMaxTracks] = {};
HANDLE plugin_processes[kMaxTracks] = {};
DWORD plugin_process_ids[kMaxTracks] = {};
#endif
float playhead_beat = 0.f;
float pixels_per_beat = 30.f;
float loop_start_beat = 0.f;
float loop_end_beat = kBeatsPerBar;
float loop_menu_beat = loop_start_beat;
ClipDragSession clip_drag;
Rml::String clip_menu_id;
int empty_paste_track = -1;
float empty_paste_beat = 0.f;
std::chrono::steady_clock::time_point previous_frame;
#if defined RMLUI_PLATFORM_WIN32
std::vector<std::pair<std::filesystem::path, POINT>> mock_audio_drops;
#endif

void EndClipDrag();
void StopMockRecording();
void EndPluginGesture();
int ClipResizeEdge(Rml::Element* element, float mouse_x)
{
	const float edge = std::min(8.f * context->GetDensityIndependentPixelRatio(), element->GetClientWidth() / 3.f);
	const float x = mouse_x - element->GetAbsoluteLeft();
	return x < edge ? -1 : x > element->GetClientWidth() - edge ? 1 : 0;
}
#include "NativeWindow.h"

Rml::Element* Get(const char* id) { return document ? document->GetElementById(id) : nullptr; }

void SetText(const char* id, const Rml::String& value)
{
	if (Rml::Element* element = Get(id))
			if (element->GetInnerRML() != Rml::StringUtilities::EncodeRml(value)) element->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
}

void MoveTempo(Rml::Event& event)
{
	if (!tempo_dragging) return;
	const float delta = (tempo_start_y - event.GetParameter("mouse_y", tempo_start_y)) / context->GetDensityIndependentPixelRatio();
	bpm = std::round(std::clamp(tempo_start_bpm + delta * .5f, 20.f, 300.f) * 10.f) / 10.f;
	SetText("tempo-value", Rml::CreateString("%.1f", bpm));
}

void UpdateMixerGain()
{
	SetText("mixer-gain", Rml::CreateString("%.1f dB", mixer_gain_db));
	if (Rml::Element* thumb = Get("mixer-fader-thumb"))
		thumb->SetProperty("top", Rml::CreateString("%.2f%%", (6.f - mixer_gain_db) / 54.f * 100.f));
}

void MoveMixerFader(Rml::Event& event)
{
	if (Rml::Element* rail = Get("mixer-fader-rail"))
	{
		const float height = std::max(1.f, rail->GetClientHeight());
		const float ratio = (event.GetParameter("mouse_y", rail->GetAbsoluteTop()) - rail->GetAbsoluteTop()) / height;
		mixer_gain_db = std::round(std::clamp(6.f - ratio * 54.f, -48.f, 6.f) * 10.f) / 10.f;
		UpdateMixerGain();
	}
}

#if defined RMLUI_PLATFORM_WIN32
void OpenToolWindow(HANDLE& tool_process, DWORD& tool_process_id, const std::wstring& arguments)
{
	if (tool_process && WaitForSingleObject(tool_process, 0) == WAIT_TIMEOUT)
	{
		EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
			DWORD process_id = 0;
			GetWindowThreadProcessId(window, &process_id);
			if (process_id == DWORD(parameter) && IsWindowVisible(window))
			{
				ShowWindow(window, SW_RESTORE);
				SetForegroundWindow(window);
				return FALSE;
			}
			return TRUE;
		}, LPARAM(tool_process_id));
		return;
	}
	if (tool_process) { CloseHandle(tool_process); tool_process = nullptr; }
	wchar_t executable[32768] = {};
	if (!GetModuleFileNameW(nullptr, executable, 32768)) return;
	std::wstring command_line = L"\"" + std::wstring(executable) + L"\" " + arguments;
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	if (CreateProcessW(executable, &command_line[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
	{
		tool_process = process.hProcess;
		tool_process_id = process.dwProcessId;
		CloseHandle(process.hThread);
	}
	else Rml::Log::Message(Rml::Log::LT_ERROR, "Could not open the mixer window.");
}
#endif

void OpenMixerWindow()
{
#if defined RMLUI_PLATFORM_WIN32
	OpenToolWindow(mixer_process, mixer_process_id, L"--mixer --session=" + std::to_wstring(mixer_session_id));
#endif
}

void OpenPianoWindow(int track)
{
	if (track < 0 || track >= track_count) return;
#if defined RMLUI_PLATFORM_WIN32
	if(track_audio[track] || track_automation[track] || track==5) return;
	PreparePiano(track);
	OpenToolWindow(piano_processes[track], piano_process_ids[track], L"--piano=" + std::to_wstring(track)+L" --session="+std::to_wstring(mixer_session_id));
#endif
}

#include "PianoEditor.h"

void SetSelected(const char* selector, Rml::Element* selected)
{
	if (!document)
		return;
	Rml::ElementList elements;
	document->QuerySelectorAll(elements, selector);
	for (Rml::Element* element : elements)
		element->SetClass("selected", element == selected);
}

#include "TrackColors.h"
#include "TrackControls.h"
#include "TrackOrder.h"
#include "MixerTracks.h"

#include "TransportWave.h"

ClipState* FindClip(const Rml::String& id)
{
	for (ClipState& clip : clips)
		if (id == clip.id)
			return &clip;
	return nullptr;
}

float SnapBeat(float beat)
{
	return snap_enabled ? std::round(beat / snap_steps[snap_index]) * snap_steps[snap_index] : beat;
}

void UpdateClipGeometry(const ClipState& clip)
{
	if (Rml::Element* element = Get(clip.id.c_str()))
	{
		element->SetProperty("left", Rml::CreateString("%.1fpx", clip.start_beat * pixels_per_beat));
		element->SetProperty("width", Rml::CreateString("%.1fpx", clip.length_beats * pixels_per_beat - 6.f));
	}
}

void UpdateLoopRange()
{
	if (Rml::Element* range = Get("loop-range"))
	{
		range->SetClass("enabled", looping);
		range->SetProperty("left", Rml::CreateString("%.1fpx", loop_start_beat * pixels_per_beat));
		range->SetProperty("width", Rml::CreateString("%.1fpx", (loop_end_beat - loop_start_beat) * pixels_per_beat));
	}
}

void DismissLoopMenu()
{
	if (Rml::Element* menu = Get("loop-context-menu"))
		menu->SetClass("open", false);
}

void ShowLoopMenu(Rml::Event& event, Rml::Element* ruler)
{
	DismissTrackColors();
	DismissTrackFx();
	const float mouse_x = event.GetParameter("mouse_x", 0.f);
	const float mouse_y = event.GetParameter("mouse_y", 0.f);
	loop_menu_beat = std::clamp(SnapBeat((mouse_x - ruler->GetAbsoluteLeft()) / pixels_per_beat), 0.f, kProjectBars * kBeatsPerBar);
	if (Rml::Element* menu = Get("loop-context-menu"))
	{
		const Rml::Vector2i dimensions = context->GetDimensions();
		const float menu_x = std::clamp(mouse_x, 4.f, float(dimensions.x) - 246.f);
		const float menu_y = std::clamp(mouse_y + 5.f, 4.f, float(dimensions.y) - 76.f);
		menu->SetProperty("left", Rml::CreateString("%.1fpx", menu_x));
		menu->SetProperty("top", Rml::CreateString("%.1fpx", menu_y));
		menu->SetClass("open", true);
	}
	event.StopPropagation();
}

void UpdateTimelineGeometry()
{
	const float timeline_width = kProjectBars * kBeatsPerBar * pixels_per_beat;
	const Rml::String width = Rml::CreateString("%.0fpx", timeline_width);
	for (const char* id : {"ruler-canvas", "lane-canvas", "grid-overlay", "playhead"})
		if (Rml::Element* element = Get(id))
			element->SetProperty("width", width);

	for (const ClipState& clip : clips)
		UpdateClipGeometry(clip);
	UpdateLoopRange();

	for (int bar = 1; bar <= 33; ++bar)
	{
		if (Rml::Element* marker = Get(Rml::CreateString("bar-%d", bar).c_str()))
			marker->SetProperty("left", Rml::CreateString("%.0fpx", (bar - 1) * kBeatsPerBar * pixels_per_beat));
	}
	for (int bar = 2; bar <= 32; bar += 2)
	{
		if (Rml::Element* band = Get(Rml::CreateString("band-%d", bar).c_str()))
		{
			band->SetProperty("left", Rml::CreateString("%.0fpx", (bar - 1) * kBeatsPerBar * pixels_per_beat));
			band->SetProperty("width", Rml::CreateString("%.0fpx", kBeatsPerBar * pixels_per_beat));
		}
	}
	for (int beat = 0; beat <= 128; ++beat)
	{
		if (Rml::Element* line = Get(Rml::CreateString("beat-%d", beat).c_str()))
			line->SetProperty("left", Rml::CreateString("%.0fpx", beat * pixels_per_beat));
	}
	SetText("zoom-value", Rml::CreateString("%.0f%%", pixels_per_beat / 30.f * 100.f));
}

void UpdatePlayhead()
{
	if (Rml::Element* playhead = Get("playhead-line"))
		playhead->SetProperty("left", Rml::CreateString("%.1fpx", playhead_beat * pixels_per_beat));
	const int total_beats = std::max(0, int(std::floor(playhead_beat)));
	const int bar = total_beats / 4 + 1;
	const int beat = total_beats % 4 + 1;
	const int ticks = int((playhead_beat - std::floor(playhead_beat)) * 960.f);
	const Rml::String digits = Rml::CreateString("%02d%02d%03d", bar, beat, ticks);
	for (int i = 0; i < 7; ++i)
		SetText(Rml::CreateString("position-digit-%d", i).c_str(), digits.substr(i, 1));
}

void ScrubToMouse(Rml::Event& event, Rml::Element* element)
{
	const float mouse_x = event.GetParameter("mouse_x", 0.f);
	const float local_x = mouse_x - element->GetAbsoluteLeft();
	playhead_beat = std::clamp(SnapBeat(local_x / pixels_per_beat), 0.f, kProjectBars * kBeatsPerBar);
	UpdatePlayhead();
}

#include "ClipEditing.h"

void BeginClipDrag(Rml::Event& event, const Rml::String& clip_id, Rml::Element* element)
{
	ClipState* clip = FindClip(clip_id);
	if (!clip)
		return;
	Get("empty-lane-menu")->SetClass("open", false); empty_paste_track = -1;
	if (event.GetParameter("button", 0) == 1)
	{
		EndClipDrag();
		DismissLoopMenu(); DismissTrackColors(); DismissTrackFx();
		Get("view-menu")->SetClass("open", false); Get("snap-menu")->SetClass("open", false);
		clip_menu_id = clip_id;
		SetSelected(".clip", element); SetText("selection-name", clip->label);
		const float density = context->GetDensityIndependentPixelRatio();
		const auto size = context->GetDimensions();
		auto* menu = Get("clip-context-menu");
		auto* paste = Get("clip-menu-paste");
		paste->SetClass("unavailable", !CanPasteClip(ClipTrack(element)));
		if (CanPasteClip(ClipTrack(element))) paste->RemoveAttribute("disabled"); else paste->SetAttribute("disabled", true);
		menu->SetProperty("left", Rml::CreateString("%.0fpx", std::clamp(event.GetParameter("mouse_x", 0.f), 0.f, std::max(0.f, size.x - 124.f * density))));
		menu->SetProperty("top", Rml::CreateString("%.0fpx", std::clamp(event.GetParameter("mouse_y", 0.f) + 4.f * density, 0.f, std::max(0.f, size.y - 124.f * density))));
		menu->SetClass("open", true);
		event.StopPropagation();
		return;
	}
	if (event.GetParameter("button", 0) != 0) return;
	Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
	const float mouse_x = event.GetParameter("mouse_x", 0.f);
	clip_drag = {clip, element, mouse_x, clip->start_beat, clip->length_beats, ClipResizeEdge(element, mouse_x)};
	clip_drag.source_offset=clip->source_offset;
	element->SetClass("dragging", true);
	SetSelected(".clip", element);
	SetText("selection-name", clip->label);
	// Allow RmlUi's mousedown processing to detect double-clicks on clips.
}

void MoveClipDrag(Rml::Event& event)
{
	if (!clip_drag.clip || !clip_drag.element)
		return;
	const float mouse_x = event.GetParameter("mouse_x", clip_drag.pointer_start_x);
	const float delta_beats = (mouse_x - clip_drag.pointer_start_x) / pixels_per_beat;
	const float minimum = std::min(clip_drag.clip_length_beats, std::max(.25f, 48.f / pixels_per_beat));
	const float end = clip_drag.clip_start_beat + clip_drag.clip_length_beats;
	if (clip_drag.resize_edge > 0)
		clip_drag.clip->length_beats = std::clamp(SnapBeat(end + delta_beats), clip_drag.clip_start_beat + minimum, kProjectBars * kBeatsPerBar) - clip_drag.clip_start_beat;
	else if (clip_drag.resize_edge < 0)
	{
		clip_drag.clip->start_beat = std::clamp(SnapBeat(clip_drag.clip_start_beat + delta_beats), 0.f, std::max(0.f, end - minimum));
		clip_drag.clip->length_beats = end - clip_drag.clip->start_beat;
		if(clip_drag.clip->audio) {
			double offset=clip_drag.source_offset+(clip_drag.clip->start_beat-clip_drag.clip_start_beat)*60/bpm;
			if(offset<0) { clip_drag.clip->start_beat=clip_drag.clip_start_beat-float(clip_drag.source_offset*bpm/60); offset=0; clip_drag.clip->length_beats=end-clip_drag.clip->start_beat; }
			clip_drag.clip->source_offset=offset;
		}
	}
	else
	{
		const float max_start = kProjectBars * kBeatsPerBar - clip_drag.clip->length_beats;
		clip_drag.clip->start_beat = std::clamp(SnapBeat(clip_drag.clip_start_beat + delta_beats), 0.f, max_start);
	}
	UpdateClipGeometry(*clip_drag.clip);
	event.StopPropagation();
}

void EndClipDrag()
{
	if (clip_drag.clip && (clip_drag.clip->start_beat != clip_drag.clip_start_beat || clip_drag.clip->length_beats != clip_drag.clip_length_beats))
	{
		auto snapshot = CaptureClips();
		for (auto& saved : snapshot.clips) if (saved.state.id == clip_drag.clip->id)
		{ saved.state.start_beat = clip_drag.clip_start_beat; saved.state.length_beats = clip_drag.clip_length_beats; saved.state.source_offset=clip_drag.source_offset; }
		SaveClipUndo(std::move(snapshot));
	}
	if (clip_drag.element)
		clip_drag.element->SetClass("dragging", false);
	clip_drag = {};
}

#include "PluginCatalog.h"
#include "MockTracks.h"
#include "PluginMock.h"
#include "Project.h"
void EndPluginGesture() { plugin_drag_param = -1; }

void UpdatePluginAutomation()
{
	if (!plugin_shared) return;
	const int request = InterlockedExchange(&plugin_shared->request, 0) - 1;
	if (request >= 0 && request < kMaxTracks * kPluginParams)
	{
		const int source = request / kPluginParams, param = request % kPluginParams;
		if (source < track_count)
		{
			const int automation = AddMockTrack(false, true);
			if (automation >= 0) InterlockedExchange(&plugin_shared->binding[source][param], automation + 1);
		}
	}
	static double next_read = 0;
	const double time = Rml::GetSystemInterface()->GetElapsedTime();
	if (time < next_read) return; next_read = time + .2;
	for (int track = 0; track < track_count; ++track)
	{
		if (!track_automation[track]) continue;
		Rml::String label;
		for (int source = 0; source < track_count; ++source)
			for (int param = 0; param < kPluginParams; ++param)
				if (PluginRead(&plugin_shared->binding[source][param]) == track + 1)
				{
					if (!label.empty()) label += " / ";
					label += Rml::CreateString("%s → %s (%.3f)", track_names[source], plugin_param_names[param], PluginRead(&plugin_shared->value[source][param]) / 1000.f);
				}
		if (label.empty()) label = u8"Automation / パラメーター未接続";
		auto* hint = Get(Rml::CreateString("track-lane-%d", track).c_str())->QuerySelector(".empty-track-hint");
		if (hint && hint->GetAttribute<Rml::String>("binding-text", "") != label)
		{ hint->SetAttribute("binding-text", label); hint->SetInnerRML(Rml::StringUtilities::EncodeRml(label)); }
	}
}

void DeleteMenuClip()
{
	const Rml::String id = clip_menu_id;
	Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
	for (auto it = clips.begin(); it != clips.end(); ++it)
	{
		if (it->id != id) continue;
		EndClipDrag();
		SaveClipUndo();
		for (auto& recording : mock_record_clips) if (recording == &*it) recording = nullptr;
		if (auto* element = Get(id.c_str()))
		{
			auto* lane = element->GetParentNode();
			lane->RemoveChild(element);
			if (!lane->QuerySelector(".clip"))
				if (auto* hint = lane->QuerySelector(".empty-track-hint")) hint->SetProperty("display", "block");
		}
		clips.erase(it);
		SetText("selection-name", "—");
		break;
	}
}

class TimelineEventListener final : public Rml::EventListener {
public:
	TimelineEventListener(Rml::String command, Rml::Element* element) : command(std::move(command)), element(element) {}

	void ProcessEvent(Rml::Event& event) override
	{
		try { DispatchEvent(event); }
		catch(const std::exception& e) { MockNotice(e.what()); }
	}
	void DispatchEvent(Rml::Event& event)
	{
		if(PianoCommand(command,event)) return;
		if (command == "exit")
			Backend::RequestExit();
		else if (command == "project-save") SaveProject();
		else if (command == "project-open") LoadProject();
		else if (command == "audio-import") ImportAudioDialog();
		else if (command == "audio-export") ExportAudio();
		else if(command=="audio-settings") AudioSettings(element);
		else if(command=="vst-scan") StartPluginScan();
		else if(command=="vst-effect-add") AddVstEffect();
		else if(command.rfind("mixer-vst-",0)==0 && shared_volume) {
            int slot=std::atoi(command.c_str()+command.find_last_of(':')+1); if(slot<0 || slot>3) return;
            int request=command.rfind("mixer-vst-edit:",0)==0?kMixerEditRequest:command.rfind("mixer-vst-remove:",0)==0?kMixerRemoveRequest:kMixerBypassRequest;
            if(InterlockedCompareExchange(shared_volume+request,mixer_selected_channel*4+slot+1,0)!=0) MockNotice("Previous VST3 request is pending");
        }
		else if(command.rfind("vst-effect-remove:",0)==0 && audio_engine) {
			int t=std::atoi(command.c_str()+18),slot=std::atoi(command.c_str()+command.find_last_of(':')+1); std::string error;
			if(t>=0 && t<track_count && slot>=0 && slot<4 && audio_engine->unloadPlugin(t,error,slot)) { if(!slot) { project_plugins[t]={}; cached_plugin_states[t].clear(); } else project_effects[t][slot-1]={}; DismissTrackFx(); }
			if(!error.empty()) MockNotice(error);
		}
		else if(command.rfind("vst-effect-edit:",0)==0 && audio_engine) {
			int t=std::atoi(command.c_str()+16),slot=std::atoi(command.c_str()+command.find_last_of(':')+1); std::string error;
			if(audio_engine->pluginHealthy(t,slot)) audio_engine->editor(t,error,slot);
			else if(auto* info=audio_engine->pluginInfo(t,slot)) { Kinu::PluginInfo copy=*info; if(audio_engine->loadPlugin(t,copy,error,slot)) audio_engine->editor(t,error,slot); }
			if(!error.empty()) MockNotice(error);
		}
		else if (command.rfind("open-plugin:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + 12);
#if defined RMLUI_PLATFORM_WIN32
			if (track >= 0 && track < track_count && !track_automation[track] && audio_engine) {
				std::string error;
				if (audio_engine->hasPlugin(track)) {
					if(!audio_engine->pluginHealthy(track)) { auto info=*audio_engine->pluginInfo(track); if(audio_engine->loadPlugin(track,info,error)) audio_engine->restorePluginState(track,cached_plugin_states[track]); }
					if (!audio_engine->editor(track,error)) MockNotice(error);
				}
				else OpenToolWindow(instruments_process,instruments_process_id,L"--instruments --target="+std::to_wstring(track)+L" --session="+std::to_wstring(mixer_session_id));
			}
#endif
			event.StopPropagation();
		}
		else if (command.rfind("plugin-tab:", 0) == 0) PluginSetTab(std::clamp(std::atoi(command.c_str() + 11), 0, 2));
		else if (command == "plugin-search") PluginSearch();
		else if (command == "plugin-pin")
		{
			mixer_topmost = !mixer_topmost;
			SetWindowPos(daw_native_window, mixer_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			Get("plugin-pin")->SetClass("pinned", mixer_topmost);
		}
		else if (command.rfind("plugin-route-toggle:", 0) == 0 && plugin_shared)
		{
			const int side = std::clamp(std::atoi(command.c_str() + 20), 0, 1);
			InterlockedXor(&plugin_shared->route[plugin_track][side * 3], 1); plugin_next_poll = 0; UpdatePluginWindow();
		}
		else if (command.rfind("plugin-route-step:", 0) == 0 && plugin_shared)
		{
			const int field = std::atoi(command.c_str() + 18), delta = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			if (field == 1 || field == 2 || field == 4 || field == 5) InterlockedExchange(&plugin_shared->route[plugin_track][field], std::clamp(PluginRead(&plugin_shared->route[plugin_track][field]) + delta, 0L, 15L));
			plugin_next_poll = 0; UpdatePluginWindow();
		}
		else if (command.rfind("plugin-param-begin:", 0) == 0)
		{
			if (event.GetParameter("button", 0) != 0) return;
			plugin_drag_param = std::clamp(std::atoi(command.c_str() + 19), 0, kPluginParams - 1); PluginMoveParameter(event);
		}
		else if (command == "plugin-param-move") PluginMoveParameter(event);
		else if (command == "plugin-param-end")
		{
			if (event.GetType() != "mouseleave" || event.GetTargetElement() == element) plugin_drag_param = -1;
		}
		else if (command.rfind("plugin-gui:", 0) == 0 && plugin_shared)
		{
			const int param = std::clamp(std::atoi(command.c_str() + 11), 0, 3);
			InterlockedExchange(&plugin_shared->value[plugin_track][param], (PluginRead(&plugin_shared->value[plugin_track][param]) + 100) % 1100); plugin_next_poll = 0; UpdatePluginWindow();
		}
		else if (command.rfind("plugin-bind:", 0) == 0 && plugin_shared)
		{
			const int param = std::clamp(std::atoi(command.c_str() + 12), 0, kPluginParams - 1);
			const int target = std::atoi(event.GetParameter<Rml::String>("value", "0").c_str());
			if (target == 0 || (target <= kMaxTracks && target > 0 && plugin_track_is_auto[target - 1])) InterlockedExchange(&plugin_shared->binding[plugin_track][param], target);
		}
		else if (command.rfind("plugin-auto:", 0) == 0 && plugin_shared)
		{
			const int param = std::clamp(std::atoi(command.c_str() + 12), 0, kPluginParams - 1);
			InterlockedCompareExchange(&plugin_shared->request, plugin_track * kPluginParams + param + 1, 0);
		}
		else if (command == "add-audio") AddMockTrack(true);
		else if (command == "add-automation") AddVolumeAutomation();
		else if(command.rfind("automation-point:",0)==0) AutomationClick(std::atoi(command.c_str()+17),event);
		else if (command == "add-instruments")
		{
#if defined RMLUI_PLATFORM_WIN32
			OpenToolWindow(instruments_process, instruments_process_id, L"--instruments --session=" + std::to_wstring(mixer_session_id));
#endif
		}
		else if (command == "instrument-search") RefreshInstrumentList();
		else if (command.rfind("instrument-favorite:", 0) == 0) { ToggleInstrumentFavorite(std::atoi(command.c_str() + command.find_last_of(':') + 1)); event.StopPropagation(); }
		else if (command.rfind("instrument-select:", 0) == 0) { instrument_selected = std::atoi(command.c_str() + command.find_last_of(':') + 1); RefreshInstrumentList(); }
		else if (command == "instrument-add") ChooseMockInstrument();
		else if (command == "mock-record") ToggleMockRecording();
		else if (command.rfind("track-record-arm:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + 17);
			if (track >= 0 && track < track_count && track != 5)
			{
				track_record_armed[track] = !track_record_armed[track];
				if (!track_record_armed[track]) mock_record_clips[track] = nullptr;
				InitialiseTrackRecordButton(track);
			}
			event.StopPropagation();
		}
		else if (command == "track-wheel")
		{
			auto* lane = Get("lane-canvas");
			lane->SetScrollTop(lane->GetScrollTop() + event.GetParameter("wheel_delta_y", 0.f) * 46.f * context->GetDensityIndependentPixelRatio());
			event.StopPropagation();
		}
		else if (command == "block-double-click") event.StopPropagation();
		else if (command == "mixer-pin")
		{
#if defined RMLUI_PLATFORM_WIN32
			if (mixer_window && daw_native_window && SetWindowPos(daw_native_window, mixer_topmost ? HWND_NOTOPMOST : HWND_TOPMOST,
				0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
			{
				mixer_topmost = !mixer_topmost;
				Get("mixer-pin")->SetClass("pinned", mixer_topmost);
				Get("mixer-pin")->SetAttribute("title", mixer_topmost ? u8"\u6700\u524d\u9762\u56fa\u5b9a\u3092\u89e3\u9664" : u8"\u6700\u524d\u9762\u306b\u56fa\u5b9a");
			}
#endif
		}
		else if (command.rfind("mixer-select:", 0) == 0) SelectMixerChannel(std::atoi(command.c_str() + command.find_last_of(':') + 1));
		else if (command.rfind("mixer-channel-begin:", 0) == 0)
		{
			mixer_drag_channel = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			SelectMixerChannel(mixer_drag_channel);
			MoveMixerChannel(event);
		}
		else if (command.rfind("mixer-channel-reset:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			SetTrackGain(track, 0.f); UpdateMixerChannel(track);
		}
		else if (command.rfind("mixer-channel-mute:", 0) == 0 || command.rfind("mixer-channel-solo:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			ToggleTrackSwitch(track, command.find("-mute:") != Rml::String::npos);
		}
		else if (command == "rename-key")
		{
			const int key = event.GetParameter("key_identifier", 0);
			if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_ESCAPE)
			{
				EndTrackRename(key == Rml::Input::KI_RETURN);
				event.StopImmediatePropagation();
			}
		}
		else if (command == "rename-blur") EndTrackRename(true);
		else if (command == "tempo-begin")
		{
			if (event.GetParameter("button", 0) != 0) return;
			tempo_dragging = true;
			tempo_start_y = event.GetParameter("mouse_y", 0.f);
			tempo_start_bpm = bpm;
		}
		else if (command.rfind("track-reorder:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			auto* target = event.GetTargetElement();
			if (target->GetTagName() == "input") return;
			while (target && target != element && !target->IsClassSet("track-controls") && !target->IsClassSet("track-color-button")) target = target->GetParentNode();
			if (track != 5 && target == element && event.GetParameter("button", 0) == 1)
			{
				BeginTrackRename(track); event.StopImmediatePropagation(); return;
			}
			if (track != 5 && target == element && event.GetParameter("button", 0) == 0)
			{
				DismissTrackColors(); DismissTrackFx(); DismissLoopMenu();
				reorder_track = track; reorder_start_y = event.GetParameter("mouse_y", 0.f);
			}
		}
		else if (command.rfind("track-knob:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			const bool pan = command.find(":pan:") != Rml::String::npos;
			track_knob_drag = {track, pan, event.GetParameter(pan ? "mouse_y" : "mouse_x", 0.f), pan ? track_channels[track].pan : track_channels[track].gain_db};
		}
		else if (command.rfind("knob-reset:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			if (command.find(":pan:") != Rml::String::npos) track_channels[track].pan = 0.f;
			else SetTrackGain(track, 0.f);
			UpdateTrackKnobs(track);
			event.StopPropagation();
		}
		else if (command.rfind("track-mute:", 0) == 0 || command.rfind("track-solo:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			ToggleTrackSwitch(track, command.rfind("track-mute:", 0) == 0);
			event.StopPropagation();
		}
		else if (command.rfind("track-fx:", 0) == 0)
		{
			DismissTrackColors(); DismissLoopMenu();
			ShowTrackFx(std::atoi(command.c_str() + command.find_last_of(':') + 1), element);
			event.StopPropagation();
		}
		else if (command.rfind("track-effect:", 0) == 0 && track_fx_open >= 0)
		{
			const int effect = std::atoi(command.c_str() + command.find_last_of(':') + 1);
			if(effect<0 || effect>2) return;
			bool& enabled = track_channels[track_fx_open].effects[effect];
			enabled = !enabled; element->SetClass("active", enabled);
			if(shared_volume) { LONG flags=0; for(int i=0;i<3;++i) if(track_channels[track_fx_open].effects[i]) flags|=1<<i; InterlockedExchange(shared_volume+kFxStateBase+track_fx_open,flags); }
			bool any = false; for (bool fx : track_channels[track_fx_open].effects) any |= fx;
			Get(Rml::CreateString("track-fx-%d", track_fx_open).c_str())->SetClass("active", any);
			event.StopPropagation();
		}
		else if (command.rfind("color-menu:", 0) == 0)
		{
			DismissTrackFx();
			DismissLoopMenu();
			if (Rml::Element* menu = Get("view-menu")) menu->SetClass("open", false);
			ShowTrackColors(std::atoi(command.c_str() + 11), element, event);
		}
		else if (command.rfind("color-preset:", 0) == 0)
		{
			ApplyTrackColor(color_menu_track, std::atoi(command.c_str() + 13));
			DismissTrackColors();
			event.StopPropagation();
		}
		else if (command.rfind("open-piano:", 0) == 0)
		{
			OpenPianoWindow(std::atoi(command.c_str() + 11));
			event.StopPropagation();
		}
		else if (command == "view-menu")
		{
			DismissTrackFx();
			DismissTrackColors();
			if (Rml::Element* menu = Get("view-menu")) menu->SetClass("open", !menu->IsClassSet("open"));
			event.StopPropagation();
		}
		else if (command == "open-mixer")
		{
			OpenMixerWindow();
			if (Rml::Element* menu = Get("view-menu")) menu->SetClass("open", false);
			event.StopPropagation();
		}
		else if (command == "mixer-fader-begin")
		{
			mixer_fader_dragging = true;
			MoveMixerFader(event);
			event.StopPropagation();
		}
		else if (command == "mixer-fader-move") { MoveMixerChannel(event); if (mixer_fader_dragging) MoveMixerFader(event); }
		else if (command == "mixer-fader-end")
		{
			if (event.GetType() == "mouseleave" && event.GetTargetElement() != element) return;
			mixer_fader_dragging = false; mixer_drag_channel = -1;
		}
		else if (command == "mixer-fader-reset") { mixer_gain_db = 0.f; UpdateMixerGain(); }
		else if(command.rfind("mixer-builtin:",0)==0) {
			int i=std::atoi(command.c_str()+14); if(i>=0 && i<3 && shared_volume) { InterlockedXor(shared_volume+kFxStateBase+mixer_selected_channel,1<<i); ShowMixerRack(); }
		}
		else if (command == "mixer-add-effect")
		{
			AddMixerEffect();
		}
		else if (command == "mixer-remove-effect")
		{
			RemoveMixerEffect(element->GetParentNode());
			event.StopPropagation();
		}
		else if (command == "toggle:effect")
		{
			ToggleMixerEffect(element);
			event.StopPropagation();
		}
		else if (command == "play")
		{
			if (playing && mock_recording) StopMockRecording();
			playing = !playing;
			if (Rml::Element* button = Get("play-button")) button->SetClass("active", playing);
			SetText("play-label", playing ? "PAUSE" : "PLAY");
		}
		else if (command == "stop")
		{
			if (mock_recording) StopMockRecording();
			playing = false;
			playhead_beat = 0.f;
			if (Rml::Element* button = Get("play-button")) button->SetClass("active", false);
			SetText("play-label", "PLAY");
			UpdatePlayhead();
		}
		else if (command == "loop")
		{
			looping = !looping;
			element->SetClass("active", looping);
			UpdateLoopRange();
		}
		else if (command == "dismiss-loop-menu")
		{
			Get("empty-lane-menu")->SetClass("open", false); empty_paste_track = -1;
			Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
			Get("snap-menu")->SetClass("open", false);
			DismissTrackFx();
			DismissTrackColors();
			DismissLoopMenu();
			if (Rml::Element* menu = Get("view-menu")) menu->SetClass("open", false);
		}
		else if (command == "set-loop-begin")
		{
			loop_start_beat = std::min(loop_menu_beat, kProjectBars * kBeatsPerBar - kSnapStepBeats);
			if (loop_start_beat >= loop_end_beat)
				loop_end_beat = std::min(kProjectBars * kBeatsPerBar, loop_start_beat + kBeatsPerBar);
			UpdateLoopRange();
			DismissLoopMenu();
			event.StopPropagation();
		}
		else if (command == "set-loop-end")
		{
			loop_end_beat = std::max(loop_menu_beat, kSnapStepBeats);
			if (loop_end_beat <= loop_start_beat)
				loop_start_beat = std::max(0.f, loop_end_beat - kBeatsPerBar);
			UpdateLoopRange();
			DismissLoopMenu();
			event.StopPropagation();
		}
		else if (command.rfind("snap-preset:", 0) == 0)
		{
			const int index = std::atoi(command.c_str() + 12);
			snap_enabled = index >= 0;
			if (index >= 0 && index < 8) snap_index = index;
			Get("snap-button")->SetClass("active", snap_enabled);
			SetText("snap-value", snap_enabled ? snap_labels[snap_index] : "OFF");
			Get("snap-menu")->SetClass("open", false);
			event.StopPropagation();
		}
		else if (command == "snap")
		{
			auto* menu = Get("snap-menu");
			const bool open = !menu->IsClassSet("open");
			DismissTrackColors(); DismissTrackFx(); DismissLoopMenu();
			Get("view-menu")->SetClass("open", false);
			const float density = context->GetDensityIndependentPixelRatio();
			menu->SetProperty("left", Rml::CreateString("%.0fpx", std::clamp(element->GetAbsoluteLeft(), 0.f, std::max(0.f, context->GetDimensions().x - 94.f * density))));
			menu->SetProperty("top", Rml::CreateString("%.0fpx", element->GetAbsoluteTop() + element->GetOffsetHeight() + 5.f * density));
			for (int i = -1; i < 8; ++i) Get(Rml::CreateString("snap-option-%d", i + 1).c_str())->SetClass("active", snap_enabled ? i == snap_index : i == -1);
			menu->SetClass("open", open);
			event.StopPropagation();
		}
		else if (command == "zoom-in" || command == "zoom-out")
		{
			const float old_pixels = pixels_per_beat;
			pixels_per_beat = std::clamp(pixels_per_beat + (command == "zoom-in" ? 6.f : -6.f), 18.f, 54.f);
			if (old_pixels != pixels_per_beat) UpdateTimelineGeometry();
			UpdatePlayhead();
		}
		else if (command == "ruler-pointer")
		{
			const int button = event.GetParameter("button", 0);
			if (button == 1)
				ShowLoopMenu(event, element);
			else if (button == 0)
			{
				DismissLoopMenu();
				scrubbing = true;
				ScrubToMouse(event, element);
				event.StopPropagation();
			}
		}
		else if (command == "scrub-move" && scrubbing)
		{
			ScrubToMouse(event, element);
			event.StopPropagation();
		}
		else if (command == "scrub-end")
			scrubbing = false;
		else if (command == "empty-lane-context")
		{
			if (event.GetParameter("button", 0) != 1) return;
			auto* target = event.GetTargetElement();
			while (target && !target->IsClassSet("lane"))
			{
				if (target->IsClassSet("clip")) return;
				target = target->GetParentNode();
			}
			if (!target) return;
			const int track = std::atoi(target->GetId().c_str() + 11);
			if (track < 0 || track >= track_count || track == 5 || track_automation[track]) return;
			EndClipDrag(); DismissLoopMenu(); DismissTrackColors(); DismissTrackFx();
			Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
			Get("view-menu")->SetClass("open", false); Get("snap-menu")->SetClass("open", false);
			empty_paste_track = track;
			empty_paste_beat = std::clamp((event.GetParameter("mouse_x", 0.f) - target->GetAbsoluteLeft()) / pixels_per_beat, 0.f, kProjectBars * kBeatsPerBar);
			clip_paste_track = track;
			SetSelected(".track-header", Get(Rml::CreateString("track-header-%d", track).c_str()));
			auto* menu = Get("empty-lane-menu"); auto* paste = Get("empty-lane-paste");
			paste->SetClass("unavailable", !CanPasteClip(track));
			if (CanPasteClip(track)) paste->RemoveAttribute("disabled"); else paste->SetAttribute("disabled", true);
			const auto size = context->GetDimensions(); const float density = context->GetDensityIndependentPixelRatio();
			menu->SetProperty("left", Rml::CreateString("%.0fpx", std::clamp(event.GetParameter("mouse_x", 0.f), 0.f, std::max(0.f, size.x - 124.f * density))));
			menu->SetProperty("top", Rml::CreateString("%.0fpx", std::clamp(event.GetParameter("mouse_y", 0.f) + 4.f * density, 0.f, std::max(0.f, size.y - 38.f * density))));
			menu->SetClass("open", true); event.StopPropagation();
		}
		else if (command == "empty-lane-paste")
		{
			const int track = empty_paste_track;
			Get("empty-lane-menu")->SetClass("open", false); empty_paste_track = -1;
			if (track >= 0 && track < track_count && track != 5 && !track_automation[track])
			{ clip_paste_track = track; PasteClip(empty_paste_beat); }
			event.StopPropagation();
		}
		else if (command == "clip-cut")
		{
			if (auto* clip = Get(clip_menu_id.c_str())) { SetSelected(".clip", clip); CopyClip(); DeleteMenuClip(); }
			event.StopPropagation();
		}
		else if (command == "clip-copy")
		{
			if (auto* clip = Get(clip_menu_id.c_str())) { SetSelected(".clip", clip); CopyClip(); }
			Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
			event.StopPropagation();
		}
		else if (command == "clip-paste")
		{
			if (auto* clip = Get(clip_menu_id.c_str())) clip_paste_track = ClipTrack(clip);
			Get("clip-context-menu")->SetClass("open", false); clip_menu_id.clear();
			PasteClip(); event.StopPropagation();
		}
		else if (command == "clip-delete") { DeleteMenuClip(); event.StopPropagation(); }
		else if (command.rfind("clip-drag:", 0) == 0)
			BeginClipDrag(event, command.substr(10), element);
		else if (command == "clip-drag-move")
		{ MoveTempo(event); MoveTrackOrder(event); MoveTrackKnob(event); MoveClipDrag(event); MoveAutomation(event); }
		else if (command == "clip-drag-end")
		{
			// Child mouseleave events must not terminate a drag across controls.
			if (event.GetType() == "mouseleave" && event.GetTargetElement() != element) return;
			EndTrackOrder(event.GetType() == "mouseup");
			EndAutomationGesture();
			tempo_dragging = false;
			track_knob_drag = {};
			scrubbing = false;
			EndClipDrag();
		}
		else if (command.rfind("track:", 0) == 0)
		{
			const int track = std::atoi(command.c_str() + 6);
			clip_paste_track = track;
			selected_audio_track = track_audio[track] ? track : -1;
			SetSelected(".track-header", element);
			SetText("selection-name", track_names[std::atoi(command.c_str() + 6)]);
		}
		else if (command.rfind("clip:", 0) == 0)
		{
			SetSelected(".clip", element);
			SetText("selection-name", command.substr(5));
			event.StopPropagation();
		}
		else if (command.rfind("toggle:", 0) == 0)
		{
			element->SetClass("active", !element->IsClassSet("active"));
			event.StopPropagation();
		}
	}

	void OnDetach(Rml::Element*) override { delete this; }

private:
	Rml::String command;
	Rml::Element* element;
};

bool DawKeyDown(Rml::Context* key_context, Rml::Input::KeyIdentifier key, int modifiers, float density, bool priority)
{
	if (priority && !mixer_window && !instrument_window && piano_track < 0 && plugin_track < 0)
	{
		auto* focus = key_context->GetFocusElement();
		bool text_input = false;
		for (auto* node = focus; node; node = node->GetParentNode())
			text_input |= node->GetTagName() == "input" || node->GetTagName() == "textarea";
		if (!text_input && !(modifiers & (Rml::Input::KM_ALT | Rml::Input::KM_META)))
		{
			if (key == Rml::Input::KI_SPACE && !(modifiers & Rml::Input::KM_CTRL))
			{
				if (playing) StopMockRecording();
				playing = !playing;
				Get("play-button")->SetClass("active", playing); SetText("play-label", playing ? "PAUSE" : "PLAY");
				return false;
			}
			if (modifiers & Rml::Input::KM_CTRL)
			{
				if (key == Rml::Input::KI_S) SaveProject();
				else if (key == Rml::Input::KI_O) LoadProject();
				else if (key == Rml::Input::KI_Z) UndoClips(false);
				else if (key == Rml::Input::KI_Y) UndoClips(true);
				else if (key == Rml::Input::KI_C) CopyClip();
				else if (key == Rml::Input::KI_V) PasteClip();
				else if (key == Rml::Input::KI_D)
				{
					if (auto* selected = document->QuerySelector(".clip.selected")) { clip_menu_id = selected->GetId(); DeleteMenuClip(); }
				}
				else return Shell::ProcessKeyDownShortcuts(key_context, key, modifiers, density, priority);
				return false;
			}
		}
	}
	return Shell::ProcessKeyDownShortcuts(key_context, key, modifiers, density, priority);
}

class TimelineEventInstancer final : public Rml::EventListenerInstancer {
public:
	Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element* element) override
	{
		return new TimelineEventListener(value, element);
	}
};
} // namespace

#if defined RMLUI_PLATFORM_WIN32
static void ApplyDarkNativeTitleBar()
{
	if (HWND window = GetActiveWindow())
	{
		daw_native_window = window;
		const BOOL dark_mode = TRUE;
		const COLORREF caption_color = RGB(9, 11, 15);
		const COLORREF text_color = RGB(220, 229, 234);
		DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(20), &dark_mode, sizeof(dark_mode));
		DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(35), &caption_color, sizeof(caption_color));
		DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(36), &text_color, sizeof(text_color));
	}
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, char* command_line, int)
#else
int main(int argc, char** argv)
#endif
{
#if defined RMLUI_PLATFORM_WIN32
	const std::string arguments(command_line ? command_line : "");
	ui_test=arguments.find("--ui-test")!=std::string::npos;
	const auto density_argument=arguments.find("--ui-density="); if(density_argument!=std::string::npos) ui_density=std::clamp(float(std::atof(arguments.c_str()+density_argument+13)),.5f,2.f);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	mixer_window = arguments.find("--mixer") != std::string::npos;
	instrument_window = arguments.find("--instruments") != std::string::npos;
	const auto target_argument=arguments.find("--target=");
	if(target_argument!=std::string::npos) instrument_target=std::atoi(arguments.c_str()+target_argument+9);
	const auto slot_argument=arguments.find("--slot="); if(slot_argument!=std::string::npos) instrument_slot=std::clamp(std::atoi(arguments.c_str()+slot_argument+7),0,3);
	const size_t plugin_argument = arguments.find("--plugin=");
	if (plugin_argument != std::string::npos) plugin_track = std::atoi(arguments.c_str() + plugin_argument + 9);
	const size_t session_argument = arguments.find("--session=");
	mixer_session_id = session_argument == std::string::npos ? GetCurrentProcessId() : std::strtoul(arguments.c_str() + session_argument + 10, nullptr, 10);
	if ((mixer_window || instrument_window || plugin_track >= 0 || arguments.find("--piano=")!=std::string::npos) && session_argument != std::string::npos)
		mixer_owner_process = OpenProcess(SYNCHRONIZE, FALSE, mixer_session_id);
	const size_t piano_argument = arguments.find("--piano=");
	if (piano_argument != std::string::npos) piano_track = std::atoi(arguments.c_str() + piano_argument + 8);
#else
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument(argv[i]);
		if (argument == "--mixer") mixer_window = true;
		if (argument == "--instruments") instrument_window = true;
		if (argument.rfind("--session=", 0) == 0) mixer_session_id = std::strtoul(argument.c_str() + 10, nullptr, 10);
		if (argument.rfind("--piano=", 0) == 0) piano_track = std::atoi(argument.c_str() + 8);
	}
#endif
	if (piano_track < -1 || piano_track >= kMaxTracks) piano_track = -1;
	if (plugin_track < -1 || plugin_track >= kMaxTracks) plugin_track = -1;
	const bool piano_window = piano_track >= 0;
	if (piano_track >= 6) track_names[piano_track] = "instruments";
	const int window_width = plugin_track >= 0 ? 620 : instrument_window ? 620 : piano_window ? 1080 : mixer_window ? 900 : 1440;
	const int window_height = plugin_track >= 0 ? 620 : instrument_window ? 610 : piano_window ? 640 : mixer_window ? 480 : 900;
	const Rml::String window_title = plugin_track >= 0 ? Rml::CreateString("Kinu Parameters - Track %d", plugin_track + 1) : instrument_window ? "KinuDAW VST3" : piano_window ? Rml::String("KinuDAW Piano Roll - ") + track_names[piano_track] : mixer_window ? "KinuDAW Mixer" : "KinuDAW";
	if (!Shell::Initialize()) return -1;
	if (!Backend::Initialize(window_title.c_str(), window_width, window_height, true))
	{
		Shell::Shutdown();
		return -1;
	}
#if defined RMLUI_PLATFORM_WIN32
	ApplyDarkNativeTitleBar();
#endif

	DawSystem daw_system; daw_system.base=Backend::GetSystemInterface();
	Rml::SetSystemInterface(&daw_system);
	Rml::SetRenderInterface(Backend::GetRenderInterface());
	if (!Rml::Initialise()) return -1;
	context = Rml::CreateContext("daw", {window_width, window_height});
	if (!context) return -1;
	if(ui_density>0) context->SetDensityIndependentPixelRatio(ui_density);

	Rml::Debugger::Initialise(context);
	TimelineEventInstancer event_instancer;
	Rml::Factory::RegisterEventListenerInstancer(&event_instancer);
	Rml::ElementInstancerGeneric<TransportWave> wave_instancer;
	Rml::Factory::RegisterElementInstancer("transport-wave", &wave_instancer);
	Shell::LoadFonts();
	const bool bold_font_loaded = Rml::LoadFontFace("basic/daw_timeline/data/fonts/LINESeedJP-Bold.ttf", "LINESeedJP",
		Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
	if (!bold_font_loaded)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not load the bundled LINE Seed JP fonts.");
		return -1;
	}

	document = context->LoadDocument(plugin_track >= 0 ? "basic/daw_timeline/data/plugin.rml" : instrument_window ? "basic/daw_timeline/data/instruments.rml" : piano_window ? "basic/daw_timeline/data/piano_roll.rml" : mixer_window ? "basic/daw_timeline/data/mixer.rml" : "basic/daw_timeline/data/timeline.rml");
	if (!document) return -1;
	if (!piano_window) InitialiseVolumeSync();
	LoadPluginCatalog();
	InitialisePluginState();
	InitialisePianoShared();
	document->Show();
	context->Update();
	if (plugin_track >= 0) InitialisePluginWindow();
	else if (instrument_window) InitialiseInstrumentPicker();
	else if (piano_window) InitialisePianoRoll();
	else if (mixer_window) InitialiseMixerTracks();
	else { ApplyTrackOrder(); UpdateTimelineGeometry(); UpdatePlayhead(); for (int track = 0; track < track_count; ++track) { InitialiseTrackRecordButton(track); ApplyTrackColor(track, track_color_presets[track]); UpdateTrackKnobs(track); } SetText("selection-name", "TRACK1"); }
	previous_frame = std::chrono::steady_clock::now();
	if (!mixer_window && !piano_window && !instrument_window && plugin_track < 0) {
		for (const auto& clip:clips) if(auto* e=Get(clip.id.c_str())) e->GetParentNode()->RemoveChild(e);
		clips.clear(); audio_engine=std::make_unique<Kinu::AudioEngine>(); SyncAudio();
		for(int t=0;t<track_count;++t) if(!track_audio[t] && t!=5) Get(Rml::CreateString("track-lane-%d",t).c_str())->SetAttribute("ondblclick",Rml::CreateString("open-piano:%d",t));
		std::string error; if(!audio_engine->start(error)) MockNotice(error);
		else MockNotice(Rml::CreateString("WASAPI / 48 kHz / %.2f ms device period",audio_engine->bufferFrames()*1000./audio_engine->nativeSampleRate()));
	}
#if defined RMLUI_PLATFORM_WIN32
	InstallNativeResize();
	if(audio_engine) InitialiseMidiInput();
	if (!mixer_window && !piano_window && !instrument_window && plugin_track < 0) DragAcceptFiles(daw_native_window, TRUE);
	if(audio_engine && ui_test) {
		ImportAudioFile(3,BinaryFolder()/"ui-tone.wav",0);
		SaveClipUndo(); auto* midi=AddMockAudioClip(0,"MIDI",0,16,true); if(midi) midi->notes={{60,100,0,.5},{64,90,1,.5},{67,110,2,1}};
        if(midi) { midi->notes[1].channel=3; midi->controls={{0xb0,1,64,0},{0xe3,0,64,1}}; }
		custom_track_names[3]=u8"音声録音・日本語の長いトラック名を確認するテスト"; ApplyTrackOrder();
		SetSelected(".track-header",Get("track-header-3")); AddVolumeAutomation();
		volume_automation[track_count-1].points={{0,0},{4,-24},{8,0}}; RefreshAutomation(track_count-1);
		for(const auto& p:installed_plugins) if(p.name=="Vital") { std::string error; audio_engine->loadPlugin(0,p,error); break; }
		project_path=BinaryFolder()/"ui-test.kinu"; SaveProject();
		if(midi) midi->start_beat=12;
		LoadProjectFile(project_path);
		bool roundTrip=false; for(const auto& c:clips) if(c.notes.size()==3 && c.notes[1].channel==3 && c.controls.size()==2 && c.controls[1].status==0xe3 && c.start_beat==0) roundTrip=true;
		{ std::ofstream report(BinaryFolder()/"ui-test-result.json"); report<<nlohmann::json({{"projectRoundTrip",roundTrip},{"clips",clips.size()},{"tracks",track_count},{"vitalLoaded",audio_engine->hasPlugin(0)}}).dump(2); }
		OpenPianoWindow(0); OpenMixerWindow(); playing=true; looping=true; loop_end_beat=4; Get("play-button")->SetClass("active",true); SetText("play-label","PAUSE"); SyncAudio();
	}
#endif

	bool running = true;
	if(audio_engine && !std::filesystem::exists(BinaryFolder()/"plugins.json")) StartPluginScan();
	while (running)
	{
		bool volume_peer_active = mixer_window || instrument_window || plugin_track >= 0;
#if defined RMLUI_PLATFORM_WIN32
		volume_peer_active |= mixer_process && WaitForSingleObject(mixer_process, 0) == WAIT_TIMEOUT;
		volume_peer_active |= instruments_process != nullptr;
		for (HANDLE process : plugin_processes) volume_peer_active |= process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
		if (MixerOwnerExited()) break;
#endif
		running = Backend::ProcessEvents(context, &DawKeyDown, !volume_peer_active && !playing && !audio_engine);
		if (!running) break;
		if(ui_density>0 && std::abs(context->GetDensityIndependentPixelRatio()-ui_density)>.001f) context->SetDensityIndependentPixelRatio(ui_density);
		if(catalog_scan_process && WaitForSingleObject(catalog_scan_process,0)!=WAIT_TIMEOUT) {
			DWORD code=1; GetExitCodeProcess(catalog_scan_process,&code); CloseHandle(catalog_scan_process); catalog_scan_process=nullptr;
			// Picker indices remain stable until any currently open picker closes.
			if(instruments_process) { TerminateProcess(instruments_process,0); CloseHandle(instruments_process); instruments_process=nullptr; if(shared_volume) InterlockedExchange(shared_volume+kMaxTracks*2,0); }
			LoadPluginCatalog(); MockNotice(code==0?u8"VST3の検出が完了しました":u8"VST3スキャンに失敗しました");
		}
		if (!piano_window && !instrument_window && plugin_track < 0) ReadVolumeSync();
		const auto now = std::chrono::steady_clock::now();
		const float delta_seconds = std::chrono::duration<float>(now - previous_frame).count();
		previous_frame = now;
		if(audio_engine) { ReadPianoEdits(); ReadPluginBindings(); SyncAudio(); }
		if(audio_engine) RefreshAutomationGeometry();
		if (playing && !scrubbing)
		{
			if(audio_engine) playhead_beat=float(audio_engine->position());
			else playhead_beat += delta_seconds * bpm / 60.f;
			const float playback_end = looping ? loop_end_beat : kProjectBars * kBeatsPerBar;
			if (playhead_beat >= playback_end)
				{ playhead_beat = looping ? loop_start_beat : 0.f; if(audio_engine && !looping) { StopMockRecording(); playing=false; Get("play-button")->SetClass("active",false); SetText("play-label","PLAY"); } }
			UpdatePlayhead();
			if(audio_engine) audio_last_position=playhead_beat;
		}
		if (plugin_track >= 0) UpdatePluginWindow();
		else if (instrument_window) {}
		else if (piano_window) UpdatePianoRoll();
		else if (!mixer_window) { UpdateMockTracks(); PublishMixerTracks(); UpdateTrackMeters(float(Rml::GetSystemInterface()->GetElapsedTime())); }
		else { ReadMixerTracks(); UpdateMixerMeters(Rml::GetSystemInterface()->GetElapsedTime()); }
		context->Update();
		Backend::BeginFrame();
		if (rename_focus_pending && rename_track >= 0)
		{
			rename_focus_pending = false;
			auto* input = static_cast<Rml::ElementFormControlInput*>(Get(Rml::CreateString("track-name-input-%d", rename_track).c_str()));
			input->Focus(); input->Select(); context->Update();
		}
		context->Render();
		Backend::PresentFrame();
	}
	CloseMidiInput();
	if(audio_engine) { StopMockRecording(); SaveProject(true); audio_engine.reset(); }
	if(piano_shared && piano_track>=0) PianoRelease();
	if(piano_shared) { UnmapViewOfFile(piano_shared); piano_shared=nullptr; }
	if(piano_mapping) { CloseHandle(piano_mapping); piano_mapping=nullptr; }

#if defined RMLUI_PLATFORM_WIN32
	CloseNativeIntegration();
#endif
	document->Close();
	document = nullptr;
	Rml::Shutdown();
	CloseVolumeSync();
	ClosePluginState();
	Backend::Shutdown();
	Shell::Shutdown();
#if defined RMLUI_PLATFORM_WIN32
	if (mixer_process) { CloseHandle(mixer_process); mixer_process = nullptr; }
	if (instruments_process) CloseHandle(instruments_process);
	if(catalog_scan_process) CloseHandle(catalog_scan_process);
	for (HANDLE& process : piano_processes) if (process) { CloseHandle(process); process = nullptr; }
	for (HANDLE& process : plugin_processes) if (process) { CloseHandle(process); process = nullptr; }
#endif
	return 0;
}
