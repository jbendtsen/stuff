/*
100% native, glueless Android app.
Compile with -std=c11 -lm -landroid -lEGL -lGLESv3

AndroidManifest.xml:
	<?xml version='1.0'?>
	<manifest
		xmlns:android='http://schemas.android.com/apk/res/android'
		xmlns:app='http://schemas.android.com/apk/res-auto'
		xmlns:tools='http://schemas.android.com/tools'
		package='com.example.colors'
		android:versionCode='0'
		android:versionName='0'>

		<uses-feature android:glEsVersion="0x00030001" android:required="true" />
		<uses-sdk
			android:minSdkVersion="15"
			android:targetSdkVersion="30"
		/>
		<application android:label='Color Demo' android:hasCode="false">
			<activity android:name='android.app.NativeActivity'>
				<meta-data android:name="android.app.lib_name" android:value="colors"/>
				<intent-filter>
					<category android:name='android.intent.category.LAUNCHER'/>
					<action android:name='android.intent.action.MAIN'/>
				</intent-filter>
			</activity>
		</application>
	</manifest>
*/

#include <jni.h>
#include <alloca.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <android/input.h>
#include <android/native_activity.h>

#define APP_CMD_INPUT_QUEUE_DESTROYED  1
#define APP_CMD_WINDOW_DESTROYED       2
#define APP_CMD_WINDOW_CREATED         3
#define APP_CMD_CONFIG_CHANGED         4
#define APP_CMD_LOW_MEMORY             5
#define APP_CMD_WINDOW_REDRAW_NEEDED   6
#define APP_CMD_WINDOW_RESIZED         7
#define APP_CMD_PAUSE                  8
#define APP_CMD_RESUME                 9
#define APP_CMD_START                 10
#define APP_CMD_STOP                  11
#define APP_CMD_DESTROY               12
#define APP_CMD_GAINED_FOCUS          13
#define APP_CMD_LOST_FOCUS            14
#define APP_CMD_CONTENT_RECT_CHANGED  15

struct {
	int cmd_queue[0x200];
	int cmd_read_travel;
	int cmd_write_travel;

	ANativeActivity *activity;
	ANativeWindow *window;
	AInputQueue *input_queue;
	pthread_t thread;
	ARect pending_rect;
	bool running;

	struct {
		EGLDisplay display;
		EGLContext context;
		EGLSurface surface;
		int width;
		int height;
	} egl;

	struct {
		float x;
		float y;
	} input;

	struct {
		float back_color[3];
	} state;

} static app_global = {0};

int init_egl()
{
	const EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_NONE
	};

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(display, 0, 0);

	EGLint n_configs = 0;
	eglChooseConfig(display, attribs, NULL, 0, &n_configs);

	EGLConfig cfg = NULL;
	EGLConfig *configs = (EGLConfig*)alloca(n_configs * sizeof(EGLConfig));
	eglChooseConfig(display, attribs, configs, n_configs, &n_configs);

	for (int i = 0; i < n_configs; i++) {
		EGLint r, g, b, d;
		if (eglGetConfigAttrib(display, configs[i], EGL_RED_SIZE, &r) &&
			eglGetConfigAttrib(display, configs[i], EGL_GREEN_SIZE, &g) &&
			eglGetConfigAttrib(display, configs[i], EGL_BLUE_SIZE, &b) &&
			eglGetConfigAttrib(display, configs[i], EGL_DEPTH_SIZE, &d) &&
			r == 8 && g == 8 && b == 8 && d == 0
		) {
			cfg = configs[i];
			break;
		}
	}
	if (!cfg)
		cfg = configs[0];
	if (!cfg)
		return -1;

	EGLint format;
	eglGetConfigAttrib(display, cfg, EGL_NATIVE_VISUAL_ID, &format);

	EGLSurface surface = eglCreateWindowSurface(display, cfg, app_global.window, NULL);
	EGLContext context = eglCreateContext(display, cfg, NULL, NULL);
	if (!eglMakeCurrent(display, surface, surface, context))
		return -2;

	EGLint w, h;
	eglQuerySurface(display, surface, EGL_WIDTH, &w);
	eglQuerySurface(display, surface, EGL_HEIGHT, &h);

	app_global.egl.display = display;
	app_global.egl.context = context;
	app_global.egl.surface = surface;
	app_global.egl.width = w;
	app_global.egl.height = h;

	//glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	glEnable(GL_CULL_FACE);
	//glShadeModel(GL_SMOOTH);
	glDisable(GL_DEPTH_TEST);

	return 0;
}

