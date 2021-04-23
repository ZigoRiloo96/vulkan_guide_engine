
#include <windows.h>

#include "external.h"

#include "shared.h"

#include "vk/vk_renderer.cpp"

#include "droper.h"

static bool isRun;
static bool DEBUGGlobalShowCursor = true;
static glm::ivec2 GlobalEnforcedAspectRatio = { 16, 9 };

global_variable f64 awake_time;
const global_variable f64 FIXED_TIME = (1.0 / 60.0) * 1000000;

global_variable bool f_IsInSizeMove = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK
WindowProc(
  HWND   Window,
  UINT   Message,
  WPARAM WParam,
  LPARAM LParam
)
{
  if (ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam))
    return true;

  LRESULT Result = 0;

  switch (Message)
  {

  case WM_ENTERSIZEMOVE:
  {
  } break;

  case WM_EXITSIZEMOVE:
  {
  } break;


  case WM_SIZE:
  {
    vk::renderer::Resize();
  } break;

  case WM_DESTROY:
  case WM_CLOSE:
  {
    isRun = false;
  } break;

  case WM_SETCURSOR:
  {
    if(DEBUGGlobalShowCursor)
    {
      Result = DefWindowProcA(Window, Message, WParam, LParam);
    }
    else
    {
      SetCursor(0);
    }
  } break;

  case WM_ACTIVATEAPP:
  {} break;

  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_KEYDOWN:
  case WM_KEYUP:
  {
  } break;

  default:
  {
    Result = DefWindowProc(Window, Message, WParam, LParam);
  } break;

  }

  return Result;
}

static bool fpsWindowsOpen = true;

internal void
ImGuiShowFPS(u64 avgFPS, f64 delta)
{
  static int corner = 2;
  ImGuiIO& io = ImGui::GetIO();
  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  if (corner != -1)
  {
    const float PAD = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 work_size = viewport->WorkSize;
    ImVec2 window_pos, window_pos_pivot;
    window_pos.x = (corner & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
    window_pos.y = (corner & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
    window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
    window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoMove;
  }
  ImGui::SetNextWindowBgAlpha(0.35f);
  if (ImGui::Begin("Example: Simple overlay", &fpsWindowsOpen, window_flags))
  {
    f64 mili_delta = delta * 1.0e-6;
    ImGui::Text("FPS: (%lld)", avgFPS);

    static f32 values[180] = {};
    static int values_offset = 0;
    float average = (f32)(mili_delta);

    for (size_t i = 1; i < 180; i++)
    {
       values[i-1] = values[i];
    }

    values[179] = average;

    char overlay[32];
    sprintf(overlay, "%f", average);

    ImGui::PlotLines("", values, IM_ARRAYSIZE(values), values_offset, overlay, 0.0f, 60.0f, ImVec2(0, 80.0f));

    if (ImGui::BeginPopupContextWindow())
    {
      if (ImGui::MenuItem("Custom",       NULL, corner == -1)) corner = -1;
      if (ImGui::MenuItem("Top-left",     NULL, corner == 0)) corner = 0;
      if (ImGui::MenuItem("Top-right",    NULL, corner == 1)) corner = 1;
      if (ImGui::MenuItem("Bottom-left",  NULL, corner == 2)) corner = 2;
      if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
      if (ImGui::MenuItem("Show logger", NULL, corner == 4)) logger::g_Visible = true;
      if (fpsWindowsOpen && ImGui::MenuItem("Close")) fpsWindowsOpen = false;
      ImGui::EndPopup();
    }
  }
  ImGui::End();
}

int CALLBACK
WinMain(HINSTANCE Instance,
        HINSTANCE PrevInstance,
        LPSTR CmdLine,
        int ShowCode)
{

  logger::init("Log.txt");

  WNDCLASS WindowClass = {};
  WindowClass.style = CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
  WindowClass.lpfnWndProc = &WindowProc;
  WindowClass.hInstance = Instance;
  WindowClass.lpszClassName = "DisusWindowClass";

  RegisterClass(&WindowClass);

  HWND WindowHendle = CreateWindowEx(
    0,
    WindowClass.lpszClassName,
    "Disus Engine",
    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, //WS_OVERLAPPEDWINDOW,//|WS_VISIBLE,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    0,
    0,
    Instance,
    0
  );

  if (WindowHendle)
  {
    ImGui::CreateContext();
    IMGUI_CHECKVERSION();

    vk::renderer::vulkan_platform_state VulkanPlatformState = vk::renderer::Win32InitVulkan(WindowHendle, Instance);

    vk::renderer::Init();

    ShowWindow(WindowHendle, SW_SHOW);

    OleInitialize(NULL);

    droper dm;
    RegisterDragDrop(WindowHendle, &dm);

    isRun = true;

    LARGE_INTEGER starTime;
    LARGE_INTEGER frequency;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&starTime);

    double nanoseconds_per_count = 1.0e9 / static_cast<double>(frequency.QuadPart);

    awake_time = starTime.QuadPart * nanoseconds_per_count;

    f64 timer = 0.0;
    f64 current = awake_time;
    f64 accumulator = 0.0;
    f64 fresh = 0.0;
    f64 delta = 0.0;

    u64 fpsCounter = 0;
    u64 maxFps = 0;
    u64 minFps = U64Max;
    u64 avgFps = 0;
    f64 fpsTimer = 0.0;

    while(isRun)
    {
      MSG Message;
      while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
      {
        TranslateMessage(&Message);
        DispatchMessage(&Message);
        if(Message.message == WM_QUIT)
        {
          isRun = false;
        }
      }

      if (!isRun) break;
      
      QueryPerformanceCounter(&starTime);
      fresh = starTime.QuadPart * nanoseconds_per_count;

      delta = fresh - current;

      current = fresh;

      accumulator += delta;

      // update input

      while (accumulator >= FIXED_TIME)
      {
        // physics update
        accumulator -= FIXED_TIME;
        timer += FIXED_TIME;
      }

      //draw
      fpsTimer += delta;
      if (fpsTimer >= 1.0e9)
      {
        maxFps = fpsCounter > maxFps ? fpsCounter : maxFps;
        minFps = fpsCounter < minFps ? fpsCounter : minFps;
        avgFps = fpsCounter;
        fpsCounter = 0;
        fpsTimer = 0.0;
      }
      fpsCounter += 1;

      if(f_IsInSizeMove) continue;

      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();

      logger::ImGuiDraw();

      ImGuiShowFPS(avgFps, delta);

      ImGui::Render();

      HDC DeviceContext = GetDC(WindowHendle);
      // DRAW
      vk::renderer::Draw();
      // SWAP
      SwapBuffers(DeviceContext);
      ReleaseDC(WindowHendle, DeviceContext);
    }    
  }

  RevokeDragDrop(WindowHendle);

  logger::term();
  
  OleUninitialize();
  return (0);
}