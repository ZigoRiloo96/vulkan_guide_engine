
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

namespace logger
{
#if defined(IMGUI_API)
  static ExampleAppLog g_Log;
#endif

  static FILE* log_file;

  char buffer[1024];

#if VKE_WIN32
    static HANDLE  hConsole;
#endif

  static void
  init(const char* filename)
  {
    log_file = fopen(filename, "w");
  }

  static void
  term()
  {
    fclose(log_file);
  }

  static void
  output(int color)
  {
    fprintf(log_file, buffer);

#if VKE_CONSOLE
#if VKE_WIN32
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, (WORD)color);
#endif
    printf(buffer);
#if VKE_WIN32
    SetConsoleTextAttribute(hConsole, 7);
#endif
#endif

#if defined(IMGUI_API)
    g_Log.AddLog("[%.1f][%s]%s\n", ImGui::GetTime(), color == 8 ? "LOG" : color == 4 ? "ERR" : "WARN", buffer);
#endif
  }

  static void
  err(const char* format, ...)
  {
    va_list vl;
    va_start(vl, format);
    vsprintf(buffer, format, vl);

#if VKE_CONSOLE
    printf("[ERR]");
#endif

    fprintf(log_file, "[ERR]");

    output(4);

#if VKE_CONSOLE
    printf("\n");
#endif

    fprintf(log_file, "\n");

    va_end(vl);
  }

  static void
  log(const char* format, ...)
  {
    va_list vl;
    va_start(vl, format);
    vsprintf(buffer, format, vl);

#if VKE_CONSOLE
    printf("[LOG]");
#endif

    fprintf(log_file, "[LOG]");

    output(8);

#if VKE_CONSOLE
    printf("\n");
#endif

    fprintf(log_file, "\n");

    va_end(vl);
  }

  static void
  warn(const char* format, ...)
  {
    va_list vl;
    va_start(vl, format);
    vsprintf(buffer, format, vl);

#if VKE_CONSOLE
    printf("[WARN]");
#endif

    fprintf(log_file, "[WARN]");

    output(6);

#if VKE_CONSOLE
    printf("\n");
#endif

    fprintf(log_file, "\n");

    va_end(vl);
  }

  static bool g_Visible = false;

  static void
  ImGuiDraw()
  {
    if (g_Visible)
    {
#if defined(IMGUI_API)
      ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
      ImGui::Begin("logger", &g_Visible);
      ImGui::End();
      
      g_Log.Draw("logger", &g_Visible);
#endif
    }
  }
}


