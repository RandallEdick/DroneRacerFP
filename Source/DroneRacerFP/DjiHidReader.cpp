// DjiHidReader.cpp

#include "DjiHidReader.h"

#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogDjiHid);

TUniquePtr<FDjiHidReader> FDjiHidReader::Instance;
FCriticalSection          FDjiHidReader::InstanceMutex;

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <dbt.h>
#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS

// ============================================================================
// Singleton
// ============================================================================
static FString HexDump(const uint8* Data, int32 Len)
{
	FString S;
	for (int32 i = 0; i < Len; ++i)
	{
		S += FString::Printf(TEXT("%02X "), Data[i]);
	}
	return S;
}

FDjiHidReader& FDjiHidReader::Get()
{
	FScopeLock Lock(&InstanceMutex);

	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FDjiHidReader>();
	}
	return *Instance.Get();
}

bool FDjiHidReader::IsCreated()
{
	FScopeLock Lock(&InstanceMutex);
	return Instance.IsValid();
}

// ============================================================================
// FDjiHidReader
// ============================================================================

FDjiHidReader::FDjiHidReader(const FString& InDevicePath, uint32 InInputReportLen)
	: DevicePath(InDevicePath)
	, InputReportLen(InInputReportLen)
	, Thread(nullptr)
	, bStopRequested(false)
#if PLATFORM_WINDOWS
	, DeviceHandle(nullptr)
	, StopEvent(nullptr)
#endif
{
}

FDjiHidReader::~FDjiHidReader()
{
	Shutdown();
}

bool FDjiHidReader::Start()
{
	if (Thread)
	{
		return true; // already running
	}

#if PLATFORM_WINDOWS
	if (!StopEvent)
	{
		HANDLE NewStop = CreateEvent(nullptr, true, false, nullptr);
		if (!NewStop)
		{
			UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create StopEvent. GetLastError=%d"), GetLastError());
			return false;
		}
		StopEvent = NewStop;
	}
#endif

	bStopRequested.AtomicSet(false);

	Thread = FRunnableThread::Create(
		this,
		TEXT("FDjiHidReaderThread"),
		0,
		TPri_Normal
	);

	if (!Thread)
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create FDjiHidReader thread."));
#if PLATFORM_WINDOWS
		if (StopEvent)
		{
			CloseHandle(static_cast<HANDLE>(StopEvent));
			StopEvent = nullptr;
		}
#endif
		return false;
	}

	return true;
}

void FDjiHidReader::Shutdown()
{
	if (!Thread)
	{
#if PLATFORM_WINDOWS
		CloseWindowsHandles();
#endif
		return;
	}

	bStopRequested.AtomicSet(true);

#if PLATFORM_WINDOWS
	if (StopEvent)
	{
		SetEvent(static_cast<HANDLE>(StopEvent));
	}
#endif

	Thread->WaitForCompletion();
	delete Thread;
	Thread = nullptr;

#if PLATFORM_WINDOWS
	CloseWindowsHandles();
#endif
}

void FDjiHidReader::Stop()
{
	bStopRequested.AtomicSet(true);

#if PLATFORM_WINDOWS
	if (StopEvent)
	{
		SetEvent(static_cast<HANDLE>(StopEvent));
	}
#endif
}

