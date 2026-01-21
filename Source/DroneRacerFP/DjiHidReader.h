// DjiHidReader.h

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "Delegates/DelegateCombinations.h"
#include "Templates/UniquePtr.h"

class FRunnableThread;

DECLARE_LOG_CATEGORY_EXTERN(LogDjiHid, Log, All);

class FDjiHidReader : public FRunnable
{
public:

	/** Delegate fired whenever we receive an input report from the device. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInputReport, const TArray<uint8>&);

	// ------------------ NEW: Channel struct ------------------
	struct FDjiChannels
	{
		// Raw integer values from HID (you can adapt types as needed)
		int16 LeftXRaw = 0;
		int16 LeftYRaw = 0;
		int16 RightXRaw = 0;
		int16 RightYRaw = 0;

		// Normalized 0..1 (or -1..1) versions
		float LeftX01 = 0.f;
		float LeftY01 = 0.f;
		float RightX01 = 0.f;
		float RightY01 = 0.f;

		// Throttle or other channels if you need them
		float Throttle01 = 0.f;
	};

	/** Singleton accessor. Creates the instance on first use with default args. */
	static FDjiHidReader& Get();

	/** Returns true if the singleton instance has already been created. */
	static bool IsCreated();

	FDjiHidReader(const FString& InDevicePath = TEXT(""), uint32 InInputReportLen = 0u);
	virtual ~FDjiHidReader();

	bool Start();
	void Shutdown();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Multicast delegate broadcast from the worker thread with each HID report. */
	FOnInputReport OnInputReport;
	FOnInputReport& GetOnInputReport() { return OnInputReport; }

	void SetDevicePath(const FString& InPath) { DevicePath = InPath; }
	void SetInputReportLen(uint32 InLen) { InputReportLen = InLen; }

	// ------------------ NEW: GetChannels API ------------------
	FDjiChannels GetChannels() const;

private:

	// Singleton state
	static TUniquePtr<FDjiHidReader> Instance;
	static FCriticalSection          InstanceMutex;

	// Instance state
	FString       DevicePath;
	uint32        InputReportLen;
	FRunnableThread* Thread;
	FThreadSafeBool  bStopRequested;

	// ------------------ NEW: Channels and mutex ------------------
	mutable FCriticalSection ChannelsMutex;
	FDjiChannels             Channels;

#if PLATFORM_WINDOWS
	// Opaque Win32 handles
	void* DeviceHandle;   // HANDLE
	void* StopEvent;      // HANDLE

	bool OpenDevice();
	void CloseWindowsHandles();
	/** Attempt to find a DJI HID device (VID_2CA3, PID_1020) and output its path. */
	bool DiscoverDjiDevicePath(FString& OutPath);
#endif
};
