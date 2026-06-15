#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <climits>
#include <gl\gl.h>
#include <gl\glu.h>
#include <atomic>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

#include "Protocol.h"
#include "NetworkModule.h"

// ---------------------------------------------------------------------------
// Window / GL globals
// ---------------------------------------------------------------------------
HDC       hDC      = NULL;
HGLRC     hRC      = NULL;
HWND      hWnd     = NULL;
HINSTANCE hInstance;

bool keys[256];
bool active    = TRUE;
bool fullscreen = FALSE;

// ---------------------------------------------------------------------------
// Layout constants  (all in virtual screen pixels, y=0 bottom)
// ---------------------------------------------------------------------------
static const int WIN_W    = 1280;
static const int WIN_H    = 720;
static const int TITLE_H  = 40;      // top title bar
static const int STATS_W  = 240;     // left panel width
static const int GRAPH_W  = 330;     // right panel width
// map occupies x: STATS_W .. WIN_W-GRAPH_W,  y: 0 .. WIN_H-TITLE_H
static const int MAP_X1   = STATS_W;
static const int MAP_X2   = WIN_W - GRAPH_W;
static const int MAP_Y1   = 0;
static const int MAP_Y2   = WIN_H - TITLE_H;
static const int CONTENT_H = WIN_H - TITLE_H;  // 680

// Font display-list base
static GLuint g_fontBase = 0;

// Start tick for uptime display
static DWORD g_startTick = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
GLvoid KillGLWindow();

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------
static void BuildFont()
{
    HFONT font = CreateFont(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH,
        L"Courier New");
    g_fontBase = glGenLists(96);
    HFONT old  = (HFONT)SelectObject(hDC, font);
    wglUseFontBitmaps(hDC, 32, 96, g_fontBase);
    SelectObject(hDC, old);
    DeleteObject(font);
}

static void KillFont() { glDeleteLists(g_fontBase, 96); }

// Print text at screen position (x,y) — y=0 is bottom
static void DrawText2D(float x, float y, const char* fmt, ...)
{
    char text[256];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    vsprintf_s(text, fmt, ap);
    va_end(ap);
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(g_fontBase - 32);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);
    glPopAttrib();
}

// ---------------------------------------------------------------------------
// Primitive helpers
// ---------------------------------------------------------------------------
static void FillRect2D(float x1, float y1, float x2, float y2,
                       float r, float g, float b, float a = 1.0f)
{
    if (a < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x1, y1); glVertex2f(x2, y1);
    glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
    if (a < 1.0f) glDisable(GL_BLEND);
}

static void StrokeRect2D(float x1, float y1, float x2, float y2,
                         float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x1, y1); glVertex2f(x2, y1);
    glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}

static void DrawHLine(float x1, float x2, float y, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_LINES);
    glVertex2f(x1, y); glVertex2f(x2, y);
    glEnd();
}

static void DrawVLine(float x, float y1, float y2, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_LINES);
    glVertex2f(x, y1); glVertex2f(x, y2);
    glEnd();
}

// Dashed horizontal line using GL_LINE_STIPPLE
static void DrawDashedHLine(float x1, float x2, float y,
                            float r, float g, float b)
{
    glColor3f(r, g, b);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(2, 0x5555);
    glBegin(GL_LINES);
    glVertex2f(x1, y); glVertex2f(x2, y);
    glEnd();
    glDisable(GL_LINE_STIPPLE);
}

// ---------------------------------------------------------------------------
// Graph helper
//   Draws the history ring buffer as a line strip inside [x1,y1]-[x2,y2].
//   threshold1 / threshold2: draw dashed reference lines (0 = skip).
// ---------------------------------------------------------------------------
static void DrawGraph(float x1, float y1, float x2, float y2,
                      const float* data, int historySize, int headIndex,
                      float maxVal,
                      float lr, float lg, float lb,
                      float threshold1, float threshold2)
{
    if (maxVal <= 0.0f) maxVal = 1.0f;
    const float w = x2 - x1;
    const float h = y2 - y1;

    // Reference threshold lines
    if (threshold1 > 0.0f && threshold1 <= maxVal)
    {
        float ty = y1 + (threshold1 / maxVal) * h;
        DrawDashedHLine(x1, x2, ty, 1.0f, 1.0f, 0.2f);
    }
    if (threshold2 > 0.0f && threshold2 <= maxVal)
    {
        float ty = y1 + (threshold2 / maxVal) * h;
        DrawDashedHLine(x1, x2, ty, 1.0f, 0.4f, 0.1f);
    }

    // Data line
    glColor3f(lr, lg, lb);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < historySize; ++i)
    {
        // Walk forward from the sample just after head (oldest) to head-1 (newest)
        int idx = (headIndex + i) % historySize;
        float val = data[idx];
        float px  = x1 + (float)i / (float)(historySize - 1) * w;
        float py  = y1 + (val / maxVal) * h;
        if (py > y2) py = y2;
        glVertex2f(px, py);
    }
    glEnd();
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
static void SetDelayColor(int ms)
{
    if      (ms <  100) glColor3f(0.2f, 1.0f, 0.4f);  // green: good
    else if (ms <  150) glColor3f(1.0f, 1.0f, 0.2f);  // yellow: caution
    else                glColor3f(1.0f, 0.3f, 0.2f);  // red: bad
}

