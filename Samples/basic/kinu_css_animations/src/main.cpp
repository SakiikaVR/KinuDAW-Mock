#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi_Backend.h>
#include <Shell.h>

namespace {
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
bool animations_paused = false;
bool restore_scene_class = false;
bool force_update = false;

class DemoEventListener final : public Rml::EventListener {
public:
	explicit DemoEventListener(Rml::String command) : command(std::move(command)) {}

	void ProcessEvent(Rml::Event&) override
	{
		if (command == "exit")
			Backend::RequestExit();
		else if (command == "replay" && document)
		{
			document->GetElementById("scene")->SetClass("running", false);
			restore_scene_class = true;
			document->GetElementById("status")->SetInnerRML("RESTARTING");
		}
		else if (command == "pause" && document)
		{
			animations_paused = !animations_paused;
			document->GetElementById("pause-label")->SetInnerRML(animations_paused ? "RESUME" : "FREEZE");
			document->GetElementById("status")->SetInnerRML(animations_paused ? "FROZEN" : "LIVE");
			force_update = true;
		}
	}

	void OnDetach(Rml::Element*) override { delete this; }

private:
	Rml::String command;
};

class DemoEventInstancer final : public Rml::EventListenerInstancer {
public:
	Rml::EventListener* InstanceEventListener(const Rml::String& value, Rml::Element*) override { return new DemoEventListener(value); }
};
} // namespace

#if defined RMLUI_PLATFORM_WIN32
	#include <RmlUi_Include_Windows.h>
int APIENTRY WinMain(HINSTANCE, HINSTANCE, char*, int)
#else
int main(int, char**)
#endif
{
	constexpr int window_width = 1180;
	constexpr int window_height = 760;
	if (!Shell::Initialize())
		return -1;
	if (!Backend::Initialize("KinuUI CSS Animations", window_width, window_height, true))
	{
		Shell::Shutdown();
		return -1;
	}

	Rml::SetSystemInterface(Backend::GetSystemInterface());
	Rml::SetRenderInterface(Backend::GetRenderInterface());
	if (!Rml::Initialise())
	{
		Backend::Shutdown();
		Shell::Shutdown();
		return -1;
	}

	context = Rml::CreateContext("kinu", {window_width, window_height});
	if (!context)
	{
		Rml::Shutdown();
		Backend::Shutdown();
		Shell::Shutdown();
		return -1;
	}
	Rml::Debugger::Initialise(context);
	DemoEventInstancer event_instancer;
	Rml::Factory::RegisterEventListenerInstancer(&event_instancer);
	Shell::LoadFonts();

	document = context->LoadDocument("basic/kinu_css_animations/data/demo.rml");
	if (!document)
	{
		Rml::Shutdown();
		Backend::Shutdown();
		Shell::Shutdown();
		return -1;
	}
	document->Show();

	bool running = true;
	while (running)
	{
		running = Backend::ProcessEvents(context, &Shell::ProcessKeyDownShortcuts);
		if (!animations_paused || force_update)
		{
			context->Update();
			force_update = false;
		}
		if (restore_scene_class)
		{
			restore_scene_class = false;
			document->GetElementById("scene")->SetClass("running", true);
			document->GetElementById("status")->SetInnerRML("LIVE");
			context->Update();
		}
		Backend::BeginFrame();
		context->Render();
		Backend::PresentFrame();
	}

	document->Close();
	document = nullptr;
	Rml::Shutdown();
	Backend::Shutdown();
	Shell::Shutdown();
	return 0;
}
