// SPDX-License-Identifier: MIT
// Copyright (c) github.com/vblendpd

#include "ColorEdit.h"

CustomEditBox gEditBox;

constexpr int MARGIN = 10;

LRESULT CALLBACK HostWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_SIZE) {
    if (HWND hEditBox = gEditBox.GetHwnd(); hEditBox) {
      RECT rect{};
      GetClientRect(hEditBox, &rect);
      if (!MoveWindow(hEditBox, MARGIN, MARGIN, LOWORD(lParam) - MARGIN * 2, rect.bottom - rect.top, TRUE)) {
        return 0;
      }
    }
    return 0;
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
  HFONT hFont = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Consolas");

  if (!hFont) {
    hFont = static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
  }

  TEXTMETRICW tm{};
  tm.tmHeight = 22; //fallback

  if (HDC hDC = GetDC(nullptr); hDC) {
    if (HFONT oldFont = static_cast<HFONT>(SelectObject(hDC, hFont)); oldFont) {
      GetTextMetricsW(hDC, &tm);
      SelectObject(hDC, oldFont);
    }
    ReleaseDC(nullptr, hDC);
  }

  WNDCLASSEXW wc{};
  wc.cbSize        = sizeof wc;
  wc.lpfnWndProc   = HostWndProc;
  wc.hInstance     = hInst;
  wc.lpszClassName = L"5AF92292";
  wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

  if (!RegisterClassExW(&wc)) {
    return -1;
  }

  RECT clientRect = {0, 0, 700, MARGIN * 2 + tm.tmHeight};
  AdjustWindowRectEx(&clientRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

  const int cx = clientRect.right - clientRect.left;
  const int cy = clientRect.bottom - clientRect.top;

  HWND hWnd = CreateWindowW(L"5AF92292", L"ColorEdit", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                            cx, cy, nullptr, nullptr, hInst, nullptr);
  if (!hWnd) {
    return -1;
  }

  GetClientRect(hWnd, &clientRect);
  if (!gEditBox.Create(hWnd, hInst, MARGIN, MARGIN, clientRect.right - MARGIN * 2, tm.tmHeight, hFont)) {
    return -1;
  }

  gEditBox.RegisterKeyword(L"if");
  gEditBox.RegisterKeyword(L"else");
  gEditBox.RegisterKeyword(L"clear");
  gEditBox.RegisterKeyword(L"exit");
  gEditBox.Focus();

  MSG msg;
  BOOL ret;
  while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
    if (ret == -1) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