uint32 FDjiHidReader::Run()
{
#if !PLATFORM_WINDOWS
	UE_LOG(LogDjiHid, Warning, TEXT("DJI: FDjiHidReader only implemented for Windows."));
	return 0;
#else
	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop starting"));

	if (!OpenDevice())
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to open HID device: %s"), *DevicePath);
		return 0;
	}

	const uint32 ReportLen = (InputReportLen > 0u) ? InputReportLen : 64u;

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(ReportLen);

	while (!bStopRequested)
	{
		HANDLE Handle = static_cast<HANDLE>(DeviceHandle);
		if (!Handle)
		{
			UE_LOG(LogDjiHid, Warning, TEXT("DJI: DeviceHandle is null, exiting read loop."));
			break;
		}

		OVERLAPPED Overlapped = {};
		Overlapped.hEvent = CreateEvent(nullptr, true, false, nullptr);
		if (!Overlapped.hEvent)
		{
			UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create overlapped event. GetLastError=%d"), GetLastError());
			break;
		}

		DWORD BytesRead = 0;
		BOOL bReadOk = ReadFile(
			Handle,
			Buffer.GetData(),
			ReportLen,
			&BytesRead,
			&Overlapped
		);

		if (!bReadOk)
		{
			const DWORD Err = GetLastError();

			if (Err == ERROR_IO_PENDING)
			{
				HANDLE WaitHandles[2];
				DWORD HandleCount = 0;

				WaitHandles[HandleCount++] = Overlapped.hEvent;

				HANDLE StopHandle = static_cast<HANDLE>(StopEvent);
				if (StopHandle)
				{
					WaitHandles[HandleCount++] = StopHandle;
				}

				const DWORD WaitRes = WaitForMultipleObjects(
					HandleCount,
					WaitHandles,
					false,      // bWaitAll = false
					INFINITE
				);

				if (WaitRes == WAIT_OBJECT_0)
				{
					// Overlapped I/O completed
					if (!GetOverlappedResult(Handle, &Overlapped, &BytesRead, false))
					{
						const DWORD ResErr = GetLastError();
						UE_LOG(LogDjiHid, Warning,
							TEXT("DJI: GetOverlappedResult failed. Err=%d"),
							ResErr);

						CloseHandle(Overlapped.hEvent);

						// Hard disconnect: bail out
						if (ResErr == ERROR_DEVICE_NOT_CONNECTED)
						{
							UE_LOG(LogDjiHid, Warning,
								TEXT("DJI: Device disconnected during overlapped read (Err=%d). Exiting read loop."),
								ResErr);
							break;
						}

						// Generic failure (e.g. ERR=31): log and keep trying
						if (ResErr == ERROR_GEN_FAILURE)
						{
							UE_LOG(LogDjiHid, Warning,
								TEXT("DJI: Generic failure during overlapped read (Err=%d). Continuing."),
								ResErr);
							if (bStopRequested)
							{
								break;
							}
							continue;
						}

						// Other errors: log and continue unless we're stopping
						if (bStopRequested)
						{
							break;
						}
						continue;
					}
				}
				else if (StopHandle && WaitRes == WAIT_OBJECT_0 + 1)
				{
					// StopEvent signaled
					CancelIoEx(Handle, &Overlapped);
					CloseHandle(Overlapped.hEvent);
					UE_LOG(LogDjiHid, Verbose, TEXT("DJI: Stop event signaled; breaking read loop."));
					break;
				}
				else
				{
					// Unexpected wait result
					UE_LOG(LogDjiHid, Warning,
						TEXT("DJI: WaitForMultipleObjects returned 0x%08X"),
						WaitRes);
					CancelIoEx(Handle, &Overlapped);
					CloseHandle(Overlapped.hEvent);
					break;
				}
			}
			else
			{
				// Non-overlapped error path
				UE_LOG(LogDjiHid, Warning,
					TEXT("DJI: ReadFile failed immediately. Err=%d"),
					Err);

				CloseHandle(Overlapped.hEvent);

				// Hard disconnect → exit
				if (Err == ERROR_DEVICE_NOT_CONNECTED)
				{
					UE_LOG(LogDjiHid, Warning,
						TEXT("DJI: Device not connected (Err=%d). Exiting read loop."),
						Err);
					break;
				}

				// Generic failure (31) → log and keep trying
				if (Err == ERROR_GEN_FAILURE)
				{
					UE_LOG(LogDjiHid, Warning,
						TEXT("DJI: Generic failure (Err=%d). Keeping read loop alive."),
						Err);
					if (bStopRequested)
					{
						break;
					}
					continue;
				}

				// Other errors: log and continue unless we’re stopping
				if (bStopRequested)
				{
					break;
				}
				continue;
			}
		}

		// Done with the overlapped event
		CloseHandle(Overlapped.hEvent);

		if (bStopRequested)
		{
			break;
		}

		if (BytesRead > 0)
		{
			TArray<uint8> Copy;
			Copy.Append(Buffer.GetData(), BytesRead);

			// Debug: dump raw packet (you can change Warning -> Verbose later)
			UE_LOG(LogDjiHid, Warning,
				TEXT("DJI: Report (%d bytes): %s"),
				BytesRead,
				*HexDump(Copy.GetData(), Copy.Num()));

			// Broadcast to any listeners (if anyone else is bound)
			OnInputReport.Broadcast(Copy);

			// ------------------ BYTE-ALIGNED DJI MAPPING ------------------
			{
				FScopeLock Lock(&ChannelsMutex);

				if (Copy.Num() >= 13)
				{
					// Based on A4 flickering, your stick data actually starts 
					// later in the packet than the standard DJI documentation suggests.
					// We will skip the first 3 bytes (Header) and start at Byte 4.

					uint16 RawCh1 = ((uint16)Copy[3] | ((uint16)Copy[4] << 8)) & 0x07FF;
					uint16 RawCh2 = (((uint16)Copy[4] >> 3) | ((uint16)Copy[5] << 5)) & 0x07FF;
					uint16 RawCh3 = (((uint16)Copy[5] >> 6) | ((uint16)Copy[6] << 2) | ((uint16)Copy[7] << 10)) & 0x07FF;
					uint16 RawCh4 = (((uint16)Copy[7] >> 1) | ((uint16)Copy[8] << 7)) & 0x07FF;

					auto NormalizeDji = [](uint16 Raw) -> float {
						// DJI Standard: 1024 is center. Range 364 to 1684.
						float Val = (static_cast<float>(Raw) - 1024.f) / 660.f;
						if (FMath::Abs(Val) < 0.05f) Val = 0.0f; // Deadzone
						return FMath::Clamp(Val, -1.0f, 1.0f);
						};

					// If A4 was the one moving, RawCh4 is likely your Throttle or Yaw.
					float Val4 = NormalizeDji(RawCh4);

					// Let's test this specific mapping:
					Channels.LeftY01 = Val4;
					Channels.Throttle01 = (Val4 + 1.0f) * 0.5f;

					static int32 LogTick = 0;
					if (++LogTick % 20 == 0)
					{
						UE_LOG(LogDjiHid, Warning, TEXT("REALIGN -> T: %.2f | Raw4: %u | A4: %u"),
							Channels.Throttle01, RawCh4, ((uint16)Copy[7] | ((uint16)Copy[8] << 8)));
					}
				}
			}

		}

	}

	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop exiting"));

	return 0;
