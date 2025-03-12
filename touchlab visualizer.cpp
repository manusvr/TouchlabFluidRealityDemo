#include <windows.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <sstream>
#include <string>
#include <atomic>
#include <chrono>
#include <algorithm> 
#include <commctrl.h>

#include "FluidReality.h"

#define GRID_SIZE 4
#define CELL_SIZE 100
#define PRESSURE_MIN 0
#define PRESSURE_MAX 6500
#define PRESSURE_SCALED_MIN 0
#define PRESSURE_SCALED_MAX 255

// Custom clamp function
template <typename T>
T clamp(T value, T minValue, T maxValue) {
    return (value < minValue) ? minValue : (value > maxValue) ? maxValue : value;
}

float scalingStart(1.5f);
float offsetStart(120.0f);

std::mutex frameMutex;
std::vector<float> latestFrame(16, 0.0f);
std::vector<float> tareValues(16, 0.0f);
std::atomic<bool> running(true);
std::atomic<bool> christina(true);
std::atomic<bool> hasTare(false);
std::atomic<float> scalingFactor(1.0f);
std::atomic<float> offsetValue(0.0f);
std::atomic<float> latestActuationValue(0.0f);
HWND hwnd, button, slider, valueField, sliderValueText,offsetSlider, offsetValueText;


void AttachConsoleWindow() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        std::cout << "Console attached successfully!\n";
    }
}

COLORREF GetPressureColor(float pressure, float tare) {
    float adjustedPressure = pressure - tare;
    adjustedPressure = clamp(adjustedPressure, (float)PRESSURE_MIN, (float)PRESSURE_MAX);
    float normalized = (adjustedPressure - PRESSURE_MIN) / (PRESSURE_MAX - PRESSURE_MIN);
    int red = static_cast<int>(normalized * 255);
    int blue = 255 - red;
    return RGB(red, 0, blue);
}

COLORREF GetActuatorColor(float actuatorValue, float offset) {
    // Compute the actual range considering offset
    float rangeMin = offset;      // Start of range
    float rangeMax = 255.0f;      // End of range (always 255)

    // Adjust the value within this new range
    float adjustedActuator = actuatorValue - rangeMin;

    // Clamp within the valid range
    adjustedActuator = max(0.0f, min(rangeMax - rangeMin, adjustedActuator));

    // Normalize the adjusted range to [0,1]
    float normalized = adjustedActuator / (rangeMax - rangeMin);

    // Compute color gradient (Blue to Red)
    int red = static_cast<int>(normalized * 255);
    int blue = 255 - red;

    return RGB(red, 0, blue);
}



uint8_t mapPressureToActuator(float pressureValue) {

    float averageTare = 0.0f;
    {
        for (float val : tareValues) averageTare += val;
        averageTare /= tareValues.size();
    }

    // Apply scaling to increase responsiveness
    double scaledValue = ((pressureValue-averageTare) / 6500.0) * 254 * scalingFactor;

    // Map to actuator range, ensuring it doesn't exceed ACTUATOR_MAX
    uint8_t actuatorValue = static_cast<uint8_t>(clamp(scaledValue + offsetValue,
        static_cast<double>(PRESSURE_SCALED_MIN),
        static_cast<double>(PRESSURE_SCALED_MAX)));

    return actuatorValue;
}

