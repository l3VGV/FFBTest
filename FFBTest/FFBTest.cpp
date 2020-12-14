// FFBTest.cpp : Defines the entry point for the application.
//
//

#include "pch.h"
#include "framework.h"
#include "FFBTest.h"



#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name


LPDIRECTINPUT8          g_pDI = nullptr;
LPDIRECTINPUTDEVICE8    g_pDevice = nullptr;
LPDIRECTINPUTEFFECT     g_pEffect[4] = { nullptr };
DWORD                   g_dwNumForceFeedbackAxis = 0;

#define IDT_TIMER_FFBUPDATE 2

DWORD FFBdelayTime = 16;
HANDLE DownloadThread;
DWORD  DownloadThreadID;
DWORD FFBEffectUpdateThread_run = 0;
LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
LARGE_INTEGER Frequency;
DWORD callsNumber = 0, callsTime = 0;
HINSTANCE g_hInstance;
HWND g_hDlg;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    InitCommonControls();

    g_hInstance = hInstance;

    INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    DialogBox(hInst, MAKEINTRESOURCE(IDD_FFBTEST), nullptr, MainDlgProc);


    return 0;
}







VOID FreeDirectInput()
{
    // Unacquire the device one last time just in case 
    // the app tried to exit while the device is still acquired.
    if (g_pDevice)
        g_pDevice->Unacquire();

    for (int i = 0; i < 4; i++)
        SAFE_RELEASE(g_pEffect[i]);

    SAFE_RELEASE(g_pDevice);
    SAFE_RELEASE(g_pDI);
}





BOOL CALLBACK EnumFFDevicesCallback(const DIDEVICEINSTANCE* pInst,
    VOID* pContext)
{
    LPDIRECTINPUTDEVICE8 pDevice;
    HRESULT hr;

    // Obtain an interface to the enumerated force feedback device.
    hr = g_pDI->CreateDevice(pInst->guidInstance, &pDevice, nullptr);

    // If it failed, then we can't use this device for some
    // bizarre reason.  (Maybe the user unplugged it while we
    // were in the middle of enumerating it.)  So continue enumerating
    if (FAILED(hr))
        return DIENUM_CONTINUE;

    // We successfully created an IDirectInputDevice8.  So stop looking 
    // for another one.
    g_pDevice = pDevice;



    HWND  hEdit = GetDlgItem(g_hDlg, IDC_STATIC_DRIVER);
    wchar_t output[1024] = { 0 };

    if (IID_IDirectInputPIDDriver == pInst->guidFFDriver)
        SetWindowText(hEdit, _T("{CLSID_Microsoft USB PID Class Driver}"));
    else
    {
        hr = StringFromGUID2(pInst->guidFFDriver, output, 1000);
        if (SUCCEEDED(hr))
        {
            SetWindowText(hEdit, output);
        }
        
    }
    

    hEdit = GetDlgItem(g_hDlg, IDC_STATIC_JOYNAME);
    SetWindowText(hEdit, pInst->tszProductName);


    //DIPROP_GETPORTDISPLAYNAME

    DIPROPSTRING dip;
    dip.diph.dwSize = sizeof(DIPROPSTRING);
    dip.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dip.diph.dwObj = 0;
    dip.diph.dwHow = DIPH_DEVICE;

    hr = g_pDevice->GetProperty(DIPROP_GETPORTDISPLAYNAME, &dip.diph);

    if (SUCCEEDED(hr))
    {
        hEdit = GetDlgItem(g_hDlg, IDC_STATIC_PORT);
        SetWindowText(hEdit, dip.wsz);
    }


    //DIPROPDWORD dipdw;  // DIPROPDWORD contains a DIPROPHEADER structure. 
    //HRESULT hr;

    //dipdw.diph.dwSize = sizeof(DIPROPDWORD);
    //dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    //dipdw.diph.dwObj = 0; // device property 
    //dipdw.diph.dwHow = DIPH_DEVICE;

    //hr = idirectinputdevice9_GetProperty(pdid, DIPROP_BUFFERSIZE, &dipdw.diph);
    //if (SUCCEEDED(hr)) {


    //    hEdit = GetDlgItem(hDlg, );

    //}

    //
   // 
    //

       // SetWindowText(hEdit, output);

    return DIENUM_STOP;
}

BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi,
    VOID* pContext)
{
    auto pdwNumForceFeedbackAxis = reinterpret_cast<DWORD*>(pContext);

    if ((pdidoi->dwFlags & DIDOI_FFACTUATOR) != 0)
        (*pdwNumForceFeedbackAxis)++;

    return DIENUM_CONTINUE;
}




