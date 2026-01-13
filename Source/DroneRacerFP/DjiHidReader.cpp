#include "DjiHidReader.h"

#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Misc/AssertionMacros.h"
#include "Containers/Array.h"

DEFINE_LOG_CATEGORY_STATIC(LogDjiHid, Log, All);

// ============================================================================
//  Windows HID headers (only on Windows)
// ============================================================================

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS

// ============================================================================
//  Singleton plumbing
// ============================================================================

FDjiHidReader& FDjiHidReader::Get()
{
    static FDjiHidReader Singleton;
    return Singleton;
}

FDjiHidReader::FDjiHidReader()
    : Thread(nullptr)
    , bStopRequested(false)
#if PLATFORM_WINDOWS
    , DeviceHandle(nullptr)
    , InputReportLen(0)
#endif
{
    FMemory::Memzero(&Channels, sizeof(Channels));
}

FDjiHidReader::~FDjiHidReader()
{
    Shutdown();
}

void FDjiHidReader::Start()
{
    if (Thread)
    {
        return; // already running
    }

    bStopRequested = false;

    Thread = FRunnableThread::Create(
        this,
        TEXT("DJI_HID_Reader"),
        0,
        TPri_Normal);

    if (!Thread)
    {
        UE_LOG(LogDjiHid, Error, TEXT("FDjiHidReader: failed to create thread"));
    }
}

void FDjiHidReader::Shutdown()
{
    bStopRequested = true;

    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }

#if PLATFORM_WINDOWS
    CloseDevice();
#endif
}

FDjiChannels FDjiHidReader::GetChannels() const
{
    FScopeLock Lock(&ChannelsMutex);
    return Channels;
}

bool FDjiHidReader::Init()
{
    return true;
}

void FDjiHidReader::Stop()
{
    bStopRequested = true;
}

uint32 FDjiHidReader::Run()
{
#if !PLATFORM_WINDOWS
    return 0;
#else
    UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop starting"));

    while (!bStopRequested)
    {
        // Ensure device is open
        if (DeviceHandle == nullptr)
        {
            OpenDevice();

            if (DeviceHandle == nullptr)
            {
                FPlatformProcess::SleepNoStats(1.0f);
                continue;
            }
        }

        if (InputReportLen <= 0)
        {
            FPlatformProcess::SleepNoStats(0.1f);
            continue;
        }

        TArray<uint8> Buffer;
        Buffer.SetNumZeroed(InputReportLen);

        HANDLE Handle = static_cast<HANDLE>(DeviceHandle);

        // Simple blocking read via HidD_GetInputReport
        BOOLEAN bOk = HidD_GetInputReport(
            Handle,
            Buffer.GetData(),
            Buffer.Num());

        if (!bOk)
        {
            DWORD Err = GetLastError();
            UE_LOG(LogDjiHid, Warning,
                TEXT("DJI: HidD_GetInputReport failed (err=%lu)"), Err);

            FPlatformProcess::SleepNoStats(0.01f);
            continue;
        }

        // ---- DEBUG: dump raw bytes when they change ----
        {
            static TArray<uint8> LastBuf;
            if (LastBuf.Num() != Buffer.Num())
            {
                LastBuf.SetNumZeroed(Buffer.Num());
            }

            if (FMemory::Memcmp(LastBuf.GetData(), Buffer.GetData(), Buffer.Num()) != 0)
            {
                FString BytesStr;
                for (int32 i = 0; i < Buffer.Num(); ++i)
                {
                    BytesStr += FString::Printf(TEXT("%02X "), Buffer[i]);
                }

                UE_LOG(LogDjiHid, Warning,
                    TEXT("DJI RAW [%d]: %s"), Buffer.Num(), *BytesStr);

                FMemory::Memcpy(LastBuf.GetData(), Buffer.GetData(), Buffer.Num());
            }
        }

        // Parse into roll / pitch / yaw / throttle
        ParseReport(Buffer.GetData(), Buffer.Num());

        FPlatformProcess::SleepNoStats(0.002f);
    }

    UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop exiting"));
    return 0;
#endif // PLATFORM_WINDOWS
}

// ============================================================================
//  Windows HID helpers
// ============================================================================

#if PLATFORM_WINDOWS

