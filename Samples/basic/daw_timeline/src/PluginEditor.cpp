// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "PluginEditor.h"
#include "VstHost.h"
#include "PluginIPC.h"
#include "json.hpp"
#include <RmlUi_Backend.h>
#include <Shell.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <dwmapi.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Kinu {
static std::string utf8(const TChar* value) {
    auto* text=reinterpret_cast<const wchar_t*>(value); int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    std::string result(n>0?n:0,'\0'); if(n>0) { WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),n,nullptr,nullptr); result.pop_back(); } return result;
}
class PluginEditor::Listener final : public Rml::EventListener {
public:
    Listener(PluginEditor& owner,Rml::String command,Rml::Element* element):owner_(owner),command_(std::move(command)),element_(element) {}
    void ProcessEvent(Rml::Event& event) override { try { owner_.command(command_,event,element_); } catch(const std::exception& e) { if(auto* footer=owner_.document_->GetElementById("plugin-status")) footer->SetInnerRML(Rml::StringUtilities::EncodeRml(e.what())); } }
    void OnDetach(Rml::Element*) override { delete this; }
private:
    PluginEditor& owner_; Rml::String command_; Rml::Element* element_;
};
Rml::EventListener* PluginEditor::InstanceEventListener(const Rml::String& value,Rml::Element* element) { return new Listener(*this,value,element); }
PluginEditor::PluginEditor(Plugin& plugin,bool generic):plugin_(plugin) {
    try {
        if(!generic) plugin_.view_=owned(plugin_.controller_->createView(ViewType::kEditor));
        if(plugin_.view_ && plugin_.view_->isPlatformTypeSupported(kPlatformTypeHWND)!=kResultOk) plugin_.view_.reset();
        ViewRect rect{0,0,620,520}; if(plugin_.view_) plugin_.view_->getSize(&rect);
        plugin_.editorWidth_=rect.getWidth(); plugin_.editorHeight_=rect.getHeight();
        int width=std::max(620,rect.getWidth()),height=std::max(520,rect.getHeight())+100;
        if(!Shell::Initialize()) throw std::runtime_error("Plugin UI assets missing; keep Samples beside KinuDAW.exe");
        auto title="KinuDAW VST3 - "+plugin_.info.name+" - "+std::to_string(GetCurrentProcessId());
        if(!Backend::Initialize(title.c_str(),width,height,true)) throw std::runtime_error("Plugin UI renderer initialization failed"); backend_=true;
        auto wide=std::filesystem::u8path(title).wstring(); window_=FindWindowW(wide.c_str(),nullptr); plugin_.window_=window_;
        SetWindowLongPtrW(window_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
        windowProc_=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&windowProc)));
        BOOL dark=TRUE; DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
        Rml::SetSystemInterface(Backend::GetSystemInterface()); Rml::SetRenderInterface(Backend::GetRenderInterface());
        if(!Rml::Initialise()) throw std::runtime_error("Plugin RmlUi initialization failed"); rml_=true;
        context_=Rml::CreateContext("plugin",{width,height});
        Rml::Factory::RegisterEventListenerInstancer(this); Shell::LoadFonts();
        if(!Rml::LoadFontFace("basic/daw_timeline/data/fonts/LINESeedJP-Bold.ttf","LINESeedJP",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Bold)) throw std::runtime_error("Plugin UI font missing");
        document_=context_->LoadDocument("basic/daw_timeline/data/plugin.rml"); if(!document_) throw std::runtime_error("Plugin UI document missing");
        document_->GetElementById("plugin-track-title")->SetInnerRML(Rml::StringUtilities::EncodeRml(plugin_.info.name));
        if(plugin_.view_) {
            surface_=CreateWindowW(L"STATIC",L"",WS_POPUP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|SS_BLACKRECT,0,76,rect.getWidth(),rect.getHeight(),window_,nullptr,GetModuleHandleW(nullptr),nullptr); plugin_.viewWindow_=surface_; SetWindowLongW(surface_,GWL_EXSTYLE,WS_EX_TOOLWINDOW);
            plugin_.view_->setFrame(&plugin_);
            if(plugin_.view_->attached(surface_,kPlatformTypeHWND)!=kResultOk) { plugin_.view_->setFrame(nullptr); plugin_.view_.reset(); DestroyWindow(surface_); surface_=nullptr; plugin_.viewWindow_=nullptr; }
            else plugin_.view_->onSize(&rect);
        }
        if(plugin_.view_) document_->GetElementById("plugin-native-surface")->SetInnerRML("");
        document_->Show(); setTab(plugin_.view_?0:2); pump();
    } catch(...) { shutdown(); throw; }
}
PluginEditor::~PluginEditor() { shutdown(); }
void PluginEditor::shutdown() {
    if(windowProc_) { SetWindowLongPtrW(window_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(windowProc_)); SetWindowLongPtrW(window_,GWLP_USERDATA,0); windowProc_=nullptr; }
    if(plugin_.view_) { plugin_.view_->removed(); plugin_.view_->setFrame(nullptr); plugin_.view_.reset(); }
    if(surface_) { DestroyWindow(surface_); surface_=nullptr; }
    plugin_.window_=nullptr; plugin_.viewWindow_=nullptr;
    if(rml_) { Rml::Shutdown(); rml_=false; } if(backend_) { Backend::Shutdown(); backend_=false; } Shell::Shutdown();
}
void PluginEditor::show() { ShowWindow(window_,SW_SHOW); SetForegroundWindow(window_); }
LRESULT CALLBACK PluginEditor::windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* editor=reinterpret_cast<PluginEditor*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    auto result=CallWindowProcW(editor->windowProc_,window,message,w,l);
    if(message==WM_MOVE && editor->document_) editor->layoutSurface();
    if(message==WM_GETMINMAXINFO) { auto* limits=reinterpret_cast<MINMAXINFO*>(l); limits->ptMinTrackSize.x=620; limits->ptMinTrackSize.y=400; }
    return result;
}
void PluginEditor::resize(int width,int height) { plugin_.editorWidth_=width; plugin_.editorHeight_=height; RECT rect{0,0,std::max(620,width),height+100}; AdjustWindowRect(&rect,GetWindowLongW(window_,GWL_STYLE),FALSE); SetWindowPos(window_,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER); }
void PluginEditor::buildParameters() {
    if(parametersBuilt_) return; parametersBuilt_=true;
        for(int32 i=0;i<plugin_.controller_->getParameterCount();++i) {
            ParameterInfo parameter{}; if(plugin_.controller_->getParameterInfo(i,parameter)!=kResultOk) continue;
            int index=int(ids_.size()); ids_.push_back(parameter.id); readonly_.push_back((parameter.flags&ParameterInfo::kIsReadOnly)!=0); values_.push_back(-1); bindings_.push_back(-1);
            auto card=document_->CreateElement("div"); card->SetClass("parameter-card",true); card->SetId("plugin-param-"+std::to_string(index));
            card->SetAttribute("parameter-name",Rml::StringUtilities::ToLower(utf8(parameter.title)));
            card->SetAttribute("parameter-title",utf8(parameter.title));
            card->SetInnerRML(Rml::CreateString("<div class=\"parameter-label\"><b title=\"%s\">%s</b><span id=\"plugin-param-value-%d\">0.000</span></div><div id=\"plugin-param-rail-%d\" class=\"parameter-rail\" onmousedown=\"plugin-param-begin:%d\"><i id=\"plugin-param-fill-%d\" class=\"parameter-fill\"></i><i id=\"plugin-param-thumb-%d\" class=\"parameter-thumb\"></i></div><div class=\"binding-row\"><select id=\"plugin-binding-%d\" onchange=\"plugin-bind:%d\"><option value=\"0\">未接続</option></select><button onclick=\"plugin-auto:%d\" title=\"Automationトラックを作成して接続\">＋</button></div>",Rml::StringUtilities::EncodeRml(utf8(parameter.title)).c_str(),Rml::StringUtilities::EncodeRml(utf8(parameter.title)).c_str(),index,index,index,index,index,index,index,index));
            if(readonly_.back()) { card->SetClass("readonly",true); card->GetChild(2)->SetProperty("display","none"); }
            document_->GetElementById("plugin-parameters")->AppendChild(std::move(card));
        }
}
void PluginEditor::setTab(int tab) {
    drag_=-1; tab_=tab; if(tab==2) buildParameters();
    for(int i=0;i<3;++i) { document_->GetElementById("plugin-tab-"+std::to_string(i))->SetClass("active",i==tab); document_->GetElementById("plugin-page-"+std::to_string(i))->SetProperty("display",i==tab?"flex":"none"); }
    if(surface_ && tab!=0) ShowWindow(surface_,SW_HIDE);
}
void PluginEditor::search() {
    auto query=Rml::StringUtilities::ToLower(static_cast<Rml::ElementFormControlInput*>(document_->GetElementById("plugin-search"))->GetValue()); int count=0;
    for(size_t i=0;i<ids_.size();++i) { auto* card=document_->GetElementById("plugin-param-"+std::to_string(i)); bool match=card->GetAttribute<Rml::String>("parameter-name","").find(query)!=std::string::npos; card->SetProperty("display",match?"block":"none"); count+=match; }
    document_->GetElementById("plugin-param-empty")->SetProperty("display",count?"none":"block");
}
void PluginEditor::move(Rml::Event& event) {
    if(drag_<0) return; auto* rail=document_->GetElementById("plugin-param-rail-"+std::to_string(drag_)); double value=std::clamp((event.GetParameter<float>("mouse_x",rail->GetAbsoluteLeft())-rail->GetAbsoluteLeft())/std::max(1.f,rail->GetClientWidth()),0.f,1.f);
    plugin_.controller_->setParamNormalized(ids_[drag_],value); plugin_.performEdit(ids_[drag_],value); values_[drag_]=-1; update();
}
void PluginEditor::command(const Rml::String& value,Rml::Event& event,Rml::Element* element) {
    auto argument=[&] { return std::atoi(value.c_str()+value.find_last_of(':')+1); };
    if(value.rfind("plugin-tab:",0)==0) setTab(std::clamp(argument(),0,2));
    else if(value=="plugin-pin") { pinned_=!pinned_; SetWindowPos(window_,pinned_?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE); if(surface_) SetWindowPos(surface_,pinned_?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); element->SetClass("pinned",pinned_); }
    else if(value=="plugin-search") search();
    else if(value.rfind("plugin-param-begin:",0)==0) { int index=argument(); if(index>=0 && index<int(ids_.size()) && !readonly_[index]) { drag_=index; move(event); } }
    else if(value=="plugin-param-move") move(event);
    else if(value=="plugin-param-end") drag_=-1;
    else if(value.rfind("plugin-route-toggle:",0)==0 && plugin_.ipc_) { int side=std::clamp(argument(),0,1); InterlockedXor(plugin_.ipc_->route+side*3,1); }
    else if(value.rfind("plugin-route-step:",0)==0 && plugin_.ipc_) { int field=std::atoi(value.c_str()+18),delta=argument(); if(field==1 || field==2 || field==4 || field==5) { auto* target=plugin_.ipc_->route+field; InterlockedExchange(target,std::clamp(InterlockedCompareExchange(target,0,0)+delta,0L,15L)); } }
    else if((value.rfind("plugin-auto:",0)==0 || value.rfind("plugin-bind:",0)==0) && plugin_.ipc_) {
        int index=argument(); if(index<0 || index>=int(ids_.size()) || readonly_[index]) return;
        if(InterlockedCompareExchange(&plugin_.ipc_->bindRequest,0,0)) return;
        int track=value.rfind("plugin-auto:",0)==0?-1:std::atoi(static_cast<Rml::ElementFormControl*>(element)->GetValue().c_str());
        plugin_.ipc_->bindTrack=track; plugin_.ipc_->bindParam=LONG(ids_[index]); plugin_.ipc_->bindValue=plugin_.controller_->getParamNormalized(ids_[index]);
        auto title=document_->GetElementById("plugin-param-"+std::to_string(index))->GetAttribute<Rml::String>("parameter-title",""); strncpy_s(plugin_.ipc_->bindName,title.c_str(),_TRUNCATE); InterlockedExchange(&plugin_.ipc_->bindRequest,1);
    }
}
void PluginEditor::layoutSurface() {
    if(!surface_ || tab_!=0) return;
    auto* area=document_->GetElementById("plugin-native-surface"); int width=int(area->GetClientWidth()),height=int(area->GetClientHeight());
    if(width<=0 || height<=0) return;
    if(plugin_.view_->canResize()!=kResultOk) { width=plugin_.editorWidth_; height=plugin_.editorHeight_; }
    ViewRect rect{0,0,width,height}; plugin_.view_->checkSizeConstraint(&rect); RECT current{}; GetClientRect(surface_,&current);
    RECT outer{}; GetWindowRect(surface_,&outer); POINT target{int(area->GetAbsoluteLeft()),int(area->GetAbsoluteTop())}; ClientToScreen(window_,&target); POINT point{outer.left,outer.top};
    if(point.x!=target.x || point.y!=target.y || current.right!=rect.getWidth() || current.bottom!=rect.getHeight())
        MoveWindow(surface_,target.x,target.y,rect.getWidth(),rect.getHeight(),TRUE);
    if(current.right!=rect.getWidth() || current.bottom!=rect.getHeight()) plugin_.view_->onSize(&rect);
    int clipWidth=std::min(rect.getWidth(),int(area->GetClientWidth())),clipHeight=std::min(rect.getHeight(),int(area->GetClientHeight()));
    if(clipWidth!=clipWidth_ || clipHeight!=clipHeight_) { auto region=CreateRectRgn(0,0,clipWidth,clipHeight); if(!SetWindowRgn(surface_,region,TRUE)) DeleteObject(region); clipWidth_=clipWidth; clipHeight_=clipHeight; }
    auto foreground=GetForegroundWindow();
    if(foreground==window_ || foreground==surface_ || IsChild(surface_,foreground))
        SetWindowPos(surface_,(GetWindowLongW(window_,GWL_EXSTYLE)&WS_EX_TOPMOST)?HWND_TOPMOST:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
void PluginEditor::update() {
    if(tab_==2) for(size_t i=0;i<ids_.size();++i) { double value=plugin_.controller_->getParamNormalized(ids_[i]); if(!std::isfinite(value)) continue; value=std::clamp(value,0.,1.); if(std::abs(values_[i]-value)<.00001) continue; values_[i]=value;
        document_->GetElementById("plugin-param-value-"+std::to_string(i))->SetInnerRML(Rml::CreateString("%.3f",value));
        document_->GetElementById("plugin-param-fill-"+std::to_string(i))->SetProperty("width",Rml::CreateString("%.1f%%",value*100)); document_->GetElementById("plugin-param-thumb-"+std::to_string(i))->SetProperty("left",Rml::CreateString("%.1f%%",value*100));
    }
    auto* ipc=plugin_.ipc_; if(!ipc) return;
    nlohmann::json metadata;
    try { auto path=std::filesystem::temp_directory_path()/("kinu-daw-tracks-"+std::to_string(ipc->owner)+".txt"); std::ifstream file(path); int count=0; file>>count; std::string line; std::getline(file,line); for(int i=0;i<count;++i) std::getline(file,line); if(std::getline(file,line)) metadata=nlohmann::json::parse(line); } catch(...) {}
    if(metadata.contains("plugins")) for(auto p:metadata["plugins"]) if(p["track"]==ipc->hostTrack && p["slot"]==ipc->hostSlot) { auto title=p["name"].get<std::string>(); if(title!=metadata_) { document_->GetElementById("plugin-track-title")->SetInnerRML(Rml::StringUtilities::EncodeRml(title)); metadata_=title; } }
    std::string options="<option value=\"0\">未接続</option>";
    if(metadata.contains("automation")) for(auto a:metadata["automation"]) options+="<option value=\""+std::to_string(a["track"].get<int>()+1)+"\">"+Rml::StringUtilities::EncodeRml(a["name"].get<std::string>())+"</option>";
    if(options!=optionKey_) { for(size_t i=0;i<ids_.size();++i) { document_->GetElementById("plugin-binding-"+std::to_string(i))->SetInnerRML(options); bindings_[i]=-1; } optionKey_=options; }
    for(size_t i=0;i<ids_.size();++i) { int binding=0; if(metadata.contains("automation")) for(auto a:metadata["automation"]) if(a.value("target",-1)==ipc->hostTrack && a.value("slot",-1)==ipc->hostSlot && a.value("param",~0u)==ids_[i]) binding=a["track"].get<int>()+1;
        if(metadata.contains("automation")) for(auto a:metadata["automation"]) if(a.contains("bindings")) for(auto b:a["bindings"]) if(b.value("target",-1)==ipc->hostTrack && b.value("slot",-1)==ipc->hostSlot && b.value("param",~0u)==ids_[i]) binding=a["track"].get<int>()+1;
        if(bindings_[i]!=binding) { static_cast<Rml::ElementFormControl*>(document_->GetElementById("plugin-binding-"+std::to_string(i)))->SetValue(std::to_string(binding)); bindings_[i]=binding; }
    }
    std::string connections;
    for(int side=0;side<2;++side) { int base=side*3; int enabled=InterlockedCompareExchange(ipc->route+base,0,0),port=InterlockedCompareExchange(ipc->route+base+1,0,0),channel=InterlockedCompareExchange(ipc->route+base+2,0,0);
        auto* toggle=document_->GetElementById("plugin-route-on-"+std::to_string(side)); toggle->SetClass("active",enabled!=0); toggle->SetInnerRML(enabled?"ON":"OFF"); document_->GetElementById("plugin-port-"+std::to_string(side))->SetInnerRML(Rml::CreateString("%02d",port)); document_->GetElementById("plugin-ch-"+std::to_string(side))->SetInnerRML(Rml::CreateString("%02d",channel+1));
        std::string peers; if(enabled && metadata.contains("plugins")) for(auto p:metadata["plugins"]) { if(p["track"]==ipc->hostTrack && p["slot"]==ipc->hostSlot) continue; auto r=p["route"]; int opposite=(1-side)*3; if(r[opposite].get<int>() && r[opposite+1]==port && r[opposite+2]==channel) { if(!peers.empty()) peers+=", "; peers+=p["name"].get<std::string>(); } }
        connections+=(side?"OUTPUT: ":"INPUT: ")+(!enabled?"OFF":peers.empty()?"接続先なし":peers)+"\n";
    }
    if(InterlockedCompareExchange(&ipc->routingStatus,0,0)<0) connections+="循環接続のため内部MIDIを停止しています";
    document_->GetElementById("plugin-connections")->SetInnerRML(Rml::StringUtilities::EncodeRml(connections));
    document_->GetElementById("plugin-status")->SetInnerRML(Rml::CreateString("VST3 / 遅延 %u samples / MIDIとパラメーター設定をプロジェクトに保存",plugin_.latency()));
}
void PluginEditor::pump() {
    if(!Backend::ProcessEvents(context_,nullptr,false)) { drag_=-1; ShowWindow(window_,SW_HIDE); }
    if(!IsWindowVisible(window_) || IsIconic(window_)) { drag_=-1; if(surface_) ShowWindow(surface_,SW_HIDE); return; }

    if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000)) drag_=-1;
    auto now=Rml::GetSystemInterface()->GetElapsedTime(); if(now>=nextPoll_) { nextPoll_=now+.05; update(); }
    context_->Update(); layoutSurface(); if(surface_ && tab_==0 && !IsWindowVisible(surface_)) ShowWindow(surface_,SW_SHOWNOACTIVATE); Backend::BeginFrame(); context_->Render(); Backend::PresentFrame();
}
}