HRESULT InitDirectInput(HWND hDlg)
{
    DIPROPDWORD dipdw;
    HRESULT hr;

    // Register with the DirectInput subsystem and get a pointer
    // to a IDirectInput interface we can use.
    if (FAILED(hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8, (VOID**)&g_pDI, nullptr)))
    {
        return hr;
    }

    // Look for a force feedback device we can use
    if (FAILED(hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL,
        EnumFFDevicesCallback, nullptr,
        DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK)))
    {
        return hr;
    }

    if (!g_pDevice)
    {
        MessageBox(nullptr, _T("Force feedback device not found. ")
            _T("Going to exit."),
            _T("FFBTest"), MB_ICONERROR | MB_OK);
        EndDialog(hDlg, 0);
        return S_OK;
    }

    // Set the data format to "simple joystick" - a predefined data format. A
    // data format specifies which controls on a device we are interested in,
    // and how they should be reported.
    //
    // This tells DirectInput that we will be passing a DIJOYSTATE structure to
    // IDirectInputDevice8::GetDeviceState(). Even though we won't actually do
    // it in this sample. But setting the data format is important so that the
    // DIJOFS_* values work properly.
    if (FAILED(hr = g_pDevice->SetDataFormat(&c_dfDIJoystick)))
        return hr;

    // Set the cooperative level to let DInput know how this device should
    // interact with the system and with other DInput applications.
    // Exclusive access is required in order to perform force feedback.
    if (FAILED(hr = g_pDevice->SetCooperativeLevel(hDlg,
        DISCL_EXCLUSIVE |
        DISCL_FOREGROUND)))
    {
        return hr;
    }

    // Since we will be playing force feedback effects, we should disable the
    // auto-centering spring.
    dipdw.diph.dwSize = sizeof(DIPROPDWORD);
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipdw.diph.dwObj = 0;
    dipdw.diph.dwHow = DIPH_DEVICE;
    dipdw.dwData = FALSE;

    if (FAILED(hr = g_pDevice->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph)))
        return hr;

    // Enumerate and count the axes of the joystick 
    if (FAILED(hr = g_pDevice->EnumObjects(EnumAxesCallback,
        (VOID*)&g_dwNumForceFeedbackAxis, DIDFT_AXIS)))
        return hr;

    // This simple sample only supports one or two axis joysticks
    if (g_dwNumForceFeedbackAxis > 2)
        g_dwNumForceFeedbackAxis = 2;

    // This application needs only one effect: Applying raw forces.
    DWORD rgdwAxes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG rglDirection[2] = { 0, 0 };
    DICONSTANTFORCE cf = { 0 };

    DIEFFECT eff = {};
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwSamplePeriod = 0;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.dwTriggerRepeatInterval = 0;
    eff.cAxes = g_dwNumForceFeedbackAxis;
    eff.rgdwAxes = rgdwAxes;
    eff.rglDirection = rglDirection;
    eff.lpEnvelope = 0;
    eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
    eff.lpvTypeSpecificParams = &cf;
    eff.dwStartDelay = 0;

    

    // Create the prepared effect
    for (int i = 0; i < 4; i++)
    if (FAILED(hr = g_pDevice->CreateEffect(GUID_ConstantForce,
        &eff, &g_pEffect[i], nullptr)))
    {
        return hr;
    }

    if (!g_pEffect[0])
        return E_FAIL;

    return S_OK;
}





DWORD WINAPI FFBEffectUpdateThread(LPVOID lpParam)
{

    while (FFBEffectUpdateThread_run)
    {
        for (DWORD i = 0; i < 4; i++)
        {
            if (!g_pEffect[i]) continue;

            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&StartingTime);

            g_pEffect[i]->Download();

            QueryPerformanceCounter(&EndingTime);

            Sleep(FFBdelayTime);

            ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

            ElapsedMicroseconds.QuadPart *= 1000000;
            ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
            callsNumber += 1;

            callsTime += (DWORD)ElapsedMicroseconds.QuadPart;
        }
    }

    return 0;
}


