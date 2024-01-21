#include <windows.h>
#include <stdint.h>
#include <xinput.h>
#include <dsound.h>

// TODO(joao): Implement sine myself
#include <math.h>

#define internal static // For only allowing function calls from its file (translation unit)
#define local_persist static // For local vars persisting through function calls
#define global_var static // Reminder that this also clears var to 0 when defined

#define pi32 3.14159265359f

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

struct win32_offscreen_buffer {
    // NOTE(joao): Pixels are always 32-bits wide, memory order BB GG RR xx
    BITMAPINFO info;
    void *memory;
    int width;
    int height;
    int pitch;
};

struct win32_window_dimensions {
    int width;
    int height;
};

// TODO(joao): global only for now
global_var bool32 _running;
global_var win32_offscreen_buffer globalBackBuffer;
global_var LPDIRECTSOUNDBUFFER globalSoundBuffer;

// NOTE(joao): Putting these two here so I don't have to bother depending on some lib with weird platform requirements
#define XINPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
#define XINPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef XINPUT_GET_STATE(xinput_get_state);
typedef XINPUT_SET_STATE(xinput_set_state);

XINPUT_GET_STATE(XInputGetStateStub) {
    return (ERROR_DEVICE_NOT_CONNECTED);
}

XINPUT_SET_STATE(XInputSetStateStub) {
    return (ERROR_DEVICE_NOT_CONNECTED);
}

global_var xinput_get_state *XInputGetState_ = XInputGetStateStub;
global_var xinput_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputGetState XInputGetState_ // NOTE(joao): Defining these so I can use the original name of the functions
#define XInputSetState XInputSetState_ // but make it call the functions pointed to by my pointers instead

// NOTE(joao): Attempts to get XInput by doing the same steps the Windows loader would
internal void win32LoadXInput(void) {
    HMODULE xInputLibrary = LoadLibraryA("xinput1_4.dll");
    if (!xInputLibrary) LoadLibraryA("xinput9_1_0.dll");
    if (!xInputLibrary) LoadLibraryA("xinput1_3.dll");

    if (xInputLibrary) {
        XInputGetState = (xinput_get_state *)GetProcAddress(xInputLibrary, "XInputGetState");
        XInputSetState = (xinput_set_state *)GetProcAddress(xInputLibrary, "XInputSetState");
    }
}

// NOTE(joao): Now do the same with DSound
#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal void win32InitDSound(HWND window, int32 samplesPerSecond, int32 bufferSize) {
    HMODULE dSoundLibrary = LoadLibraryA("dsound.dll");

    if (dSoundLibrary) {
        // NOTE(joao): Get a DirectSound object! - cooperative
        direct_sound_create *DirectSoundCreate = (direct_sound_create  *)
            GetProcAddress(dSoundLibrary, "DirectSoundCreate");

        // TODO(joao): Double-check if this works on XP - DirectSound8 or 7?
        LPDIRECTSOUND DirectSound;
        if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0))) {
            WAVEFORMATEX waveFormat = {};
            waveFormat.wFormatTag = WAVE_FORMAT_PCM;
            waveFormat.nChannels = 2;
            waveFormat.nSamplesPerSec = samplesPerSecond;
            waveFormat.wBitsPerSample = 16;
            waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
            waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
            //waveFormat.cbSize = 0;
            
            if (SUCCEEDED(DirectSound->SetCooperativeLevel(window, DSSCL_PRIORITY))) {
                DSBUFFERDESC bufferDescription = {};
                bufferDescription.dwSize = sizeof(bufferDescription);
                bufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

                // NOTE(joao): "Create" a primary buffer
                LPDIRECTSOUNDBUFFER primaryBuffer;
                
                if (SUCCEEDED(DirectSound->CreateSoundBuffer(&bufferDescription, &primaryBuffer, 0))) {
                    HRESULT error = primaryBuffer->SetFormat(&waveFormat);
                    if(SUCCEEDED(error)) {
                        
                    } else {
                        // TODO(joao): Diagnostic
                    }
                } else {
                    // TODO(joao): Diagnostic
                }
            } else {
                // TODO(joao): Diagnostic
            }                        
            // NOTE(joao): "Create" a secondary buffer            
            DSBUFFERDESC bufferDescription = {};
            bufferDescription.dwSize = sizeof(bufferDescription);
            bufferDescription.dwFlags = 0;
            bufferDescription.dwBufferBytes = bufferSize;
            bufferDescription.lpwfxFormat = &waveFormat;

            HRESULT error = DirectSound->CreateSoundBuffer(&bufferDescription, &globalSoundBuffer, 0);
            if (SUCCEEDED(error)) {
                
            } else {
                // TODO(joao): Diagnostic
            }            
            // NOTE(joao): Start it playing
        } else {
            // TODO(joao): Diagnostic
        }
    }
}

