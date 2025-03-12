#include <stdint.h>
#include <windows.h>
#include <devguid.h>
#include <regstr.h>
#include <setupapi.h>
#include <stdio.h>
#include <conio.h>

#include "FluidReality.h"




HANDLE fluidSerialHandle;
uint16_t fluidSerialPortNr = 0;

#define NUM_DRIVERS			 1
#define NUM_BYTES_PER_DRIVER 8

const char driverOrder[NUM_DRIVERS] = { 0 };


bool scan_ports(wchar_t* outComPort, size_t outSize, char* vid, char* pid)
{

	bool found = false;

	SP_DEVINFO_DATA device_data = { sizeof device_data };
	HDEVINFO device_list =
		SetupDiGetClassDevsA(&GUID_DEVINTERFACE_COMPORT, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	DWORD error = GetLastError();
	if (!device_list && device_list == INVALID_HANDLE_VALUE) return false;

	bool ended = false;
	wchar_t friendly_name[128];
	for (int i = 0; i < 6000 && !ended; i++) {
		if (SetupDiEnumDeviceInfo(device_list, i, &device_data)) {

			char deviceInstanceID[MAX_PATH];
			DWORD size = 0;

			if (SetupDiGetDeviceInstanceIdA(device_list, &device_data, deviceInstanceID, sizeof(deviceInstanceID), &size)) {
				printf("Device Found: %s\n", deviceInstanceID);

				// Check if VID & PID match
				if (strstr(deviceInstanceID, vid) && strstr(deviceInstanceID, pid)) {
					printf("Matching device found: %s\n", deviceInstanceID);


					if (SetupDiGetDeviceRegistryPropertyW(device_list, &device_data, SPDRP_FRIENDLYNAME, nullptr,
						reinterpret_cast<PBYTE>(friendly_name), sizeof friendly_name, nullptr)) {

						// Extract COM port from friendly name
						wchar_t* comPort = wcsstr(friendly_name, L"COM");

						if (comPort) {
							wchar_t extractedCom[24];

							// Copy only "COMx" (skip extra characters like ')')
							int i = 0;
							while (comPort[i] && iswalnum(comPort[i]) &&
								i < (sizeof(extractedCom) / sizeof(extractedCom[0])) - 1) {
								extractedCom[i] = comPort[i];
								i++;
							}
							extractedCom[i] = L'\0'; // Null-terminate string


							if (swprintf_s(outComPort, outSize, L"\\\\.\\%s", extractedCom) > 0) {
								wprintf(L"Device COM Port: %s\n", outComPort);
								//wcsncpy(outComPort, extractedCom, outSize - 1); // Copy COM port to output buffer
								outComPort[outSize - 1] = L'\0';		   // Ensure null termination
								found = true;
								break;
							}
						}
						else {
							printf("COM port not found in Friendly Name\n");
						}
					}
				}
			}
		}
		else {
			ended = (GetLastError() == ERROR_NO_MORE_ITEMS);
		}
	}

	SetupDiDestroyDeviceInfoList(device_list);
	return found;
}


int initFluidReality()
{

	wchar_t comPort[64] = L""; // Buffer to store the COM port

	char vid[] = "VID_16C0";
	char pid[] = "PID_0483";



	if (scan_ports(comPort, sizeof(comPort) / sizeof(comPort[0]), vid, pid))
	{
		wprintf(L"COM Port for Fluid Haptics Found: %s\n", comPort);
	}
	else
	{
		wprintf(L"No matching COM port for Fluid Haptics found.\n");
		return -1;
	}

	fluidSerialHandle = CreateFileW(comPort, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (fluidSerialHandle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		printf("Error opening COM port: %lu\n", err);
		return -1;
	}

	// Do some basic settings
	DCB serialParams = { 0 };
	serialParams.DCBlength = sizeof(serialParams);

	if (!GetCommState(fluidSerialHandle, &serialParams)) {
		printf("Error getting serial state: %lu\n", GetLastError());
		CloseHandle(fluidSerialHandle);
		return -1;
	}

	serialParams.BaudRate = 250000;
	serialParams.ByteSize = 8;
	serialParams.StopBits = ONESTOPBIT;
	serialParams.Parity = NOPARITY;

	if (!SetCommState(fluidSerialHandle, &serialParams)) {
		printf("Error setting serial state: %lu\n", GetLastError());
		CloseHandle(fluidSerialHandle);
		return -1;
	}

	PurgeComm(fluidSerialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

	printf("COM port opened successfully!\n");

	return 0;
}

void exitFluidReality()
{
	// close the com port
	CloseHandle(fluidSerialHandle);
}


int EnablePSU()
{
	DWORD bytesWritten;

	char enablePSU[] = { 0xaa, 0xe1, 0x01, 0xcc, 0x88, 0xc8, 0x8c };

	for (int i = 0; i < sizeof(enablePSU); i++) {
		if (!WriteFile(fluidSerialHandle, &enablePSU[i], sizeof(enablePSU[i]), &bytesWritten, NULL)) {
			printf("Error writing to COM port: %lu\n", GetLastError());
			return -1;
		}
		else {
			printf("Sent %lu bytes: %s\n", bytesWritten, enablePSU);
		}
	}
	char zeroes[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	setFluidValues(zeroes);

	return 0;
}

int DisablePSU()
{
	DWORD bytesWritten;

	char zeroes[8] = { 0,0,0,0,0,0,0,0 };
	setFluidValues(zeroes);

	char disablePSU[] = { 0xaa, 0xe1, 0x00, 0xcc, 0x88, 0xc8, 0x8c };

	for (int i = 0; i < sizeof(disablePSU); i++) {
		if (!WriteFile(fluidSerialHandle, &disablePSU[i], sizeof(disablePSU[i]), &bytesWritten, NULL)) {
			printf("Error writing to COM port: %lu\n", GetLastError());
			return -1;
		}
		else {
			printf("Sent %lu bytes: %s\n", bytesWritten, disablePSU);
		}
	}
	return 0;
}

int setFluidValues(char values[8])
{
	char check = 0xFF;
	DWORD bytesWritten;

	char weVibin[] = { 0xaa, 0xac, 0x0, 0xFF, 0xcc, 0x88, 0xc8, 0x8c };
	char vals[1][8] = { 0 };
	vals[0][0] = values[0];
	vals[0][1] = values[1];
	vals[0][2] = values[2];
	vals[0][3] = values[3];
	vals[0][4] = values[4];
	vals[0][5] = values[5];
	vals[0][6] = values[6];
	vals[0][7] = values[7];

	for (int i = 0; i < NUM_DRIVERS; i++) {

		for (int j = 0; j < sizeof(weVibin); j++) {
			if (!(weVibin[j] == check)) {
				if (!WriteFile(fluidSerialHandle, &weVibin[j], sizeof(weVibin[j]), &bytesWritten, NULL)) {
					printf("Error writing to COM port: %lu\n", GetLastError());
				}
				else {
					printf("Sent %lu bytes: ", bytesWritten);
					for (int z = 0; z < sizeof(weVibin[j]); z++) {
						printf("%d, ", weVibin[j]);
					}
					printf("\n");
				}
			}
			else {
				if (!WriteFile(fluidSerialHandle, &vals[i], sizeof(vals[i]), &bytesWritten, NULL)) {
					printf("Error writing to COM port: %lu\n", GetLastError());
				}
				else {
					printf("Sent %lu bytes: ", bytesWritten);
					for (int z = 0; z < sizeof(values); z++) {
						printf("%d, ", values[z]);
					}
					unsigned char ch = (unsigned char)values[0];
					printf("     %u   ", (unsigned int)ch);
					printf("\n");
				}
			}
		}
	}
	printf("\n");
	return 0;
}