// ---------------------------------------------------------------------------
// Main draw
// ---------------------------------------------------------------------------
static int DrawGLScene()
{
    // --- Gather data ---
    int    ptSize     = 0;
    float* positions  = nullptr;
    int*   states     = nullptr;
    GetPointCloud(&ptSize, &positions, &states);

    const int  activeCount  = active_clients.load();
    const int  totalConns   = num_connections.load();
    const int  deadCount    = g_player_dead.load();
    const int  delayNow     = global_delay.load();
    int        delayMin     = delay_min_val.load();
    const int  delayMax     = delay_max_val.load();
    const int  delayAvg     = delay_avg_val.load();
    const TestPhase phase   = current_phase.load();
    const float avgMonsters = g_avg_visible_monsters.load();
    const float avgPlayers  = g_avg_visible_players.load();

    int zoneCounts[ZONE_COUNT];
    for (int z = 0; z < ZONE_COUNT; ++z)
        zoneCounts[z] = g_zone_player_count[z].load(std::memory_order_relaxed);

    const int monsterKnown = g_monster_known.load(std::memory_order_relaxed);
    const int monsterDead  = g_monster_dead.load(std::memory_order_relaxed);

    // Sentinel check: delay_min_val initialises to INT_MAX before first sample
    if (delayMin > 99999) delayMin = 0;

    // Uptime
    DWORD elapsed = GetTickCount() - g_startTick;
    int hours   = (int)(elapsed / 3600000);
    int minutes = (int)((elapsed % 3600000) / 60000);
    int seconds = (int)((elapsed % 60000)   / 1000);

    // -----------------------------------------------------------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // =======================================================================
    // TITLE BAR  (y: 680 .. 720)
    // =======================================================================
    FillRect2D(0.0f, (float)MAP_Y2, (float)WIN_W, (float)WIN_H,
               0.07f, 0.07f, 0.12f);
    DrawHLine(0.0f, (float)WIN_W, (float)MAP_Y2, 0.25f, 0.35f, 0.5f);

    glColor3f(0.85f, 0.95f, 1.0f);
    DrawText2D(12,  (float)MAP_Y2 + 13, "MMORPG STRESS TEST");

    glColor3f(0.45f, 0.65f, 0.85f);
    DrawText2D(370, (float)MAP_Y2 + 13, "Server: 127.0.0.1:4000");

    glColor3f(0.6f, 0.6f, 0.6f);
    DrawText2D(860, (float)MAP_Y2 + 13, "Uptime: %02d:%02d:%02d",
               hours, minutes, seconds);

    // =======================================================================
    // LEFT STATS PANEL  (x: 0..240, y: 0..680)
    // =======================================================================
    FillRect2D(0.0f, 0.0f, (float)STATS_W, (float)MAP_Y2,
               0.04f, 0.04f, 0.07f);
    DrawVLine((float)STATS_W, 0.0f, (float)MAP_Y2, 0.22f, 0.28f, 0.38f);

    const float LX  = 10.0f;   // left x for text
    const float LH  = 18.0f;   // line height
    float sy        = (float)MAP_Y2 - 24.0f;  // current y, moving downward

    // Helper: draw a section header
    auto SectionHeader = [&](const char* title) {
        glColor3f(0.35f, 0.75f, 1.0f);
        DrawText2D(LX, sy, title);
        sy -= LH * 0.4f;
        DrawHLine(LX, (float)STATS_W - 8.0f, sy, 0.2f, 0.3f, 0.45f);
        sy -= LH * 0.8f;
    };

    // -- CONNECTIONS --
    SectionHeader("[ CONNECTIONS ]");
    glColor3f(0.2f, 1.0f, 0.45f);
    DrawText2D(LX, sy, "Active  : %d", activeCount);   sy -= LH;
    glColor3f(1.0f, 0.38f, 0.38f);
    DrawText2D(LX, sy, "Dead    : %d", deadCount);      sy -= LH;
    glColor3f(0.75f, 0.75f, 0.75f);
    DrawText2D(LX, sy, "Total   : %d", totalConns);     sy -= LH * 1.8f;

    // -- PHASE --
    SectionHeader("[ PHASE ]");
    const char* phaseStr;
    float pr, pg, pb;
    switch (phase) {
    case TestPhase::RAMP_UP:
        phaseStr = "^ RAMP UP";   pr = 0.2f;  pg = 1.0f;  pb = 0.45f; break;
    case TestPhase::STABLE:
        phaseStr = "= STABLE";    pr = 1.0f;  pg = 1.0f;  pb = 0.2f;  break;
    case TestPhase::REDUCING:
        phaseStr = "v REDUCING";  pr = 1.0f;  pg = 0.38f; pb = 0.2f;  break;
    default:
        phaseStr = "UNKNOWN";     pr = pg = pb = 0.7f;                  break;
    }
    glColor3f(pr, pg, pb);
    DrawText2D(LX, sy, "%s", phaseStr);
    sy -= LH * 1.8f;

    // -- NETWORK --
    SectionHeader("[ NETWORK ]");
    SetDelayColor(delayNow);
    DrawText2D(LX, sy, "Current : %dms", delayNow);  sy -= LH;
    glColor3f(0.5f, 1.0f, 0.6f);
    DrawText2D(LX, sy, "Min     : %dms", delayMin);  sy -= LH;
    glColor3f(1.0f, 0.5f, 0.5f);
    DrawText2D(LX, sy, "Max     : %dms", delayMax);  sy -= LH;
    glColor3f(0.75f, 0.75f, 0.75f);
    DrawText2D(LX, sy, "Avg     : %dms", delayAvg);  sy -= LH * 1.8f;

    // -- VISIBILITY --
    SectionHeader("[ VISIBILITY ]");
    glColor3f(1.0f, 0.65f, 0.25f);
    DrawText2D(LX, sy, "Monsters: %.1f", avgMonsters); sy -= LH;
    glColor3f(0.55f, 0.78f, 1.0f);
    DrawText2D(LX, sy, "Players : %.1f", avgPlayers);  sy -= LH * 1.8f;

    // -- MONSTERS --
    SectionHeader("[ MONSTERS ]");
    glColor3f(0.75f, 0.75f, 0.75f);
    DrawText2D(LX, sy, "Total   : %d", MAX_MONSTER);   sy -= LH;
    glColor3f(0.55f, 0.78f, 1.0f);
    DrawText2D(LX, sy, "Known   : %d", monsterKnown);  sy -= LH;
    glColor3f(1.0f, 0.38f, 0.38f);
    DrawText2D(LX, sy, "Dead    : %d", monsterDead);   sy -= LH * 1.8f;

    // -- ZONES (4x4 player count grid) --
    SectionHeader("[ ZONES ]");
    // Column header row
    glColor3f(0.35f, 0.55f, 0.75f);
    DrawText2D(LX, sy, "   Z1   Z2   Z3   Z4");         sy -= LH;
    for (int row = 0; row < 4; ++row)
    {
        const int base = row * 4;
        glColor3f(0.2f, 1.0f, 0.45f);
        DrawText2D(LX, sy, "R%d %4d %4d %4d %4d",
            row + 1,
            zoneCounts[base + 0], zoneCounts[base + 1],
            zoneCounts[base + 2], zoneCounts[base + 3]);
        sy -= LH;
    }
    sy -= LH * 0.5f;

    // -- LEGEND (compact) --
    glColor3f(0.2f, 1.0f, 0.45f);
    DrawText2D(LX, sy, "* Alive");
    glColor3f(1.0f, 0.3f, 0.3f);
    DrawText2D(LX + 80.0f, sy, "* Dead");

    // =======================================================================
    // WORLD MAP  (x: 240..950, y: 0..680)
    // =======================================================================
    FillRect2D((float)MAP_X1, (float)MAP_Y1, (float)MAP_X2, (float)MAP_Y2,
               0.02f, 0.02f, 0.05f);

    // Grid lines (10 divisions, every 200 world units)
    glColor3f(0.08f, 0.12f, 0.18f);
    const float mapW = (float)(MAP_X2 - MAP_X1);
    const float mapH = (float)(MAP_Y2 - MAP_Y1);
    glBegin(GL_LINES);
    for (int g = 1; g < 10; ++g)
    {
        float gx = (float)MAP_X1 + (float)g / 10.0f * mapW;
        float gy = (float)MAP_Y1 + (float)g / 10.0f * mapH;
        glVertex2f(gx, (float)MAP_Y1); glVertex2f(gx, (float)MAP_Y2);  // vertical
        glVertex2f((float)MAP_X1, gy); glVertex2f((float)MAP_X2, gy);  // horizontal
    }
    glEnd();

    // Zone boundaries (every 500 world units = 4 zones per axis)
    glColor3f(0.20f, 0.35f, 0.55f);
    glBegin(GL_LINES);
    for (int z = 1; z < 4; ++z)
    {
        float zx = (float)MAP_X1 + (float)z / 4.0f * mapW;
        float zy = (float)MAP_Y1 + (float)z / 4.0f * mapH;
        glVertex2f(zx, (float)MAP_Y1); glVertex2f(zx, (float)MAP_Y2);
        glVertex2f((float)MAP_X1, zy); glVertex2f((float)MAP_X2, zy);
    }
    glEnd();

    // Map border
    StrokeRect2D((float)MAP_X1, (float)MAP_Y1, (float)MAP_X2, (float)MAP_Y2,
                 0.28f, 0.38f, 0.52f);

    // Map title
    glColor3f(0.45f, 0.55f, 0.65f);
    DrawText2D((float)MAP_X1 + 8, (float)MAP_Y2 - 16,
               "WORLD MAP (2000x2000)  |  %d clients visible", ptSize);

    // Draw client points
    glPointSize(2.5f);
    glBegin(GL_POINTS);
    for (int i = 0; i < ptSize; ++i)
    {
        float wx = positions[i * 2];
        float wy = positions[i * 2 + 1];
        float sx = (float)MAP_X1 + (wx / 2000.0f) * mapW;
        float sy_p = (float)MAP_Y1 + (1.0f - wy / 2000.0f) * mapH;

        if (states[i] == 1)
            glColor3f(1.0f, 0.22f, 0.22f);   // dead  → red
        else
            glColor3f(0.22f, 1.0f, 0.45f);   // alive → green
        glVertex2f(sx, sy_p);
    }
    glEnd();

    // Zone labels with player counts (one per zone cell)
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const int zoneId = row * 4 + col;
            const float cx = (float)MAP_X1 + (col + 0.5f) / 4.0f * mapW;
            const float cy = (float)MAP_Y1 + (1.0f - (row + 0.5f) / 4.0f) * mapH;

            // Zone number
            glColor3f(0.25f, 0.45f, 0.70f);
            DrawText2D(cx - 14.0f, cy + 7.0f, "Z%02d", zoneId + 1);

            // Player count
            const int cnt = zoneCounts[zoneId];
            if (cnt > 0) glColor3f(0.20f, 0.90f, 0.42f);
            else         glColor3f(0.35f, 0.35f, 0.35f);
            DrawText2D(cx - 14.0f, cy - 8.0f, "%d", cnt);
        }
    }

    // =======================================================================
    // RIGHT GRAPH PANEL  (x: 950..1280, y: 0..680)
    // =======================================================================
    FillRect2D((float)MAP_X2, (float)MAP_Y1, (float)WIN_W, (float)MAP_Y2,
               0.04f, 0.04f, 0.07f);
    DrawVLine((float)MAP_X2, (float)MAP_Y1, (float)MAP_Y2, 0.22f, 0.28f, 0.38f);

    const float GX1 = (float)MAP_X2 + 10.0f;
    const float GX2 = (float)WIN_W  - 8.0f;

    // Mid-divider between two graphs
    const float G_MID_Y = (float)MAP_Y2 / 2.0f;  // 340
    DrawHLine((float)MAP_X2, (float)WIN_W, G_MID_Y, 0.18f, 0.24f, 0.32f);

    // ------------------------------------------------------------------
    // TOP GRAPH: DELAY  (y: G_MID_Y .. MAP_Y2)
    // ------------------------------------------------------------------
    const float DGY1 = G_MID_Y + 28.0f;   // graph draw area bottom
    const float DGY2 = (float)MAP_Y2 - 20.0f;  // graph draw area top

    glColor3f(0.35f, 0.75f, 1.0f);
    DrawText2D(GX1, (float)MAP_Y2 - 16.0f, "DELAY (ms)");

    StrokeRect2D(GX1, DGY1, GX2, DGY2, 0.18f, 0.24f, 0.32f);

    {
        float maxDelay = (float)max(200, delayMax + 20);
        DrawGraph(GX1, DGY1, GX2, DGY2,
                  g_delay_history, GRAPH_HISTORY_SIZE, g_history_index,
                  maxDelay, 0.3f, 0.9f, 0.3f, 100.0f, 150.0f);

        // Threshold labels
        float t1y = DGY1 + (100.0f / maxDelay) * (DGY2 - DGY1);
        float t2y = DGY1 + (150.0f / maxDelay) * (DGY2 - DGY1);
        if (t1y < DGY2) {
            glColor3f(1.0f, 1.0f, 0.2f);
            DrawText2D(GX2 - 50.0f, t1y + 2.0f, "100ms");
        }
        if (t2y < DGY2) {
            glColor3f(1.0f, 0.4f, 0.15f);
            DrawText2D(GX2 - 50.0f, t2y + 2.0f, "150ms");
        }

        // Current value
        SetDelayColor(delayNow);
        DrawText2D(GX1, DGY1 - 16.0f, "Now:%dms  Max:%dms", delayNow, delayMax);
    }

    // ------------------------------------------------------------------
    // BOTTOM GRAPH: CLIENTS  (y: 0 .. G_MID_Y)
    // ------------------------------------------------------------------
    const float CGY1 = 28.0f;
    const float CGY2 = G_MID_Y - 20.0f;

    glColor3f(0.35f, 0.75f, 1.0f);
    DrawText2D(GX1, G_MID_Y - 16.0f, "CLIENTS");

    StrokeRect2D(GX1, CGY1, GX2, CGY2, 0.18f, 0.24f, 0.32f);

    {
        float maxClients = 100.0f;
        for (int i = 0; i < GRAPH_HISTORY_SIZE; ++i)
            if (g_client_history[i] > maxClients) maxClients = g_client_history[i];
        maxClients *= 1.15f;
        if (maxClients < 100.0f) maxClients = 100.0f;

        DrawGraph(GX1, CGY1, GX2, CGY2,
                  g_client_history, GRAPH_HISTORY_SIZE, g_history_index,
                  maxClients, 0.35f, 0.6f, 1.0f, 0.0f, 0.0f);

        glColor3f(0.2f, 1.0f, 0.45f);
        DrawText2D(GX1, CGY1 - 16.0f, "Active:%d  Peak:%.0f", activeCount, maxClients / 1.15f);
    }

    return TRUE;
}