win32_window_dimensions win32GetWindowDimensions(HWND window) {
    win32_window_dimensions result;

    RECT clientRect;
    GetClientRect(window, &clientRect);
    result.width = clientRect.right - clientRect.left;
    result.height = clientRect.bottom - clientRect.top;
    
    return (result);
}

internal void renderWeirdGradient(win32_offscreen_buffer *buffer, int xOffset, int yOffset) {    
    uint8 *row = (uint8 *)buffer->memory;
    
    for (int y = 0; y < buffer->height; ++y) {
        uint32 *pixel = (uint32 *)row;
        
        for (int x = 0; x < buffer->width; ++x) {
            uint8 blue = (x + xOffset);
            uint8 green = (y + yOffset);

            // Remember this is in little endian notation
            /*
              Memory:   BB GG RR xx
              Register: xx RR GG BB

              Pixel (32-bits)
             */
            
            *pixel++ = ((green << 8) | blue);
        }
        
        row += buffer->pitch;
    }
}

internal void win32ResizeDIBSection(win32_offscreen_buffer *buffer, int width, int height) {    
    if (buffer->memory) {
        VirtualFree(buffer->memory, 0, MEM_RELEASE);
    }

    buffer->width = width;
    buffer->height = height;

    int bytesPerPixel = 4;

    // NOTE(joao): when biHeight is negative, Windows treats the bitmap as top-down, not bottom-up,
    // meaning that the first three bytes of the image are the color for the top left pixel
    // (instead of bottom left)
    buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
    buffer->info.bmiHeader.biWidth = buffer->width;
    buffer->info.bmiHeader.biHeight = -buffer->height;
    buffer->info.bmiHeader.biPlanes = 1; // This must be 1
    buffer->info.bmiHeader.biBitCount = 32; // 32 bits per pixel (8 red, 8 green, 8 blue and 1 padding byte)
    buffer->info.bmiHeader.biCompression = BI_RGB;

    // NOTE(joao): no need for DeviceContext thanks to using StretchDIBits
    // instead of BitBlt

    int bitmapMemorySize = bytesPerPixel * (buffer->width * buffer->height);
    buffer->memory = VirtualAlloc(0, bitmapMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

    buffer->pitch = width * bytesPerPixel;

    // TODO(joao): probably clear this to black
}

internal void win32DisplayBufferInWindow(win32_offscreen_buffer *buffer,
                                         HDC deviceContext, int windowWidth, int windowHeight) {
    // TODO(joao): Aspect ratio correction

    StretchDIBits(deviceContext,
                  0, 0, windowWidth, windowHeight,
                  0, 0, buffer->width, buffer->height,
                  buffer->memory,
                  &buffer->info,
                  DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK win32MainWindowCallback(HWND window, UINT msg, WPARAM wParam, LPARAM lParam){
    LRESULT result = 0;
    
    switch (msg){
        case WM_DESTROY: {
            // TODO(joao): Handle as an error - possibly recreate window
            OutputDebugStringA("WM_DESTROY\n");
            _running = false;
        } break;
        case WM_CLOSE: {
            // TODO(joao): Handle with message to user
            OutputDebugStringA("WM_CLOSE\n");
            _running = false;
        } break;
        case WM_ACTIVATEAPP: {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        // NOTE(joao): Keyboard input handling messages
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP: {
            uint32 vkCode = wParam;
            #define keyMsgWasDownBit (1 << 30)
            #define keyMsgIsDownBit (1 << 31)
            bool32 wasDown  = ((lParam & keyMsgWasDownBit) != 0);
            bool32 isDown = ((lParam & keyMsgIsDownBit) == 0);

            if (wasDown != isDown) {
                if (vkCode == 'W') {
                    
                } else if (vkCode == 'A') {
                
                } else if (vkCode == 'S') {
                
                } else if (vkCode == 'D') {

                } else if (vkCode == 'Q') {

                } else if (vkCode == 'E') {
                    
                } else if (vkCode == VK_ESCAPE) {
                    
                }
            }

            bool32 altKeyIsDown = (lParam & (1 << 29));
            if (altKeyIsDown && (vkCode == VK_F4)) {
                _running = false;
            }
        } break;
            
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC deviceContext = BeginPaint(window, &paint);
            int x = paint.rcPaint.left;
            int y = paint.rcPaint.top;
            int width = paint.rcPaint.right - paint.rcPaint.left;
            int height = paint.rcPaint.bottom - paint.rcPaint.top;

            win32_window_dimensions dimensions = win32GetWindowDimensions(window);
            win32DisplayBufferInWindow(&globalBackBuffer, deviceContext, dimensions.width, dimensions.height);

            EndPaint(window, &paint);
        } break;
        default: {
//            OutputDebugStringA("default\n");
            result = DefWindowProcA(window, msg, wParam, lParam);
        } break;
    }

    return (result);
};

struct win32_sound_output {
    int samplesPerSecond;
    int toneHz;
    int16 toneVolume;
    uint32 runningSampleIndex;
    int wavePeriod;
    int bytesPerSample;
    int globalBufferSize;
    real32 tSine;
    int latencySampleCount;
};

internal void win32FillSoundBuffer(win32_sound_output *soundOutput, DWORD byteToLock, DWORD bytesToWrite) {
    VOID *region1;
    DWORD region1Size;
    VOID *region2;
    DWORD region2Size;
                    
    if (SUCCEEDED(globalSoundBuffer->Lock(byteToLock, bytesToWrite,
                                          &region1, &region1Size,
                                          &region2, &region2Size, 0)))
    {
        // TODO(joao): Assert that region sizes are valid

        // TODO(joao): Collapse these two loops
                        
        DWORD region1SampleCount = region1Size / soundOutput->bytesPerSample;
        int16 *sampleOut = (int16 *)region1;
                        
        for (DWORD sampleIndex = 0; sampleIndex < region1SampleCount; ++sampleIndex) {
            real32 sineValue = sinf(soundOutput->tSine);
            int16 sampleValue = (int16)(sineValue * soundOutput->toneVolume);
            *sampleOut++ = sampleValue;
            *sampleOut++ = sampleValue;

            soundOutput->tSine += 2.0f * pi32 * 1.0f / (real32)soundOutput->wavePeriod;
            ++soundOutput->runningSampleIndex;
        }

        DWORD region2SampleCount = region2Size / soundOutput->bytesPerSample;
        sampleOut = (int16 *)region2;
                        
        for (DWORD sampleIndex = 0; sampleIndex < region2SampleCount; ++sampleIndex) {
            real32 t = 2.0f * pi32 * (real32)soundOutput->runningSampleIndex / (real32)soundOutput->wavePeriod;
            real32 sineValue = sinf(t);
            int16 sampleValue = (int16)(sineValue * soundOutput->toneVolume);
            *sampleOut++ = sampleValue;
            *sampleOut++ = sampleValue;

            ++soundOutput->runningSampleIndex;
        }

        globalSoundBuffer->Unlock(region1, region1Size, region2, region2Size);
    }    
}

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCode){
    win32LoadXInput();
    
    WNDCLASSA windowClass = {};

    win32ResizeDIBSection(&globalBackBuffer, 1280, 720);
    
    windowClass.style = CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
    windowClass.lpfnWndProc = win32MainWindowCallback;
    windowClass.hInstance = instance;
//    WindowClass.hIcon;
    windowClass.lpszClassName = "HorrifieldWindowClass";

    if(RegisterClassA(&windowClass)) {
        HWND window = CreateWindowEx(0, //dwExStyle (Extended Window Styles)
                                           windowClass.lpszClassName, //lpClassName
                                           "Horrifield", //lpWindowName
                                           WS_OVERLAPPEDWINDOW|WS_VISIBLE, //dwStyle (Window Styles)
                                           CW_USEDEFAULT, //x (position)
                                           CW_USEDEFAULT, //y (position)
                                           CW_USEDEFAULT, //nWidth
                                           CW_USEDEFAULT, //nHeight
                                           0, //hWndParent
                                           0, //hMenu
                                           instance, //hInstance
                                           0); //lpParam
        if (window) {
            HDC deviceContext = GetDC(window);
            _running = true;

            // NOTE(joao): Graphics test variables
            int xOffset = 0;
            int yOffset = 0;

            
            win32_sound_output soundOutput = {};
    
            soundOutput.samplesPerSecond = 48000;
            soundOutput.toneHz = 256;
            soundOutput.toneVolume = 2000;
            soundOutput.runningSampleIndex = 0;
            soundOutput.wavePeriod = soundOutput.samplesPerSecond / soundOutput.toneHz;
            soundOutput.bytesPerSample = sizeof(int16) * 2;
            soundOutput.globalBufferSize = soundOutput.samplesPerSecond * soundOutput.bytesPerSample;
            soundOutput.latencySampleCount = soundOutput.samplesPerSecond / 15;
            
            win32InitDSound(window, soundOutput.samplesPerSecond, soundOutput.globalBufferSize);
            win32FillSoundBuffer(&soundOutput, 0, soundOutput.latencySampleCount * soundOutput.bytesPerSample);
            globalSoundBuffer->Play(0, 0, DSBPLAY_LOOPING);                    
            
            bool32 soundIsPlaying = false;
            
            // NOTE(joao): Main game loop
            while (_running) {
                MSG msg;

                { // NOTE(joao): Loop handling messages the system sends to the application
                    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
                        if (msg.message == WM_QUIT) _running = false;
                   
                        TranslateMessage(&msg);
                        DispatchMessageA(&msg);
                    }
                }

                { // NOTE(joao): Loop handling controller input 
                    for (DWORD controllerIndex = 0; controllerIndex < XUSER_MAX_COUNT; ++controllerIndex) {
                        XINPUT_STATE controllerState;

                        if (XInputGetState(controllerIndex, &controllerState) == ERROR_SUCCESS) {
                            // NOTE(joao): Controller is plugged in
                            XINPUT_GAMEPAD *pad = &controllerState.Gamepad;

                            bool32 dPadUp = (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                            bool32 dPadDown = (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                            bool32 dPadLeft = (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                            bool32 dPadRight = (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                            bool32 start = (pad->wButtons & XINPUT_GAMEPAD_START);
                            bool32 back = (pad->wButtons & XINPUT_GAMEPAD_BACK);
                            bool32 leftShoulder = (pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                            bool32 rightShoulder = (pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                            bool32 btnA = (pad->wButtons & XINPUT_GAMEPAD_A);
                            bool32 btnB = (pad->wButtons & XINPUT_GAMEPAD_B);
                            bool32 btnX = (pad->wButtons & XINPUT_GAMEPAD_X);
                            bool32 btnY = (pad->wButtons & XINPUT_GAMEPAD_Y);

                            int16 stickLX = pad->sThumbLX;
                            int16 stickLY = pad->sThumbLY;

                            xOffset += stickLX / 4096;
                            yOffset += stickLY / 4096;
                            
                            if (btnA) --yOffset;
                        }
                        else {
                            // NOTE(joao): Controller not available
                        }
                    }
                }
                
                renderWeirdGradient(&globalBackBuffer, xOffset, yOffset);

                DWORD playCursor;
                DWORD writeCursor;

                // NOTE(joao): DirectSound output test
                if (!soundIsPlaying &&
                    SUCCEEDED(globalSoundBuffer->GetCurrentPosition(&playCursor, &writeCursor))) {
                    DWORD byteToLock = (soundOutput.runningSampleIndex * soundOutput.bytesPerSample) % soundOutput.globalBufferSize;
                    DWORD targetCursor =
                        ((playCursor +
                          (soundOutput.latencySampleCount * soundOutput.bytesPerSample)) % soundOutput.globalBufferSize);
                    DWORD bytesToWrite;

                    if (byteToLock > targetCursor) {
                        bytesToWrite = soundOutput.globalBufferSize - byteToLock;
                        bytesToWrite += targetCursor;
                    } else {
                        bytesToWrite = targetCursor - byteToLock;
                    }                    

                    win32FillSoundBuffer(&soundOutput, byteToLock, bytesToWrite);
                }                
                
                win32_window_dimensions dimensions = win32GetWindowDimensions(window);
                win32DisplayBufferInWindow(&globalBackBuffer, deviceContext, dimensions.width, dimensions.height);
            }
        }
        else {
            // TODO(joao): Logging
        }
    }
    else {
        // TODO(joao): Logging
    }
    
    return(0);
}
