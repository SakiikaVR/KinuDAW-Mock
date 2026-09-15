// Copyright (c) 2026 KinuDAW contributors. MIT License.
constexpr int kPianoHighestPitch=108,kPianoLowestPitch=21;
struct SharedPianoTrack { LONG gate,revision,pressed,gesture; int count; char clip[128],label[128]; Kinu::Note notes[128]; };
struct SharedPiano { SharedPianoTrack tracks[kMaxTracks]; };
HANDLE piano_mapping=nullptr; SharedPiano* piano_shared=nullptr;
std::vector<Kinu::Note> piano_notes;
int piano_revision=-1,piano_visible=0,piano_drag=-1,piano_audition=-1;
bool piano_resize=false; float piano_density=0,piano_start_x=0,piano_start_y=0;
Kinu::Note piano_original;
void PreparePiano(int track);
void ReadPianoEdits();
void InitialisePianoShared() {
    auto name=L"Local\\KinuPiano-v3-"+std::to_wstring(mixer_session_id);
    piano_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedPiano),name.c_str());
    bool fresh=GetLastError()!=ERROR_ALREADY_EXISTS;
    if(piano_mapping) piano_shared=static_cast<SharedPiano*>(MapViewOfFile(piano_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedPiano)));
    if(fresh && piano_shared) { std::memset(piano_shared,0,sizeof(SharedPiano)); for(auto& t:piano_shared->tracks) t.pressed=-1; }
}
Rml::String PianoPitchName(int pitch) { const char* names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return Rml::CreateString("%s%d",names[pitch%12],pitch/12-1); }
bool PianoBlack(int pitch) { int s=pitch%12; return s==1||s==3||s==6||s==8||s==10; }
void PianoAddElement(const char* parent,const Rml::String& id,const char* cls,const Rml::String& text="") {
    auto e=document->CreateElement("div"); e->SetId(id); e->SetClass(cls,true); e->SetInnerRML(text); Get(parent)->AppendChild(std::move(e));
}
void PianoPublish() {
    if(!piano_shared) return; auto& shared=piano_shared->tracks[piano_track];
    if(InterlockedCompareExchange(&shared.gate,1,0)) return;
    shared.count=int(piano_notes.size()); std::copy(piano_notes.begin(),piano_notes.end(),shared.notes);
    piano_revision=InterlockedIncrement(&shared.revision); InterlockedExchange(&shared.gate,0);
}
void PianoRelease() {
    if(piano_shared && piano_track>=0) InterlockedExchange(&piano_shared->tracks[piano_track].pressed,-1);
    if(piano_audition>=0) { if(auto* key=Get(Rml::CreateString("piano-key-%d",piano_audition).c_str())) key->SetClass("active",false); }
    piano_audition=-1; piano_drag=-1;
}
void UpdatePianoRoll() {
    if(piano_shared && piano_drag<0) { auto& s=piano_shared->tracks[piano_track];
        if(piano_revision!=InterlockedCompareExchange(&s.revision,0,0) && !InterlockedCompareExchange(&s.gate,1,0)) {
            piano_notes.assign(s.notes,s.notes+std::clamp(s.count,0,128)); piano_revision=s.revision;
            SetText("piano-track-name",s.label); InterlockedExchange(&s.gate,0);
        }
    }
    float density=context->GetDensityIndependentPixelRatio(),row=18*density,beat=64*density; piano_density=density;
    for(const char* id:{"piano-grid","piano-ruler-content","piano-velocity-content"}) Get(id)->SetProperty("width",Rml::CreateString("%.0fpx",16*beat));
    for(const char* id:{"piano-grid","piano-key-content"}) Get(id)->SetProperty("height",Rml::CreateString("%.0fpx",88*row));
    for(int p=kPianoHighestPitch;p>=kPianoLowestPitch;--p) {
        auto* gridRow=Get(Rml::CreateString("piano-row-%d",p).c_str()); gridRow->SetProperty("top",Rml::CreateString("%.1fpx",(108-p)*row)); gridRow->SetProperty("height",Rml::CreateString("%.1fpx",row));
        // Natural keys extend halfway across neighbouring accidental rows.
        // The narrow black key sits above them, leaving a continuous white-key
        // surface at the right, with matching visual and input geometry.
        float above=!PianoBlack(p) && p<108 && PianoBlack(p+1)?row*.5f:0;
        float below=!PianoBlack(p) && p>21 && PianoBlack(p-1)?row*.5f:0;
        auto* key=Get(Rml::CreateString("piano-key-%d",p).c_str());
        key->SetProperty("top",Rml::CreateString("%.1fpx",(108-p)*row-above)); key->SetProperty("height",Rml::CreateString("%.1fpx",row+above+below));
        key->SetProperty("padding-top",Rml::CreateString("%.1fpx",above));
    }
    for(int b=0;b<=16;++b) { Get(Rml::CreateString("piano-beat-%d",b).c_str())->SetProperty("left",Rml::CreateString("%.0fpx",b*beat)); if(b%4==0) Get(Rml::CreateString("piano-bar-%d",b/4).c_str())->SetProperty("left",Rml::CreateString("%.0fpx",b*beat)); }
    while(piano_visible>int(piano_notes.size())) { --piano_visible; for(const char* prefix:{"piano-note-","piano-velocity-"}) { auto* e=Get((std::string(prefix)+std::to_string(piano_visible)).c_str()); e->GetParentNode()->RemoveChild(e); } }
    while(piano_visible<int(piano_notes.size())) { int i=piano_visible++;
        PianoAddElement("piano-grid",Rml::CreateString("piano-note-%d",i),"piano-note");
        Get(Rml::CreateString("piano-note-%d",i).c_str())->SetAttribute("onmousedown",Rml::CreateString("piano-note:%d",i));
        Get(Rml::CreateString("piano-note-%d",i).c_str())->SetAttribute("onmousewheel",Rml::CreateString("piano-velocity:%d",i));
        PianoAddElement("piano-velocity-content",Rml::CreateString("piano-velocity-%d",i),"piano-velocity-bar");
    }
    for(int i=0;i<int(piano_notes.size());++i) { auto& note=piano_notes[i]; auto* e=Get(Rml::CreateString("piano-note-%d",i).c_str());
        e->SetProperty("left",Rml::CreateString("%.0fpx",note.beat*beat)); e->SetProperty("top",Rml::CreateString("%.0fpx",(108-note.pitch)*row+2*density)); e->SetProperty("width",Rml::CreateString("%.0fpx",std::max(2.,note.duration*beat-2*density))); e->SetProperty("height",Rml::CreateString("%.0fpx",row-3*density));
        if(e->GetInnerRML()!=PianoPitchName(note.pitch)) e->SetInnerRML(PianoPitchName(note.pitch));
        auto* v=Get(Rml::CreateString("piano-velocity-%d",i).c_str()); v->SetProperty("left",Rml::CreateString("%.0fpx",note.beat*beat)); v->SetProperty("height",Rml::CreateString("%.0f%%",note.velocity/127.f*90));
    }
    auto* scroll=Get("piano-scroll"); Get("piano-key-content")->SetProperty("top",Rml::CreateString("%.0fpx",-scroll->GetScrollTop()));
    for(const char* id:{"piano-ruler-content","piano-velocity-content"}) Get(id)->SetProperty("left",Rml::CreateString("%.0fpx",-scroll->GetScrollLeft()));
}
void InitialisePianoRoll() {
    document->QuerySelector(".piano-keyboard")->SetAttribute("onmousedown","piano-keyboard");
    for(int p=108;p>=21;--p) { int s=p%12; bool black=s==1||s==3||s==6||s==8||s==10;
        PianoAddElement("piano-key-content",Rml::CreateString("piano-key-%d",p),black?"piano-key-black":"piano-key-white",PianoPitchName(p));
        Get(Rml::CreateString("piano-key-%d",p).c_str())->SetAttribute("onmousedown",Rml::CreateString("piano-key:%d",p));
        PianoAddElement("piano-grid",Rml::CreateString("piano-row-%d",p),black?"piano-row-black":"piano-row-white");
    }
    for(int b=0;b<=16;++b) { PianoAddElement("piano-grid",Rml::CreateString("piano-beat-%d",b),b%4==0?"piano-bar-line":"piano-beat-line"); if(b%4==0) PianoAddElement("piano-ruler-content",Rml::CreateString("piano-bar-%d",b/4),"piano-bar-label",std::to_string(b/4+1)); }
    UpdatePianoRoll(); context->Update(); Get("piano-scroll")->SetScrollTop((108-78)*18*piano_density); UpdatePianoRoll();
}
bool PianoCommand(const Rml::String& command,Rml::Event& event) {
    if(piano_track<0 || command.rfind("piano-",0)!=0) return false;
    float x=event.GetParameter("mouse_x",0.f),y=event.GetParameter("mouse_y",0.f); int button=event.GetParameter("button",0);
    if(piano_shared && (command=="piano-add" || command.rfind("piano-note:",0)==0 || command.rfind("piano-velocity:",0)==0)) InterlockedIncrement(&piano_shared->tracks[piano_track].gesture);
    if(command=="piano-release") PianoRelease();
    else if(command.rfind("piano-key:",0)==0 || command=="piano-keyboard") {
        int pitch;
        if(command=="piano-keyboard") {
            float local=(y-Get("piano-key-content")->GetAbsoluteTop())/(18*piano_density);
            pitch=std::clamp(108-int(std::floor(local)),21,108);
            if(PianoBlack(pitch) && x-Get("piano-key-content")->GetAbsoluteLeft()>=47*piano_density) pitch+=local-std::floor(local)<.5f?1:-1;
        } else pitch=std::atoi(command.c_str()+10);
        PianoRelease(); piano_audition=std::clamp(pitch,21,108); Get(Rml::CreateString("piano-key-%d",piano_audition).c_str())->SetClass("active",true); if(piano_shared) InterlockedExchange(&piano_shared->tracks[piano_track].pressed,piano_audition);
    }
    else if(command=="piano-add") {
        auto* target=event.GetTargetElement(); while(target && target!=Get("piano-grid")) { if(target->IsClassSet("piano-note")) return true; target=target->GetParentNode(); }
        if(piano_notes.size()<128) { int pitch=std::clamp(108-int((y-Get("piano-grid")->GetAbsoluteTop())/(18*piano_density)),21,108); double beat=std::clamp(std::round((x-Get("piano-grid")->GetAbsoluteLeft())/(64*piano_density)*4)/4.,0.,15.75); piano_notes.push_back({pitch,100,beat,.5}); PianoPublish(); }
    }
    else if(command.rfind("piano-note:",0)==0) {
        int i=std::atoi(command.c_str()+11); if(i>=0 && i<int(piano_notes.size())) {
            if(button==1) { piano_notes.erase(piano_notes.begin()+i); PianoPublish(); }
            else { piano_drag=i; piano_original=piano_notes[i]; piano_start_x=x; piano_start_y=y;
                auto* e=Get(Rml::CreateString("piano-note-%d",i).c_str()); piano_resize=x>e->GetAbsoluteLeft()+e->GetOffsetWidth()-8*piano_density; }
        }
    }
    else if(command=="piano-move" && piano_drag>=0) {
        auto& note=piano_notes[piano_drag]; double delta=std::round((x-piano_start_x)/(64*piano_density)*4)/4.;
        if(piano_resize) note.duration=std::clamp(piano_original.duration+delta,.25,16-note.beat);
        else { note.beat=std::clamp(piano_original.beat+delta,0.,16-note.duration); note.pitch=std::clamp(piano_original.pitch-int(std::round((y-piano_start_y)/(18*piano_density))),21,108); }
        PianoPublish();
    }
    else if(command.rfind("piano-velocity:",0)==0) { int i=std::atoi(command.c_str()+15); if(i>=0 && i<int(piano_notes.size())) { piano_notes[i].velocity=std::clamp(piano_notes[i].velocity-int(event.GetParameter("wheel_delta",0.f))*4,1,127); PianoPublish(); } }
    event.StopPropagation(); return true;
}
