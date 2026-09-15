// Windows enters a modal message loop while moving/resizing a native window.
// Keep rendering on that same UI thread instead of stretching the last frame.
#if defined RMLUI_PLATFORM_WIN32
WNDPROC daw_original_proc = nullptr;
bool daw_sizing = false;
bool daw_rendering = false;
HANDLE mixer_owner_process = nullptr;
constexpr UINT_PTR kDawSizingTimer = 0x4b494e55;

bool MixerOwnerExited()
{
	return mixer_owner_process && WaitForSingleObject(mixer_owner_process, 0) == WAIT_OBJECT_0;
}

void RenderNativeResize()
{
	if (!context || !document || daw_rendering || IsIconic(daw_native_window)) return;
	daw_rendering = true;
	RECT client = {};
	GetClientRect(daw_native_window, &client);
	context->SetDimensions({client.right, client.bottom});
	context->Update();
	Backend::BeginFrame();
	context->Render();
	Backend::PresentFrame();
	daw_rendering = false;
}

LRESULT CALLBACK DawWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_DROPFILES)
	{
		HDROP drop = reinterpret_cast<HDROP>(wparam);
		POINT point = {}; DragQueryPoint(drop, &point);
		const UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
		for (UINT index = 0; index < count && index < 32; ++index)
		{
			const UINT length = DragQueryFileW(drop, index, nullptr, 0);
			std::wstring path(length + 1, L'\0');
			DragQueryFileW(drop, index, &path[0], length + 1); path.resize(length);
			mock_audio_drops.emplace_back(std::filesystem::path(path), point);
		}
		DragFinish(drop);
		return 0;
	}
	// End scrubbing at the native boundary even if RML mouseup is consumed
	// by a descendant, or the release occurs outside the document.
	if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_CANCELMODE || message == WM_KILLFOCUS ||
		(message == WM_MOUSEMOVE && !(wparam & MK_LBUTTON)))
	{
		scrubbing = false;
		EndClipDrag();
		EndPluginGesture();
	}
	if (message == WM_ENTERSIZEMOVE)
	{
		daw_sizing = true;
		SetTimer(window, kDawSizingTimer, 33, nullptr);
	}
	if (message == WM_TIMER && wparam == kDawSizingTimer)
	{
		if (MixerOwnerExited()) PostMessageW(window, WM_CLOSE, 0, 0);
		else RenderNativeResize();
		return 0;
	}
	const LRESULT result = CallWindowProcW(daw_original_proc, window, message, wparam, lparam);
	if (!mixer_window && !instrument_window && piano_track < 0 && context &&
		(message == WM_MOUSEMOVE || (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) || message == WM_LBUTTONUP))
	{
		POINT point = {}; GetCursorPos(&point); ScreenToClient(window, &point);
		auto* hit = context->GetElementAtPoint({float(point.x), float(point.y)});
		while (hit && !hit->IsClassSet("clip")) hit = hit->GetParentNode();
		const bool resize = clip_drag.clip ? clip_drag.resize_edge != 0 : hit && ClipResizeEdge(hit, float(point.x)) != 0;
		if (resize) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); if (message == WM_SETCURSOR) return TRUE; }
		else if (message == WM_LBUTTONUP) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
	}
	if (message == WM_SIZE && daw_sizing) RenderNativeResize();
	if (message == WM_EXITSIZEMOVE)
	{
		daw_sizing = false;
		KillTimer(window, kDawSizingTimer);
		RenderNativeResize();
	}
	return result;
}

void InstallNativeResize()
{
	daw_original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(daw_native_window, GWLP_WNDPROC,
		reinterpret_cast<LONG_PTR>(&DawWindowProcedure)));
}

void CloseNativeIntegration()
{
	KillTimer(daw_native_window, kDawSizingTimer);
	if (daw_original_proc) SetWindowLongPtrW(daw_native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(daw_original_proc));
	if (mixer_owner_process) CloseHandle(mixer_owner_process);
	mixer_owner_process = nullptr;
}
#endif
