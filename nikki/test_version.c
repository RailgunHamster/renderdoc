#include <windows.h>
#include <stdio.h>

int main()
{
  char path[] = "C:\\Windows\\System32\\version.dll";
  DWORD handle = 0;
  DWORD size = GetFileVersionInfoSizeA(path, &handle);
  printf("GetFileVersionInfoSizeA returned %u (expect > 0)\n", size);
  if(size > 0)
  {
    void *buf = malloc(size);
    if(GetFileVersionInfoA(path, 0, size, buf))
    {
      LPVOID val = NULL;
      UINT len = 0;
      if(VerQueryValueA(buf, "\\", &val, &len))
        printf("VerQueryValueA OK, len=%u\n", len);
      else
        printf("VerQueryValueA FAILED\n");
      free(buf);
    }
    else
    {
      printf("GetFileVersionInfoA FAILED err=%u\n", GetLastError());
    }
  }
  return 0;
}
