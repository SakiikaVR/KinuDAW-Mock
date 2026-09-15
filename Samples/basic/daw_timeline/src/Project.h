// Copyright (c) 2026 KinuDAW contributors. MIT License.
std::filesystem::path project_path;
struct VolumeAutomation { int target=-1; std::vector<Kinu::AutomationPoint> points; };
std::array<VolumeAutomation,kMaxTracks> volume_automation;
int automation_drag_track=-1,automation_drag_point=-1;
void RefreshAutomationGeometry() {
    for(int t=0;t<track_count;++t) if(track_automation[t]) for(size_t i=0;i<volume_automation[t].points.size();++i)
        if(auto* p=Get(Rml::CreateString("auto-%d-%d",t,int(i)).c_str())) {
            auto point=volume_automation[t].points[i]; p->SetProperty("left",Rml::CreateString("%.1fpx",point.beat*pixels_per_beat));
            p->SetProperty("top",Rml::CreateString("%.1f%%",(6-point.db)/54*80));
        }
}
void RefreshAutomation(int t) {
    auto* lane=Get(Rml::CreateString("track-lane-%d",t).c_str()); auto& curve=volume_automation[t];
    lane->SetAttribute("onmousedown",Rml::CreateString("automation-point:%d",t));
    std::string markup="<span class=\"empty-track-hint\">"+Rml::StringUtilities::EncodeRml(std::string(curve.target>=0?track_names[curve.target]:"Unassigned")+" / Volume")+"</span>";
    for(size_t i=0;i<curve.points.size();++i) markup+="<i class=\"automation-point\" id=\"auto-"+std::to_string(t)+"-"+std::to_string(i)+"\"></i>";
    lane->SetInnerRML(markup); RefreshAutomationGeometry();
}
void AddVolumeAutomation() {
    int target=0;
    if(auto* h=document->QuerySelector(".track-header.selected")) target=std::atoi(h->GetId().c_str()+13);
    if(target==5 || target<0 || target>=track_count || track_automation[target]) target=0;
    int t=AddMockTrack(false,true); if(t<0) return;
    volume_automation[t].target=target; volume_automation[t].points={{0,0},{128,0}}; RefreshAutomation(t);
    MockNotice(u8"音量オートメーション：クリックでポイントを追加・ドラッグ、右クリックで削除");
}
void MoveAutomation(Rml::Event& event) {
    if(automation_drag_track<0 || automation_drag_point<0) return;
    auto& curve=volume_automation[automation_drag_track]; if(automation_drag_point>=int(curve.points.size())) return;
    auto* lane=Get(Rml::CreateString("track-lane-%d",automation_drag_track).c_str()); auto& point=curve.points[automation_drag_point];
    double low=automation_drag_point?curve.points[automation_drag_point-1].beat+.001:0;
    double high=automation_drag_point+1<int(curve.points.size())?curve.points[automation_drag_point+1].beat-.001:128;
    point.beat=std::clamp(double(SnapBeat((event.GetParameter("mouse_x",lane->GetAbsoluteLeft())-lane->GetAbsoluteLeft())/pixels_per_beat)),low,high);
    point.db=std::clamp(6.f-(event.GetParameter("mouse_y",lane->GetAbsoluteTop())-lane->GetAbsoluteTop())/std::max(1.f,lane->GetOffsetHeight()*.8f)*54.f,-48.f,6.f);
    RefreshAutomationGeometry(); event.StopPropagation();
}
void EndAutomationGesture() { automation_drag_track=automation_drag_point=-1; }
void AutomationClick(int t,Rml::Event& event) {
    if(t<0 || t>=track_count || !track_automation[t]) return; auto& curve=volume_automation[t]; auto* target=event.GetTargetElement(); int index=-1;
    if(target->IsClassSet("automation-point")) index=std::atoi(target->GetId().c_str()+target->GetId().find_last_of('-')+1);
    if(event.GetParameter("button",0)==1) { if(index>=0 && index<int(curve.points.size())) { curve.points.erase(curve.points.begin()+index); RefreshAutomation(t); } }
    else {
        if(index<0 && curve.points.size()<64) {
            auto* lane=Get(Rml::CreateString("track-lane-%d",t).c_str()); double beat=std::clamp(double(SnapBeat((event.GetParameter("mouse_x",0.f)-lane->GetAbsoluteLeft())/pixels_per_beat)),0.,128.);
            for(size_t i=0;i<curve.points.size();++i) if(std::abs(curve.points[i].beat-beat)<.01) index=int(i);
            if(index<0) { curve.points.push_back({beat,0}); std::sort(curve.points.begin(),curve.points.end(),[](auto a,auto b){return a.beat<b.beat;}); for(size_t i=0;i<curve.points.size();++i) if(curve.points[i].beat==beat) index=int(i); RefreshAutomation(t); }
        }
        automation_drag_track=t; automation_drag_point=index; MoveAutomation(event);
    } event.StopPropagation();
}
bool audio_last_playing=false; float audio_last_position=0;
std::unique_ptr<Kinu::Scene> render_scene=std::make_unique<Kinu::Scene>();
std::filesystem::path FileDialog(bool save,const wchar_t* filter,const wchar_t* extension) {
    wchar_t path[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=daw_native_window;
    dialog.lpstrFilter=filter; dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.lpstrDefExt=extension;
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)) return path; return {};
}
void SyncAudio() {
    if(!audio_engine) return;
    SetText("audio-xruns",Rml::CreateString("XRUN %u",audio_engine->underruns()));
    auto& scene=*render_scene; scene.count=0; scene.bpm=bpm; scene.end=kProjectBars*kBeatsPerBar;
    scene.loop=looping; scene.loopStart=loop_start_beat; scene.loopEnd=loop_end_beat;
    for(int t=0;t<track_count;++t) {
        const auto& c=track_channels[t]; scene.channels[t]={std::pow(10.f,c.gain_db/20.f),c.pan,c.muted,c.solo};
        std::copy_n(c.effects,3,scene.channels[t].effects);
        scene.channels[t].automationCount=0;
    }
    for(int t=0;t<track_count;++t) if(track_automation[t]) { auto& curve=volume_automation[t]; if(curve.target>=0 && curve.target<track_count) {
        auto& c=scene.channels[curve.target]; c.automationCount=int(std::min<size_t>(64,curve.points.size())); std::copy_n(curve.points.data(),c.automationCount,c.automation.data());
    } }
    for(const auto& clip:clips) {
        if(scene.count>=Kinu::MaxClips) break;
        int t=ClipTrack(Get(clip.id.c_str())); if(t<0 || t==5 || track_automation[t]) continue;
        auto& out=scene.clips[scene.count++]; out.track=t; out.beat=clip.start_beat; out.length=clip.length_beats;
        out.audio=clip.audio; out.offset=clip.source_offset; out.pattern=clip.pattern_length;
        out.noteCount=int(std::min<size_t>(clip.notes.size(),out.notes.size()));
        std::copy_n(clip.notes.data(),out.noteCount,out.notes.data());
    }
    audio_engine->publish(scene);
    if(playing!=audio_last_playing || std::abs(playhead_beat-audio_last_position)>.002f || scrubbing) audio_engine->transport(playing,playhead_beat);
    audio_last_playing=playing; audio_last_position=playing?float(audio_engine->position()):playhead_beat;
    // Position will be sampled again by the UI below; don't interpret ordinary
    // callback progress between frames as a user seek.
    if(playing && !scrubbing) { playhead_beat=audio_last_position; }
    static auto last=std::chrono::steady_clock::now();
    if(std::chrono::steady_clock::now()-last>std::chrono::seconds(30)) { last=std::chrono::steady_clock::now(); SaveProject(true); }
}
void ImportAudioFile(int track,const std::filesystem::path& path,float beat) {
    if(!audio_engine || track<0 || track>=track_count || !track_audio[track]) return;
    std::string error; const auto* media=audio_engine->import(path,error); if(!media) { MockNotice(error); return; }
    float length=float(media->samples.size()/2./48000.*bpm/60.);
    length=std::min(length,kProjectBars*kBeatsPerBar);
    SaveClipUndo(); auto* clip=AddMockAudioClip(track,path.filename().u8string(),beat,length);
    if(!clip) return; clip->audio=media;
    const char* levels[]={u8"▁",u8"▂",u8"▃",u8"▄",u8"▅",u8"▆",u8"▇",u8"█"}; std::string wave;
    for(size_t i=0;i<64;++i) {
        size_t start=i*media->samples.size()/64,end=(i+1)*media->samples.size()/64; float peak=0;
        for(size_t p=start;p<end;++p) peak=std::max(peak,std::abs(media->samples[p])); wave+=levels[std::clamp(int(peak*8),0,7)];
    }
    Get(clip->id.c_str())->SetInnerRML("<b>"+Rml::StringUtilities::EncodeRml(clip->label)+"</b><span class=\"waveform\">"+wave+"</span>");
    MockNotice(u8"音声を読み込みました。PLAYで再生できます。"); SyncAudio();
}
void ImportAudioDialog() {
    auto path=FileDialog(false,L"Audio\0*.wav;*.flac;*.mp3\0All files\0*.*\0\0",nullptr);
    if(path.empty()) return; int t=selected_audio_track;
    if(auto* header=document->QuerySelector(".track-header.selected")) { int selected=std::atoi(header->GetId().c_str()+13); if(selected>=0 && selected<track_count && track_audio[selected]) t=selected; }
    ImportAudioFile(t,path,playhead_beat);
}
void SaveProject(bool recovery) {
    if(!audio_engine) return;
    try {
        std::string stateWarnings;
        auto path=recovery?BinaryFolder()/"recovery.kinu":project_path;
        if(path.empty()) path=FileDialog(true,L"KinuDAW project\0*.kinu\0\0",L"kinu");
        if(path.empty()) return;
        nlohmann::json root={{"format","KinuDAW"},{"version",1},{"bpm",bpm},{"loop",looping},{"loopStart",loop_start_beat},{"loopEnd",loop_end_beat},{"tracks",nlohmann::json::array()},{"clips",nlohmann::json::array()}};
        for(int t=0;t<track_count;++t) {
            auto c=track_channels[t]; nlohmann::json j={{"name",track_names[t]},{"audio",track_audio[t]},{"automation",track_automation[t]},{"gain",c.gain_db},{"pan",c.pan},{"mute",c.muted},{"solo",c.solo},{"color",track_color_presets[t]},{"order",track_order[t]}};
            j["effects"]={c.effects[0],c.effects[1],c.effects[2]};
            if(track_automation[t]) { auto& curve=volume_automation[t]; j["volumeAutomation"]={{"target",curve.target},{"points",nlohmann::json::array()}}; for(auto point:curve.points) j["volumeAutomation"]["points"].push_back({point.beat,point.db}); }
            if(auto* info=audio_engine->pluginInfo(t)) project_plugins[t]=*info;
            const auto& info=project_plugins[t];
            if(!info.uid.empty()) {
                if(!recovery && audio_engine->pluginHealthy(t)) { try { cached_plugin_states[t]=audio_engine->pluginState(t); } catch(const std::exception& e) { stateWarnings+=info.name+": "+e.what()+"; "; } }
                j["plugin"]={{"path",info.path},{"uid",info.uid},{"name",info.name},{"category",info.category},{"vendor",info.vendor},{"state",cached_plugin_states[t]}};
            }
            j["vstEffects"]=nlohmann::json::array();
            for(int s=1;s<4;++s) {
                auto& cached=project_effects[t][s-1]; if(auto* p=audio_engine->pluginInfo(t,s)) cached.info=*p;
                if(!cached.info.uid.empty()) {
                    if(!recovery && audio_engine->pluginHealthy(t,s)) { try { cached.state=audio_engine->pluginState(t,s); } catch(const std::exception& e) { stateWarnings+=cached.info.name+": "+e.what()+"; "; } }
                    const auto& p=cached.info; j["vstEffects"].push_back({{"slot",s},{"path",p.path},{"uid",p.uid},{"name",p.name},{"category",p.category},{"vendor",p.vendor},{"state",cached.state}});
                }
            }
            root["tracks"].push_back(j);
        }
        for(const auto& c:clips) {
            int t=ClipTrack(Get(c.id.c_str())); if(t<0 || t==5) continue;
            nlohmann::json j={{"id",c.id},{"label",c.label},{"track",t},{"beat",c.start_beat},{"length",c.length_beats},{"offset",c.source_offset},{"pattern",c.pattern_length},{"notes",nlohmann::json::array()}};
            if(c.audio) j["file"]=c.audio->path;
            for(const auto& n:c.notes) j["notes"].push_back({{"pitch",n.pitch},{"velocity",n.velocity},{"beat",n.beat},{"duration",n.duration}});
            root["clips"].push_back(j);
        }
        auto temporary=path; temporary+=L".tmp";
        { std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out<<root.dump(2); out.flush(); if(!out) throw std::runtime_error("Project write failed; previous save preserved"); }
        HANDLE f=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE) { FlushFileBuffers(f); CloseHandle(f); }
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Project replace failed; previous save preserved");
        if(!recovery) { project_path=path; MockNotice(stateWarnings.empty()?u8"プロジェクトを保存しました":u8"保存完了。一部の音源は前回の状態を保存しました: "+stateWarnings); }
    } catch(const std::exception& e) { MockNotice(e.what()); }
}
void LoadProjectFile(const std::filesystem::path& path) {
    if(path.empty() || !audio_engine) return;
    try {
        if(std::filesystem::file_size(path)>64*1024*1024) throw std::runtime_error("Project exceeds 64 MB limit");
        std::ifstream in(path); nlohmann::json root; in>>root;
        if(root.at("format")!="KinuDAW" || root.at("version")!=1) throw std::runtime_error("Unsupported project format");
        float tempo=root.at("bpm"); if(!std::isfinite(tempo) || tempo<20 || tempo>300) throw std::runtime_error("Invalid tempo");
        bool projectLoop=root.value("loop",false); float projectLoopStart=root.value("loopStart",0.f),projectLoopEnd=root.value("loopEnd",4.f);
        if(!std::isfinite(projectLoopStart)||!std::isfinite(projectLoopEnd)||projectLoopStart<0||projectLoopEnd>128||projectLoopEnd-projectLoopStart<.25f) throw std::runtime_error("Invalid loop range");
        auto& tracks=root.at("tracks"); if(!tracks.is_array() || !root.at("clips").is_array() || tracks.size()<6 || tracks.size()>32 || root.at("clips").size()>Kinu::MaxClips) throw std::runtime_error("Project track/clip limit exceeded");
        auto next=std::make_unique<Kinu::AudioEngine>(); ClipSnapshot snapshot; std::string warnings;
        std::vector<int> orders;
        for(size_t t=0;t<tracks.size();++t) {
            auto& j=tracks[t]; int order=j.at("order"); orders.push_back(order);
            auto name=j.at("name").get<std::string>(); if(name.size()>1024) throw std::runtime_error("Track name too long");
            for(const char* flag:{"audio","automation","mute","solo"}) if(!j.at(flag).is_boolean()) throw std::runtime_error("Invalid channel flag");
            if(j.contains("effects")) { if(!j["effects"].is_array() || j["effects"].size()!=3) throw std::runtime_error("Invalid built-in effects"); for(auto e:j["effects"]) if(!e.is_boolean()) throw std::runtime_error("Invalid effect flag"); }
            float gain=j.at("gain"),pan=j.at("pan");
            if(!std::isfinite(gain)||!std::isfinite(pan)||gain < -48 || gain>6 || pan < -1 || pan>1 || j.at("color").get<int>()<0 || j.at("color").get<int>()>8) throw std::runtime_error("Invalid channel values");
            if(j.contains("volumeAutomation")) {
                auto curve=j["volumeAutomation"]; int target=curve.at("target"); if(target < -1 || target>=int(tracks.size()) || curve.at("points").size()>64) throw std::runtime_error("Invalid automation routing");
                double previous=-1; for(auto p:curve.at("points")) { double beat=p.at(0); float db=p.at(1); if(!std::isfinite(beat)||!std::isfinite(db)||beat<0||beat>128||beat<=previous||db < -48||db>6) throw std::runtime_error("Invalid automation point"); previous=beat; }
            }
        }
        std::sort(orders.begin(),orders.end()); for(int i=0;i<int(orders.size());++i) if(orders[i]!=i) throw std::runtime_error("Invalid track order");
        for(const auto& j:root.at("clips")) {
            int t=j.at("track"); float beat=j.at("beat"),length=j.at("length");
            if(t<0 || t>=int(tracks.size()) || t==5 || !std::isfinite(beat)||!std::isfinite(length)||beat<0||length<=0||beat+length>128.01) throw std::runtime_error("Invalid clip geometry");
            ClipState c{j.at("id"),j.at("label"),beat,length}; c.source_offset=j.value("offset",0.); c.pattern_length=j.value("pattern",16.);
            if(c.id.rfind("saved-",0)!=0 && c.id.rfind("mock-audio-",0)!=0 && c.id.rfind("pasted-clip-",0)!=0 && c.id.rfind("midi-",0)!=0) throw std::runtime_error("Invalid clip identifier");
            for(auto& old:snapshot.clips) if(old.state.id==c.id) throw std::runtime_error("Duplicate clip identifier");
            if(c.id.size()>128 || c.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos || c.label.size()>4096) throw std::runtime_error("Invalid clip text");
            if(!std::isfinite(c.source_offset)||c.source_offset<0 || c.source_offset>3600 || !std::isfinite(c.pattern_length)||c.pattern_length<.25 || c.pattern_length>128) throw std::runtime_error("Invalid clip source");
            if(j.contains("file")) { std::string error; c.audio=next->import(std::filesystem::u8path(j.at("file").get<std::string>()),error); if(!c.audio) throw std::runtime_error(error+": "+j.at("file").get<std::string>()); }
            for(const auto& n:j.at("notes")) {
                Kinu::Note note{n.at("pitch"),n.at("velocity"),n.at("beat"),n.at("duration")};
                if(note.pitch<0||note.pitch>127||note.velocity<1||note.velocity>127||!std::isfinite(note.beat)||!std::isfinite(note.duration)||note.beat<0||note.beat>128||note.duration<=0||note.duration>128||c.notes.size()>=128) throw std::runtime_error("Invalid MIDI note"); c.notes.push_back(note);
            }
            snapshot.clips.push_back({c,t,"<b>"+Rml::StringUtilities::EncodeRml(c.label)+"</b>",c.audio?"clip audio":"clip"});
        }
        // Validation and media decode complete before changing the current document.
        for(size_t t=0;t<tracks.size();++t) if(tracks[t].contains("plugin")) {
            auto p=tracks[t]["plugin"]; Kinu::PluginInfo info{p.at("path"),p.at("uid"),p.at("name"),p.value("category",""),p.value("vendor","")}; std::string error;
            if(!next->loadPlugin(int(t),info,error) || !next->restorePluginState(int(t),p.at("state").get<std::vector<unsigned char>>())) warnings+=info.name+": "+error+"; ";
        }
        for(size_t t=0;t<tracks.size();++t) if(tracks[t].contains("vstEffects")) for(auto p:tracks[t]["vstEffects"]) {
            int s=p.at("slot"); if(s<1 || s>3) throw std::runtime_error("Invalid VST effect slot");
            Kinu::PluginInfo info{p.at("path"),p.at("uid"),p.at("name"),p.value("category",""),p.value("vendor","")}; std::string error;
            if(!next->loadPlugin(int(t),info,error,s) || !next->restorePluginState(int(t),p.at("state").get<std::vector<unsigned char>>(),s)) warnings+=info.name+": "+error+"; ";
        }
        if(mock_recording) StopMockRecording();
        EndClipDrag(); EndAutomationGesture();
        for(int t=0;t<kMaxTracks;++t) if(piano_processes[t]) { TerminateProcess(piano_processes[t],0); CloseHandle(piano_processes[t]); piano_processes[t]=nullptr; }
        playing=false; audio_engine->stop();
        while(track_count<int(tracks.size())) AddMockTrack(tracks[track_count].at("audio"),tracks[track_count].at("automation"));
        while(track_count>int(tracks.size())) { int t=--track_count; for(const char* prefix:{"track-header-","track-lane-"}) if(auto* e=Get((std::string(prefix)+std::to_string(t)).c_str())) e->GetParentNode()->RemoveChild(e); }
        for(int t=0;t<track_count;++t) {
            auto j=tracks[t]; custom_track_names[t]=j.at("name"); track_audio[t]=j.at("audio"); track_automation[t]=j.at("automation"); track_order[t]=j.at("order"); track_color_presets[t]=j.at("color");
            track_channels[t].gain_db=j.at("gain"); track_channels[t].pan=j.at("pan"); track_channels[t].muted=j.at("mute"); track_channels[t].solo=j.at("solo");
            if(j.contains("effects")) for(int i=0;i<3;++i) track_channels[t].effects[i]=j["effects"].at(i);
            volume_automation[t]={}; if(j.contains("volumeAutomation")) { auto curve=j["volumeAutomation"]; volume_automation[t].target=curve.at("target"); for(auto p:curve.at("points")) volume_automation[t].points.push_back({p.at(0),p.at(1)}); }
            if(shared_volume) { InterlockedExchange(shared_volume+t,LONG(track_channels[t].gain_db*10)); InterlockedExchange(shared_volume+kMaxTracks+t,(track_channels[t].muted?1:0)|(track_channels[t].solo?2:0)); }
            if(shared_volume) { LONG flags=0; for(int i=0;i<3;++i) if(track_channels[t].effects[i]) flags|=1<<i; InterlockedExchange(shared_volume+kFxStateBase+t,flags); }
            project_plugins[t]={}; cached_plugin_states[t].clear();
            for(auto& p:project_effects[t]) p={};
            if(j.contains("vstEffects")) for(auto p:j["vstEffects"]) {
                auto& cache=project_effects[t][p.at("slot").get<int>()-1]; cache.info={p.at("path"),p.at("uid"),p.at("name"),p.value("category",""),p.value("vendor","")}; cache.state=p.at("state").get<std::vector<unsigned char>>();
            }
            if(j.contains("plugin")) { auto p=j["plugin"]; project_plugins[t]={p.at("path"),p.at("uid"),p.at("name"),p.value("category",""),p.value("vendor","")}; cached_plugin_states[t]=p.at("state").get<std::vector<unsigned char>>(); }
        }
        ApplyTrackOrder(); RestoreClips(snapshot); clip_undo.clear(); clip_redo.clear(); clip_clipboard_valid=false;
        for(int t=0;t<track_count;++t) if(track_automation[t]) RefreshAutomation(t);
        for(int t=0;t<track_count;++t) UpdateTrackKnobs(t);
        audio_engine=std::move(next); bpm=tempo; looping=projectLoop; loop_start_beat=projectLoopStart; loop_end_beat=projectLoopEnd;
        playhead_beat=0; audio_last_position=0; audio_last_playing=false; project_path=path;
        // Advance generated IDs beyond anything restored from disk.
        mock_clip_serial+=10000; clip_paste_serial+=10000;
        SetText("tempo-value",Rml::CreateString("%.1f",bpm)); UpdateTimelineGeometry(); SyncAudio(); std::string error; audio_engine->start(error);
        MockNotice(warnings.empty()?u8"プロジェクトを開きました":warnings);
    } catch(const std::exception& e) { MockNotice(e.what()); }
}
void LoadProject() { LoadProjectFile(FileDialog(false,L"KinuDAW project\0*.kinu\0\0",L"kinu")); }
void ExportAudio() {
    if(!audio_engine) return; auto path=FileDialog(true,L"WAV audio\0*.wav\0\0",L"wav"); if(path.empty()) return;
    double end=0; for(const auto& c:clips) end=std::max(end,double(c.start_beat+c.length_beats));
    std::string error; SyncAudio(); if(!audio_engine->exportWav(path,std::max(4.,end),error)) MockNotice(error); else MockNotice(u8"WAVを書き出しました");
}
void AudioSettings(Rml::Element* trigger) {
    if(!audio_engine) return; HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"WASAPI 共有 / 128フレーム（既定）");
    AppendMenuW(menu,MF_STRING,2,L"WASAPI 排他 / 128フレーム（低遅延）");
    AppendMenuW(menu,MF_STRING,3,L"WASAPI 共有 / 512フレーム（安定性優先）");
    POINT point{int(trigger->GetAbsoluteLeft()),int(trigger->GetAbsoluteTop()+trigger->GetOffsetHeight())}; ClientToScreen(daw_native_window,&point);
    UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,daw_native_window,nullptr); DestroyMenu(menu);
    if(choice) { std::string error;
        if(!audio_engine->configure(choice==3?512:128,choice==2,error)) MockNotice(error);
        else MockNotice(Rml::CreateString("WASAPI / %.2f ms device period",audio_engine->bufferFrames()*1000./audio_engine->nativeSampleRate()));
    }
}
void AddVstEffect() {
    int t=track_fx_open; if(!audio_engine || t<0 || t>=track_count || t==5) return;
    int slot=1; while(slot<4 && audio_engine->hasPlugin(t,slot)) ++slot;
    if(slot>=4) { MockNotice(u8"VST3エフェクトは1トラック3個までです"); return; }
    OpenToolWindow(instruments_process,instruments_process_id,L"--instruments --target="+std::to_wstring(t)+L" --slot="+std::to_wstring(slot)+L" --session="+std::to_wstring(mixer_session_id));
}
void PreparePiano(int track) {
    if(!piano_shared || !audio_engine) return;
    ClipState* clip=nullptr;
    if(auto* selected=document->QuerySelector(".clip.selected")) if(ClipTrack(selected)==track) clip=FindClip(selected->GetId());
    if(!clip) for(auto& c:clips) if(ClipTrack(Get(c.id.c_str()))==track) { clip=&c; break; }
    if(!clip) {
        SaveClipUndo(); clip=AddMockAudioClip(track,"MIDI",std::min(playhead_beat,112.f),16,true);
        if(!clip) return; Get(clip->id.c_str())->SetInnerRML("<b>MIDI</b>");
    }
    auto& s=piano_shared->tracks[track]; if(InterlockedCompareExchange(&s.gate,1,0)) return;
    strncpy_s(s.clip,clip->id.c_str(),_TRUNCATE); strncpy_s(s.label,track_names[track],_TRUNCATE);
    s.count=int(std::min<size_t>(128,clip->notes.size())); std::copy_n(clip->notes.data(),s.count,s.notes);
    InterlockedIncrement(&s.revision); InterlockedExchange(&s.gate,0);
}
void ReadPianoEdits() {
    if(!piano_shared || !audio_engine) return;
    static int revisions[kMaxTracks]{},gestures[kMaxTracks]{}; static int pressed[kMaxTracks]; static bool init=false;
    if(!init) { std::fill_n(pressed,kMaxTracks,-1); init=true; }
    for(int t=0;t<track_count;++t) {
        auto& s=piano_shared->tracks[t]; int pitch=InterlockedCompareExchange(&s.pressed,0,0);
        if(piano_processes[t] && WaitForSingleObject(piano_processes[t],0)!=WAIT_TIMEOUT) { InterlockedExchange(&s.pressed,-1); pitch=-1; }
        if(pitch!=pressed[t]) {
            if(pressed[t]>=0) { audio_engine->midi(t,pressed[t],0,false); RecordMidi(t,pressed[t],0,false); }
            if(pitch>=0 && pitch<128) { audio_engine->midi(t,pitch,100,true); RecordMidi(t,pitch,100,true); } pressed[t]=pitch;
        }
        if(revisions[t]!=InterlockedCompareExchange(&s.revision,0,0) && !InterlockedCompareExchange(&s.gate,1,0)) {
            if(auto* clip=FindClip(s.clip)) {
                if(gestures[t]!=s.gesture) { SaveClipUndo(); gestures[t]=s.gesture; }
                clip->notes.assign(s.notes,s.notes+std::clamp(s.count,0,128));
                Get(clip->id.c_str())->SetInnerRML("<b>MIDI / "+std::to_string(clip->notes.size())+" notes</b>");
            }
            revisions[t]=s.revision; InterlockedExchange(&s.gate,0);
        }
    }
}
std::array<HMIDIIN,16> midi_inputs{};
void CALLBACK MidiCallback(HMIDIIN,UINT message,DWORD_PTR owner,DWORD_PTR data,DWORD_PTR) {
    if(message==MIM_DATA) PostMessageW(reinterpret_cast<HWND>(owner),WM_APP+20,0,LPARAM(data));
}
void InitialiseMidiInput() {
    for(UINT i=0;i<std::min<UINT>(midiInGetNumDevs(),midi_inputs.size());++i)
        if(midiInOpen(&midi_inputs[i],i,DWORD_PTR(&MidiCallback),DWORD_PTR(daw_native_window),CALLBACK_FUNCTION)==MMSYSERR_NOERROR) midiInStart(midi_inputs[i]);
}
void CloseMidiInput() { for(auto& midi:midi_inputs) if(midi) { midiInStop(midi); midiInReset(midi); midiInClose(midi); midi=nullptr; } }
void ExternalMidi(DWORD packed) {
    if(!audio_engine) return; int status=packed&0xf0,pitch=(packed>>8)&127,velocity=(packed>>16)&127;
    if(status!=0x90 && status!=0x80) return; bool on=status==0x90 && velocity;
    bool routed=false;
    for(int t=0;t<track_count;++t) if(track_record_armed[t] && !track_audio[t] && !track_automation[t] && t!=5) {
        audio_engine->midi(t,pitch,velocity,on); RecordMidi(t,pitch,velocity,on); routed=true;
    }
    if(!routed) if(auto* selected=document->QuerySelector(".track-header.selected")) {
        int t=std::atoi(selected->GetId().c_str()+13); if(t>=0 && t<track_count && !track_audio[t] && t!=5) audio_engine->midi(t,pitch,velocity,on);
    }
}
std::array<std::array<double,128>,kMaxTracks> record_note_start;
std::array<std::array<int,128>,kMaxTracks> record_note_velocity{};
void RecordMidi(int t,int pitch,int velocity,bool on) {
    if(!mock_recording || t<0 || t>=track_count || pitch<0 || pitch>127 || track_audio[t] || !mock_record_clips[t]) return;
    double beat=(Rml::GetSystemInterface()->GetElapsedTime()-mock_record_start)*bpm/60.;
    if(on) { record_note_start[t][pitch]=beat; record_note_velocity[t][pitch]=velocity; }
    else if(record_note_start[t][pitch]>=0) {
        auto* clip=mock_record_clips[t]; double start=record_note_start[t][pitch];
        if(clip->notes.size()<128) clip->notes.push_back({pitch,std::clamp(record_note_velocity[t][pitch],1,127),start,std::max(.02,beat-start)});
        record_note_start[t][pitch]=-1;
    }
}
void ToggleRecording() {
    if(!audio_engine) return;
    if(mock_recording) { StopMockRecording(); playing=false; Get("play-button")->SetClass("active",false); SetText("play-label","PLAY"); return; }
    if(playhead_beat>=127.75f) { MockNotice(u8"録音開始位置をプロジェクトの終端より前にしてください"); return; }
    bool audio=false; int armed=0;
    for(int t=0;t<track_count;++t) if(t!=5 && track_record_armed[t] && !track_automation[t]) { ++armed; audio|=track_audio[t]; }
    if(!armed) { MockNotice(u8"録音するトラックの丸をクリックして録音待機にしてください"); return; }
    if(clips.size()+armed>Kinu::MaxClips) { MockNotice(u8"録音用クリップの空きがありません"); return; }
    looping=false; if(auto* loop=document->QuerySelector(".loop")) loop->SetClass("active",false); UpdateLoopRange();
    std::string error;
    if(audio) {
        auto folder=BinaryFolder()/"Recordings"; std::filesystem::create_directories(folder);
        auto path=folder/("recording-"+std::to_string(GetTickCount64())+".wav");
        if(!audio_engine->beginRecording(path,error)) { MockNotice(error); return; }
    }
    SaveClipUndo();
    for(auto& row:record_note_start) row.fill(-1);
    for(int t=0;t<track_count;++t) if(t!=5 && track_record_armed[t] && !track_automation[t]) {
        mock_record_clips[t]=AddMockAudioClip(t,track_audio[t]?"Audio recording":"MIDI recording",playhead_beat,.25,true);
        if(mock_record_clips[t]) mock_record_clips[t]->pattern_length=128;
    }
    mock_record_start=Rml::GetSystemInterface()->GetElapsedTime(); mock_recording=true; playing=true;
    Get("mock-record")->SetClass("active",true); Get("play-button")->SetClass("active",true); SetText("play-label","PAUSE");
    MockNotice(audio?u8"WASAPIで音声録音中":u8"MIDI録音中");
}
void FinishRecording() {
    if(!audio_engine) return;
    for(int t=0;t<track_count;++t) for(int p=0;p<128;++p) if(record_note_start[t][p]>=0) RecordMidi(t,p,0,false);
    std::string error; auto path=audio_engine->endRecording(error);
    const Kinu::AudioFile* media=path.empty()?nullptr:audio_engine->import(path,error);
    for(int t=0;t<track_count;++t) if(auto* clip=mock_record_clips[t]) {
        if(track_audio[t] && media) { clip->audio=media; clip->length_beats=std::min(float(media->samples.size()/2./48000.*bpm/60.),128-clip->start_beat); }
        Get(clip->id.c_str())->SetInnerRML("<b>"+Rml::StringUtilities::EncodeRml(track_audio[t]?path.filename().u8string():"MIDI / "+std::to_string(clip->notes.size())+" notes")+"</b>"); UpdateClipGeometry(*clip);
    }
    MockNotice(error.empty()?u8"録音を保存しました":error);
}