void FFBTimer(HWND hDlg, UINT message, UINT_PTR timerID, DWORD milliseconds)
{
    if (!g_pEffect[0]) return;

    HRESULT hr;

    DIEFFECT eff = {};
    LONG rglDirection[2] = { 0, 0 };
    HWND  hEdit = GetDlgItem(hDlg, IDC_STATIC_EFFECTTIME);

    for (DWORD i = 0; i < 4; i++)
    {
        rglDirection[0] = (WORD)((0.15 * DI_FFNOMINALMAX * (rand() - RAND_MAX / 2) / RAND_MAX));
        rglDirection[1] = (WORD)((0.15 * DI_FFNOMINALMAX * (rand() - RAND_MAX / 2) / RAND_MAX));

        DICONSTANTFORCE cf;

        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.cAxes = g_dwNumForceFeedbackAxis;
        eff.rglDirection = rglDirection;
        eff.lpEnvelope = 0;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;
        eff.dwStartDelay = 0;

        cf.lMagnitude = (DWORD)sqrt((double)rglDirection[0] * (double)rglDirection[0] +
            (double)rglDirection[1] * (double)rglDirection[1]);


        //DIEP_DIRECTION DIEP_TYPESPECIFICPARAMS  DIEP_START maximum ammount of transfered commands 
        if (FAILED(hr = g_pEffect[i]->SetParameters(&eff, DIEP_DIRECTION | DIEP_NODOWNLOAD |
            DIEP_TYPESPECIFICPARAMS | DIEP_START)))
        {
            //(_T("FFB SetParameters fail\n"));
        }
 
        if (callsNumber >= 64)
        {
            wchar_t output[1024] = { 0 };

            
            swprintf_s(output, _T("%u"), callsTime / callsNumber);
            SetWindowText(hEdit, output);

            callsNumber = 0;
            callsTime = 0;
        }
    }
}

void StopFFBTest(HWND hDlg)
{
    KillTimer(hDlg, IDT_TIMER_FFBUPDATE);//effect update timer

    FFBEffectUpdateThread_run = 0;
    Sleep(100);
    CloseHandle(DownloadThread);
}


void StartFFBTest(HWND hDlg)
{
    StopFFBTest(hDlg);

    FFBEffectUpdateThread_run = 1;

    DownloadThread = CreateThread(NULL, 0, FFBEffectUpdateThread, NULL, 0, &DownloadThreadID);

    SetTimer(hDlg,                 
        IDT_TIMER_FFBUPDATE,         
        16,                     // 16-60fps
        (TIMERPROC)FFBTimer);
}

//-----------------------------------------------------------------------------
// Name: MainDlgProc
// Desc: Handles dialog messages
//-----------------------------------------------------------------------------
INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        g_hDlg = hDlg;
        if (FAILED(InitDirectInput(hDlg)))
        {
            MessageBox(nullptr, _T("Error Initializing DirectInput ")
                _T("Going to exit."),
                _T("FFBTest"), MB_ICONERROR | MB_OK);
            EndDialog(hDlg, 0);
        }


        HWND edit = GetDlgItem(hDlg, IDC_EDIT_DELAYTIME);
        HWND spin = GetDlgItem(hDlg, IDC_SPIN_DELAYTIME);

        SendMessage(spin, UDM_SETBUDDY, (WPARAM)edit, 0); //set buddy
        SendMessage(spin, UDM_SETBASE, (WPARAM)10, 0);
        SendMessage(spin, UDM_SETRANGE, 0, MAKELONG(255, 0)); //interval

        SendMessage(spin, UDM_SETPOS, 0, (LPARAM)MAKELONG((int)(16), 0)); //initial position
    }
        break;
    case WM_NOTIFY:
    {

        if (LOWORD(wParam) == IDC_SPIN_DELAYTIME)
        {
            LPNMUPDOWN lpnm;
            lpnm = (LPNMUPDOWN)lParam;


            HWND spin = GetDlgItem(hDlg, IDC_SPIN_DELAYTIME);
            int Pos = lpnm->iPos + lpnm->iDelta;

            if (Pos < 0) return 0;

            FFBdelayTime = Pos;

            return (INT_PTR)TRUE;
        }


    }
    case WM_ACTIVATE:
        if (WA_INACTIVE != wParam && g_pDevice)
        {
            // Make sure the device is acquired, if we are gaining focus.
            g_pDevice->Acquire();

            for(int i=0; i<4; i++)
            if (g_pEffect[i])
                g_pEffect[i]->Start(1, 0); // Start the effect
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(hDlg, 0);
            break;

        case IDC_BUTTON_START:
            StartFFBTest(hDlg);
            break;

        case IDC_BUTTON_STOP:
            StopFFBTest(hDlg);
            break;

        case IDC_BUTTON_ABOUT:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), 0, About);
            break;

        default:
            return FALSE; // Message not handled 
        }
        break;

    case WM_DESTROY:
        // Cleanup everything
        StopFFBTest(hDlg);
        FreeDirectInput();
        break;

    default:
        return FALSE; // Message not handled 
    }

    return TRUE; // Message handled 
}




// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