void OnWindowClose() {
    std::cout << "Window is closing! Cleaning up..." << std::endl;
    // Add any cleanup code here
    DisablePSU();
    exitFluidReality();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        std::lock_guard<std::mutex> lock(frameMutex);
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int x = 0; x < GRID_SIZE; ++x) {
                int index = y * GRID_SIZE + (GRID_SIZE - 1 - x); // Flip left-to-right
                COLORREF color = GetPressureColor(latestFrame[index], tareValues[index]);
                HBRUSH brush = CreateSolidBrush(color);
                RECT rect = { x * CELL_SIZE, y * CELL_SIZE, (x + 1) * CELL_SIZE, (y + 1) * CELL_SIZE };
                FillRect(hdc, &rect, brush);
                DeleteObject(brush);
            }
        }

        // Display latest actuation value
        wchar_t buffer[50];
        swprintf(buffer, 50, L"Actuation: %.2f", latestActuationValue.load());
        TextOut(hdc, 10, GRID_SIZE * CELL_SIZE + 50, buffer, wcslen(buffer));

        // Compute actuator color using the same gradient function as sensors
        float actuatorValue = latestActuationValue.load(); // Already scaled with offset
        COLORREF actuatorColor = GetActuatorColor(actuatorValue, offsetValue);

        // Draw the gradient square **to the right** of "Actuation"
        HBRUSH actuatorBrush = CreateSolidBrush(actuatorColor);
        RECT actuatorRect = { 10, GRID_SIZE * CELL_SIZE + 70, 40, GRID_SIZE * CELL_SIZE + 100 };
        FillRect(hdc, &actuatorRect, actuatorBrush);
        DeleteObject(actuatorBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lParam == slider) {
            // Scaling Factor Slider
            int pos = SendMessage(slider, TBM_GETPOS, 0, 0);
            scalingFactor = pos / 100.0f;

            wchar_t sliderText[20];
            swprintf(sliderText, 20, L"%.2f", scalingFactor.load());
            SetWindowText(sliderValueText, sliderText);
        }
        else if ((HWND)lParam == offsetSlider) {
            // Offset Slider
            int pos = SendMessage(offsetSlider, TBM_GETPOS, 0, 0);
            offsetValue = static_cast<float>(pos);

            wchar_t offsetText[20];
            swprintf(offsetText, 20, L"%d", pos);
            SetWindowText(offsetValueText, offsetText);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { // Button pressed
            std::lock_guard<std::mutex> lock(frameMutex);
            tareValues = latestFrame;
        }
        return 0;
    case WM_CLOSE:
        running = false;
        DestroyWindow(hwnd);  // Close the window
        return 0;
    case WM_DESTROY:
        OnWindowClose();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void SerialReaderThread(const std::wstring& portName, int baudRate) {
    AttachConsoleWindow();
    HANDLE hSerial = CreateFile(portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening serial port!" << std::endl;
        return;
    }

    // Configure the serial port
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error getting serial state" << std::endl;
        CloseHandle(hSerial);
        return;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error setting serial parameters" << std::endl;
        CloseHandle(hSerial);
        return;
    }

    // Configure timeout settings
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    SetCommTimeouts(hSerial, &timeouts);

    const int bufferSize = 512;
    char buffer[bufferSize];
    DWORD bytesRead;
    std::string lineBuffer;

    while (running) {
        if (ReadFile(hSerial, buffer, bufferSize - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buffer[bytesRead] = '\0';  // Null-terminate buffer
            lineBuffer += buffer;

            // Process complete lines
            size_t newlinePos;
            while ((newlinePos = lineBuffer.find('\n')) != std::string::npos) {

                std::string line = lineBuffer.substr(0, newlinePos);
                lineBuffer.erase(0, newlinePos + 1);  // Remove processed line

                std::vector<float> values;
                std::stringstream ss(line);
                std::string token;

                //printf("r:%s\n\n", line.c_str());

                size_t start = 0, end = 0;
                // Fast CSV parsing (manual split instead of stringstream)
                while ((end = line.find(',', start)) != std::string::npos) {
                    try {
                        values.push_back(std::stof(line.substr(start, end - start)));
                    }
                    catch (...) {
                        std::cerr << "Error converting value: " << line.substr(start, end - start) << std::endl;
                    }
                    start = end + 1;
                }

                // Last value (after the final comma)
                try {
                    values.push_back(std::stof(line.substr(start)));
                }
                catch (...) {
                    std::cerr << "Error converting last value: " << line.substr(start) << std::endl;
                }

                if (values.size() == 16) {  // Expecting exactly 16 floats
                    std::lock_guard<std::mutex> lock(frameMutex);
                    latestFrame = values;
                    christina = true;
                    if (!hasTare)
                    {
                        tareValues = values;
                        hasTare = true;
                    }
                }
                else {
                    std::cerr << "Warning: Received " << values.size() << " floats instead of 16!" << std::endl;
                }
            }
        }

    }
    CloseHandle(hSerial);
}


void PrintThread() {
    AttachConsoleWindow();
    auto prevTime = std::chrono::steady_clock::now();

    while (running) {
        if (christina)
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (!latestFrame.empty()) {
                auto timeNow = std::chrono::steady_clock::now();
                auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(timeNow - prevTime).count();
                prevTime = timeNow;

                for (float f : latestFrame)
                    std::cout << f << "\t";
                std::cout << elapsedNs << std::endl;

            }
            christina = false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));  // Allow the reader thread to work
    }
}

