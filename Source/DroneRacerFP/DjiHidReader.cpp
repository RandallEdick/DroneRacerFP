#include "DjiHidReader.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(LogDjiHid);

#if PLATFORM_WINDOWS

// Safe include of Windows headers in UE
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS

// DJI FPV Controller 2 VID/PID
static constexpr uint16 VendorId = 0x2CA3;
static constexpr uint16 ProductId = 0x1020;

// ---------------------------------------------------------
// Singleton
// ---------------------------------------------------------

FDjiHidReader& FDjiHidReader::Get()
{
    static FDjiHidReader Instance;
    return Instance;
}

FDjiHidReader::FDjiHidReader()
{
}

FDjiHidReader::~FDjiHidReader()
{
    // Do NOT call Stop() here.
    // Shutdown is handled explicitly by game code (EndPlay/GameInstance).
}

// ---------------------------------------------------------
// Public control
// ---------------------------------------------------------

void FDjiHidReader::Start()
{
    if (Thread != nullptr)
    {
        return; // already running
    }

    bStopRequested = false;

    Thread = FRunnableThread::Create(
        this,
        TEXT("DjiHidReaderThread"),
        0,
        TPri_Normal);

    if (!Thread)
    {
        UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create reader thread"));
    }
}

void FDjiHidReader::Stop()
{
    // Just signal the thread to exit its loop.
    bStopRequested = true;

    // IMPORTANT:
    // Do NOT wait on the thread or close the device here.
    // On process exit (packaged build), OS cleanup is enough.
    // In editor, this avoids shutting things down after engine systems are half-dead.

    // You can optionally do this in debug builds if you really want to try to wait:
    /*
    if (Thread)
    {
        Thread->Kill(true);  // force-kill; only safe now that destructor does not call Stop()
        delete Thread;
        Thread = nullptr;
    }
    */

    // And avoid calling CloseDevice() here; let OS tear down the handle.
    // CloseDevice();
}


// ---------------------------------------------------------
// FRunnable
// ---------------------------------------------------------

bool FDjiHidReader::Init()
{
    UE_LOG(LogDjiHid, Log, TEXT("DJI: Reader Init"));
    return true;
}

uint32 FDjiHidReader::Run()
{
#if !PLATFORM_WINDOWS
    UE_LOG(LogDjiHid, Warning, TEXT("DJI: HID reader only implemented on Windows"));
    return 0;
#else
    UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop starting"));

    TArray<uint8> Buffer;
    TArray<uint8> LastBuffer;

    bool bLoggedDeviceOff = false;

    while (!bStopRequested)
    {
        // 1) Ensure device is open
        if (DeviceHandle == nullptr || DeviceHandle == INVALID_HANDLE_VALUE)
        {
            if (!OpenDevice())
            {
                // Couldn’t open – wait a bit and try again, don’t spam logs.
                if (!bLoggedDeviceOff)
                {
                    UE_LOG(LogDjiHid, Warning,
                        TEXT("DJI: Could not open HID device (VID=%04X PID=%04X). "
                            "Controller may be off; will retry."),
                        VendorId, ProductId);
                    bLoggedDeviceOff = true;
                }

                FPlatformProcess::Sleep(0.5f);
                continue;
            }

            // Successfully opened
            bLoggedDeviceOff = false;
        }

        const uint32 ReportLen = (InputReportLen > 0) ? InputReportLen : 64u;
        if ((uint32)Buffer.Num() < ReportLen)
        {
            Buffer.SetNum(ReportLen);
        }

        DWORD BytesRead = 0;
        BOOL bOk = ::ReadFile(
            (HANDLE)DeviceHandle,
            Buffer.GetData(),
            ReportLen,
            &BytesRead,
            nullptr); // non-overlapped

        if (!bOk || BytesRead == 0)
        {
            const DWORD Err = ::GetLastError();

            // Device unplugged / powered off / reset.
            if (Err == ERROR_DEVICE_NOT_CONNECTED ||
                Err == ERROR_GEN_FAILURE ||
                Err == ERROR_OPERATION_ABORTED)
            {
                if (!bLoggedDeviceOff)
                {
                    UE_LOG(LogDjiHid, Warning,
                        TEXT("DJI: ReadFile device error (err=%lu). Controller likely off/disconnected; will retry."),
                        Err);
                    bLoggedDeviceOff = true;
                }

                CloseDevice();
            }
            else
            {
                // Other transient error - log occasionally.
                static double LastErrLogTime = 0.0;
                const double Now = FPlatformTime::Seconds();
                if (Now - LastErrLogTime > 1.0)
                {
                    UE_LOG(LogDjiHid, Warning,
                        TEXT("DJI: ReadFile failed (err=%lu, bytes=%lu)"),
                        Err, BytesRead);
                    LastErrLogTime = Now;
                }
            }

            FPlatformProcess::Sleep(0.01f);
            continue;
        }

        // Trim buffer to bytes actually read
        Buffer.SetNum(BytesRead, /*bAllowShrinking=*/false);

#if 0   // set to 1 to see raw bytes when they change
        if (LastBuffer.Num() != Buffer.Num() ||
            FMemory::Memcmp(LastBuffer.GetData(), Buffer.GetData(), Buffer.Num()) != 0)
        {
            LastBuffer = Buffer;

            FString BytesStr;
            for (int32 i = 0; i < Buffer.Num(); ++i)
            {
                BytesStr += FString::Printf(TEXT("%02X "), Buffer[i]);
            }
            UE_LOG(LogDjiHid, Verbose, TEXT("DJI RAW [%d]: %s"),
                Buffer.Num(), *BytesStr);
        }
#endif

        // Parse bytes into channels (this is your old, working mapping).
        ParseReport(Buffer.GetData(), Buffer.Num());
    }

    UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop exiting"));
    return 0;
#endif // PLATFORM_WINDOWS
}

