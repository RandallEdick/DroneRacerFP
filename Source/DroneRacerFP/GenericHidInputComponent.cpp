// GenericHidInputComponent.cpp
#include "GenericHidInputComponent.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS

#include "Framework/Application/SlateApplication.h"
#include "Windows/WindowsApplication.h"
#include "Templates/SharedPointer.h"   // ensures TSharedPtr is visible

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS

UGenericHidInputComponent* UGenericHidInputComponent::GActiveInstance = nullptr;

#if PLATFORM_WINDOWS

// ---------------------------------------------
// Helpers
// ---------------------------------------------

static bool GetRidDeviceInfo(HANDLE DeviceHandle, RID_DEVICE_INFO& OutInfo)
{
    UINT Size = sizeof(RID_DEVICE_INFO);
    OutInfo.cbSize = Size;
    const UINT Res = GetRawInputDeviceInfo(DeviceHandle, RIDI_DEVICEINFO, &OutInfo, &Size);
    return (Res != (UINT)-1);
}

static bool GetPreparsedData(HANDLE DeviceHandle, TArray<uint8>& OutPreparsed)
{
    UINT Size = 0;
    if (GetRawInputDeviceInfo(DeviceHandle, RIDI_PREPARSEDDATA, nullptr, &Size) == (UINT)-1 || Size == 0)
        return false;

    OutPreparsed.SetNumUninitialized(Size);
    if (GetRawInputDeviceInfo(DeviceHandle, RIDI_PREPARSEDDATA, OutPreparsed.GetData(), &Size) == (UINT)-1)
        return false;

    return true;
}

static FString MakeDeviceId(const RID_DEVICE_INFO_HID& HidInfo, HANDLE DeviceHandle)
{
    // NOTE: includes handle => not stable across runs; good enough for now.
    return FString::Printf(TEXT("HID_%p_VID_%04X_PID_%04X"),
        DeviceHandle, (uint32)HidInfo.dwVendorId, (uint32)HidInfo.dwProductId);
}

static void NormalizeHidValueToFloat(LONG Value, LONG LogicalMin, LONG LogicalMax, float& OutFloat)
{
    if (LogicalMax == LogicalMin)
    {
        OutFloat = 0.f;
        return;
    }

    const float t = (float)(Value - LogicalMin) / (float)(LogicalMax - LogicalMin); // 0..1
    OutFloat = (t * 2.f) - 1.f; // -1..1
}

static int32 UsageToAxisIndex(USAGE Usage)
{
    // Generic Desktop usages
    switch (Usage)
    {
    case HID_USAGE_GENERIC_X:      return 0;
    case HID_USAGE_GENERIC_Y:      return 1;
    case HID_USAGE_GENERIC_Z:      return 2;
    case HID_USAGE_GENERIC_RX:     return 3;
    case HID_USAGE_GENERIC_RY:     return 4;
    case HID_USAGE_GENERIC_RZ:     return 5;
    case HID_USAGE_GENERIC_SLIDER: return 6;
    case HID_USAGE_GENERIC_DIAL:   return 7;
    case HID_USAGE_GENERIC_WHEEL:  return 8;
    default:                       return -1;
    }
}

// ---------------------------------------------
// Windows message handler (UE 5.4: AddMessageHandler takes IWindowsMessageHandler&)
// ---------------------------------------------

class FGenericHidWindowsMessageHandler : public IWindowsMessageHandler
{
public:
    virtual bool ProcessMessage(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam, int32& OutResult) override
    {
        if (msg == WM_INPUT && UGenericHidInputComponent::GActiveInstance)
        {
            UGenericHidInputComponent::GActiveInstance->HandleRawInput((void*)lParam);
        }
        return false; // don't consume
    }
};

static FGenericHidWindowsMessageHandler GWinMsgHandler;
static bool GHandlerInstalled = false;

#endif // PLATFORM_WINDOWS

// ---------------------------------------------
// Device state
// ---------------------------------------------

struct UGenericHidInputComponent::FDeviceState
{
#if PLATFORM_WINDOWS
    HANDLE Handle = nullptr;
    FString DeviceId;
    int32 VendorId = 0;
    int32 ProductId = 0;

    TArray<uint8> Preparsed;
    HIDP_CAPS Caps{};
    TArray<HIDP_VALUE_CAPS> ValueCaps;

    // X,Y,Z,Rx,Ry,Rz,Slider,Dial,Wheel
    TArray<float> Axes;

    bool bInitialized = false;
#endif
};

// ---------------------------------------------
// Component
// ---------------------------------------------

UGenericHidInputComponent::UGenericHidInputComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UGenericHidInputComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStart)
    {
        Start();
    }
}

void UGenericHidInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Stop();
    Super::EndPlay(EndPlayReason);
}

