// DjiHidReader.cpp
#include "DjiHidReader.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include "Windows/HideWindowsPlatformTypes.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static const USHORT DJI_VENDOR = 0x2CA3;
static const USHORT DJI_PRODUCT = 0x1020;

FDjiHidReader& FDjiHidReader::Get()
{
    static FDjiHidReader Instance;
    return Instance;
}

FDjiHidReader::FDjiHidReader() {}

FDjiHidReader::~FDjiHidReader()
{
    Shutdown();
}

bool FDjiHidReader::Start()
{
    if (Thread) return true;          // already running

    if (!OpenDevice())
        return false;

    bRunning = true;
    Thread = FRunnableThread::Create(this, TEXT("DjiHidReader"), 0, TPri_Normal);
    return Thread != nullptr;
}

// FRunnable::Stop – called by engine when thread is being killed
void FDjiHidReader::Stop()
{
    // DO NOT touch Thread here – just signal the loop to exit
    bRunning = false;
}
void FDjiHidReader::Shutdown()
{
    bRunning = false;

    if (Thread)
    {
        Thread->WaitForCompletion();  // join
        delete Thread;
        Thread = nullptr;
    }

    CloseDevice();
}
FDjiChannels FDjiHidReader::GetChannels()
{
    FScopeLock Lock(&DataMutex);
    return Channels;
}
bool FDjiHidReader::OpenDevice()
{
    CloseDevice();

    UE_LOG(LogTemp, Warning, TEXT("DJI: Scanning HID devices"));

    GUID HidGuid;
    HidD_GetHidGuid(&HidGuid);

    HDEVINFO DevInfo = SetupDiGetClassDevs(&HidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogTemp, Error, TEXT("DJI: SetupDiGetClassDevs failed"));
        return false;
    }

    SP_DEVICE_INTERFACE_DATA IfData;
    IfData.cbSize = sizeof(IfData);

    HANDLE BestHandle = INVALID_HANDLE_VALUE;
    uint32 BestInputLen = 0;
    USHORT BestUsagePage = 0;
    USHORT BestUsage = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(DevInfo, nullptr, &HidGuid, i, &IfData); ++i)
    {
        DWORD RequiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(DevInfo, &IfData, nullptr, 0, &RequiredSize, nullptr);
        if (RequiredSize == 0) continue;

        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(RequiredSize);
        auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(Buffer.GetData());
        Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &IfData, Detail,
            RequiredSize, nullptr, nullptr))
        {
            continue;
        }

        HANDLE Handle = CreateFileW(
            Detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (Handle == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES Attr;
        Attr.Size = sizeof(Attr);
        if (!HidD_GetAttributes(Handle, &Attr))
        {
            CloseHandle(Handle);
            continue;
        }

        // Only consider DJI VID/PID
        if (Attr.VendorID != DJI_VENDOR || Attr.ProductID != DJI_PRODUCT)
        {
            CloseHandle(Handle);
            continue;
        }

        // Get caps (usage & report sizes)
        PHIDP_PREPARSED_DATA Preparsed = nullptr;
        if (!HidD_GetPreparsedData(Handle, &Preparsed) || !Preparsed)
        {
            CloseHandle(Handle);
            continue;
        }

        HIDP_CAPS Caps;
        NTSTATUS Status = HidP_GetCaps(Preparsed, &Caps);
        HidD_FreePreparsedData(Preparsed);

        if (Status != HIDP_STATUS_SUCCESS)
        {
            CloseHandle(Handle);
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("DJI candidate: VID=%04X PID=%04X UsagePage=0x%04X Usage=0x%04X  In=%u Out=%u Feat=%u"),
            Attr.VendorID, Attr.ProductID,
            Caps.UsagePage, Caps.Usage,
            Caps.InputReportByteLength,
            Caps.OutputReportByteLength,
            Caps.FeatureReportByteLength);

        // Prefer Generic Desktop / Joystick or Gamepad
        const bool bIsJoystick =
            (Caps.UsagePage == 0x01) && (Caps.Usage == 0x04 || Caps.Usage == 0x05);

        if (bIsJoystick)
        {
            // Take the first joystick-style interface and stop searching
            BestHandle = Handle;
            BestInputLen = Caps.InputReportByteLength;
            BestUsagePage = Caps.UsagePage;
            BestUsage = Caps.Usage;
            break;
        }

        // Not the joystick interface – close and continue
        CloseHandle(Handle);
    }

    SetupDiDestroyDeviceInfoList(DevInfo);

    if (BestHandle == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogTemp, Error,
            TEXT("DJI: No joystick/gamepad HID interface found for VID=%04X PID=%04X"),
            DJI_VENDOR, DJI_PRODUCT);
        return false;
    }

    DeviceHandle = BestHandle;
    InputReportLen = BestInputLen;

    UE_LOG(LogTemp, Warning,
        TEXT("DJI: Selected interface UsagePage=0x%04X Usage=0x%04X InputReportLen=%u"),
        BestUsagePage, BestUsage, InputReportLen);

    return true;
}


void FDjiHidReader::CloseDevice()
{
    if (DeviceHandle)
    {
        CloseHandle(DeviceHandle);
        DeviceHandle = nullptr;
    }
}

uint32 FDjiHidReader::Run()
{
    UE_LOG(LogTemp, Warning, TEXT("DJI: Run loop started"));

    if (!DeviceHandle || InputReportLen == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("DJI: Run has no DeviceHandle or InputReportLen=0"));
        return 0;
    }

    // InputReportLen already includes the report ID byte
    TArray<uint8> Buffer;
    Buffer.SetNumUninitialized(InputReportLen);

    while (bRunning && DeviceHandle)
    {
        // Clear and set report ID 0 (most devices use 0 for the first report)
        FMemory::Memset(Buffer.GetData(), 0, InputReportLen);
        Buffer[0] = 0x00;   // report ID; try 0 first

        BOOLEAN Ok = HidD_GetInputReport(DeviceHandle,
            Buffer.GetData(),
            InputReportLen);

        if (!Ok)
        {
            // HidD_* doesn’t always set GetLastError meaningfully, so this is mostly FYI
            DWORD Err = GetLastError();
            UE_LOG(LogTemp, Warning,
                TEXT("DJI: HidD_GetInputReport failed (err=%lu)"),
                Err);
            FPlatformProcess::Sleep(0.01f);
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("DJI: Got %u bytes via GetInputReport"), InputReportLen);

        DecodeReport(Buffer.GetData(), (int32)InputReportLen);

        // Small delay so we don’t hammer the device
        FPlatformProcess::Sleep(0.005f);
    }

    UE_LOG(LogTemp, Warning, TEXT("DJI: Run loop exiting"));
    return 0;
}

void FDjiHidReader::DecodeReport(const uint8* Data, int32 Length)
{
    FString Hex;
    for (int32 i = 0; i < Length; ++i)
    {
        Hex += FString::Printf(TEXT("%02X "), Data[i]);
    }
    UE_LOG(LogTemp, Warning, TEXT("DJI HID (%d): %s"), Length, *Hex);

    // TODO: Once we see real data here, we’ll map it to Channels.Roll/Pitch/Yaw/Throttle.
}
