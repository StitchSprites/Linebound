#define NOMINMAX
#include <windows.h>
#include <array>
#include <vector>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstdint>

bool appRunning = false;
float deltaTime = 0.0f;
void (*fPtr_SystemUpdate)() = 0;
void (*fPtr_SystemShutdown)() = 0;
void (*fPtr_RenderUpdate)() = 0;
void (*fPtr_RenderShutdown)() = 0;

enum kCode : uint8_t {
    _VCursor, _HCursor, _VWheel, _HWheel, _LMouse, _MMouse, _RMouse, _XMouse1, _XMouse2,
    _F1, _F2, _F3, _F4, _F5, _F6, _F7, _F8, _F9, _F10, _F11, _F12,
    _N0, _N1, _N2, _N3, _N4, _N5, _N6, _N7, _N8, _N9,
    _A, _B, _C, _D, _E, _F, _G, _H, _I, _J, _K, _L, _M, _N, _O, _P, _Q, _R, _S, _T, _U, _V, _W, _X, _Y, _Z,
    _Space, _Escape, _Left, _Up, _Right, _Down, _LCtrl, _RCtrl, _AnyCtrl, _LShift, _RShift, _AnyShift, _LAlt, _RAlt, _AnyAlt, _KEYCODE_COUNT
};

enum kState : uint8_t { TAP = 1 << 0, RELEASE_TAP = 1 << 4, DOUBLETAP = 1 << 1, RELEASE_DOUBLETAP = 1 << 5, HOLD = 1 << 2, RELEASE_HOLD = 1 << 6, DOUBLETAP_HOLD = 1 << 3, RELEASE_DOUBLETAP_HOLD = 1 << 7 };

int key(int hotkey, int action);
namespace Program { namespace Draw { void drawFrame(); } }
namespace System {
    struct ICoord { int x = 0, y = 0; };
    struct Window {
        ICoord pos = {0, 0}, size = {1280, 720};
        ICoord clientPos = {0, 0}, clientSize = {1280, 720};
        ICoord monitorPos = {0, 0}, monitorSize = {1280, 720};
        ICoord workspacePos = {0, 0}, workspaceSize = {1280, 720};
        ICoord visiblePos = {0, 0}, visibleSize = {1280, 720};
        bool fullscreen = false, maximized = false, minimized = false, focused = false;
    };
    struct Input {
        ICoord cursor;
        bool cursorLocked = false, lockCursorWhenFocused = true;
        uint8_t OSkeymap[256] = {0}, keyState[_KEYCODE_COUNT] = {0};
        int keyStrength[_KEYCODE_COUNT] = {1};
        uint8_t prevKeyCode = 0;
        uint64_t lastPressTime = 0;
    };
    
    Window window;
    Input input;
    uint32_t* framebuffer = 0;
    std::vector<float> depthBuffer;
    int frameWidth = 0;

    void setKeyState(uint8_t code, bool pressed, int strength = 1) {
        input.keyStrength[code] = strength;
        if (!pressed) { input.keyState[code] <<= 4; return; }
        if (input.keyState[code] & 0xF) return;
        input.keyState[code] = TAP;
        if (input.prevKeyCode == code) {
            uint64_t currentTime = GetTickCount64();
            if (currentTime - input.lastPressTime <= 600) input.keyState[code] |= DOUBLETAP;
            input.lastPressTime = currentTime;
        } input.prevKeyCode = code;
    }

    namespace Windows {
        struct WinData { HWND hWin = 0; HINSTANCE hInstance = 0; } winData;
        struct BitBuffer { HDC hdc = 0, backDC = 0; HBITMAP backBmp = 0, oldBackBmp = 0; } bitBltData;

        void destroyBackbuffer() {
            if (bitBltData.backDC && bitBltData.oldBackBmp) { SelectObject(bitBltData.backDC, bitBltData.oldBackBmp); bitBltData.oldBackBmp = 0; }
            if (bitBltData.backBmp) { DeleteObject(bitBltData.backBmp); bitBltData.backBmp = 0; }
            if (bitBltData.backDC) { DeleteDC(bitBltData.backDC); bitBltData.backDC = 0; }
            framebuffer = 0; depthBuffer.clear(); frameWidth = 0;
        }

        void resizeBitBlt() {
            if (window.clientSize.x <= 0 || window.clientSize.y <= 0) return;
            destroyBackbuffer();
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = window.clientSize.x;
            bmi.bmiHeader.biHeight = -1;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            bitBltData.backDC = CreateCompatibleDC(bitBltData.hdc);
            bitBltData.backBmp = CreateDIBSection(bitBltData.hdc, &bmi, DIB_RGB_COLORS, (void**)&framebuffer, 0, 0);
            bitBltData.oldBackBmp = (HBITMAP)SelectObject(bitBltData.backDC, bitBltData.backBmp);
            frameWidth = window.clientSize.x;
            depthBuffer.resize(window.clientSize.x);
        }

        void updateBitBlt() {
            if (!framebuffer || window.clientSize.x <= 0 || window.clientSize.y <= 0) return;
            SetStretchBltMode(bitBltData.hdc, COLORONCOLOR);
            StretchBlt(bitBltData.hdc, 0, 0, window.clientSize.x, window.clientSize.y, bitBltData.backDC, 0, 0, window.clientSize.x, 1, SRCCOPY);
        }

        bool setupBitBlt() {
            bitBltData.hdc = GetDC(winData.hWin);
            if (!bitBltData.hdc) return false;
            resizeBitBlt();
            fPtr_RenderUpdate = updateBitBlt;
            fPtr_RenderShutdown = destroyBackbuffer;
            return framebuffer != 0;
        }

        void updateMonitorFromWindowCenter() {
            MONITORINFO mi = {sizeof(MONITORINFO)};
            GetMonitorInfo(MonitorFromPoint({window.pos.x + window.size.x / 2, window.pos.y + window.size.y / 2}, MONITOR_DEFAULTTONEAREST), &mi);
            window.monitorPos = {mi.rcMonitor.left, mi.rcMonitor.top};
            window.monitorSize = {mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top};
            window.workspacePos = {mi.rcWork.left, mi.rcWork.top};
            window.workspaceSize = {mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
            int left = std::clamp((int)mi.rcWork.left - window.clientPos.x, 0, window.clientSize.x);
            int top = std::clamp((int)mi.rcWork.top - window.clientPos.y, 0, window.clientSize.y);
            int right = std::clamp((int)mi.rcWork.right - window.clientPos.x, 0, window.clientSize.x);
            int bottom = std::clamp((int)mi.rcWork.bottom - window.clientPos.y, 0, window.clientSize.y);
            window.visiblePos = {left, top};
            window.visibleSize = {right - left, bottom - top};
        }

        void getWindowState() {
            RECT clientRect; GetClientRect(winData.hWin, &clientRect);
            POINT topLeft = {0, 0}; ClientToScreen(winData.hWin, &topLeft);
            window.clientPos = {topLeft.x, topLeft.y};
            window.clientSize = {clientRect.right, clientRect.bottom};
            if (window.fullscreen) return;
            RECT winRect; GetWindowRect(winData.hWin, &winRect);
            window.pos = {winRect.left, winRect.top};
            window.size = {winRect.right - winRect.left, winRect.bottom - winRect.top};
        }

        void updateFullscreen(bool toggle = false) {
            if (!key(_F11, TAP) && !toggle) return;
            if (!toggle) window.fullscreen = !window.fullscreen;
            LONG style = GetWindowLong(winData.hWin, GWL_STYLE);
            ICoord& pos = window.fullscreen ? window.monitorPos : window.pos;
            ICoord& size = window.fullscreen ? window.monitorSize : window.size;
            SetWindowLong(winData.hWin, GWL_STYLE, window.fullscreen ? (style & ~WS_OVERLAPPEDWINDOW) : (style | WS_OVERLAPPEDWINDOW));
            SetWindowPos(winData.hWin, HWND_NOTOPMOST, pos.x, pos.y, size.x, size.y, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }

        void setupKeyTable() {
            const UINT vkCodes[_KEYCODE_COUNT] = {
                0,0,0,0,VK_LBUTTON,VK_MBUTTON,VK_RBUTTON,VK_XBUTTON1,VK_XBUTTON2,
                VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,
                '0','1','2','3','4','5','6','7','8','9',
                'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
                VK_SPACE,VK_ESCAPE,VK_LEFT,VK_UP,VK_RIGHT,VK_DOWN,VK_LCONTROL,VK_RCONTROL,VK_CONTROL,VK_LSHIFT,VK_RSHIFT,VK_SHIFT,VK_LMENU,VK_RMENU,VK_MENU
            };
            for (uint8_t i = 4; i < _KEYCODE_COUNT; i++) { input.OSkeymap[vkCodes[i]] = i; input.keyStrength[i] = 1; }
        }

        LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            static UINT_PTR sizingTimer = 1;
            switch (msg) {
            case WM_LBUTTONDOWN: SetCapture(hwnd); setKeyState(input.OSkeymap[VK_LBUTTON], true); break;
            case WM_RBUTTONDOWN: SetCapture(hwnd); setKeyState(input.OSkeymap[VK_RBUTTON], true); break;
            case WM_MBUTTONDOWN: SetCapture(hwnd); setKeyState(input.OSkeymap[VK_MBUTTON], true); break;
            case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
                setKeyState(input.OSkeymap[msg == WM_LBUTTONUP ? VK_LBUTTON : msg == WM_RBUTTONUP ? VK_RBUTTON : VK_MBUTTON], false);
                if (!(input.keyState[_LMouse] & HOLD) && !(input.keyState[_RMouse] & HOLD) && !(input.keyState[_MMouse] & HOLD)) ReleaseCapture(); break;
            case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
                setKeyState(input.OSkeymap[wParam], msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN); break;
            case WM_INPUT: {
                POINT p; GetCursorPos(&p);
                ScreenToClient(winData.hWin, &p);
                int dx = p.x - input.cursor.x, maxX = window.visiblePos.x + window.visibleSize.x - 1;
                int dy = p.y - input.cursor.y, maxY = window.visiblePos.y + window.visibleSize.y - 1;
                int wrapX = input.cursorLocked * (maxX - window.visiblePos.x - 1) * (dx != 0);
                int wrapY = input.cursorLocked * (maxY - window.visiblePos.y - 1) * (dy != 0);
                p.x += wrapX * (dx < 0 ? (p.x + dx * 2 < window.visiblePos.x && p.x <= window.visiblePos.x + 1) : -(p.x + dx * 2 > maxX && p.x >= maxX - 1));
                p.y += wrapY * (dy < 0 ? (p.y + dy * 2 < window.visiblePos.y && p.y <= window.visiblePos.y + 1) : -(p.y + dy * 2 > maxY && p.y >= maxY - 1));
                if (p.x != input.cursor.x + dx || p.y != input.cursor.y + dy) {
                    POINT screenP = p;
                    ClientToScreen(winData.hWin, &screenP);
                    SetCursorPos(screenP.x, screenP.y);
                }
                input.keyStrength[_HCursor] += dx;
                input.keyStrength[_VCursor] += dy;
                setKeyState(_HCursor, input.keyStrength[_HCursor] != 0, input.keyStrength[_HCursor]);
                setKeyState(_VCursor, input.keyStrength[_VCursor] != 0, input.keyStrength[_VCursor]);
                input.cursor = {p.x, p.y};
            } break;
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
                int code = msg == WM_MOUSEWHEEL ? _VWheel : _HWheel;
                input.keyStrength[code] += GET_WHEEL_DELTA_WPARAM(wParam);
                setKeyState(code, input.keyStrength[code] != 0, input.keyStrength[code]);
            } break;
            case WM_ACTIVATE: window.focused = LOWORD(wParam) != WA_INACTIVE; return 0;
            case WM_MOVE: case WM_SIZE:
                getWindowState(); updateMonitorFromWindowCenter();
                if (msg == WM_SIZE) { window.minimized = wParam == SIZE_MINIMIZED; if (!window.minimized) resizeBitBlt(); if (!window.fullscreen) window.maximized = wParam == SIZE_MAXIMIZED; }
                return 0;
            case WM_TIMER: if (wParam == sizingTimer) Program::Draw::drawFrame(); break;
            case WM_ENTERSIZEMOVE: SetTimer(hwnd, sizingTimer, 0, NULL); break;
            case WM_EXITSIZEMOVE: KillTimer(hwnd, sizingTimer); break;
            case WM_CLOSE: appRunning = false; return 0;
            case WM_ERASEBKGND: return 1;
            case WM_DESTROY: PostQuitMessage(0); return 0;
            default: return DefWindowProc(hwnd, msg, wParam, lParam);
            } return 0;
        }

