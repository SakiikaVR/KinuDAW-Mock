// Copyright (c) 2026 KinuDAW contributors. MIT License.
std::vector<Kinu::PluginInfo> installed_plugins;
struct MockInstrument { const char* name; const char* category; };
std::vector<MockInstrument> mock_instruments;
int kMockInstrumentCount=0;
std::vector<int> instrument_favorites;
int instrument_selected=-1;
std::filesystem::path BinaryFolder() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); return std::filesystem::path(path).parent_path();
}
HANDLE catalog_scan_process=nullptr;
void StartPluginScan() {
    if(catalog_scan_process) return;
    auto script=BinaryFolder()/"Tools"/"Scan-Vst3.ps1";
    if(!std::filesystem::exists(script)) { MockNotice(u8"VST3スキャン用ファイルが見つかりません"); return; }
    wchar_t windows[32768]{}; GetWindowsDirectoryW(windows,32768);
    auto shell=std::filesystem::path(windows)/"System32"/"WindowsPowerShell"/"v1.0"/"powershell.exe";
    auto command=L"\""+shell.wstring()+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" -Bin \""+BinaryFolder().wstring()+L"\"";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(CreateProcessW(shell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,BinaryFolder().c_str(),&startup,&process)) { catalog_scan_process=process.hProcess; CloseHandle(process.hThread); MockNotice(u8"VST3を検出しています…"); }
    else MockNotice(u8"VST3スキャンを開始できませんでした");
}
void LoadPluginCatalog() {
    installed_plugins.clear(); mock_instruments.clear();
    try { std::ifstream in(BinaryFolder()/"plugins.json"); if(in) {
        nlohmann::json catalog; in>>catalog;
        for(const auto& j:catalog.at("plugins")) installed_plugins.push_back({j.at("path"),j.at("uid"),j.at("name"),j.value("category",""),j.value("vendor","")});
    } } catch(const std::exception& e) { Rml::Log::Message(Rml::Log::LT_WARNING,"Plugin catalog: %s",e.what()); }
    for(const auto& p:installed_plugins) mock_instruments.push_back({p.name.c_str(),p.category.c_str()});
    kMockInstrumentCount=int(mock_instruments.size()); instrument_favorites.assign(kMockInstrumentCount,0);
}
std::filesystem::path InstrumentFavoritesPath() { return BinaryFolder()/"favorites.txt"; }
void RefreshInstrumentList() {
    auto query=Rml::StringUtilities::ToLower(static_cast<Rml::ElementFormControlInput*>(Get("instrument-search"))->GetValue());
    int visible=0;
    for(int favorite=1;favorite>=0;--favorite) for(int i=0;i<kMockInstrumentCount;++i) {
        if(bool(instrument_favorites[i])!=bool(favorite)) continue;
        auto* row=Get(Rml::CreateString("instrument-row-%d",i).c_str());
        const auto& item=installed_plugins[i];
        bool match=Rml::StringUtilities::ToLower(item.name+" "+item.category+" "+item.vendor).find(query)!=Rml::String::npos && (!instrument_slot || item.category.find("Instrument")==std::string::npos);
        row->SetProperty("display",match?"flex":"none"); row->SetClass("selected",i==instrument_selected);
        SetText(Rml::CreateString("instrument-heart-%d",i).c_str(),favorite?u8"♥":u8"♡");
        Get("instrument-list")->AppendChild(Get("instrument-list")->RemoveChild(row)); visible+=match;
    }
    Get("instrument-empty")->SetProperty("display",visible?"none":"block");
    SetText("instrument-selection",instrument_selected<0?u8"インストール済みのVST3を選択":installed_plugins[instrument_selected].name);
    if(!kMockInstrumentCount) SetText("instrument-empty",u8"VST3が見つかりません。メイン画面の「VST3再検出」をお試しください。");
    Get("instrument-add")->SetClass("ready",instrument_selected>=0);
}
void InitialiseInstrumentPicker() {
    std::ifstream in(InstrumentFavoritesPath()); for(auto& favorite:instrument_favorites) in>>favorite;
    for(int i=0;i<kMockInstrumentCount;++i) {
        auto row=document->CreateElement("div"); row->SetClass("instrument-row",true); row->SetId(Rml::CreateString("instrument-row-%d",i));
        row->SetInnerRML(Rml::CreateString("<button class=\"instrument-pick\" onclick=\"instrument-select:%d\"><b>%s</b><small>%s</small></button><span class=\"vst-tag\">VST3</span><button id=\"instrument-heart-%d\" class=\"instrument-heart\" onclick=\"instrument-favorite:%d\"></button>",i,Rml::StringUtilities::EncodeRml(installed_plugins[i].name).c_str(),Rml::StringUtilities::EncodeRml(installed_plugins[i].category).c_str(),i,i));
        Get("instrument-list")->AppendChild(std::move(row));
    }
    RefreshInstrumentList();
}
void ToggleInstrumentFavorite(int index) {
    if(index<0 || index>=kMockInstrumentCount) return;
    instrument_favorites[index]=!instrument_favorites[index]; std::ofstream out(InstrumentFavoritesPath()); for(auto f:instrument_favorites) out<<f<<' '; RefreshInstrumentList();
}
void ChooseMockInstrument() {
    if(instrument_selected<0 || instrument_selected>=kMockInstrumentCount || !shared_volume) return;
    InterlockedExchange(shared_volume+kMaxTracks*2+1,instrument_target+1);
    InterlockedExchange(shared_volume+kMaxTracks*3+2,instrument_slot);
    InterlockedExchange(shared_volume+kMaxTracks*2,instrument_selected+1); Backend::RequestExit();
}