void quit_egl()
{
    if (app_global.egl.display) {
		eglMakeCurrent(app_global.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

		if (app_global.egl.context)
			eglDestroyContext(app_global.egl.display, app_global.egl.context);
		if (app_global.egl.surface)
			eglDestroySurface(app_global.egl.display, app_global.egl.surface);

		eglTerminate(app_global.egl.display);
	}

	app_global.egl.display = NULL;
	app_global.egl.context = NULL;
	app_global.egl.surface = NULL;
}

void handle_app_command(int cmd)
{
	switch (cmd) {
		case APP_CMD_INPUT_QUEUE_DESTROYED:
		{
			app_global.input_queue = NULL;
			break;
		}
		case APP_CMD_WINDOW_DESTROYED:
		{
			app_global.window = NULL;
			quit_egl();
			break;
		}
		case APP_CMD_WINDOW_CREATED:
		{
			init_egl();
			break;
		}
		case APP_CMD_WINDOW_REDRAW_NEEDED:
		{
			break;
		}
		case APP_CMD_WINDOW_RESIZED:
		{
			break;
		}
		case APP_CMD_PAUSE:
		{
			break;
		}
		case APP_CMD_RESUME:
		{
			break;
		}
		case APP_CMD_START:
		{
			break;
		}
		case APP_CMD_STOP:
		{
			break;
		}
		case APP_CMD_DESTROY:
		{
			quit_egl();
			app_global.running = false;
			break;
		}
		case APP_CMD_GAINED_FOCUS:
		{
			break;
		}
		case APP_CMD_LOST_FOCUS:
		{
			break;
		}
		case APP_CMD_CONTENT_RECT_CHANGED:
		{
			ARect rect = app_global.pending_rect;
			break;
		}
	}
}

bool handle_motion_input(AInputEvent *event)
{
	app_global.input.x = AMotionEvent_getX(event, 0);
	app_global.input.y = AMotionEvent_getY(event, 0);
	return true;
}

bool handle_key_input(AInputEvent *event)
{
	return false;
}

void app_tick()
{
	float half_w = (float)(app_global.egl.width / 2);
	float half_h = (float)(app_global.egl.height / 2);
	float x = app_global.input.x - half_w;
	float y = app_global.input.y - half_h;

	float angle = 0;
	if ((y <= -0.001 || y > 0.001) && (x <= -0.001 || x > 0.001))
		angle = atan2(y, x);

	float radius = sqrt(x*x + y*y) / 256.0;
	float *rgb = app_global.state.back_color;

	for (int i = 0; i < 3; i++) {
		rgb[i] = cos(angle + ((float)i * M_PI * 2.0/3.0)) + 0.5;
		if (rgb[i] >= 1.0) rgb[i] = 1.0;
		if (rgb[i] < 0) rgb[i] = 0;
		if (radius < 1.0) rgb[i] *= radius;
	}
}

void *app_main(void *data)
{
	app_global.running = true;

	while (true) {
		int cmd = app_global.cmd_queue[app_global.cmd_read_travel & 0x1ff];
		if (cmd) {
			app_global.cmd_queue[app_global.cmd_read_travel & 0x1ff] = 0;
			app_global.cmd_read_travel++;
			handle_app_command(cmd);
		}

		AInputEvent *event = NULL;
		while (app_global.input_queue && AInputQueue_getEvent(app_global.input_queue, &event) >= 0) {
			if (AInputQueue_preDispatchEvent(app_global.input_queue, event))
				continue;

			bool handled = false;
			int type = AInputEvent_getType(event);
			if (type == AINPUT_EVENT_TYPE_MOTION)
				handled = handle_motion_input(event);
			else if (type == AINPUT_EVENT_TYPE_KEY)
				handled = handle_key_input(event);

			AInputQueue_finishEvent(app_global.input_queue, event, handled);
		}

		if (!app_global.running)
			break;

		app_tick();

		float *rgb = app_global.state.back_color;
		glClearColor(rgb[0], rgb[1], rgb[2], 1);
		glClear(GL_COLOR_BUFFER_BIT);

		eglSwapBuffers(app_global.egl.display, app_global.egl.surface);
	}

	return NULL;
}

void push_app_command(int cmd)
{
	app_global.cmd_queue[app_global.cmd_write_travel++ & 0x1ff] = cmd;
}

static void onInputQueueCreated(ANativeActivity *activity, AInputQueue *queue) {
	app_global.input_queue = queue;
}
static void onInputQueueDestroyed(ANativeActivity *activity, AInputQueue *queue) {
	//app_global.input_queue = NULL;
	push_app_command(APP_CMD_INPUT_QUEUE_DESTROYED);
}
static void onNativeWindowCreated(ANativeActivity *activity, ANativeWindow *window) {
	app_global.window = window;
	push_app_command(APP_CMD_WINDOW_CREATED);
}
static void onNativeWindowDestroyed(ANativeActivity *activity, ANativeWindow *window) {
	//app_global.window = NULL;
	push_app_command(APP_CMD_WINDOW_DESTROYED);
}

static void onConfigurationChanged(ANativeActivity *activity) {
	push_app_command(APP_CMD_CONFIG_CHANGED);
}
static void onLowMemory(ANativeActivity *activity) {
	push_app_command(APP_CMD_LOW_MEMORY);
}
static void onNativeWindowRedrawNeeded(ANativeActivity *activity, ANativeWindow *window) {
	push_app_command(APP_CMD_WINDOW_REDRAW_NEEDED);
}
static void onNativeWindowResized(ANativeActivity *activity, ANativeWindow *window) {
	push_app_command(APP_CMD_WINDOW_RESIZED);
}
static void onPause(ANativeActivity *activity) {
	push_app_command(APP_CMD_PAUSE);
}
static void onResume(ANativeActivity *activity) {
	push_app_command(APP_CMD_RESUME);
}
static void onStart(ANativeActivity *activity) {
	push_app_command(APP_CMD_START);
}
static void onStop(ANativeActivity *activity) {
	push_app_command(APP_CMD_STOP);
}
static void onDestroy(ANativeActivity *activity) {
	push_app_command(APP_CMD_DESTROY);
}
static void onWindowFocusChanged(ANativeActivity *activity, int hasFocus) {
	push_app_command(hasFocus ? APP_CMD_GAINED_FOCUS : APP_CMD_LOST_FOCUS);
}

static void onContentRectChanged(ANativeActivity *activity, const ARect *rect) {
	app_global.pending_rect = *rect;
	push_app_command(APP_CMD_CONTENT_RECT_CHANGED);
}

static void *onSaveInstanceState(ANativeActivity *activity, size_t *outSize) {
	*outSize = sizeof(app_global.state);
	return &app_global.state;
}

JNIEXPORT void ANativeActivity_onCreate(ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
	struct ANativeActivityCallbacks *cb = activity->callbacks;

	cb->onConfigurationChanged = onConfigurationChanged;
	cb->onContentRectChanged = onContentRectChanged;
	cb->onDestroy = onDestroy;
	cb->onInputQueueCreated = onInputQueueCreated;
	cb->onInputQueueDestroyed = onInputQueueDestroyed;
	cb->onLowMemory = onLowMemory;
	cb->onNativeWindowCreated = onNativeWindowCreated;
	cb->onNativeWindowDestroyed = onNativeWindowDestroyed;
	cb->onNativeWindowRedrawNeeded = onNativeWindowRedrawNeeded;
	cb->onNativeWindowResized = onNativeWindowResized;
	cb->onPause = onPause;
	cb->onResume = onResume;
	cb->onSaveInstanceState = onSaveInstanceState;
	cb->onStart = onStart;
	cb->onStop = onStop;
	cb->onWindowFocusChanged = onWindowFocusChanged;

	app_global.activity = activity;
	activity->instance = &app_global;

	if (savedState && savedStateSize == sizeof(app_global.state))
		memcpy(&app_global.state, savedState, savedStateSize);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&app_global.thread, &attr, app_main, NULL);

	while (!app_global.running);
}
