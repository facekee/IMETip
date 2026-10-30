#include <windows.h>

#include "app/App.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    return imetip::App::Run(instance);
}
