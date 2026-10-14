#include "Android_draw/draw.h"
#include "main.h"
#include "MenuStubs.h"
#include "MenuEternalFont.h"
#include "verdana.h"
#include "../src/fontw.h"
#include "../src/fonts/fonts.h"
//#include "../Fonts/rofico.h"
#include "../src/fonts/iconss.h"
#include "../src/fonts/icons.h"
ImFont* pixel;
// Var
EGLDisplay display = EGL_NO_DISPLAY;
EGLConfig config;
EGLSurface surface = EGL_NO_SURFACE;
EGLContext context = EGL_NO_CONTEXT;

ANativeWindow *native_window;
ImFont* verdana;
ImFont* espnamet;
ImFont* logoa;
ImFont* mains;
ImFont* mainicon = nullptr;
ImFont* menu_eternal_font = nullptr;
ImFont* hitlogfont;
inline ImFont* fontsicons = nullptr;
inline ImFont* watermarkicon = nullptr;
inline ImFont* ex = nullptr;
namespace menuscale
{
    extern  float menuscale;
}
namespace espscale
{
    extern  float espscale;
}
ImFont* iconss;
ImFont* iconsss;
//
int native_window_screen_x = 0;
int native_window_screen_y = 0;
android::ANativeWindowCreator::DisplayInfo displayInfo{0};
uint32_t orientation = 0;
bool g_Initialized = false;
ImGuiWindow *g_window = nullptr;

bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    orientation = displayInfo.orientation;

    #if defined(USE_OPENGL)
        if (!init_egl(_screen_x, _screen_y, log)) {
            return false;
        }
    #else
        InitVulkan();
        SetupVulkan();
        ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, true);
        SetupVulkanWindow(::native_window, (int) _screen_x, (int) _screen_y);
    #endif
    if (!ImGui_init()) {
        return false;
    }   

    #ifndef USE_OPENGL
        UploadFonts();
    #endif
     
    return true;
}

bool init_egl(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, false);

    ANativeWindow_acquire(native_window);
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("eglGetDisplay error=%u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglGetDisplay ok\n");
    }
    if (eglInitialize(display, 0, 0) != EGL_TRUE) {
        printf("eglInitialize error=%u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglInitialize ok\n");
    }
    EGLint num_config = 0;
    const EGLint attribList[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_BLUE_SIZE, 5,   //-->delete
            EGL_GREEN_SIZE, 6,  //-->delete
            EGL_RED_SIZE, 5,    //-->delete
            EGL_BUFFER_SIZE, 32,  //-->new field
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
    };
    const EGLint attrib_list[] = {
            EGL_CONTEXT_CLIENT_VERSION,
            3,
            EGL_NONE
    };

    if (log) {
        printf("num_config = %d\n", num_config);
    }
    if (eglChooseConfig(display, attribList, &config, 1, &num_config) != EGL_TRUE) {
        printf("eglChooseConfig  error=%u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglChooseConfig ok\n");
    }
    EGLint egl_format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, egl_format);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext  error = %u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglCreateContext ok\n");
    }
    surface = eglCreateWindowSurface(display, config, native_window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface  error = %u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglCreateWindowSurface ok\n");
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("eglMakeCurrent  error = %u\n", glGetError());
        return false;
    }
    if (log) {
        printf("eglMakeCurrent ok\n");
        printf("createNativeWindow ok\n");
    }
    return true;
}