void FDjiHidReader::OpenDevice()
{
    if (DeviceHandle != nullptr)
    {
        return; // already open
    }

    UE_LOG(LogDjiHid, Warning, TEXT("DJI: Scanning HID devices"));

    GUID HidGuid;
    HidD_GetHidGuid(&HidGuid);

    HDEVINFO DevInfoSet = SetupDiGetClassDevs(
        &HidGuid,
        nullptr,
        nullptr,
        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (DevInfoSet == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogDjiHid, Error, TEXT("DJI: SetupDiGetClassDevs failed"));
        return;
    }

    SP_DEVICE_INTERFACE_DATA IfaceData;
    FMemory::Memzero(&IfaceData, sizeof(IfaceData));
    IfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    const USHORT TargetVid = 0x2CA3; // DJI
    const USHORT TargetPid = 0x1020; // FPV Remote Controller 2

    DWORD Index = 0;
    bool bFound = false;

    while (SetupDiEnumDeviceInterfaces(
        DevInfoSet,
        nullptr,
        &HidGuid,
        Index,
        &IfaceData))
    {
        ++Index;

        DWORD DetailSize = 0;
        SetupDiGetDeviceInterfaceDetail(
            DevInfoSet,
            &IfaceData,
            nullptr,
            0,
            &DetailSize,
            nullptr);

        if (DetailSize == 0)
        {
            continue;
        }

        TArray<uint8> DetailBuffer;
        DetailBuffer.SetNumZeroed(DetailSize);

        auto DetailData =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(DetailBuffer.GetData());
        DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(
            DevInfoSet,
            &IfaceData,
            DetailData,
            DetailSize,
            nullptr,
            nullptr))
        {
            continue;
        }

        const TCHAR* DevicePath = DetailData->DevicePath;

        HANDLE TestHandle = CreateFile(
            DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (TestHandle == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        HIDD_ATTRIBUTES Attr;
        Attr.Size = sizeof(HIDD_ATTRIBUTES);
        if (!HidD_GetAttributes(TestHandle, &Attr))
        {
            CloseHandle(TestHandle);
            continue;
        }

        if (Attr.VendorID != TargetVid || Attr.ProductID != TargetPid)
        {
            CloseHandle(TestHandle);
            continue;
        }

        // Get caps so we know report sizes / usage
        PHIDP_PREPARSED_DATA Preparsed = nullptr;
        if (!HidD_GetPreparsedData(TestHandle, &Preparsed) || !Preparsed)
        {
            CloseHandle(TestHandle);
            continue;
        }

        HIDP_CAPS Caps;
        FMemory::Memzero(&Caps, sizeof(Caps));

        NTSTATUS Status = HidP_GetCaps(Preparsed, &Caps);
        HidD_FreePreparsedData(Preparsed);

        if (Status != HIDP_STATUS_SUCCESS)
        {
            CloseHandle(TestHandle);
            continue;
        }

        UE_LOG(LogDjiHid, Warning,
            TEXT("DJI candidate: VID=%04X PID=%04X UsagePage=0x%04X Usage=0x%04X  In=%d Out=%d Feat=%d"),
            Attr.VendorID, Attr.ProductID,
            Caps.UsagePage, Caps.Usage,
            Caps.InputReportByteLength,
            Caps.OutputReportByteLength,
            Caps.FeatureReportByteLength);

        DeviceHandle = TestHandle;
        InputReportLen = Caps.InputReportByteLength;

        UE_LOG(LogDjiHid, Warning,
            TEXT("DJI: Selected interface UsagePage=0x%04X Usage=0x%04X InputReportLen=%d"),
            Caps.UsagePage, Caps.Usage, InputReportLen);

        bFound = true;
        break;
    }

    SetupDiDestroyDeviceInfoList(DevInfoSet);

    if (!bFound)
    {
        UE_LOG(LogDjiHid, Warning, TEXT("DJI: No matching HID device found"));
    }
    else
    {
        UE_LOG(LogDjiHid, Warning, TEXT("DJI: Device opened, run loop will start reading"));
    }
}

void FDjiHidReader::CloseDevice()
{
    if (DeviceHandle)
    {
        HANDLE Handle = static_cast<HANDLE>(DeviceHandle);
        CloseHandle(Handle);
        DeviceHandle = nullptr;
        InputReportLen = 0;
    }
}

#endif // PLATFORM_WINDOWS

// ============================================================================
//  ParseReport – TEMP mapping until we refine it
// ============================================================================

void FDjiHidReader::ParseReport(const uint8* Data, int32 Len)
{
    if (!Data || Len <= 0)
    {
        return;
    }

    // Helper to read little-endian 16-bit values
    auto Read16 = [&](int Offset) -> uint16
        {
            if (Offset + 1 >= Len) return 0;
            return uint16(Data[Offset]) | (uint16(Data[Offset + 1]) << 8);
        };

    // *** TEMPORARY OFFSETS – WE WILL REFINE THESE FROM RAW LOGS ***
    uint16 RawYaw = Read16(1); // bytes 1–2
    uint16 RawThrottle = Read16(3); // bytes 3–4
    uint16 RawRoll = Read16(5); // bytes 5–6
    uint16 RawPitch = Read16(7); // bytes 7–8

    constexpr float Center = 2048.0f;  // placeholder centre
    constexpr float Span = 2048.0f;  // placeholder half-range

    auto Norm = [&](uint16 v) -> float
        {
            return FMath::Clamp((float(v) - Center) / Span, -1.0f, 1.0f);
        };

    FDjiChannels Local;
    Local.Yaw = Norm(RawYaw);
    Local.Roll = Norm(RawRoll);
    Local.Pitch = -Norm(RawPitch);                      // flip so stick fwd = nose-down
    Local.Throttle = 0.5f * (Norm(RawThrottle) + 1.0f);    // -1..1 → 0..1

    {
        FScopeLock Lock(&ChannelsMutex);
        Channels = Local;
    }

    UE_LOG(LogDjiHid, Warning,
        TEXT("DJI: Roll=%.3f Pitch=%.3f Yaw=%.3f Throttle=%.3f"),
        Local.Roll, Local.Pitch, Local.Yaw, Local.Throttle);
}