void FDjiHidReader::Exit()
{
    UE_LOG(LogDjiHid, Log, TEXT("DJI: Reader Exit"));
}

// ---------------------------------------------------------
// Public: get channels
// ---------------------------------------------------------

FDjiChannels FDjiHidReader::GetChannels() const
{
    FScopeLock Lock(&ChannelsMutex);
    return Channels;
}

// ---------------------------------------------------------
// Windows HID helpers
// ---------------------------------------------------------

bool FDjiHidReader::OpenDevice()
{
#if !PLATFORM_WINDOWS
    return false;
#else
    CloseDevice();

    FString DevicePath;
    uint16 ReportLen = 0;
    if (!FindDevicePath(DevicePath, ReportLen))
    {
        return false;
    }

    HANDLE Handle = ::CreateFileW(
        *DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (Handle == INVALID_HANDLE_VALUE)
    {
        const DWORD Err = ::GetLastError();
        UE_LOG(LogDjiHid, Warning,
            TEXT("DJI: CreateFile failed (err=%lu) for %s"),
            Err, *DevicePath);
        return false;
    }

    DeviceHandle = Handle;
    InputReportLen = ReportLen;

    UE_LOG(LogDjiHid, Warning,
        TEXT("DJI: Device opened, InputReportLen=%u"), (uint32)InputReportLen);

    return true;
#endif
}

void FDjiHidReader::CloseDevice()
{
#if PLATFORM_WINDOWS
    if (DeviceHandle && DeviceHandle != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle((HANDLE)DeviceHandle);
    }
#endif
    DeviceHandle = nullptr;
    InputReportLen = 0;
}

/**
 * Enumerates HID devices, finds the DJI VID/PID, returns its path + report length.
 */
bool FDjiHidReader::FindDevicePath(FString& OutDevicePath, uint16& OutInputReportLen)
{
#if !PLATFORM_WINDOWS
    return false;
#else
    GUID HidGuid;
    HidD_GetHidGuid(&HidGuid);

    HDEVINFO DevInfo = SetupDiGetClassDevsW(
        &HidGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (DevInfo == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogDjiHid, Warning, TEXT("DJI: SetupDiGetClassDevs failed"));
        return false;
    }

    bool  bFound = false;
    DWORD Index = 0;

    while (!bFound)
    {
        SP_DEVICE_INTERFACE_DATA IfData;
        FMemory::Memzero(IfData);
        IfData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (!SetupDiEnumDeviceInterfaces(DevInfo, nullptr, &HidGuid, Index, &IfData))
        {
            break; // no more
        }
        ++Index;

        // Query required buffer size
        DWORD RequiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(DevInfo, &IfData, nullptr, 0, &RequiredSize, nullptr);
        if (RequiredSize == 0)
        {
            continue;
        }

        TArray<uint8> DetailBuffer;
        DetailBuffer.SetNum(RequiredSize);

        SP_DEVICE_INTERFACE_DETAIL_DATA_W* DetailData =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(DetailBuffer.GetData());
        DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
            DevInfo,
            &IfData,
            DetailData,
            RequiredSize,
            nullptr,
            nullptr))
        {
            continue;
        }

        HANDLE TestHandle = ::CreateFileW(
            DetailData->DevicePath,
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
        Attr.Size = sizeof(Attr);
        if (!HidD_GetAttributes(TestHandle, &Attr))
        {
            ::CloseHandle(TestHandle);
            continue;
        }

        // Check VID/PID
        if (Attr.VendorID == VendorId && Attr.ProductID == ProductId)
        {
            // Get capabilities to know report length
            PHIDP_PREPARSED_DATA Preparsed = nullptr;
            if (HidD_GetPreparsedData(TestHandle, &Preparsed))
            {
                HIDP_CAPS Caps;
                NTSTATUS Status = HidP_GetCaps(Preparsed, &Caps);
                if (Status == HIDP_STATUS_SUCCESS)
                {
                    OutInputReportLen = (uint16)Caps.InputReportByteLength;

                    UE_LOG(LogDjiHid, Warning,
                        TEXT("DJI: Found candidate VID=%04X PID=%04X UsagePage=0x%04X Usage=0x%04X In=%u"),
                        Attr.VendorID, Attr.ProductID,
                        Caps.UsagePage, Caps.Usage,
                        (uint32)Caps.InputReportByteLength);

                    // Take the first matching HID interface.
                    OutDevicePath = DetailData->DevicePath;
                    bFound = true;
                }

                HidD_FreePreparsedData(Preparsed);
            }
        }

        ::CloseHandle(TestHandle);

        if (bFound)
        {
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(DevInfo);

    if (!bFound)
    {
        UE_LOG(LogDjiHid, Warning,
            TEXT("DJI: No HID device with VID=%04X PID=%04X was found"),
            VendorId, ProductId);
    }

    return bFound;
#endif
}

// ---------------------------------------------------------
// Report parsing (your original, working mapping)
// ---------------------------------------------------------

/**
 * Mapping guess for the 12-byte packet:
 *   Byte 0: Report ID (ignored)
 *   Bytes 1-2: Roll    (int16, -32768..32767)
 *   Bytes 3-4: Pitch   (int16)
 *   Bytes 5-6: Yaw     (int16)
 *   Bytes 7-8: Throttle(int16, then remap to 0..1)
 * The remaining bytes are ignored for now.
 *
 * You can refine this later using RAW logs if needed.
 */
void FDjiHidReader::ParseReport(const uint8* Data, int32 Length)
{
    if (!Data || Length < 9)
    {
        return;
    }

    auto ReadAxis = [](const uint8* P) -> float
        {
            int16 Raw = (int16)((P[1] << 8) | P[0]); // little-endian
            float Norm = (float)Raw / 32767.0f;
            return FMath::Clamp(Norm, -1.f, 1.f);
        };

    // Skip Data[0] (report ID)
    float Roll = ReadAxis(Data + 1);
    float Pitch = ReadAxis(Data + 3);
    float Yaw = ReadAxis(Data + 5);
    float ThrotN = ReadAxis(Data + 7);       // -1..+1

    // Convert throttle to 0..1, clamp
    float Throttle01 = FMath::Clamp((ThrotN + 1.f) * 0.5f, 0.f, 1.f);

    {
        FScopeLock Lock(&ChannelsMutex);
        Channels.Roll = Roll;
        Channels.Pitch = Pitch;
        Channels.Yaw = Yaw;
        Channels.Throttle = Throttle01;
    }

    UE_LOG(LogDjiHid, Verbose,
        TEXT("DJI: Roll=%.3f Pitch=%.3f Yaw=%.3f Throttle=%.3f"),
        Roll, Pitch, Yaw, Throttle01);
}