void UGenericHidInputComponent::Start()
{
#if !PLATFORM_WINDOWS
    UE_LOG(LogTemp, Warning, TEXT("GenericHidInputComponent: Windows only."));
    return;
#else
    if (bStarted)
        return;

    // Only one active instance at a time for this simple drop-in.
    GActiveInstance = this;

    // Register for raw input HID devices (Joystick/Gamepad/Multi-axis)
    RAWINPUTDEVICE Rid[3]{};

    Rid[0].usUsagePage = 0x01; // Generic Desktop
    Rid[0].usUsage = 0x04; // Joystick
    Rid[0].dwFlags = RIDEV_INPUTSINK;
    Rid[0].hwndTarget = nullptr;

    Rid[1].usUsagePage = 0x01;
    Rid[1].usUsage = 0x05; // Gamepad
    Rid[1].dwFlags = RIDEV_INPUTSINK;
    Rid[1].hwndTarget = nullptr;

    Rid[2].usUsagePage = 0x01;
    Rid[2].usUsage = 0x08; // Multi-axis controller
    Rid[2].dwFlags = RIDEV_INPUTSINK;
    Rid[2].hwndTarget = nullptr;

    if (!RegisterRawInputDevices(Rid, 3, sizeof(RAWINPUTDEVICE)))
    {
        UE_LOG(LogTemp, Error, TEXT("GenericHidInputComponent: RegisterRawInputDevices failed (%lu)"), GetLastError());
        return;
    }

    if (!FSlateApplication::IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("GenericHidInputComponent: Slate not initialized yet."));
        return;
    }

    auto PlatformApp = FSlateApplication::Get().GetPlatformApplication();
    if (!PlatformApp.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("GenericHidInputComponent: Platform application invalid."));
        return;
    }

    // Get raw pointer to the generic app and cast to Windows app.
    FWindowsApplication* WinApp = static_cast<FWindowsApplication*>(PlatformApp.Get());
    if (!WinApp)
    {
        UE_LOG(LogTemp, Error, TEXT("GenericHidInputComponent: Could not cast platform app to FWindowsApplication."));
        return;
    }

    if (!GHandlerInstalled)
    {
        WinApp->AddMessageHandler(GWinMsgHandler); // UE 5.4 expects IWindowsMessageHandler&
        GHandlerInstalled = true;
    }

    bStarted = true;
    UE_LOG(LogTemp, Log, TEXT("GenericHidInputComponent: Started (WM_INPUT handler installed)."));
#endif


}

void UGenericHidInputComponent::Stop()
{
#if PLATFORM_WINDOWS
    if (!bStarted)
        return;

    if (!bStarted)
        return;

    if (FSlateApplication::IsInitialized())
    {
        auto PlatformApp = FSlateApplication::Get().GetPlatformApplication();
        if (PlatformApp.IsValid())
        {
            FWindowsApplication* WinApp = static_cast<FWindowsApplication*>(PlatformApp.Get());
            if (WinApp && GHandlerInstalled)
            {
                WinApp->RemoveMessageHandler(GWinMsgHandler);
                GHandlerInstalled = false;
            }
        }
    }

    Devices.Empty();

    if (GActiveInstance == this)
        GActiveInstance = nullptr;

    bStarted = false;
    UE_LOG(LogTemp, Log, TEXT("GenericHidInputComponent: Stopped."));
#endif


}

bool UGenericHidInputComponent::GetLatestAxesForDevice(const FString& DeviceId, FGenericHidDeviceAxes& OutDevice) const
{
#if PLATFORM_WINDOWS
    for (const auto& It : Devices)
    {
        const TSharedPtr<FDeviceState>& D = It.Value;
        if (D.IsValid() && D->bInitialized && D->DeviceId == DeviceId)
        {
            OutDevice.DeviceId = D->DeviceId;
            OutDevice.VendorId = D->VendorId;
            OutDevice.ProductId = D->ProductId;
            OutDevice.Axes = D->Axes;
            return true;
        }
    }
#endif
    return false;
}

#if PLATFORM_WINDOWS

static bool InitDeviceCaps(UGenericHidInputComponent::FDeviceState& D)
{
    RID_DEVICE_INFO Info{};
    if (!GetRidDeviceInfo(D.Handle, Info))
        return false;

    if (Info.dwType != RIM_TYPEHID)
        return false;

    D.VendorId = (int32)Info.hid.dwVendorId;
    D.ProductId = (int32)Info.hid.dwProductId;
    D.DeviceId = MakeDeviceId(Info.hid, D.Handle);

    if (!GetPreparsedData(D.Handle, D.Preparsed))
        return false;

    PHIDP_PREPARSED_DATA PreparsedPtr = (PHIDP_PREPARSED_DATA)D.Preparsed.GetData();

    if (HidP_GetCaps(PreparsedPtr, &D.Caps) != HIDP_STATUS_SUCCESS)
        return false;

    const USHORT NumValueCaps = D.Caps.NumberInputValueCaps;
    D.ValueCaps.SetNumUninitialized(NumValueCaps);

    USHORT ValueCapsLen = NumValueCaps;
    if (HidP_GetValueCaps(HidP_Input, D.ValueCaps.GetData(), &ValueCapsLen, PreparsedPtr) != HIDP_STATUS_SUCCESS)
        return false;

    D.Axes.SetNumZeroed(9);
    D.bInitialized = true;
    return true;
}
void UGenericHidInputComponent::GetKnownDevices(TArray<FGenericHidDeviceAxes>& OutDevices) const
{
    OutDevices.Reset();

#if PLATFORM_WINDOWS
    for (const auto& It : Devices)
    {
        const TSharedPtr<FDeviceState>& D = It.Value;
        if (!D.IsValid() || !D->bInitialized) continue;

        FGenericHidDeviceAxes X;
        X.DeviceId = D->DeviceId;
        X.VendorId = D->VendorId;
        X.ProductId = D->ProductId;
        X.Axes = D->Axes;
        OutDevices.Add(X);
    }
#endif
}