        void shutdownWindows() {
            if (bitBltData.hdc) { ReleaseDC(winData.hWin, bitBltData.hdc); bitBltData.hdc = 0; }
            if (winData.hWin) { DestroyWindow(winData.hWin); winData.hWin = 0; }
        }

        void updateWindows() {
            MSG msg;
            input.cursorLocked = input.lockCursorWhenFocused && window.focused;
            updateFullscreen();
            if (key(_Escape, TAP)) input.lockCursorWhenFocused = !input.lockCursorWhenFocused;
            input.keyState[_HWheel] = input.keyState[_VWheel] = 0;
            input.keyStrength[_HWheel] = input.keyStrength[_VWheel] = 0;
            input.keyState[_HCursor] = input.keyState[_VCursor] = 0;
            input.keyStrength[_HCursor] = input.keyStrength[_VCursor] = 0;
            for (int i = 0; i < _KEYCODE_COUNT; i++) input.keyState[i] &= 0xF;
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) appRunning = false;
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
        }

        bool setupWindows() {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            winData.hInstance = GetModuleHandle(0);
            WNDCLASS wc = {};
            wc.lpfnWndProc = windowProc;
            wc.hInstance = winData.hInstance;
            wc.lpszClassName = L"LineboundWindowClass";
            wc.hCursor = LoadCursor(0, IDC_ARROW);
            wc.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH));
            if (!RegisterClass(&wc)) return false;
            updateMonitorFromWindowCenter();
            window.size.x = std::min(window.size.x, window.workspaceSize.x);
            window.size.y = std::min(window.size.y, window.workspaceSize.y);
            window.pos.x = std::clamp(window.pos.x, window.workspacePos.x, window.workspacePos.x + window.workspaceSize.x - window.size.x);
            window.pos.y = std::clamp(window.pos.y, window.workspacePos.y, window.workspacePos.y + window.workspaceSize.y - window.size.y);
            winData.hWin = CreateWindowEx(0, wc.lpszClassName, L"Linebound", WS_OVERLAPPEDWINDOW, window.pos.x, window.pos.y, window.size.x, window.size.y, 0, 0, winData.hInstance, 0);
            if (!winData.hWin) return false;
            ShowWindow(winData.hWin, window.maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
            if (window.fullscreen) updateFullscreen(true);
            getWindowState(); updateMonitorFromWindowCenter(); setupKeyTable();
            RAWINPUTDEVICE rid = {0x01, 0x02, RIDEV_INPUTSINK, winData.hWin};
            RegisterRawInputDevices(&rid, 1, sizeof(rid));
            fPtr_SystemUpdate = updateWindows;
            fPtr_SystemShutdown = shutdownWindows;
            return true;
        }
    }
}

int key(int hotkey, int action) {
    uint8_t& state = System::input.keyState[hotkey];
    if (action & (TAP | HOLD)) state |= (state & TAP) << 2;
    if (action & (DOUBLETAP | DOUBLETAP_HOLD)) state |= (state & DOUBLETAP) << 2;
    int value = !!(state & action) * System::input.keyStrength[hotkey];
    state &= ~((TAP | DOUBLETAP) & action);
    return value;
}

namespace Program {
    struct Vec2 { float x, y; };
    
    enum Object {
        AIR,
        BULB,
        CRATE,
        END,
        FIREFLY,
        LAVA,
        PLAYER,
        TORCH,
        UMBRA,
        WALL,
        YOKAI,
        OBJECT_COUNT
    };

    struct Wall {
        Vec2 a, b;
        Object object;
        int blockX, blockY;
        int faceX, faceY;
        float aoA, aoB;
    };
    
    struct Entity {
        Object object;
        Vec2 pos, dir, velocity;
        union { float rotation, angle; };
        union { float health, glowPhase; };
        union { float damageTimer, flutterPhase; };
        union { float healTimer, speed; };
        float turnTimer, attackTimer, visibility;
        float anger, unseenTimer;
        bool angry;
    };

    struct Surface {
        Vec2 pos, normal;
        float u;
        Object object;
        int wall;
    };

    struct Light {
        Vec2 pos, dir;
        uint32_t color;
        float strength, radius, base, tint, cone;
    };

    struct LightValue {
        float base, r, g, b, direct;
    };
    
    const int WORLD_W = 64;
    const int WORLD_H = 64;
    const float PI = 3.14159265358979323846f;
    const float FOV = PI * 0.5f;
    const float MAX_DISTANCE = 1024.0f;
    const float NEAR_PLANE = 0.02f;

    float horizontalFOV() {
        float fullWidth = (float)std::max(1, System::window.monitorSize.x);
        float width = (float)std::max(1, System::window.clientSize.x);
        return 2.0f * atanf(tanf(FOV * 0.5f) * width / fullWidth);
    }
    
    Object world[WORLD_H][WORLD_W];
    Entity player = {PLAYER, {32.5f, 32.5f}, {}, {}, 0.0f, 1.0f, 1000.0f, 1000.0f, 0.0f, 1000.0f, 1.0f, 0.0f, 0.0f, false};
    Vec2 playerSpawn = {32.5f, 32.5f};
    std::vector<Wall> walls;
    std::vector<Entity> entities;
    int heldEntity = -1;
    std::vector<Surface> surfaceBuffer;
    std::vector<Light> lights;
    std::vector<LightValue> lightBuffer;

    uint32_t hash32(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352d;
        x ^= x >> 15;
        x *= 0x846ca68b;
        x ^= x >> 16;
        return x;
    }
    
    float random01(int x, int y, int seed) {
        return float(hash32(uint32_t(x) * 0x9E3779B9u ^ uint32_t(y) * 0x85EBCA6Bu ^ uint32_t(seed))) / 4294967295.0f;
    }
    
    bool inWorld(int x, int y) {
        return x >= 0 && y >= 0 && x < WORLD_W && y < WORLD_H;
    }
    
    uint32_t shadeColor(uint32_t color, float brightness) {
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        uint32_t b = uint32_t((color & 255) * brightness);
        uint32_t g = uint32_t(((color >> 8) & 255) * brightness);
        uint32_t r = uint32_t(((color >> 16) & 255) * brightness);
        return b | (g << 8) | (r << 16);
    }

    uint32_t addLightColor(uint32_t color, uint32_t lightColor, float amount) {
        amount = std::max(0.0f, amount);
        uint32_t b = std::min(255u, (color & 255) + uint32_t((lightColor & 255) * amount));
        uint32_t g = std::min(255u, ((color >> 8) & 255) + uint32_t(((lightColor >> 8) & 255) * amount));
        uint32_t r = std::min(255u, ((color >> 16) & 255) + uint32_t(((lightColor >> 16) & 255) * amount));
        return b | (g << 8) | (r << 16);
    }
    
    uint32_t blendColor(uint32_t a, uint32_t b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        int ab = a & 255, ag = (a >> 8) & 255, ar = (a >> 16) & 255;
        int bb = b & 255, bg = (b >> 8) & 255, br = (b >> 16) & 255;
        uint32_t rb = uint32_t(ab + (bb - ab) * t);
        uint32_t rg = uint32_t(ag + (bg - ag) * t);
        uint32_t rr = uint32_t(ar + (br - ar) * t);
        return rb | (rg << 8) | (rr << 16);
    }
    
