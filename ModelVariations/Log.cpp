#include "Log.hpp"
#include "Helpers.hpp"

#include <cstdarg>
#include <set>

#include <Windows.h>

HANDLE logfile = INVALID_HANDLE_VALUE;
std::set<std::uintptr_t> modifiedAddresses;
bool verboseStatus = false;

bool Log::Open(const std::string &filename, bool verbose)
{
	if (logfile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(logfile);
		logfile = INVALID_HANDLE_VALUE;
	}
	
	logfile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);

	modifiedAddresses.clear();
	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	verboseStatus = verbose;
	return true;
}

bool Log::Close()
{
	if (logfile == INVALID_HANDLE_VALUE)
		return true;

	auto retVal = CloseHandle(logfile);
	logfile = INVALID_HANDLE_VALUE;
	return retVal;
}

bool WriteImpl(const char* format, va_list args)
{
	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	auto out = mvsprintf(format, args);

	if (out.empty())
		return false;

	DWORD bytesWritten = 0;

	if (!WriteFile(logfile, out.data(), static_cast<DWORD>(out.size()), &bytesWritten, nullptr) && GetLastError() != ERROR_IO_PENDING)
		return false;

	return out.size() == bytesWritten;
}

bool Log::Write(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	const bool result = WriteImpl(format, args);

	va_end(args);
	return result;
}

bool Log::WriteVerbose(const char* format, ...)
{
	if (!verboseStatus)
		return false;

	va_list args;
	va_start(args, format);

	const bool result = WriteImpl(format, args);

	va_end(args);
	return result;
}

bool Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
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