void UGenericHidInputComponent::HandleRawInput(void* RawInputHandle)
{
    if (!bStarted)
        return;

    HRAWINPUT hRawInput = (HRAWINPUT)RawInputHandle;

    UINT Size = 0;
    if (GetRawInputData(hRawInput, RID_INPUT, nullptr, &Size, sizeof(RAWINPUTHEADER)) == (UINT)-1 || Size == 0)
        return;

    TArray<uint8> Buffer;
    Buffer.SetNumUninitialized(Size);

    if (GetRawInputData(hRawInput, RID_INPUT, Buffer.GetData(), &Size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    RAWINPUT* RI = (RAWINPUT*)Buffer.GetData();
    if (RI->header.dwType != RIM_TYPEHID)
        return;

    HANDLE DeviceHandle = RI->header.hDevice;

    TSharedPtr<FDeviceState> Device = Devices.FindRef((void*)DeviceHandle);
    if (!Device.IsValid())
    {
        Device = MakeShared<FDeviceState>();
        Device->Handle = DeviceHandle;

        if (!InitDeviceCaps(*Device))
            return;

        Devices.Add((void*)DeviceHandle, Device);

        if (bLogDevices)
        {
            UE_LOG(LogTemp, Log, TEXT("GenericHID: New device %s (VID=%04X PID=%04X)"),
                *Device->DeviceId, Device->VendorId, Device->ProductId);
        }
    }

    if (!Device->bInitialized)
        return;

    PHIDP_PREPARSED_DATA PreparsedPtr = (PHIDP_PREPARSED_DATA)Device->Preparsed.GetData();

    const BYTE* ReportData = RI->data.hid.bRawData;
    const UINT  ReportSize = RI->data.hid.dwSizeHid;

    bool bAnyAxisChanged = false;

    for (const HIDP_VALUE_CAPS& VC : Device->ValueCaps)
    {
        // Most sticks/sliders are on Generic Desktop page; keep this filter for now.
        if (VC.UsagePage != 0x01)
            continue;

        const bool bRanged = (VC.IsRange != 0);

        const USAGE U0 = bRanged ? VC.Range.UsageMin : VC.NotRange.Usage;
        const USAGE U1 = bRanged ? VC.Range.UsageMax : VC.NotRange.Usage;

        for (USAGE U = U0; U <= U1; ++U)
        {
            const int32 AxisIdx = UsageToAxisIndex(U);
            if (AxisIdx < 0 || !Device->Axes.IsValidIndex(AxisIdx))
                continue;

            ULONG Value = 0;
            const NTSTATUS S = HidP_GetUsageValue(
                HidP_Input,
                VC.UsagePage,
                0,
                U,
                &Value,
                PreparsedPtr,
                (PCHAR)ReportData,
                ReportSize);

            if (S != HIDP_STATUS_SUCCESS)
                continue;

            float Norm = 0.f;
            NormalizeHidValueToFloat((LONG)Value, VC.LogicalMin, VC.LogicalMax, Norm);

            if (!FMath::IsNearlyEqual(Device->Axes[AxisIdx], Norm, 1e-4f))
            {
                Device->Axes[AxisIdx] = Norm;
                bAnyAxisChanged = true;
            }
        }
    }

    if (bAnyAxisChanged)
    {
        if (bLogDevices)
        {
            UE_LOG(LogTemp, Warning, TEXT("HID %s Axes: X=%.3f Y=%.3f Z=%.3f Rx=%.3f Ry=%.3f Rz=%.3f Sl=%.3f"),
                *Device->DeviceId,
                Device->Axes[0], Device->Axes[1], Device->Axes[2],
                Device->Axes[3], Device->Axes[4], Device->Axes[5],
                Device->Axes[6]);
        }

        FGenericHidDeviceAxes Out;
        Out.DeviceId = Device->DeviceId;
        Out.VendorId = Device->VendorId;
        Out.ProductId = Device->ProductId;
        Out.Axes = Device->Axes;

        OnAxesUpdated.Broadcast(Out);
    }
}

#endif // PLATFORM_WINDOWS
