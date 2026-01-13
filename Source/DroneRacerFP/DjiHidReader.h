#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDjiHid, Log, All);

/** Normalized stick channels from the radio. */
struct FDjiChannels
{
    float Roll = 0.f;   // -1 .. +1
    float Pitch = 0.f;   // -1 .. +1
    float Yaw = 0.f;   // -1 .. +1
    float Throttle = 0.f;   //  0 .. +1 (DJI-style)
};

/**
 * Singleton background reader that talks directly to the DJI HID device.
 * Usage:
 *   FDjiHidReader::Get().Start();
 *   ...
 *   FDjiChannels Ch = FDjiHidReader::Get().GetChannels();
 *   ...
 *   FDjiHidReader::Get().Stop();
 */
class FDjiHidReader : public FRunnable
{
public:
    static FDjiHidReader& Get();

    /** Starts the background thread (safe to call multiple times). */
    void Start();

    /** Requests the thread to stop and waits for it to exit. */
    virtual void Stop() override;

    /** FRunnable: called on worker thread before Run. */
    virtual bool Init() override;

    /** FRunnable: main loop. */
    virtual uint32 Run() override;

    /** FRunnable: called on worker thread when exiting. */
    virtual void Exit() override;

    /** Returns the latest channels (thread-safe copy). */
    FDjiChannels GetChannels() const;

private:
    FDjiHidReader();
    ~FDjiHidReader();

    FDjiHidReader(const FDjiHidReader&) = delete;
    FDjiHidReader& operator=(const FDjiHidReader&) = delete;

    // ---- Windows HID helpers ----
    bool OpenDevice();
    void CloseDevice();
    bool FindDevicePath(FString& OutDevicePath, uint16& OutInputReportLen);
    void ParseReport(const uint8* Data, int32 Length);

private:
    FRunnableThread* Thread = nullptr;
    FThreadSafeBool  bStopRequested = false;

    // Handle to the HID device (Windows only).
    void* DeviceHandle = nullptr;   // actually HANDLE, but kept as void* to avoid leaking Windows headers

    // Expected input report length (bytes).
    uint16 InputReportLen = 0;

    // Latest channels.
    mutable FCriticalSection ChannelsMutex;
    FDjiChannels Channels;

    // VID/PID for the DJI HID interface (the “HID Interface” device you saw).
    static constexpr uint16 VendorId = 0x2CA3;
    static constexpr uint16 ProductId = 0x1020;
};