    constexpr uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b) { return uint32_t(b) | (uint32_t(g) << 8) | (uint32_t(r) << 16); }

    enum CollisionShape {
        COLLISION_NONE,
        COLLISION_CIRCLE,
        COLLISION_BOX
    };

    enum TextureEffect {
        TEXTURE_STATIC,
        TEXTURE_NOISE,
        TEXTURE_FACE
    };

    struct ObjectData {
        char mapID;
        float size;
        CollisionShape collision;
        TextureEffect textureEffect;
        uint8_t faceTexture[4];
        uint32_t lightColor;
        float lightStrength, lightRadius, lightBase, lightTint, lightCone;
        bool solid, lit, blocksLight, pushable, pickup, render, lethal, randomRotation, entity;
    };

    uint32_t textureColor(uint32_t color) { return color ? makeColor((color >> 16) & 255, (color >> 8) & 255, color & 255) | 0xFF000000u : 0; }

    std::array<uint32_t,16> flatTexture(uint32_t color) {
        std::array<uint32_t,16> texture;
        texture.fill(textureColor(color));
        return texture;
    }

    template<typename... T> std::array<uint32_t,16> makeTexture(T... pixels) {
        std::array<uint32_t,16> texture = {};
        uint32_t values[] = {(uint32_t)pixels...};
        for (int i = 0; i < (int)sizeof...(pixels) && i < 16; i++) texture[i] = textureColor(values[i]);
        return texture;
    }

    const std::array<uint32_t,16> objectTextures[OBJECT_COUNT] = {{},
        makeTexture(0x606060,0xFFDC28,0xFFDC28,0x606060),
        makeTexture(0x69411E,0x87582A,0x87582A,0x87582A,0x69411E,0xC49A5A,0xC49A5A,0xC49A5A,0x69411E,0x87582A,0x87582A,0x87582A,0x69411E),
        flatTexture(0xFFFFFF),
        makeTexture(0x50FF64),
        makeTexture(0x6E0800,0xDC1C00,0xFF5000,0xFFBE00,0xFF3700,0xAA0C00,0xFF7800,0xFFE114,0xD21800,0xFF4600,0xFFA000,0x910A00,0xFF6900,0xF52D00,0xFFCD00,0xBE1200),
        flatTexture(0xEBEBEB),
        makeTexture(0x606060,0x606060,0x606060,0x606060,0x606060,0xFFFFFF,0xFFFFFF,0x606060),
        flatTexture(0x080808),
        makeTexture(0x463C18,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x8C782D,0x463C18),
        makeTexture(0x000000,0x000000,0xFFFFFF,0xFFFFFF,0xFFFFFF,0x000000,0x000000,0xFFFFFF,0xFFFFFF,0xFFFFFF,0x000000,0x000000)
    };
    
    const ObjectData objectData[OBJECT_COUNT] = {
        {' ', 0.0f, COLLISION_NONE, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false, false, false, false, false, false, false},
        {'B', 0.25f, COLLISION_NONE, TEXTURE_STATIC, {0,0,0,0}, makeColor(255,140,55), 4.0f, 20.0f, 0.35f, 0.55f, 0.0f, false, false, false, false, true, true, false, true, true},
        {'C', 0.8f, COLLISION_BOX, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, true, true, false, true, false, false, true},
        {'E', 1.0f, COLLISION_NONE, TEXTURE_STATIC, {0,0,0,0}, makeColor(120,255,255), 0.65f, 5.0f, 0.0f, 0.22f, 0.0f, false, false, false, false, false, true, false, false, true},
        {'F', 0.0625f, COLLISION_CIRCLE, TEXTURE_STATIC, {0,0,0,0}, makeColor(208,255,216), 1.0f, 4.5f, 0.0f, 0.18f, 0.0f, false, false, false, false, false, true, false, true, true},
        {'L', 1.0f, COLLISION_NONE, TEXTURE_NOISE, {0,0,0,0}, makeColor(255,40,0), 1.8f, 15.0f, 0.45f, 1.0f, 0.0f, false, true, false, false, false, false, true, false, false},
        {'P', 0.36, COLLISION_CIRCLE, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false, true, false, false, false, false, false, true},
        {'T', 0.25f, COLLISION_NONE, TEXTURE_FACE, {0,1,0,0}, makeColor(255,255,255), 4.0f, 50.0f, 1.0f, 0.0f, PI * 0.22f, false, false, false, false, true, true, false, true, true},
        {'U', 0.875f, COLLISION_CIRCLE, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false, true, false, false, false, true, false, true},
        {'W', 1.0f, COLLISION_BOX, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, true, false, false, false, false, false, false},
        {'Y', 0.75f, COLLISION_CIRCLE, TEXTURE_STATIC, {0,0,0,0}, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false, false, false, true, true, false, true}
    };

    bool solid(int x, int y) {
        return !inWorld(x, y) || objectData[world[y][x]].solid;
    }

    Entity makeEntity(Object object, Vec2 pos) {
        const ObjectData& data = objectData[object];
        return {object, pos, {}, {}, 0.0f, 1.0f, 1000.0f, 1000.0f, 0.0f, 1000.0f, 1.0f, 0.0f, 0.0f, false};
    }

    uint32_t objectTexture(Object object, float u, int face = -1) {
        if (object == AIR) return 0;
        int resolution = std::clamp((int)roundf(objectData[object].size * 16.0f), 1, 16);
        int pixel = std::clamp((int)floorf(u * resolution), 0, resolution - 1);
        if (objectData[object].textureEffect == TEXTURE_FACE && face >= 0) return objectTextures[object][std::clamp((int)objectData[object].faceTexture[face] * resolution + pixel, 0, 15)];
        return objectTextures[object][pixel];
    }

    const char* WORLD_MAP[WORLD_H] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "W                                                              W",
        "W   WWWWWWWWWWW                                                W",
        "W   W         W                                                W",
        "W   W    T    W              WWWWWWWWWWWWWWW                   W",
        "W   W         W              W             W                   W",
        "W   W         W              W      T      W                   W",
        "W   WWWW WWWWWW              W             W                   W",
        "W      W W          C        WWWWWW WWWWWWWW                   W",
        "W      W W                        W W                          W",
        "W      W WWWWWWWWW                W W                          W",
        "W      W         W                W W                          W",
        "W      W    T    W                W W        WWWWWWWWWWW       W",
        "W      W         W                  W        W       E W       W",
        "W      WWWWW WWWWW                  WWWWWWWWWW    T    W       W",
        "W          W W                    W          W         W       W",
        "W          W W                    WWWWW WWWWWWWWW WWWWWW       W",
        "W          W W                        W W         W            W",
        "W          W WWWWWWWW  WWW            W W      U  W            W",
        "W          W             W            W W         W            W",
        "W          W      T      W            W W         W            W",
        "W          W             WWWWWWWWWWWWWW WWWWW     W            W",
        "W          WWWWWW WWWWWWWW                  W     W            W",
        "W      U        W W                         W     W            W",
        "W               W W       WWWWWWWWWWW       W     W            W",
        "W               W W       W         W       W                  W",
        "W     WWWWWWWWWWW WWWWWWWWW    T    W       W                  W",
        "W     W                   W         W       W                  W",
        "W     W    T              WWWWW WWWWW       W     W            W",
        "W     W                       W W           W     W            W",
        "W     WWWWWWWWWWWWWW          W W           W     W            W",
        "L                  W          W W           W     W            W",
        "W                  W          W W           W     W            W",
        "W                  W          W W           W     W            W",
        "W     WWWWWWWWWWWWWW          W W           W     W            W",
        "L     W                       W W           W     W            W",
        "L     W    T                  W WWWWWWWWWWWWW     W            W",
        "W     W                       W    C            WWWWWWWWW      W",
        "L     WWWWW WWWWWWW           W                 W       W      W",
        "L         W W                 W                 W   T   W      W",
        "L           W                 WWWWWWWWW WWWWWWWWW       W      W",
        "L           W                         W W       WWW WWWWW      W",
        "W         W WWWWWWWWWWW               W W         W W          W",
        "W         W           W               W W         W W          W",
        "W         W     T     W               W W         W W          W",
        "L         W           W               W W         W W          W",
        "W         WWWWWWW WWWWW               W WWWWWWWWWWW W          W",
        "W               W W                   W             W          W",
        "W               W W                   W             W          W",
        "W               W WWWWWWWWWWWWWWWWWWWWWW                W      W",
        "W               W                         W             W      W",
        "W               W                         W             W      W",
        "W       WWWWWWWWW WWWWWWW                 WWWWW WWWWWWWWW      W",
        "W       W               W                     W W              W",
        "W       W       T   P   W                     W W              W",
        "W       W   C           WWWWWWWWWWW  WWWWWWWW W                W",
        "W       WWWWWWWWW WWWWWWW                     W                W",
        "W               W W                           W                W",
        "W               W WWWWWWWWWWWWWWWWWWWWWWWWWWWW                 W",
        "W               W                                              W",
        "W               WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW          W",
        "W      U                                                       W",
        "W                                                              W",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
    };

    const char* WORLD_MAP2[WORLD_H] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "W                                                              W",
        "W                                                    T         W",
        "W                 E                                            W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W            WWWWWWWWWWWWWWWWWW                                W",
        "W            W                                                 W",
        "W            W  B                                              W",
        "W            W                                                 W",
        "W            W                                                 W",
        "W            W             WWWWW                               W",
        "W            W                                                 W",
        "W            W                              W                  W",
        "W            W               P              W                  W",
        "W            W                              W                  W",
        "W            W                              W                  W",
        "W            W                              W                  W",
        "W            W             WWWWW            W                  W",
        "W            W              LL              W                  W",
        "W            W                              W                  W",
        "W                                           W                  W",
        "W                                           W                  W",
        "W                               WWWWWWWWWWWWW                  W",
        "W                                                              W",
        "W                                        T                     W",
        "W                                                              W",
        "W       Y                                                      W",
        "W                                                              W",
        "W       WWWWWWWWWWWWWWWWWWWWWWWWW                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W                                                              W",
        "W    T                                                     T   W",
        "W                                                              W",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"
    };

    bool entityBlocksLight(Vec2 lightPos, Vec2 worldPos, const Entity& entity) {
        if (!objectData[entity.object].blocksLight || objectData[entity.object].size <= 0.0f) return false;
        float dx = worldPos.x - lightPos.x, dy = worldPos.y - lightPos.y;
        float length2 = dx * dx + dy * dy;
        if (length2 <= 0.000001f) return false;
        float t = ((entity.pos.x - lightPos.x) * dx + (entity.pos.y - lightPos.y) * dy) / length2;
        if (t <= 0.001f || t >= 0.999f) return false;
        float x = lightPos.x + dx * t - entity.pos.x, y = lightPos.y + dy * t - entity.pos.y;
        return x * x + y * y < objectData[entity.object].size * objectData[entity.object].size * 0.25f;
    }

    bool lightVisible(Vec2 lightPos, Vec2 worldPos, int targetBlockX, int targetBlockY) {
        float dx = worldPos.x - lightPos.x;
        float dy = worldPos.y - lightPos.y;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance < 0.0001f) return true;
        dx /= distance;
        dy /= distance;
        int x = (int)floorf(lightPos.x);
        int y = (int)floorf(lightPos.y);
        int endX = (int)floorf(worldPos.x - dx * 0.001f);
        int endY = (int)floorf(worldPos.y - dy * 0.001f);
        int stepX = dx > 0.0f ? 1 : -1;
        int stepY = dy > 0.0f ? 1 : -1;
        float deltaX = dx != 0.0f ? fabsf(1.0f / dx) : 1e30f;
        float deltaY = dy != 0.0f ? fabsf(1.0f / dy) : 1e30f;
        float sideX = dx > 0.0f ? ((float)(x + 1) - lightPos.x) * deltaX : (lightPos.x - (float)x) * deltaX;
        float sideY = dy > 0.0f ? ((float)(y + 1) - lightPos.y) * deltaY : (lightPos.y - (float)y) * deltaY;
        while (x != endX || y != endY) {
            if (sideX < sideY) {
                sideX += deltaX;
                x += stepX;
            } else {
                sideY += deltaY;
                y += stepY;
            }
            if (x == targetBlockX && y == targetBlockY) return true;
            if (!inWorld(x, y) || objectData[world[y][x]].blocksLight) return false;
        }
        for (const Entity& entity : entities) {
            float sx = entity.pos.x - lightPos.x, sy = entity.pos.y - lightPos.y;
            float tx = entity.pos.x - worldPos.x, ty = entity.pos.y - worldPos.y;
            if (sx * sx + sy * sy > objectData[entity.object].size * objectData[entity.object].size * 0.25f && tx * tx + ty * ty > objectData[entity.object].size * objectData[entity.object].size * 0.25f && entityBlocksLight(lightPos, worldPos, entity)) return false;
        }
        return true;
    }

    float pointLight(Vec2 worldPos, Vec2 normal, Vec2 lightPos, Vec2 lightDir, float strength, float radius, float cone, int targetBlockX, int targetBlockY) {
        float lx = lightPos.x - worldPos.x;
        float ly = lightPos.y - worldPos.y;
        float d2 = lx * lx + ly * ly;
        if (d2 >= radius * radius) return 0.0f;
        float d = sqrtf(std::max(d2, 0.0001f));
        lx /= d; ly /= d;
        float diffuse = std::max(0.0f, normal.x * lx + normal.y * ly);
        if (diffuse <= 0.0f) return 0.0f;
        float directional = 1.0f;
        if (cone > 0.0f) {
            float angle = -lx * lightDir.x - ly * lightDir.y;
            float outer = cosf(cone);
            float radial = std::clamp((angle - outer) / (1.0f - outer), 0.0f, 1.0f);
            if (radial <= 0.0f) return 0.0f;
            float edge = std::clamp(radial / 0.04f, 0.0f, 1.0f);
            edge = edge * edge * (3.0f - 2.0f * edge);
            float ring = expf(-powf((radial - 0.16f) / 0.055f, 2.0f));
            float center = std::clamp((radial - 0.32f) / 0.68f, 0.0f, 1.0f);
            center = center * center * (3.0f - 2.0f * center);
            directional = edge * std::clamp(0.14f + ring * 0.48f + center * 0.86f, 0.0f, 1.0f);
        }
        if (!lightVisible(lightPos, worldPos, targetBlockX, targetBlockY)) return 0.0f;
        float attenuation = cone > 0.0f ? 1.0f / (1.0f + d * 0.025f) : 1.0f / (1.0f + d2 * 0.12f);
        float cutoff = std::clamp(1.0f - d / radius, 0.0f, 1.0f);
        cutoff = cutoff * cutoff * (3.0f - 2.0f * cutoff);
        return strength * attenuation * cutoff * diffuse * directional;
    }
    
    LightValue sampleLight(Vec2 worldPos, Vec2 normal, int bx, int by) {
        LightValue value = {};
        for (const Light& light : lights) {
            float amount = pointLight(worldPos, normal, light.pos, light.dir, light.strength, light.radius, light.cone, bx, by);
            value.direct += amount;
            value.base += amount * light.base;
            value.r += float((light.color >> 16) & 255) / 255.0f * amount * light.tint;
            value.g += float((light.color >> 8) & 255) / 255.0f * amount * light.tint;
            value.b += float(light.color & 255) / 255.0f * amount * light.tint;
        }
        value.base = std::clamp(value.base, 0.0f, 1.0f);
        value.r = std::clamp(value.r, 0.0f, 1.0f);
        value.g = std::clamp(value.g, 0.0f, 1.0f);
        value.b = std::clamp(value.b, 0.0f, 1.0f);
        value.direct = std::clamp(value.direct, 0.0f, 1.0f);
        return value;
    }

    bool collidesEntity(const Entity& entity, Vec2 pos, float rotation, const Entity& other) {
        const ObjectData& a = objectData[entity.object], &b = objectData[other.object];
        if (a.collision == COLLISION_NONE || b.collision == COLLISION_NONE) return false;
        float radiusA = a.collision == COLLISION_BOX ? a.size * 0.70710678f : a.size * 0.5f;
        float radiusB = b.collision == COLLISION_BOX ? b.size * 0.70710678f : b.size * 0.5f;
        float dx = pos.x - other.pos.x, dy = pos.y - other.pos.y, combined = radiusA + radiusB;
        return dx * dx + dy * dy < combined * combined * 0.55f;
    }

    namespace Gameplay {
        void generateWorld() {
            entities.clear();
            for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < WORLD_W; x++) {
                char tile = WORLD_MAP[y][x];
                world[y][x] = AIR;
                if (tile == objectData[PLAYER].mapID) { playerSpawn = {x + 0.5f, y + 0.5f}; continue; }
                Object object = AIR;
                for (int i = 1; i < OBJECT_COUNT; i++) if (objectData[i].mapID == tile) { object = (Object)i; break; }
                if (object == AIR) continue;
                if (!objectData[object].entity) { world[y][x] = object; continue; }
                Entity entity = makeEntity(object, {x + 0.5f, y + 0.5f});
                if (objectData[object].randomRotation) entity.rotation = random01(x, y, 1700 + object) * PI * 2.0f;
                if (object == FIREFLY) {
                    entity.dir = {cosf(entity.rotation), sinf(entity.rotation)};
                    entity.glowPhase = random01(x, y, 906) * PI * 2.0f;
                    entity.flutterPhase = random01(y, x, 907) * PI * 2.0f;
                    entity.speed = 0.42f + random01(x, y, 908) * 0.48f;
                    entity.turnTimer = 0.15f + random01(y, x, 909) * 0.8f;
                    entity.attackTimer = 1000.0f;
                }
                if (object == UMBRA) entity.speed = 1.35f;
                if (object == YOKAI) { entity.attackTimer = 0.0f; entity.anger = 0.0f; entity.unseenTimer = 0.0f; }
                entities.push_back(entity);
            }
            player.pos = playerSpawn;
        }

        void addWall(Vec2 a, Vec2 b, Object object, int blockX, int blockY, int faceX, int faceY) {
            float tx = b.x - a.x, ty = b.y - a.y;
            float length = sqrtf(tx * tx + ty * ty);
            tx /= length; ty /= length;
            int ax = (int)floorf(a.x - tx * 0.01f + faceX * 0.01f), ay = (int)floorf(a.y - ty * 0.01f + faceY * 0.01f);
            int bx = (int)floorf(b.x + tx * 0.01f + faceX * 0.01f), by = (int)floorf(b.y + ty * 0.01f + faceY * 0.01f);
            float aoA = solid(ax, ay) ? 0.35f : 1.0f;
            float aoB = solid(bx, by) ? 0.35f : 1.0f;
            walls.push_back({a, b, object, blockX, blockY, faceX, faceY, aoA, aoB});
        }

        void rebuildWalls() {
            walls.clear();
            for (int y = 0; y < WORLD_H; y++) {
                for (int x = 0; x < WORLD_W; x++) {
                    Object object = world[y][x];
                    if (object == AIR) continue;
                    if (!inWorld(x - 1, y) || world[y][x - 1] != object) addWall({(float)x,(float)y + 1}, {(float)x,(float)y}, object, x, y, -1, 0);
                    if (!inWorld(x + 1, y) || world[y][x + 1] != object) addWall({(float)x + 1,(float)y}, {(float)x + 1,(float)y + 1}, object, x, y, 1, 0);
                    if (!inWorld(x, y - 1) || world[y - 1][x] != object) addWall({(float)x,(float)y}, {(float)x + 1,(float)y}, object, x, y, 0, -1);
                    if (!inWorld(x, y + 1) || world[y + 1][x] != object) addWall({(float)x + 1,(float)y + 1}, {(float)x,(float)y + 1}, object, x, y, 0, 1);
                }
            }
        }

        void generateFireflies() {
            for (int i = (int)entities.size() - 1; i >= 0; i--) if (entities[i].object == FIREFLY) entities.erase(entities.begin() + i);
        }

        void spawnFireflyGroups() {
            const int maxFireflies = 5;
            const float spawnMin = 10.0f, spawnMax = 24.0f;
            while (true) {
                int fireflyCount = 0;
                for (const Entity& entity : entities) if (entity.object == FIREFLY) fireflyCount++;
                if (fireflyCount >= maxFireflies) break;
                bool cluster = !entities.empty() && random01((int)GetTickCount64(), (int)entities.size(), 900 + (int)entities.size()) < 0.72f;
                Vec2 center = player.pos;
                if (cluster) center = entities[(int)(random01((int)GetTickCount64(), (int)player.pos.x, 901) * entities.size()) % entities.size()].pos;
                float a = random01((int)GetTickCount64(), (int)(player.pos.y * 31), 902 + (int)entities.size()) * PI * 2.0f;
                float d = cluster ? 1.5f + random01((int)(center.x * 37), (int)(center.y * 41), 903 + (int)entities.size()) * 4.5f : spawnMin + random01((int)(player.pos.x * 43), (int)(player.pos.y * 47), 904 + (int)entities.size()) * (spawnMax - spawnMin);
                Vec2 pos = {center.x + cosf(a) * d, center.y + sinf(a) * d};
                float pdx = pos.x - player.pos.x, pdy = pos.y - player.pos.y;
                int x = (int)floorf(pos.x), y = (int)floorf(pos.y);
                if (!inWorld(x, y) || solid(x, y) || pdx * pdx + pdy * pdy < spawnMin * spawnMin || pdx * pdx + pdy * pdy > spawnMax * spawnMax) { if (cluster) continue; else { center = player.pos; continue; } }
                float dir = random01(x, y, 905 + (int)entities.size()) * PI * 2.0f;
                float phase = random01(x, y, 906 + (int)entities.size()) * PI * 2.0f;
                float flutter = random01(y, x, 907 + (int)entities.size()) * PI * 2.0f;
                float speed = 0.42f + random01(x, y, 908 + (int)entities.size()) * 0.48f;
                Entity firefly = makeEntity(FIREFLY, pos); firefly.dir = {cosf(dir), sinf(dir)}; firefly.rotation = dir; firefly.turnTimer = 0.15f + random01(y, x, 909) * 0.8f; firefly.attackTimer = 1000.0f; firefly.glowPhase = phase; firefly.flutterPhase = flutter; firefly.speed = speed; entities.push_back(firefly);
            }
        }

        void angerFireflies(Vec2 pos) {
            for (Entity& firefly : entities) {
                if (firefly.object != FIREFLY) continue;
                float dx = firefly.pos.x - pos.x, dy = firefly.pos.y - pos.y;
                if (dx * dx + dy * dy < 36.0f) { firefly.angry = true; firefly.attackTimer = 0.0f; }
            }
        }

        void updateFireflies(float dt) {
            const float despawnDistance = 32.0f;
            for (int i = (int)entities.size() - 1; i >= 0; i--) {
                if (entities[i].object != FIREFLY) continue;
                float dx = entities[i].pos.x - player.pos.x, dy = entities[i].pos.y - player.pos.y;
                if (dx * dx + dy * dy > despawnDistance * despawnDistance) entities.erase(entities.begin() + i);
            }
            spawnFireflyGroups();
            player.damageTimer += dt;
            player.healTimer += dt;
            if (player.damageTimer >= 1.0f && player.healTimer >= 1.0f && player.health < 1.0f) { player.health = std::min(1.0f, player.health + 0.05f); player.healTimer -= 1.0f; }
            for (Entity& firefly : entities) {
                if (firefly.object != FIREFLY) continue;
                firefly.attackTimer += dt;
                firefly.glowPhase += dt * (1.1f + firefly.speed * 0.8f);
                firefly.flutterPhase += dt * (5.0f + firefly.speed * 3.0f);
                if (firefly.angry && firefly.attackTimer >= 10.0f) firefly.angry = false;
                float dx = player.pos.x - firefly.pos.x, dy = player.pos.y - firefly.pos.y;
                float d = sqrtf(dx * dx + dy * dy);
                if (firefly.angry && d > 0.001f) {
                    float tx = dx / d, ty = dy / d;
                    float weave = sinf(firefly.flutterPhase) * 0.38f;
                    firefly.dir.x += (tx - ty * weave - firefly.dir.x) * std::min(1.0f, dt * 4.0f);
                    firefly.dir.y += (ty + tx * weave - firefly.dir.y) * std::min(1.0f, dt * 4.0f);
                } else {
                    firefly.turnTimer -= dt;
                    float curve = sinf(firefly.flutterPhase * 0.73f) * 1.7f + sinf(firefly.flutterPhase * 1.91f) * 0.55f;
                    float cs = cosf(curve * dt), sn = sinf(curve * dt);
                    Vec2 dir = {firefly.dir.x * cs - firefly.dir.y * sn, firefly.dir.x * sn + firefly.dir.y * cs};
                    firefly.dir = dir;
                    if (firefly.turnTimer <= 0.0f) {
                        float kick = (random01((int)(firefly.pos.x * 97), (int)(firefly.pos.y * 89), (int)(firefly.flutterPhase * 10.0f)) - 0.5f) * 1.8f;
                        cs = cosf(kick); sn = sinf(kick);
                        firefly.dir = {firefly.dir.x * cs - firefly.dir.y * sn, firefly.dir.x * sn + firefly.dir.y * cs};
                        firefly.turnTimer = 0.12f + random01((int)(firefly.pos.y * 83), (int)(firefly.pos.x * 79), 810) * 0.75f;
                    }
                }
                float length = sqrtf(firefly.dir.x * firefly.dir.x + firefly.dir.y * firefly.dir.y);
                if (length > 0.001f) { firefly.dir.x /= length; firefly.dir.y /= length; }
                float speed = firefly.angry ? 2.2f : firefly.speed * (0.72f + sinf(firefly.flutterPhase * 1.37f) * 0.28f);
                Vec2 next = {firefly.pos.x + firefly.dir.x * speed * dt, firefly.pos.y + firefly.dir.y * speed * dt};
                if (!solid((int)floorf(next.x), (int)floorf(next.y))) firefly.pos = next;
                else { firefly.dir.x = -firefly.dir.x; firefly.dir.y = -firefly.dir.y; firefly.turnTimer = 0.05f; }
                dx = player.pos.x - firefly.pos.x; dy = player.pos.y - firefly.pos.y;
                if (firefly.angry && dx * dx + dy * dy < 0.20f && player.damageTimer >= 0.45f) { player.health = std::max(0.0f, player.health - 0.08f); player.damageTimer = 0.0f; player.healTimer = 0.0f; }
            }
        }

        bool entityLineOfSight(Vec2 a, Vec2 b) {
            float dx = b.x - a.x, dy = b.y - a.y, distance = sqrtf(dx * dx + dy * dy);
            if (distance < 0.001f) return true;
            int steps = std::max(1, (int)ceilf(distance * 8.0f));
            for (int i = 1; i < steps; i++) {
                float t = float(i) / float(steps);
                int x = (int)floorf(a.x + dx * t), y = (int)floorf(a.y + dy * t);
                if (solid(x, y)) return false;
            }
            return true;
        }

        void buildLights() {
            lights.clear();
            for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < WORLD_W; x++) {
                const ObjectData& data = objectData[world[y][x]];
                if (data.lightStrength > 0.0f) lights.push_back({{x + 0.5f,y + 0.5f},{},data.lightColor,data.lightStrength,data.lightRadius,data.lightBase,data.lightTint,data.lightCone});
            }
            for (const Entity& entity : entities) {
                const ObjectData& data = objectData[entity.object];
                if (data.lightStrength <= 0.0f) continue;
                uint32_t color = data.lightColor;
                float strength = data.lightStrength;
                if (entity.object == FIREFLY) {
                    strength = entity.angry ? 0.70f : 0.25f + (0.5f + 0.5f * sinf(entity.glowPhase)) * 0.75f;
                    if (entity.angry) color = makeColor(255,216,216);
                }
                Vec2 dir = {cosf(entity.rotation), sinf(entity.rotation)};
                lights.push_back({entity.pos,dir,color,strength,data.lightRadius,data.lightBase,data.lightTint,data.lightCone});
            }
        }

        float umbraLight(Vec2 pos) {
            float value = 0.0f;
            for (const Light& light : lights) {
                float dx = light.pos.x - pos.x, dy = light.pos.y - pos.y, distance = sqrtf(dx * dx + dy * dy);
                if (distance < light.radius && entityLineOfSight(pos, light.pos)) value += (1.0f - distance / light.radius) * light.strength;
            }
            return value;
        }

        float yokaiLight(Vec2 pos) {
            float value = 0.0f;
            for (const Light& light : lights) {
                float dx = pos.x - light.pos.x, dy = pos.y - light.pos.y, d2 = dx * dx + dy * dy;
                if (d2 >= light.radius * light.radius) continue;
                float d = sqrtf(std::max(d2, 0.0001f));
                if (!lightVisible(light.pos, pos, (int)floorf(pos.x), (int)floorf(pos.y))) continue;
                float directional = 1.0f;
                if (light.cone > 0.0f) {
                    float angle = dx / d * light.dir.x + dy / d * light.dir.y;
                    float outer = cosf(light.cone);
                    float radial = std::clamp((angle - outer) / (1.0f - outer), 0.0f, 1.0f);
                    if (radial <= 0.0f) continue;
                    float edge = std::clamp(radial / 0.04f, 0.0f, 1.0f);
                    edge = edge * edge * (3.0f - 2.0f * edge);
                    float ring = expf(-powf((radial - 0.16f) / 0.055f, 2.0f));
                    float center = std::clamp((radial - 0.32f) / 0.68f, 0.0f, 1.0f);
                    center = center * center * (3.0f - 2.0f * center);
                    directional = edge * std::clamp(0.14f + ring * 0.48f + center * 0.86f, 0.0f, 1.0f);
                }
                float attenuation = light.cone > 0.0f ? 1.0f / (1.0f + d * 0.025f) : 1.0f / (1.0f + d2 * 0.12f);
                float cutoff = std::clamp(1.0f - d / light.radius, 0.0f, 1.0f);
                cutoff = cutoff * cutoff * (3.0f - 2.0f * cutoff);
                value += light.strength * attenuation * cutoff * directional;
            }
            return value;
        }

        void updateUmbra(float dt) {
            for (Entity& umbra : entities) {
                if (umbra.object != UMBRA) continue;
                float dx = player.pos.x - umbra.pos.x, dy = player.pos.y - umbra.pos.y;
                float distance = sqrtf(dx * dx + dy * dy);
                bool seesPlayer = distance > 0.001f && entityLineOfSight(umbra.pos, player.pos);
                if (seesPlayer) {
                    umbra.dir = {dx / distance, dy / distance};
                    umbra.rotation = atan2f(umbra.dir.y, umbra.dir.x);
                } else {
                    umbra.turnTimer -= dt;
                    if (umbra.turnTimer <= 0.0f || umbra.dir.x == 0.0f && umbra.dir.y == 0.0f) {
                        float baseLight = umbraLight(umbra.pos);
                        for (int attempt = 0; attempt < 12; attempt++) {
                            float angle = random01((int)(umbra.pos.x * 113.0f) + attempt, (int)(umbra.pos.y * 127.0f), (int)GetTickCount64() / 250) * PI * 2.0f;
                            Vec2 dir = {cosf(angle), sinf(angle)};
                            Vec2 probe = {umbra.pos.x + dir.x * 1.5f, umbra.pos.y + dir.y * 1.5f};
                            if (!solid((int)floorf(probe.x), (int)floorf(probe.y)) && umbraLight(probe) <= baseLight + 0.01f) { umbra.dir = dir; break; }
                        }
                        umbra.rotation = atan2f(umbra.dir.y, umbra.dir.x);
                        umbra.turnTimer = 0.6f + random01((int)(umbra.pos.x * 173.0f), (int)(umbra.pos.y * 181.0f), (int)GetTickCount64() / 500) * 1.8f;
                    }
                }
                Vec2 next = {umbra.pos.x + umbra.dir.x * umbra.speed * dt, umbra.pos.y + umbra.dir.y * umbra.speed * dt};
                bool enteringMoreLight = umbraLight(next) > umbraLight(umbra.pos) + 0.01f;
                if (!solid((int)floorf(next.x), (int)floorf(next.y)) && (seesPlayer || !enteringMoreLight)) umbra.pos = next;
                else if (!seesPlayer) umbra.turnTimer = 0.0f;
            }
        }

        void orientMovingEntities() {
            for (int i = 0; i < (int)entities.size(); i++) {
                Entity& entity = entities[i];
                if (i == heldEntity || entity.object == PLAYER || objectData[entity.object].pushable) continue;
                float dx = entity.dir.x, dy = entity.dir.y;
                if (dx * dx + dy * dy > 0.000001f) entity.rotation = atan2f(dy, dx);
            }
        }

        void updateYokai(float dt) {
            const float desiredLight = 0.30f, stareTeleportTime = 3.0f;
            for (Entity& yokai : entities) {
                if (yokai.object != YOKAI) continue;
                float dx = player.pos.x - yokai.pos.x, dy = player.pos.y - yokai.pos.y, distance = sqrtf(dx * dx + dy * dy);
                if (distance <= 0.001f) continue;
                Vec2 playerDir = {dx / distance, dy / distance}, lookDir = {-playerDir.x, -playerDir.y};
                yokai.rotation = atan2f(playerDir.y, playerDir.x);
                float relativeAngle = atan2f(-playerDir.y, -playerDir.x) - player.angle;
                while (relativeAngle > PI) relativeAngle -= PI * 2.0f;
                while (relativeAngle < -PI) relativeAngle += PI * 2.0f;
                bool lookedAt = fabsf(relativeAngle) <= horizontalFOV() * 0.3f && entityLineOfSight(player.pos, yokai.pos);
                if (lookedAt) {
                    yokai.anger = std::min(1.0f, yokai.anger + dt * 0.22f); yokai.unseenTimer = 0.0f; yokai.attackTimer += dt;
                    if (yokai.attackTimer >= stareTeleportTime) {
                        float bestScore = -1e30f; Vec2 bestPos = yokai.pos;
                        int seed = (int)GetTickCount64() ^ (int)(yokai.pos.x * 131.0f) ^ (int)(yokai.pos.y * 197.0f);
                        for (int i = 0; i < 32; i++) {
                            float angle = random01(seed, i, 3100) * PI * 2.0f, range = 5.0f + random01(i, seed, 3101) * 9.0f;
                            Vec2 pos = {player.pos.x + cosf(angle) * range, player.pos.y + sinf(angle) * range};
                            int x = (int)floorf(pos.x), y = (int)floorf(pos.y);
                            if (!inWorld(x, y) || solid(x, y) || yokaiLight(pos) > desiredLight) continue;
                            float pdx = pos.x - player.pos.x, pdy = pos.y - player.pos.y;
                            float score = pdx * pdx + pdy * pdy + random01(i, seed, 3102) * 20.0f;
                            if (score > bestScore) { bestScore = score; bestPos = pos; }
                        }
                        if (bestScore > -1e29f) yokai.pos = bestPos;
                        yokai.attackTimer = -1.0f - random01(seed, (int)(yokai.anger * 1000.0f), 3103) * 2.0f;
                    }
                    if (yokai.anger <= 0.5f || yokaiLight(yokai.pos) > desiredLight) continue;
                }
                yokai.attackTimer = std::max(0.0f, yokai.attackTimer);
                yokai.unseenTimer += dt;
                if (yokai.unseenTimer > 2.5f) yokai.anger = std::max(0.0f, yokai.anger - dt * 0.10f);
                float currentLight = yokaiLight(yokai.pos), speed = 2.0f + yokai.anger * 6.0f;
                Vec2 moveDir = playerDir;
                if (currentLight > desiredLight) {
                    float bestLight = 1e30f; Vec2 bestDir = {};
                    for (int i = 0; i < 16; i++) {
                        float angle = PI * 2.0f * float(i) / 16.0f;
                        Vec2 dir = {cosf(angle), sinf(angle)}, sample = {yokai.pos.x + dir.x * 1.5f, yokai.pos.y + dir.y * 1.5f};
                        if (solid((int)floorf(sample.x), (int)floorf(sample.y))) continue;
                        float light = yokaiLight(sample);
                        if (light < bestLight) { bestLight = light; bestDir = dir; }
                    }
                    if (bestLight < 1e29f) moveDir = bestDir;
                    speed = std::max(speed, 4.0f);
                } else {
                    yokai.flutterPhase += dt * (2.2f + yokai.anger * 1.8f);
                    float sway = sinf(yokai.flutterPhase) * (0.42f + std::min(distance / 16.0f, 1.0f) * 0.28f);
                    moveDir = {playerDir.x - playerDir.y * sway, playerDir.y + playerDir.x * sway};
                    float length = sqrtf(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
                    if (length > 0.001f) { moveDir.x /= length; moveDir.y /= length; }
                }
                yokai.dir = playerDir;
                Vec2 next = {yokai.pos.x + moveDir.x * speed * dt, yokai.pos.y + moveDir.y * speed * dt};
                bool blocked = solid((int)floorf(next.x), (int)floorf(next.y));
                if (currentLight <= desiredLight && yokaiLight(next) > desiredLight) blocked = true;
                if (!blocked) for (const Entity& other : entities) if (&other != &yokai && objectData[other.object].solid && collidesEntity(yokai, next, yokai.rotation, other)) { blocked = true; break; }
                if (!blocked) yokai.pos = next;
            }
        }

        bool targetEntity(Object object, float maxDistance, float maxSide, int& index) {
            Vec2 forward = {cosf(player.angle), sinf(player.angle)}, right = {-forward.y, forward.x};
            index = -1;
            for (int i = 0; i < (int)entities.size(); i++) {
                if (entities[i].object != object || i == heldEntity) continue;
                float dx = entities[i].pos.x - player.pos.x, dy = entities[i].pos.y - player.pos.y;
                float depth = dx * forward.x + dy * forward.y, side = dx * right.x + dy * right.y;
                if (depth > NEAR_PLANE && depth < maxDistance && fabsf(side) < maxSide && entityLineOfSight(player.pos, entities[i].pos)) { maxDistance = depth; index = i; }
            }
            return index >= 0;
        }

        bool hitFirefly() {
            int index;
            if (!targetEntity(FIREFLY, 6.0f, 0.18f, index)) return false;
            angerFireflies(entities[index].pos);
            return true;
        }

        bool collidesWorld(const Entity& entity, Vec2 pos, float rotation) {
            const ObjectData& data = objectData[entity.object];
            if (data.collision == COLLISION_NONE) return false;
            if (data.collision == COLLISION_CIRCLE) {
                float radius = data.size * 0.5f;
                int minX = (int)floorf(pos.x - radius), maxX = (int)floorf(pos.x + radius), minY = (int)floorf(pos.y - radius), maxY = (int)floorf(pos.y + radius);
                for (int y = minY; y <= maxY; y++) for (int x = minX; x <= maxX; x++) {
                    if (!inWorld(x, y)) return true;
                    if (!objectData[world[y][x]].solid) continue;
                    float cx = std::clamp(pos.x, (float)x, (float)x + 1.0f), cy = std::clamp(pos.y, (float)y, (float)y + 1.0f), dx = pos.x - cx, dy = pos.y - cy;
                    if (dx * dx + dy * dy < radius * radius) return true;
                }
                return false;
            }
            float cr = cosf(rotation), sr = sinf(rotation), half = objectData[entity.object].size * 0.5f;
            Vec2 local[9] = {{-half,-half},{half,-half},{half,half},{-half,half},{0.0f,-half},{half,0.0f},{0.0f,half},{-half,0.0f},{0.0f,0.0f}};
            for (int i = 0; i < 9; i++) {
                Vec2 point = {pos.x + local[i].x * cr - local[i].y * sr, pos.y + local[i].x * sr + local[i].y * cr};
                if (solid((int)floorf(point.x), (int)floorf(point.y))) return true;
            }
            return false;
        }

        bool collidesEntity(Vec2 pos, float radius, const Entity& entity) {
            const ObjectData& data = objectData[entity.object];
            if (data.collision == COLLISION_NONE) return false;
            float dx = pos.x - entity.pos.x, dy = pos.y - entity.pos.y;
            if (data.collision == COLLISION_BOX) {
                float cr = cosf(entity.rotation), sr = sinf(entity.rotation), lx = dx * cr + dy * sr, ly = -dx * sr + dy * cr, half = data.size * 0.5f;
                dx = lx - std::clamp(lx, -half, half); dy = ly - std::clamp(ly, -half, half);
                return dx * dx + dy * dy < radius * radius;
            }
            float combined = radius + data.size * 0.5f;
            return dx * dx + dy * dy < combined * combined;
        }

        bool movePushable(Entity& crate, Vec2 push, float dt) {
            float length = sqrtf(push.x * push.x + push.y * push.y);
            if (length < 0.0001f) return false;
            push.x /= length; push.y /= length;
            Vec2 next = {crate.pos.x + push.x * dt * 0.75f, crate.pos.y + push.y * dt * 0.75f};
            float torque = (push.x * (player.pos.y - crate.pos.y) - push.y * (player.pos.x - crate.pos.x)) * dt * 0.9f;
            float nextRotation = crate.rotation + torque;
            if (collidesWorld(crate, next, nextRotation)) return false;
            for (const Entity& other : entities) if (&other != &crate && objectData[other.object].solid && collidesEntity(crate, next, nextRotation, other)) return false;
            crate.pos = next;
            crate.rotation = nextRotation;
            return true;
        }

        bool playerCollides(Vec2 pos) {
            Entity playerEntity = makeEntity(PLAYER, pos);
            if (collidesWorld(playerEntity, pos, 0.0f)) return true;
            for (const Entity& entity : entities) if (objectData[entity.object].solid && collidesEntity(pos, objectData[PLAYER].size * 0.5f, entity)) return true;
            return false;
        }
        
        void movePlayerAxis(bool xAxis, float dt) {
            float& velocity = xAxis ? player.velocity.x : player.velocity.y;
            Vec2 next = player.pos;
            if (xAxis) next.x += velocity * dt; else next.y += velocity * dt;
            if (!playerCollides(next)) { if (xAxis) player.pos.x = next.x; else player.pos.y = next.y; return; }
            bool pushed = false;
            for (Entity& entity : entities) if (objectData[entity.object].pushable && collidesEntity(next, objectData[PLAYER].size * 0.5f, entity)) { pushed = movePushable(entity, xAxis ? Vec2{velocity,0.0f} : Vec2{0.0f,velocity}, dt); break; }
            if (pushed && !playerCollides(next)) { if (xAxis) player.pos.x = next.x; else player.pos.y = next.y; } else velocity = 0.0f;
        }
        
        void respawnPlayer() {
            player.pos = playerSpawn; player.velocity = {}; player.health = 1.0f; player.damageTimer = 1000.0f; player.healTimer = 1000.0f;
        }
        
        void updatePlayerHazards() {
            int x = (int)floorf(player.pos.x), y = (int)floorf(player.pos.y);
            if (inWorld(x, y) && objectData[world[y][x]].lethal) { respawnPlayer(); return; }
            for (const Entity& entity : entities) if (objectData[entity.object].lethal) {
                float dx = player.pos.x - entity.pos.x, dy = player.pos.y - entity.pos.y, radius = objectData[PLAYER].size * 0.5f + objectData[entity.object].size * 0.5f;
                if (dx * dx + dy * dy < radius * radius) { respawnPlayer(); return; }
            }
            if (player.health <= 0.0f) respawnPlayer();
        }
        
        void updatePlayer(float dt) {
            if (!System::window.focused || !System::input.cursorLocked) return;
            float moveSpeed = 3.0f, acceleration = 18.0f, deceleration = 24.0f;
            int dx = key(_HCursor, HOLD);
            if (dx) player.angle += float(dx) * 0.0025f;
            Vec2 forward = {cosf(player.angle), sinf(player.angle)};
            Vec2 right = {-forward.y, forward.x};
            Vec2 movement = {};
            if (key(_W, HOLD)) { movement.x += forward.x; movement.y += forward.y; }
            if (key(_S, HOLD)) { movement.x -= forward.x; movement.y -= forward.y; }
            if (key(_D, HOLD)) { movement.x += right.x; movement.y += right.y; }
            if (key(_A, HOLD)) { movement.x -= right.x; movement.y -= right.y; }
            float length = sqrtf(movement.x * movement.x + movement.y * movement.y);
            Vec2 target = {};
            if (length > 0.0f) { target.x = movement.x / length * moveSpeed; target.y = movement.y / length * moveSpeed; }
            float rate = length > 0.0f ? acceleration : deceleration;
            float change = std::clamp(rate * dt, 0.0f, 1.0f);
            player.velocity.x += (target.x - player.velocity.x) * change;
            player.velocity.y += (target.y - player.velocity.y) * change;
            if (length == 0.0f && player.velocity.x * player.velocity.x + player.velocity.y * player.velocity.y < 0.0001f) player.velocity = {};
            movePlayerAxis(true, dt);
            movePlayerAxis(false, dt);
            for (const Entity& entity : entities) if (entity.object == END) {
                float dx = player.pos.x - entity.pos.x, dy = player.pos.y - entity.pos.y;
                if (fabsf(dx) < objectData[entity.object].size * 0.5f && fabsf(dy) < objectData[entity.object].size * 0.5f) { player.pos = playerSpawn; player.velocity = {}; break; }
            }
        }

        bool targetWall(int& index) {
            if (System::window.clientSize.x <= 1 || surfaceBuffer.empty()) return false;
            int center = (System::window.clientSize.x * 9 / 10) / 2;
            index = surfaceBuffer[center].wall;
            if (index < 0 || index >= (int)walls.size()) return false;
            return System::depthBuffer[center] <= 6.0f;
        }

        bool targetPickup(int& index) {
            Vec2 forward = {cosf(player.angle), sinf(player.angle)}, right = {-forward.y, forward.x};
            float maxDistance = 3.0f;
            index = -1;
            for (int i = 0; i < (int)entities.size(); i++) {
                if (!objectData[entities[i].object].pickup || i == heldEntity) continue;
                float dx = entities[i].pos.x - player.pos.x, dy = entities[i].pos.y - player.pos.y;
                float depth = dx * forward.x + dy * forward.y, side = dx * right.x + dy * right.y;
                if (depth > NEAR_PLANE && depth < maxDistance && fabsf(side) < 0.22f && entityLineOfSight(player.pos, entities[i].pos)) { maxDistance = depth; index = i; }
            }
            return index >= 0;
        }

        void updateHeldEntity(float dt) {
            for (int i = 0; i < (int)entities.size(); i++) {
                Entity& entity = entities[i];
                if (!objectData[entity.object].pickup) continue;
                if (i == heldEntity) {
                    entity.visibility = std::max(0.0f, entity.visibility - dt * 7.0f);
                    if (entity.visibility <= 0.0f) { entity.pos = player.pos; entity.rotation = player.angle; }
                } else entity.visibility = std::min(1.0f, entity.visibility + dt * 7.0f);
            }
        }

        void toggleHeldEntity() {
            if (heldEntity >= 0 && heldEntity < (int)entities.size()) {
                Entity& entity = entities[heldEntity];
                Vec2 forward = {cosf(player.angle), sinf(player.angle)};
                Vec2 drop = {player.pos.x + forward.x * 0.75f, player.pos.y + forward.y * 0.75f};
                if (!solid((int)floorf(drop.x), (int)floorf(drop.y))) entity.pos = drop;
                else entity.pos = player.pos;
                entity.rotation = player.angle;
                heldEntity = -1;
                return;
            }
            int index;
            if (!targetPickup(index)) return;
            heldEntity = index;
        }

        void updateInput() {
            if (!System::window.focused) return;
            if (key(_LMouse, TAP)) hitFirefly();
            if (key(_RMouse, TAP)) toggleHeldEntity();
        }
    }

    namespace Draw {
        struct ProjectedLine {
            float xa, xb, iza, izb, uza, uzb;
            int x0, x1;
        };

        float sampleAmbientOcclusion(Vec2 worldPos, const Wall& wall) {
            float dx = wall.b.x - wall.a.x, dy = wall.b.y - wall.a.y, length2 = dx * dx + dy * dy;
            float u = length2 > 0.0f ? std::clamp(((worldPos.x - wall.a.x) * dx + (worldPos.y - wall.a.y) * dy) / length2, 0.0f, 1.0f) : 0.0f;
            return wall.aoA + (wall.aoB - wall.aoA) * u;
        }

        bool projectLine(Vec2 wa, Vec2 wb, ProjectedLine& line) {
            float ca = cosf(player.angle), sa = sinf(player.angle), tanHalfFOV = tanf(horizontalFOV() * 0.5f);
            Vec2 a = {wa.x * ca + wa.y * sa, -wa.x * sa + wa.y * ca}, b = {wb.x * ca + wb.y * sa, -wb.x * sa + wb.y * ca};
            if (a.x < NEAR_PLANE && b.x < NEAR_PLANE) return false;
            float ua = 0.0f, ub = 1.0f;
            if (a.x < NEAR_PLANE) { float t = (NEAR_PLANE - a.x) / (b.x - a.x); a.y += (b.y - a.y) * t; ua += (ub - ua) * t; a.x = NEAR_PLANE; }
            if (b.x < NEAR_PLANE) { float t = (NEAR_PLANE - b.x) / (a.x - b.x); b.y += (a.y - b.y) * t; ub += (ua - ub) * t; b.x = NEAR_PLANE; }
            float pa = a.y / (a.x * tanHalfFOV), pb = b.y / (b.x * tanHalfFOV);
            if ((pa < -1.0f && pb < -1.0f) || (pa > 1.0f && pb > 1.0f)) return false;
            int width = System::window.clientSize.x;
            line.xa = (pa * 0.5f + 0.5f) * float(width - 1); line.xb = (pb * 0.5f + 0.5f) * float(width - 1);
            line.iza = 1.0f / a.x; line.izb = 1.0f / b.x; line.uza = ua * line.iza; line.uzb = ub * line.izb;
            if (line.xa > line.xb) { std::swap(line.xa, line.xb); std::swap(line.iza, line.izb); std::swap(line.uza, line.uzb); }
            line.x0 = std::max(0, (int)ceilf(line.xa)); line.x1 = std::min(width - 1, (int)floorf(line.xb));
            return line.x0 <= line.x1;
        }

        bool sampleLine(const ProjectedLine& line, int x, float& depth, float& u) {
            float span = line.xb - line.xa, t = span > 0.000001f ? (float(x) - line.xa) / span : 0.0f;
            float iz = line.iza + (line.izb - line.iza) * t;
            if (iz <= 0.0f) return false;
            depth = 1.0f / iz;
            if (depth >= System::depthBuffer[x] || depth > MAX_DISTANCE) return false;
            u = std::clamp((line.uza + (line.uzb - line.uza) * t) / iz, 0.0f, 1.0f);
            return true;
        }

        uint32_t applyLighting(uint32_t material, Vec2 worldPos, Vec2 normal, LightValue light) {
            uint32_t color = shadeColor(material, std::clamp(0.08f + light.base, 0.0f, 1.0f));
            color = addLightColor(color, makeColor(255,0,0), light.r);
            color = addLightColor(color, makeColor(0,255,0), light.g);
            color = addLightColor(color, makeColor(0,0,255), light.b);
            float viewX = player.pos.x - worldPos.x, viewY = player.pos.y - worldPos.y, length = sqrtf(viewX * viewX + viewY * viewY);
            if (length > 0.0001f) { viewX /= length; viewY /= length; }
            float facing = std::clamp(normal.x * viewX + normal.y * viewY, 0.0f, 1.0f);
            return blendColor(color, 0x00000000, (0.30f + (1.0f - facing) * 0.65f) * (1.0f - light.direct));
        }

        uint32_t blurPixel(const std::vector<uint32_t>& source, int x, int radius) {
            int width = (int)source.size(), r = 0, g = 0, b = 0, count = 0;
            for (int i = std::max(0, x - radius); i <= std::min(width - 1, x + radius); i++) { b += source[i] & 255; g += (source[i] >> 8) & 255; r += (source[i] >> 16) & 255; count++; }
            return makeColor(r / count, g / count, b / count);
        }

        void applyCelShading() {
            int width = System::window.clientSize.x;
            for (int x = 1; x < width; x++) {
                float a = System::depthBuffer[x - 1], b = System::depthBuffer[x];
                if (a >= MAX_DISTANCE && b >= MAX_DISTANCE) continue;
                float nearDepth = std::min(a, b), difference = fabsf(a - b), threshold = std::max(0.12f, nearDepth * 0.03f);
                if (difference > threshold || a >= MAX_DISTANCE || b >= MAX_DISTANCE) {
                    int side = a < b ? x - 1 : x;
                    for (int j = -2; j <= 2; j++) { int ex = side + j; if (ex >= 0 && ex < width && System::depthBuffer[ex] <= nearDepth + 0.08f) System::framebuffer[ex] = 0; }
                } else if (surfaceBuffer[x - 1].normal.x * surfaceBuffer[x].normal.x + surfaceBuffer[x - 1].normal.y * surfaceBuffer[x].normal.y <= 0.5f) {
                    int side = a <= b ? x - 1 : x;
                    for (int j = -1; j <= 1; j++) { int ex = side + j; if (ex >= 0 && ex < width && System::depthBuffer[ex] <= nearDepth + threshold) System::framebuffer[ex] = blendColor(System::framebuffer[ex], 0x00FFFFFF, 0.32f); }
                }
            }
        }

        void rasterizeEntity(const Entity& entity, Object object, uint32_t color = 0) {
            if (!objectData[object].render) return;
            float cr = cosf(entity.rotation), sr = sinf(entity.rotation), half = objectData[entity.object].size * 0.5f;
            Vec2 local[4] = {{-half,-half},{half,-half},{half,half},{-half,half}}, corner[4];
            for (int i = 0; i < 4; i++) corner[i] = {entity.pos.x + local[i].x * cr - local[i].y * sr, entity.pos.y + local[i].x * sr + local[i].y * cr};
            for (int face = 0; face < 4; face++) {
                Vec2 edge = {corner[(face + 1) & 3].x - corner[face].x, corner[(face + 1) & 3].y - corner[face].y}, normal = {edge.y, -edge.x};
                float length = sqrtf(normal.x * normal.x + normal.y * normal.y);
                if (length > 0.0001f) { normal.x /= length; normal.y /= length; }
                Vec2 toPlayer = {player.pos.x - corner[face].x, player.pos.y - corner[face].y};
                if (normal.x * toPlayer.x + normal.y * toPlayer.y <= 0.0f) continue;
                ProjectedLine line;
                if (!projectLine({corner[face].x - player.pos.x, corner[face].y - player.pos.y}, {corner[(face + 1) & 3].x - player.pos.x, corner[(face + 1) & 3].y - player.pos.y}, line)) continue;
                for (int x = line.x0; x <= line.x1; x++) {
                    float depth, u;
                    if (!sampleLine(line, x, depth, u)) continue;
                    Vec2 worldPos = {corner[face].x + edge.x * u, corner[face].y + edge.y * u};
                    if (object == YOKAI && (face != 1 || !((u > 0.27f && u < 0.38f) || (u > 0.62f && u < 0.73f)))) continue;
                    uint32_t material = color ? color : objectTexture(object, u, face);
                    material = blendColor(material, makeColor(255,40,40), fabsf(normal.x) * 0.06f);
                    float faceLight = 0.72f + 0.28f * std::clamp(normal.x * 0.5f + normal.y * -0.5f + 0.5f, 0.0f, 1.0f);
                    material = shadeColor(material, faceLight);
                    if (entity.visibility < 1.0f && random01(x, (int)(worldPos.x * 4096.0f) ^ (int)(worldPos.y * 4096.0f), 2401) > entity.visibility) continue;
                    System::framebuffer[x] = material; System::depthBuffer[x] = depth; surfaceBuffer[x] = {worldPos, normal, u, object, -1};
                }
            }
        }

        void rasterizeWalls() {
            int width = System::window.clientSize.x;
            if ((int)System::depthBuffer.size() != width) System::depthBuffer.resize(width);
            if ((int)surfaceBuffer.size() != width) surfaceBuffer.resize(width);
            for (int x = 0; x < width; x++) { System::framebuffer[x] = 0; System::depthBuffer[x] = MAX_DISTANCE; surfaceBuffer[x] = {{}, {}, 0.0f, AIR, -1}; }
            for (int i = 0; i < (int)walls.size(); i++) {
                const Wall& wall = walls[i];
                ProjectedLine line;
                if (!projectLine({wall.a.x - player.pos.x, wall.a.y - player.pos.y}, {wall.b.x - player.pos.x, wall.b.y - player.pos.y}, line)) continue;
                for (int x = line.x0; x <= line.x1; x++) {
                    float depth, u;
                    if (!sampleLine(line, x, depth, u)) continue;
                    Vec2 pos = {wall.a.x + (wall.b.x - wall.a.x) * u, wall.a.y + (wall.b.y - wall.a.y) * u}, normal = {(float)wall.faceX,(float)wall.faceY};
                    System::depthBuffer[x] = depth; surfaceBuffer[x] = {pos, normal, u, wall.object, i};
                    float textureU = objectData[wall.object].textureEffect == TEXTURE_NOISE ? u + random01((int)(pos.x * 17.0f), (int)(pos.y * 19.0f), (int)(GetTickCount64() / 120)) : u; System::framebuffer[x] = blendColor(objectTexture(wall.object, textureU), makeColor(255,40,40), fabsf(normal.x) * 0.06f);
                }
            }
        }

        void lightSurfaces() {
            int width = System::window.clientSize.x;
            if ((int)lightBuffer.size() != width) lightBuffer.resize(width);
            for (int x = 0; x < width; x++) {
                const Surface& surface = surfaceBuffer[x];
                lightBuffer[x] = {};
                if (surface.object == AIR || !objectData[surface.object].lit) continue;
                if (surface.wall >= 0) { const Wall& wall = walls[surface.wall]; lightBuffer[x] = sampleLight(surface.pos, surface.normal, wall.blockX, wall.blockY); }
                else lightBuffer[x] = sampleLight(surface.pos, surface.normal, -9999, -9999);
                const ObjectData& data = objectData[surface.object];
                if (data.lightStrength > 0.0f) {
                    lightBuffer[x].base = std::max(lightBuffer[x].base, std::min(1.0f, data.lightStrength));
                    lightBuffer[x].r = std::max(lightBuffer[x].r, float((data.lightColor >> 16) & 255) / 255.0f * data.lightTint);
                    lightBuffer[x].g = std::max(lightBuffer[x].g, float((data.lightColor >> 8) & 255) / 255.0f * data.lightTint);
                    lightBuffer[x].b = std::max(lightBuffer[x].b, float(data.lightColor & 255) / 255.0f * data.lightTint);
                    lightBuffer[x].direct = std::max(lightBuffer[x].direct, std::min(1.0f, data.lightStrength));
                }
            }
            const float penumbra = 0.25f;
            float tanHalfFOV = tanf(horizontalFOV() * 0.5f);
            for (int x = 0; x < width; x++) {
                const Surface& surface = surfaceBuffer[x];
                if (surface.object == AIR || !objectData[surface.object].lit) continue;
                int radius = std::max(1, (int)ceilf(penumbra * float(width - 1) / std::max(0.0001f, 2.0f * System::depthBuffer[x] * tanHalfFOV)));
                LightValue sum = {}; float weightSum = 0.0f;
                for (int i = -radius; i <= radius; i++) {
                    int sx = x + i;
                    if (sx < 0 || sx >= width || surfaceBuffer[sx].object == AIR) continue;
                    const Surface& sample = surfaceBuffer[sx];
                    if (surface.normal.x != sample.normal.x || surface.normal.y != sample.normal.y) continue;
                    float dx = sample.pos.x - surface.pos.x, dy = sample.pos.y - surface.pos.y, distance = sqrtf(dx * dx + dy * dy);
                    if (distance > penumbra) continue;
                    float weight = 1.0f - distance / penumbra; weight = weight * weight * (3.0f - 2.0f * weight);
                    sum.base += lightBuffer[sx].base * weight; sum.r += lightBuffer[sx].r * weight; sum.g += lightBuffer[sx].g * weight; sum.b += lightBuffer[sx].b * weight; sum.direct += lightBuffer[sx].direct * weight; weightSum += weight;
                }
                LightValue value = weightSum > 0.0f ? LightValue{sum.base / weightSum, sum.r / weightSum, sum.g / weightSum, sum.b / weightSum, sum.direct / weightSum} : lightBuffer[x];
                System::framebuffer[x] = applyLighting(System::framebuffer[x], surface.pos, surface.normal, value);
                if (surface.wall >= 0) System::framebuffer[x] = shadeColor(System::framebuffer[x], sampleAmbientOcclusion(surface.pos, walls[surface.wall]));
            }
        }

        void drawCrosshair() {
            int center = System::window.clientSize.x / 2;
            float distance = System::depthBuffer[center];
            if (distance < 6.0f) System::framebuffer[center] = blendColor(System::framebuffer[center], makeColor(255,255,255), std::clamp(1.0f - distance / 6.0f, 0.0f, 1.0f));
        }

        void applyScreenEffects() {
            bool focus = !(System::window.focused && System::input.cursorLocked);
            float umbraNear = 0.0f;
            for (const Entity& entity : entities) if (entity.object == UMBRA) {
                float dx = entity.pos.x - player.pos.x, dy = entity.pos.y - player.pos.y;
                umbraNear = std::max(umbraNear, std::clamp(1.0f - sqrtf(dx * dx + dy * dy) / 10.0f, 0.0f, 1.0f));
            }
            bool damageActive = player.damageTimer <= 0.35f;
            int width = System::window.clientSize.x;
            const float focalSharp = 0.07f, focalFade = 0.38f, nearSharp = 1.0f, fullBlurDistance = 8.0f;
            const int focalMaxBlur = 7;
            std::vector<uint32_t> source(System::framebuffer, System::framebuffer + width);
            uint32_t timeSeed = uint32_t(GetTickCount64() / 120);
            float umbraStrength = umbraNear * umbraNear * 0.10f, damage = damageActive ? 1.0f - std::clamp(player.damageTimer / 0.35f, 0.0f, 1.0f) : 0.0f;
            for (int x = 0; x < width; x++) {
                float edge = fabsf((float(x) + 0.5f) / float(width) * 2.0f - 1.0f);
                float focal = std::clamp((edge - focalSharp) / focalFade, 0.0f, 1.0f);
                focal = focal * focal * (3.0f - 2.0f * focal);
                float depth = System::depthBuffer[x];
                float depthBlur = std::clamp((depth - nearSharp) / (fullBlurDistance - nearSharp), 0.0f, 1.0f);
                depthBlur = depthBlur * depthBlur * (3.0f - 2.0f * depthBlur);
                int focalRadius = (int)ceilf(focal * depthBlur * focalMaxBlur);
                int radius = std::max(focalRadius, focus ? 4 : std::max(0, (int)ceilf(umbraNear * 4.0f)));
                uint32_t color = radius > 0 ? blurPixel(source, x, radius) : source[x];
                if (umbraNear > 0.0f) {
                    int amount = int((float(hash32(uint32_t(x) ^ timeSeed * 0x9E3779B9u) & 255) / 255.0f - 0.5f) * umbraStrength * 255.0f);
                    int b = std::clamp(int(color & 255) + amount, 0, 255), g = std::clamp(int((color >> 8) & 255) + amount, 0, 255), r = std::clamp(int((color >> 16) & 255) + amount, 0, 255);
                    color = makeColor(r,g,b);
                }
                if (damageActive) {
                    float tint = std::clamp((edge - 0.55f) / 0.45f, 0.0f, 1.0f);
                    color = blendColor(color, makeColor(255,0,0), tint * tint * damage * 0.45f);
                }
                if (focus) color = shadeColor(color, 0.35f);
                System::framebuffer[x] = color;
            }
        }

        void drawFrame() {
            if (!(System::framebuffer && System::window.clientSize.x > 1)) return;
            rasterizeWalls();
            for (const Entity& entity : entities) rasterizeEntity(entity, entity.object, entity.object == FIREFLY && entity.angry ? makeColor(255,80,80) : 0);
            applyCelShading();
            lightSurfaces();
            drawCrosshair();
            applyScreenEffects();
            if (fPtr_RenderUpdate) fPtr_RenderUpdate();
        }
    }

    void runMain() {
        if (fPtr_SystemUpdate) fPtr_SystemUpdate();
        if (System::window.focused && System::input.cursorLocked) {
            Gameplay::updatePlayer(deltaTime);
            Gameplay::updateFireflies(deltaTime);
            Gameplay::updateHeldEntity(deltaTime);
            Gameplay::buildLights();
            Gameplay::updateUmbra(deltaTime);
            Gameplay::updateYokai(deltaTime);
            Gameplay::orientMovingEntities();
            Gameplay::updatePlayerHazards();
            Gameplay::updateInput();
        }
        Draw::drawFrame();
    }
}