void GUIThread() {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"PressureGrid";
    RegisterClass(&wc);

    hwnd = CreateWindowEx(0, L"PressureGrid", L"Pressure Sensor Grid", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, GRID_SIZE * CELL_SIZE + 16, GRID_SIZE * CELL_SIZE + 200,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    // "Set Tare" Button
    button = CreateWindow(L"BUTTON", L"Set Tare", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        10, GRID_SIZE * CELL_SIZE + 10, 100, 30, hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);

    // Scaling Factor Label
    CreateWindow(L"STATIC", L"Scaling Factor", WS_VISIBLE | WS_CHILD,
        120, GRID_SIZE * CELL_SIZE - 10, 100, 20, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    // Scaling Factor Slider
    slider = CreateWindow(TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
        120, GRID_SIZE * CELL_SIZE + 10, 200, 30, hwnd, (HMENU)2, GetModuleHandle(nullptr), nullptr);
    SendMessage(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000)); // 0.0 - 10.0
    SendMessage(slider, TBM_SETPOS, TRUE, (int)(scalingStart * 100)); // Default 1.0

    // Scaling Factor Value Text
    sliderValueText = CreateWindow(L"STATIC", std::to_wstring(scalingStart).c_str(), WS_VISIBLE | WS_CHILD,
        330, GRID_SIZE * CELL_SIZE + 10, 50, 30, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    // Offset Value Label
    CreateWindow(L"STATIC", L"Offset Value", WS_VISIBLE | WS_CHILD,
        120, GRID_SIZE * CELL_SIZE + 40, 100, 20, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    // Offset Slider
    offsetSlider = CreateWindow(TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
        120, GRID_SIZE * CELL_SIZE + 60, 200, 30, hwnd, (HMENU)3, GetModuleHandle(nullptr), nullptr);
    SendMessage(offsetSlider, TBM_SETRANGE, TRUE, MAKELPARAM(-255, 255)); // -255 to 255
    SendMessage(offsetSlider, TBM_SETPOS, TRUE, offsetStart); // Default 0

    // Offset Value Text
    offsetValueText = CreateWindow(L"STATIC", std::to_wstring(offsetStart).c_str(), WS_VISIBLE | WS_CHILD,
        330, GRID_SIZE * CELL_SIZE + 60, 50, 30, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);


    scalingFactor = scalingStart;
    offsetValue = offsetStart;

    ShowWindow(hwnd, SW_SHOW);

    MSG msg = {};
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        //float average = 0.0f;
        //{
        //    std::lock_guard<std::mutex> lock(frameMutex);
        //    for (float val : latestFrame) average += val;
        //    average /= latestFrame.size();
        //}
        float average = 0.0f;
        {
            std::lock_guard<std::mutex> lock(frameMutex);

            if (latestFrame.size() >= 5) {  // Ensure there are at least 5 values
                std::vector<float> tempFrame = latestFrame;  // Copy the frame
                std::partial_sort(tempFrame.begin(), tempFrame.begin() + 5, tempFrame.end(), std::greater<float>());

                // Compute the average of the top 5
                float sum = 0.0f;
                for (int i = 0; i < 5; ++i) {
                    sum += tempFrame[i];
                }
                average = sum / 5.0f;
            }
        }

        char values[8];
        char scaledChar = mapPressureToActuator(average);
        for (char& val : values) {
            val = scaledChar;
        }
        setFluidValues(values);
        latestActuationValue = static_cast<unsigned char>(scaledChar);

        InvalidateRect(hwnd, nullptr, FALSE);
        Sleep(100);
    }
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    AttachConsoleWindow();

    initFluidReality();
    EnablePSU();

    wchar_t comPort[64] = L""; // Buffer to store the COM port
    char vid[] = "VID_2886";
    char pid[] = "PID_802F";

    if (scan_ports(comPort, sizeof(comPort) / sizeof(comPort[0]), vid, pid))
    {
        wprintf(L"COM Port for Fluid Haptics Found: %s\n", comPort);
    }
    else
    {
        wprintf(L"No matching COM port for Fluid Haptics found.\n");
        return -1;
    }
    int baudRate = 115200;

    
    std::thread guiThread(GUIThread);
    std::thread readerThread(SerialReaderThread, comPort, baudRate);
    std::thread printerThread(PrintThread);


    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();
    running = false;

    guiThread.join();
    printerThread.join();
    readerThread.join();

    DisablePSU();
    exitFluidReality();

    return 0;
}