void screen_config() {
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}
bool ImGui_init() {
    if (g_Initialized) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplAndroid_Init(native_window);
    #if defined(USE_OPENGL)
        ImGui_ImplOpenGL3_Init("#version 300 es");
    #endif
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    static ImFontConfig font_cfg;
    font_cfg.SizePixels = 22.0f;
	
	 
//	io.Fonts->AddFontFromMemoryTTF(&_pixel, sizeof _pixel, 24);
   //io.Fonts->AddFontFromMemoryTTF((void *) OPPOSans_H, OPPOSans_H_size, 32.0f, &font_cfg, io.Fonts->GetGlyphRangesChineseFull());
	//ImFontConfig font_cfg22;
	static ImFontConfig font_cfg2;
    font_cfg2.SizePixels = 22.0f;
     static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0x0 };
 ImFontConfig icons_config;
	/*mains->Scale = menuscale::menuscale;
 logoa->Scale = menuscale::menuscale;
 font::icomoon->Scale = menuscale::menuscale;
 iconsss->Scale = menuscale::menuscale;*/
 //font::icomoon_widget->Scale = menuscale::menuscale;
	//font_cfg2.GlyphRanges = io.Fonts->GetGlyphRangesCyrillic();
       ex = io.Fonts->AddFontFromMemoryTTF(Verdana, sizeof(Verdana), 22.5f * menuscale::menuscale, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
  font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF(regular, sizeof(regular), 25.f * menuscale::menuscale, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
  fontsicons = io.Fonts->AddFontFromMemoryTTF(regular, sizeof(regular), 18.5 * menuscale::menuscale, &icons_config, icon_ranges);
  watermarkicon = io.Fonts->AddFontFromMemoryTTF(Verdana, sizeof(Verdana), 22.5 * menuscale::menuscale, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
  mainicon = io.Fonts->AddFontFromMemoryTTF(Verdana, sizeof(Verdana), 28.5 * menuscale::menuscale, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
  hitlogfont = io.Fonts->AddFontFromMemoryTTF(Verdana, sizeof(Verdana), 11.5f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
  // menu.rar ships Source Sans 3 SemiBold.  Use the adapted font for the
  // menu/overlay default while retaining the existing ESP font handles.
  menu_eternal_font = MenuEternalFont::LoadSemiBold(21.0f * menuscale::menuscale);
  if (menu_eternal_font) io.FontDefault = menu_eternal_font;
  MenuEternalFont::LoadIcons(16.0f * menuscale::menuscale, true);



	verdana = io.Fonts->AddFontFromMemoryTTF((void *) Verdana, sizeof Verdana, 17.0f, &font_cfg2, io.Fonts->GetGlyphRangesCyrillic());
	espnamet = io.Fonts->AddFontFromMemoryTTF((void *) Verdana, sizeof Verdana, 14.0f, &font_cfg2, io.Fonts->GetGlyphRangesCyrillic());
 //mains = io.Fonts->AddFontFromMemoryTTF(&regular, sizeof regular , 22.5f, NULL, io.Fonts->GetGlyphRangesCyrillic());
 //name = io.Fonts->AddFontFromMemoryTTF(&regular, sizeof regular, 13.5f, NULL, io.Fonts->GetGlyphRangesCyrillic());

  logoa = io.Fonts->AddFontFromMemoryTTF(Verdana, sizeof(Verdana), 37.5f, NULL, io.Fonts->GetGlyphRangesCyrillic());
   //fontsicons = io.Fonts->AddFontFromMemoryTTF(font_awesome_binary, sizeof font_awesome_binary, 18.5, &icons_config, icon_ranges)
  iconsss = io.Fonts->AddFontFromMemoryTTF(&icon, sizeof icon, 28, NULL, io.Fonts->GetGlyphRangesCyrillic());
	pixel = io.Fonts->AddFontFromMemoryTTF(&_pixel, sizeof _pixel, 10.5*espscale::espscale);
    // Фолбэк-шрифты темы (нет lexend/icomoon TTF в 67 — используем Verdana)
    font::lexend_bold = verdana;
    font::lexend_regular = verdana;
    font::lexend_general_bold = verdana;
    font::icomoon = fontsicons;
    font::icomoon_widget2 = font::icomoon_widget;
    io.Fonts->AddFontDefault(&font_cfg);
   // ImGui::GetStyle().ScaleAllSizes(3.0f);
    ::g_Initialized = true;
    return true;
}

// Окно оверлея создаётся один раз с фиксированным размером. При повороте
// экрана логическое пространство слоёв меняется (720x1600 <-> 1600x720), а
// окно оставалось старого размера и обрезалось. Поэтому при смене размера
// пересоздаём окно и EGL-поверхность под новый размер.
static bool recreateOverlaySurface(int w, int h) {
    #ifdef USE_OPENGL
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) return false;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) {
        eglDestroySurface(display, surface);
        surface = EGL_NO_SURFACE;
    }
    ANativeWindow* old = native_window;
    ANativeWindow* nw = android::ANativeWindowCreator::Create("AImGui", w, h, false);
    if (!nw) {
        printf("[draw] recreate: Create failed\n"); fflush(stdout);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
        return false;
    }
    ANativeWindow_acquire(nw);
    EGLint egl_format = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(nw, 0, 0, egl_format);
    surface = eglCreateWindowSurface(display, config, nw, nullptr);
    if (surface == EGL_NO_SURFACE) {
        printf("[draw] recreate: eglCreateWindowSurface error\n"); fflush(stdout);
        return false;
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("[draw] recreate: eglMakeCurrent error\n"); fflush(stdout);
        return false;
    }
    native_window = nw;
    if (old) {
        ANativeWindow_release(old);
        android::ANativeWindowCreator::Destroy(old);
    }
    return true;
    #else
    return false;
    #endif
}

void drawBegin() {
    screen_config();
    // Поворот ИЛИ смена разрешения: окно, тач и ImGui должны жить в одной сетке.
    if (::orientation != displayInfo.orientation ||
        (displayInfo.width > 0 && displayInfo.width != native_window_screen_x) ||
        (displayInfo.height > 0 && displayInfo.height != native_window_screen_y)) {
        const bool sizeChanged = displayInfo.width > 0 && displayInfo.height > 0 &&
            (displayInfo.width != native_window_screen_x || displayInfo.height != native_window_screen_y);
        ::orientation = displayInfo.orientation;
        if (sizeChanged) {
            if (recreateOverlaySurface(displayInfo.width, displayInfo.height)) {
                native_window_screen_x = displayInfo.width;
                native_window_screen_y = displayInfo.height;
                abs_ScreenX = displayInfo.width;
                abs_ScreenY = displayInfo.height;
            }
        }
        touch::update(native_window_screen_x, native_window_screen_y, displayInfo.orientation);
        printf("[draw] rotate: %dx%d orient=%u\n", native_window_screen_x, native_window_screen_y, displayInfo.orientation);
        fflush(stdout);
    }

    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_NewFrame();
    #else
        ImGui_ImplVulkan_NewFrame();
    #endif        
    ImGui_ImplAndroid_NewFrame(native_window_screen_x, native_window_screen_y);
    // Касания отдаём в очередь событий ImGui строго до NewFrame(): там
    // UpdateInputEvents() разложит tap на кадры (hover -> down -> up), и
    // короткое нажатие не потеряется.
    touch::pump();
    ImGui::NewFrame();
}

void drawEnd() {
    ImGui::Render();
    
    #ifdef USE_OPENGL
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        eglSwapBuffers(display, surface);
    #else
        FrameRender(ImGui::GetDrawData());
        FramePresent();
    #endif
}



void shutdown() {
    if (!g_Initialized) {
        return;
    }
    // Cleanup
    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_Shutdown();
    #else
        DeviceWait();
        ImGui_ImplVulkan_Shutdown();
    #endif
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    
    
    #ifdef USE_OPENGL
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) {
                eglDestroyContext(display, context);
            }
            if (surface != EGL_NO_SURFACE) {
                eglDestroySurface(display, surface);
            }
            eglTerminate(display);
        }
        display = EGL_NO_DISPLAY;
        context = EGL_NO_CONTEXT;
        surface = EGL_NO_SURFACE;
    #else    
        CleanupVulkanWindow();
        CleanupVulkan();
    #endif
    
    ANativeWindow_release(native_window);
    android::ANativeWindowCreator::Destroy(native_window);
    ::g_Initialized = false;
}
