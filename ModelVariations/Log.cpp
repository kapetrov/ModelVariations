#include "Log.hpp"
#include "Helpers.hpp"

#include <cstdarg>
#include <mutex>
#include <set>

#include <Windows.h>

HANDLE logfile = INVALID_HANDLE_VALUE;
std::set<std::uintptr_t> modifiedAddresses;
std::mutex logMutex;

bool Log::Open(const std::string &filename)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(logfile);
		logfile = INVALID_HANDLE_VALUE;
	}
	
	logfile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);

	modifiedAddresses.clear();
	return logfile != INVALID_HANDLE_VALUE;
}

bool Log::Close()
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE)
		return true;

	auto retVal = CloseHandle(logfile);
	logfile = INVALID_HANDLE_VALUE;
	return retVal;
}

bool Log::Write(const char* format, ...)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	va_list argptr;
	va_start(argptr, format);

	auto out = mvsprintf(format, argptr);

	va_end(argptr);

	if (out.empty())
		return false;

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, out.data(), out.size(), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (out.size() != bytesWritten)
		return false;

	return true;
}

bool Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE || modifiedAddresses.contains(address))
		return false;

	va_list argptr;
	va_start(argptr, format);

	auto out = mvsprintf(format, argptr);

	va_end(argptr);

	if (out.empty())
		return false;

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, out.data(), out.size(), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (out.size() != bytesWritten)
		return false;

	modifiedAddresses.insert(address);		

	return true;
}
