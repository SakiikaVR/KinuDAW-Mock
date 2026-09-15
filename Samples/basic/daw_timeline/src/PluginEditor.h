// Copyright (c) 2026 KinuDAW contributors. MIT License.
#pragma once
#include <RmlUi/Core.h>
#include <windows.h>
#include <vector>
#include <string>
namespace Kinu {
class Plugin;
class PluginEditor final : public Rml::EventListenerInstancer {
public:
    PluginEditor(Plugin&,bool generic); ~PluginEditor();
    void show(); void pump(); void resize(int width,int height);
    Rml::EventListener* InstanceEventListener(const Rml::String&,Rml::Element*) override;
private:
    class Listener;
    Plugin& plugin_;
    Rml::Context* context_=nullptr; Rml::ElementDocument* document_=nullptr;
    HWND window_=nullptr,surface_=nullptr;
    WNDPROC windowProc_=nullptr;
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    int clipWidth_=-1,clipHeight_=-1;
    bool parametersBuilt_=false;
    bool backend_=false,rml_=false,pinned_=false; int tab_=0,drag_=-1;
    std::vector<unsigned> ids_; std::vector<bool> readonly_;
    std::vector<double> values_; std::vector<int> bindings_;
    std::string metadata_,optionKey_; double nextPoll_=0;
    void command(const Rml::String&,Rml::Event&,Rml::Element*);
    void buildParameters(); void setTab(int); void search(); void move(Rml::Event&); void update(); void layoutSurface();
    void shutdown();
};
}