// ---------------------------------------------------------------------------
// GL window setup
// ---------------------------------------------------------------------------
GLvoid ReSizeGLScene(GLsizei width, GLsizei height)
{
    if (height == 0) height = 1;
    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Orthographic: pixel coords, y=0 bottom, y=WIN_H top
    glOrtho(0.0, (double)WIN_W, 0.0, (double)WIN_H, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static int InitGL()
{
    glShadeModel(GL_SMOOTH);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClearDepth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    BuildFont();
    return TRUE;
}

GLvoid KillGLWindow()
{
    if (fullscreen)
    {
        ChangeDisplaySettings(NULL, 0);
        ShowCursor(TRUE);
    }
    if (hRC)
    {
        if (!wglMakeCurrent(NULL, NULL))
            MessageBox(NULL, L"Release Of DC And RC Failed.", L"SHUTDOWN ERROR",
                       MB_OK | MB_ICONINFORMATION);
        if (!wglDeleteContext(hRC))
            MessageBox(NULL, L"Release Rendering Context Failed.", L"SHUTDOWN ERROR",
                       MB_OK | MB_ICONINFORMATION);
        hRC = NULL;
    }
    if (hDC && !ReleaseDC(hWnd, hDC))
    {
        MessageBox(NULL, L"Release Device Context Failed.", L"SHUTDOWN ERROR",
                   MB_OK | MB_ICONINFORMATION);
        hDC = NULL;
    }
    if (hWnd && !DestroyWindow(hWnd))
    {
        MessageBox(NULL, L"Could Not Release hWnd.", L"SHUTDOWN ERROR",
                   MB_OK | MB_ICONINFORMATION);
        hWnd = NULL;
    }
    if (!UnregisterClass(L"OpenGL", hInstance))
    {
        MessageBox(NULL, L"Could Not Unregister Class.", L"SHUTDOWN ERROR",
                   MB_OK | MB_ICONINFORMATION);
        hInstance = NULL;
    }
    KillFont();
}

BOOL CreateGLWindow(const wchar_t* title, int width, int height,
                    BYTE bits, bool fullscreenflag)
{
    WNDCLASS wc{};
    DWORD    dwExStyle, dwStyle;
    RECT     wr = { 0, 0, (LONG)width, (LONG)height };

    fullscreen = fullscreenflag;

    hInstance            = GetModuleHandle(NULL);
    wc.style             = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc       = (WNDPROC)WndProc;
    wc.hInstance         = hInstance;
    wc.hIcon             = LoadIcon(NULL, IDI_WINLOGO);
    wc.hCursor           = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName     = L"OpenGL";

    if (!RegisterClass(&wc))
    {
        MessageBox(NULL, L"Failed To Register The Window Class.", L"ERROR",
                   MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (fullscreen)
    {
        DEVMODE dm{};
        dm.dmSize       = sizeof(dm);
        dm.dmPelsWidth  = (DWORD)width;
        dm.dmPelsHeight = (DWORD)height;
        dm.dmBitsPerPel = bits;
        dm.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

        if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
        {
            if (MessageBox(NULL,
                    L"Fullscreen mode not supported. Use windowed mode?",
                    L"Stress Test", MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
                fullscreen = FALSE;
            else
            {
                MessageBox(NULL, L"Program will now close.", L"ERROR",
                           MB_OK | MB_ICONSTOP);
                return FALSE;
            }
        }
    }

    if (fullscreen)
    {
        dwExStyle = WS_EX_APPWINDOW;
        dwStyle   = WS_POPUP;
        ShowCursor(FALSE);
    }
    else
    {
        dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
        dwStyle   = WS_OVERLAPPEDWINDOW;
    }

    AdjustWindowRectEx(&wr, dwStyle, FALSE, dwExStyle);

    hWnd = CreateWindowEx(dwExStyle, L"OpenGL", title,
                          dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                          0, 0,
                          wr.right - wr.left, wr.bottom - wr.top,
                          NULL, NULL, hInstance, NULL);
    if (!hWnd)
    {
        KillGLWindow();
        MessageBox(NULL, L"Window Creation Error.", L"ERROR",
                   MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    static PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, bits,
        0,0,0,0,0,0, 0, 0, 0, 0,0,0,0,
        16, 0, 0, PFD_MAIN_PLANE, 0, 0,0,0
    };

    GLuint pf;
    if (!(hDC = GetDC(hWnd)))                                  goto err;
    if (!(pf  = ChoosePixelFormat(hDC, &pfd)))                 goto err;
    if (!SetPixelFormat(hDC, pf, &pfd))                        goto err;
    if (!(hRC = wglCreateContext(hDC)))                        goto err;
    if (!wglMakeCurrent(hDC, hRC))                             goto err;

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
    SetFocus(hWnd);
    ReSizeGLScene(width, height);
    if (!InitGL()) goto err;
    return TRUE;

err:
    KillGLWindow();
    MessageBox(NULL, L"Initialization Failed.", L"ERROR", MB_OK | MB_ICONEXCLAMATION);
    return FALSE;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hW, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ACTIVATE:
        active = !HIWORD(wParam);
        return 0;

    case WM_SYSCOMMAND:
        if (wParam == SC_SCREENSAVE || wParam == SC_MONITORPOWER) return 0;
        break;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        keys[wParam] = TRUE;
        return 0;

    case WM_KEYUP:
        keys[wParam] = FALSE;
        return 0;

    case WM_SIZE:
        ReSizeGLScene(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }
    return DefWindowProc(hW, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain / main
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    MSG  msg{};
    BOOL done = FALSE;

    fullscreen = FALSE;

    if (!CreateGLWindow(L"MMORPG Stress Test", WIN_W, WIN_H, 32, fullscreen))
        return 0;

    g_startTick = GetTickCount();
    InitializeNetwork();

    while (!done)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = TRUE;
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            if (active)
            {
                if (!DrawGLScene() || keys[VK_ESCAPE])
                    done = TRUE;
                else
                    SwapBuffers(hDC);
            }

            if (keys[VK_F1])
            {
                keys[VK_F1] = FALSE;
                KillGLWindow();
                fullscreen = !fullscreen;
                if (!CreateGLWindow(L"MMORPG Stress Test", WIN_W, WIN_H, 32, fullscreen))
                    return 0;
            }
        }
    }

    KillGLWindow();
    return (int)msg.wParam;
}

int main() { return WinMain(0, 0, 0, 0); }
