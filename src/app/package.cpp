#include "app/package.h"

#include <windows.h>

#include <appmodel.h>

bool IsPackaged() {
    static const bool packaged = [] {
        UINT32 length = 0;
        return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    }();
    return packaged;
}
