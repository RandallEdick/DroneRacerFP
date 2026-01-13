#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "HAL/RunnableThread.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDjiHid, Log, All);

// Simple normalized channel bundle for the drone.
struct FDjiChannels
{
    float Roll = 0.f;  // -1..+1
    float Pitch = 0.f;  // -1..+1
    float Yaw = 0.f;  // -1..+1
    float Throttle = 0.f;  // 0..1
};

/**
 * FDjiHidReader
 *
 * - Singleton FRunnable that reads the DJI FPV Controller 2 via HID.
 * - Updates FDjiChannels, which your DroneFPCharacter reads via GetChannels().
 * - Safe Stop() (no Kill(true)), and mild logging when device is disconnected.
 */
class FDjiHidReader : public FRunnable
{
public:
    static FDjiHidReader& Get();

    /** Start the background reader thread (no-op if already running). */
    void Start();

    /** Stop the background reader thread and close the device. */
    void Stop();

    /** Thread entry points. */
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Exit() override;

    /** Get a snapshot of the current channels (thread-safe copy). */
    FDjiChannels GetChannels() const;

private:
    FDjiHidReader();
    ~FDjiHidReader();

    // Non-copyable
    FDjiHidReader(const FDjiHidReader&) = delete;
    FDjiHidReader& operator=(const FDjiHidReader&) = delete;

private:
    // HID helpers
    bool OpenDevice();
    void CloseDevice();
    bool FindDevicePath(FString& OutDevicePath, uint16& OutInputReportLen);

    void ParseReport(const uint8* Data, int32 Length);

private:
    // Thread stuff
    FRunnableThread* Thread = nullptr;
    volatile bool    bStopRequested = false;

    // HID device
    void* DeviceHandle = nullptr;
    uint16 InputReportLen = 0;

    // Channel state
    mutable FCriticalSection ChannelsMutex;
    FDjiChannels             Channels;
};
