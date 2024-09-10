import WinSDK

let MY_IDI_APPLICATION = UnsafePointer<Int8>(bitPattern: 32512)! // MAKEINTRESOURCE(32512)
let MY_IDC_ARROW       = UnsafePointer<Int8>(bitPattern: 32512)! // MAKEINTRESOURCE(32512)

let INITIAL_TITLE = "Swift WinAPI Demo"
let CLASS_NAME = "swift-winapi"
let INITIAL_WIDTH = Int32(640)
let INITIAL_HEIGHT = Int32(360)
let WND_BACKGROUND = UInt32(0x50e030)

// This is heartbreakingly close to WinMain.
// It's possible to pass -Xlinker /subsystem:windows to the compiler to get it to call this method directly,
//  and it's possible to expose this method as WinMain by using @_silgen_name("WinMain") or @_cdecl("WinMain")
// However, despite the existance of the @convention directive to change the calling convention of function,
//  that only seems to apply to lambdas and such.
// For some reason, Swift does not allow you to both export a method as a native function AND change its calling convention at the same time,
//  which is what Windows demands for WinMain (even though Swift lets you do these things separately).
// Exporting this method without changing the calling convention results in a crash,
//  since the stack is setup differently in the WinAPI calling convention.
func main(_ hInstance: HINSTANCE?, _ hPrevInstance: HINSTANCE?, _ lpCmdLine: LPSTR?, _ nCmdShow: Int32) -> Int32
{
	guard let inst = hInstance ?? GetModuleHandleA(nil) else {
		MessageBoxA(nil, "Failed to obtain instance handle", "Error!", UINT(MB_ICONEXCLAMATION | MB_OK))
		return 0
	}

	let hwnd = initGui(hInstance: inst, mainProc: handleWindowEvents, title: INITIAL_TITLE, className: CLASS_NAME)
	if (hwnd == nil) {
		MessageBoxA(nil, "Window Creation Failed!", "Error!", UINT(MB_ICONEXCLAMATION | MB_OK))
		return 0
	}

	ShowWindow(hwnd, SW_NORMAL)
	UpdateWindow(hwnd)

	var msg = MSG()
	while (GetMessageA(&msg, nil, 0, 0)) {
		TranslateMessage(&msg)
		DispatchMessageA(&msg)
	}

	return Int32(msg.wParam);
}

func initGui(hInstance: HINSTANCE, mainProc: WNDPROC, title: LPCSTR, className: LPCSTR) -> HWND?
{
	var wc = WNDCLASSEX();
	wc.cbSize        = UINT(MemoryLayout<WNDCLASSEX>.stride);
	wc.style         = UINT(CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW);
	wc.lpfnWndProc   = mainProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = LoadIconA(nil, MY_IDI_APPLICATION);
	wc.hCursor       = LoadCursorA(nil, MY_IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(WND_BACKGROUND);
	wc.lpszMenuName  = nil;
	wc.lpszClassName = className;
	wc.hIconSm       = LoadIconA(nil, MY_IDI_APPLICATION);

	if (RegisterClassExA(&wc) == 0) {
		MessageBoxA(nil, "Window Registration Failed!", "Error!", UINT(MB_ICONEXCLAMATION | MB_OK));
		return nil;
	}

	return CreateWindowExA(
		DWORD(WS_EX_CLIENTEDGE),
		className,
		title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, INITIAL_WIDTH, INITIAL_HEIGHT,
		nil, nil, hInstance, nil
	);
}

func resizeWindow(_ width: Int32, _ height: Int32)
{
	
}

func handleWindowEvents(hwnd: HWND?, uMsg: UINT, wParam: WPARAM, lParam: LPARAM) -> LRESULT
{
	var result: LRESULT = 0
	let msg = Int32(uMsg)

	switch msg {
		case WM_CREATE:
			resizeWindow(INITIAL_WIDTH, INITIAL_HEIGHT);

		case WM_SIZE:
			var rect = RECT();
			GetClientRect(hwnd, &rect);
			resizeWindow(rect.right, rect.bottom);

		case WM_CLOSE:
			DestroyWindow(hwnd);

		case WM_DESTROY:
			PostQuitMessage(0);

		default:
			result = DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}

	return result;
}

let res = main(nil, nil, nil, 0)