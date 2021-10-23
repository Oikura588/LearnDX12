#include <windows.h>
#ifdef _DEBUG
#include <iostream>
#endif


using namespace std;

void DebugOutputFormatString(const char* format,...)
{
#ifdef _DEBUG
    va_list valist;
    va_start(valist,format);
    printf(format,valist);
    va_end(valist);
#endif
}
LRESULT WindowProcedure(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
    if(msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,msg,wparam,lparam);
}


bool InitWindows()
{

    
}