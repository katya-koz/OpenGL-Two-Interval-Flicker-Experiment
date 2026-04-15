#include "App.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <shlobj.h>
#include <sstream>
#include "selectVariantDialog.h"


int selectedVariant = 0;




INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {

        case IDOK:
        {
            if (IsDlgButtonChecked(hwnd, IDC_RADIO1))
                selectedVariant = 1;
            else if (IsDlgButtonChecked(hwnd, IDC_RADIO2))
                selectedVariant = 2;
            else if (IsDlgButtonChecked(hwnd, IDC_RADIO3))
                selectedVariant = 3;

            EndDialog(hwnd, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            selectedVariant = -1;
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}
void ShowVariantDialog()
{
    DialogBox(
        GetModuleHandle(nullptr),
        MAKEINTRESOURCE(IDD_DIALOG1),
        nullptr,
        DialogProc
    );
}


int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
) {


    ShowVariantDialog();

    if (selectedVariant == -1)
    {
        // user cancelled
        return -1;
    }

    //TO DO: use monitor dimensions, open across 2 monitors. mirror them.
    App app;



    if (!app.init("config.json")) { // path to the config here !!
        return -1;
    }



    app.run();

    return 0;
}