namespace Init {
    struct BinaryWriter {
        std::ofstream file;
        BinaryWriter(const char* path) : file(path, std::ios::binary) {}
        operator bool() const { return (bool)file; }
        template<typename T> BinaryWriter& operator<<(const T& v) { file.write((const char*)&v, sizeof(v)); return *this; }
    };
    
    struct BinaryReader {
        std::ifstream file;
        BinaryReader(const char* path) : file(path, std::ios::binary) {}
        operator bool() const { return (bool)file; }
        template<typename T> BinaryReader& operator>>(T& v) { file.read((char*)&v, sizeof(v)); return *this; }
    };

    void saveConfig() {
        BinaryWriter writer("config.bin");
        if (!writer) return;
        const System::Window& window = System::window;
        writer << window.pos.x << window.pos.y << window.size.x << window.size.y << window.maximized << window.fullscreen;
    }

    void loadConfig() {
        BinaryReader reader("config.bin");
        if (!reader) return;
        System::Window& window = System::window;
        reader >> window.pos.x >> window.pos.y >> window.size.x >> window.size.y >> window.maximized >> window.fullscreen;
    }

    void load() {
        Program::Gameplay::generateWorld();
        Program::Gameplay::rebuildWalls();
        Program::Gameplay::generateFireflies();
    }
}

int main() {
    Init::loadConfig();
    if (!System::Windows::setupWindows() || !System::Windows::setupBitBlt()) return 0;
    Init::load();
    appRunning = true;
    LARGE_INTEGER frequency, previous, current;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    while (appRunning) {
        QueryPerformanceCounter(&current);
        deltaTime = std::min(float(double(current.QuadPart - previous.QuadPart) / double(frequency.QuadPart)), 0.05f);
        previous = current;
        Program::runMain();
    }
    Init::saveConfig();
    if (fPtr_RenderShutdown) fPtr_RenderShutdown();
    if (fPtr_SystemShutdown) fPtr_SystemShutdown();
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) { return main(); }