#endif // PLATFORM_WINDOWS
}

void FDjiHidReader::Exit()
{
	// Nothing special; cleanup in Shutdown / destructor.
}

// ============================================================================
// Channels API
// ============================================================================

FDjiHidReader::FDjiChannels FDjiHidReader::GetChannels() const
{
	FScopeLock Lock(&ChannelsMutex);
	return Channels; // return a copy
}

// ============================================================================
// Windows-specific helpers
// ============================================================================

#if PLATFORM_WINDOWS

bool FDjiHidReader::OpenDevice()
{
	if (DeviceHandle)
	{
		return true; // already open
	}

	// If no path has been set explicitly, try to auto-discover the DJI controller.
	if (DevicePath.IsEmpty())
	{
		FString FoundPath;
		if (!DiscoverDjiDevicePath(FoundPath))
		{
			UE_LOG(LogDjiHid, Error, TEXT("DJI: DevicePath is empty and auto-discovery failed."));
			return false;
		}

		DevicePath = FoundPath;
	}

	const FString& Path = DevicePath;
	if (Path.IsEmpty())
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: DevicePath is empty in OpenDevice()."));
		return false;
	}

	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Opening device path: %s"), *Path);

	HANDLE Handle = CreateFileW(
		*Path,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr
	);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		const DWORD Err = GetLastError();
		UE_LOG(LogDjiHid, Error, TEXT("DJI: CreateFileW failed for HID device. Err=%d"), Err);
		DeviceHandle = nullptr;
		return false;
	}

	DeviceHandle = Handle;

	// (Optional) Query HID capabilities here and adjust InputReportLen

	return true;
}
bool FDjiHidReader::DiscoverDjiDevicePath(FString& OutPath)
{
	OutPath.Empty();

	// HID class GUID
	GUID HidGuid;
	HidD_GetHidGuid(&HidGuid);

	// Get a handle to all HID-class devices present
	HDEVINFO DeviceInfoSet = SetupDiGetClassDevs(
		&HidGuid,
		nullptr,
		nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
	);

	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: SetupDiGetClassDevs failed. GetLastError=%d"), GetLastError());
		return false;
	}

	SP_DEVICE_INTERFACE_DATA InterfaceData;
	FMemory::Memzero(InterfaceData);
	InterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	// We’ll look for VID_2CA3 & PID_1020 in the device path
	const FString TargetVidPid = TEXT("vid_2ca3&pid_1020");

	bool bFound = false;

	for (DWORD Index = 0; ; ++Index)
	{
		if (!SetupDiEnumDeviceInterfaces(
			DeviceInfoSet,
			nullptr,
			&HidGuid,
			Index,
			&InterfaceData))
		{
			const DWORD Err = GetLastError();
			if (Err != ERROR_NO_MORE_ITEMS)
			{
				UE_LOG(LogDjiHid, Warning,
					TEXT("DJI: SetupDiEnumDeviceInterfaces stopped with error %d at index %d"),
					Err, Index);
			}
			break; // done enumerating
		}

		// First call: get required buffer size
		DWORD RequiredSize = 0;
		SetupDiGetDeviceInterfaceDetail(
			DeviceInfoSet,
			&InterfaceData,
			nullptr,
			0,
			&RequiredSize,
			nullptr);

		if (RequiredSize == 0)
		{
			continue;
		}

		TArray<uint8> DetailDataBuffer;
		DetailDataBuffer.SetNumUninitialized(RequiredSize);

		auto* DetailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(DetailDataBuffer.GetData());
		DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		if (!SetupDiGetDeviceInterfaceDetail(
			DeviceInfoSet,
			&InterfaceData,
			DetailData,
			RequiredSize,
			nullptr,
			nullptr))
		{
			UE_LOG(LogDjiHid, Warning,
				TEXT("DJI: SetupDiGetDeviceInterfaceDetail failed. Err=%d"),
				GetLastError());
			continue;
		}

		// DetailData->DevicePath is a NULL-terminated wide string
		const FString ThisPath(DetailData->DevicePath);

		FString LowerPath = ThisPath.ToLower();

		if (LowerPath.Contains(TargetVidPid))
		{
			UE_LOG(LogDjiHid, Warning, TEXT("DJI: Auto-discovered DJI HID path: %s"), *ThisPath);
			OutPath = ThisPath;
			bFound = true;
			break; // pick first match
		}
	}

	SetupDiDestroyDeviceInfoList(DeviceInfoSet);

	if (!bFound)
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: Could not find any HID device matching %s"), *TargetVidPid);
	}

	return bFound;
}

void FDjiHidReader::CloseWindowsHandles()
{
	HANDLE Handle = static_cast<HANDLE>(DeviceHandle);
	HANDLE StopHandle = static_cast<HANDLE>(StopEvent);

	if (Handle)
	{
		CancelIoEx(Handle, nullptr);
		CloseHandle(Handle);
		DeviceHandle = nullptr;
	}

	if (StopHandle)
	{
		CloseHandle(StopHandle);
		StopEvent = nullptr;
	}
}

#endif // PLATFORM_WINDOWS
