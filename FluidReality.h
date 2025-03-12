#pragma once

int initFluidReality();

void exitFluidReality();

int setFluidValues(char values[8]);

int EnablePSU();

int DisablePSU();

bool scan_ports(wchar_t* outComPort, size_t outSize, char* vid, char* pid);