// SPDX-License-Identifier: MIT
// Copyright (c) github.com/vblendpd

#include "ColorEdit.h"
#include <array>
#include <charconv>
#include <cwctype>

void Error(const wchar_t* what) {
  wchar_t buf[512];
  wsprintfW(buf, L"[Error] %ls failed (GetLastError=%lu)\n", what, GetLastError());
  OutputDebugStringW(buf);
}

LRESULT CALLBACK ColorEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  CustomEditBox* editBox;

  if (msg == WM_NCCREATE) {
    const CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

    editBox = static_cast<CustomEditBox*>(cs->lpCreateParams);
    if (editBox) {
      SetLastError(0);
      if (!SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editBox)) && GetLastError() != 0) {
        Error(L"SetWindowLongPtrW");
      }
      //fill the HWND that was not yet available at construction time
      editBox->m_hWnd = hWnd;
    }
  }
  else {
    editBox = reinterpret_cast<CustomEditBox*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
  }

  if (editBox)
    return editBox->HandleMessage(hWnd, msg, wParam, lParam);

  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

CustomEditBox::~CustomEditBox() {
  DeleteBackBuffer();
}

HWND CustomEditBox::Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, HFONT font) {
  m_Width  = w;
  m_Height = h;
  m_Font   = font;

  TEXTMETRICW tm{};
  tm.tmHeight = 22; //fallback

  if (HDC hDC = GetDC(nullptr); hDC) {
    if (HFONT oldFont = static_cast<HFONT>(SelectObject(hDC, font)); oldFont) {
      GetTextMetricsW(hDC, &tm);
      SelectObject(hDC, oldFont);
    }
    ReleaseDC(nullptr, hDC);
  }
  m_LineHeight = tm.tmHeight;

  WNDCLASSEXW wc{};
  wc.cbSize        = sizeof wc;
  wc.style         = CS_DBLCLKS; //needed for WM_LBUTTONDBLCLK
  wc.lpfnWndProc   = ColorEditProc;
  wc.hInstance     = hInst;
  wc.lpszClassName = L"AB012HE4";                    //deliberately obscure to avoid collision with other classes
  wc.hCursor       = LoadCursor(nullptr, IDC_IBEAM); //text-editing cursor
  wc.hbrBackground = nullptr;                        //no system background erase; we paint everything ourselves

  if (!RegisterClassExW(&wc)) {
    Error(L"RegisterClassExW");
    return nullptr;
  }

  //pass 'this' as lpCreateParams so ColorEditProc can store it on WM_NCCREATE
  m_hWnd = CreateWindowExW(0, L"AB012HE4", L"", WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, hInst, this);
  if (!m_hWnd) {
    Error(L"CreateWindowExW");
  }

  //move caret to the end of the text
  OnKeyDown(VK_END);
  return m_hWnd;
}

HWND CustomEditBox::GetHwnd() const {
  return m_hWnd;
}

void CustomEditBox::Focus() const {
  if (m_hWnd && !SetFocus(m_hWnd))
    Error(L"SetFocus");
}

void CustomEditBox::SetBkgColor(COLORREF cr) {
  m_crBackground = cr;
}

void CustomEditBox::SetTextColor(COLORREF cr) {
  m_crText = cr;
}

void CustomEditBox::SetSelColor(COLORREF cr) {
  m_crSelectionBkg = cr;
}

void CustomEditBox::SetCaretColor(COLORREF cr) {
  m_crCaret = cr;
}

void CustomEditBox::RegisterKeyword(std::wstring_view str) {
  m_Keywords.insert(std::wstring(str));
}

std::wstring CustomEditBox::GetText() const {
  return m_Text;
}

void CustomEditBox::SetText(std::wstring_view str) {
  m_Text = str;

  //collapse the selection and place the caret at the very end
  m_CaretPos = m_SelAnchor = static_cast<int>(m_Text.size());

  //always start scrolled to the left for a newly set string
  m_ScrollPx = 0;

  if (m_hWnd) {
    EnsureCaretVisible();
    if (!InvalidateRect(m_hWnd, nullptr, FALSE))
      Error(L"InvalidateRect");
  }
}

bool CustomEditBox::IsNumber(std::string_view str) {
  if (str.empty())
    return false;

  //detect 0x/0X and 0b/0B prefixes
  const bool hasPrefix = str.size() >= 2 && str[0] == '0';
  const char marker    = hasPrefix ? static_cast<char>(std::tolower(static_cast<unsigned char>(str[1]))) : '\0';

  if (marker == 'x' || marker == 'b') {
    //validate the digit run after the prefix (an empty digit run is not a valid literal)
    const std::string_view digits = str.substr(2);
    if (digits.empty())
      return false;

    const int base           = marker == 'x' ? 16 : 2;
    unsigned long long value = 0;

    //from_chars returns the pointer it stopped at, a valid literal must consume all digits
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
    return ec == std::errc() && ptr == digits.data() + digits.size();
  }

  //strip optional float suffix f/F before attempting numeric parsing
  std::string_view numPart  = str;
  const bool hasFloatSuffix = !numPart.empty() && (numPart.back() == 'f' || numPart.back() == 'F');
  if (hasFloatSuffix)
    numPart.remove_suffix(1);

  if (numPart.empty())
    return false;

  if (!hasFloatSuffix) {
    //try integer parse first
    unsigned long long ivalue = 0;
    const auto [iptr, iec]    = std::from_chars(numPart.data(), numPart.data() + numPart.size(), ivalue, 10);

    if (iec == std::errc() && iptr == numPart.data() + numPart.size())
      return true;
  }
  else if (numPart.find('.') == std::string_view::npos && numPart.find_first_of("eE") == std::string_view::npos) {
    //"42f" is syntactically invalid
    return false;
  }

  //floating-point parse: covers "3.14", "1e10", "3.14e-2", ".5" etc.
  double dvalue        = 0;
  const auto [ptr, ec] = std::from_chars(numPart.data(), numPart.data() + numPart.size(), dvalue, std::chars_format::general);
  return ec == std::errc() && ptr == numPart.data() + numPart.size();
}

size_t CustomEditBox::ScanEnd(std::wstring_view text, size_t from, auto pred) {
  const auto tail = text.substr(from);

  //find_if_not stops at the first character that does not satisfy pred
  const auto stop = std::ranges::find_if_not(tail, pred);
  return from + static_cast<size_t>(std::ranges::distance(tail.begin(), stop));
}

std::vector<CustomEditBox::Token> CustomEditBox::Tokenize(std::wstring_view text) {
  using namespace std::string_view_literals;

  constexpr auto IsSpace = [](wchar_t c) {
    return std::iswspace(static_cast<wint_t>(c)) != 0;
  };

  constexpr auto IsDigit = [](wchar_t c) {
    return std::iswdigit(static_cast<wint_t>(c)) != 0;
  };

  constexpr auto IsAlpha = [](wchar_t c) {
    return std::iswalpha(static_cast<wint_t>(c)) != 0;
  };

  //alphanumeric or underscore
  constexpr auto IsWordChar = [](wchar_t c) {
    return std::iswalnum(static_cast<wint_t>(c)) != 0 || c == L'_';
  };

  //superset of IsWordChar: also accepts dot (float point) and minus (negative literal)
  constexpr auto IsNumberBodyChar = [](wchar_t c) {
    return IsWordChar(c) || c == L'.' || c == L'-';
  };

  std::vector<Token> tokens;
  size_t i = 0;

  while (i < text.size()) {
    const wchar_t c = text[i];

    //whitespace run (emitted as defaultColor to preserve exact character offsets for TextOut)
    if (IsSpace(c)) {
      const size_t end = ScanEnd(text, i, IsSpace);
      tokens.push_back({.Start = static_cast<int>(i), .Length = static_cast<int>(end - i), .Color = m_crText});
      i = end;
      continue;
    }

    //numeric: plain digit start, or dot-then-digit to catch .5f style float literals
    if (IsDigit(c) || (c == L'.' && i + 1 < text.size() && IsDigit(text[i + 1]))) {
      const size_t end = ScanEnd(text, i, IsNumberBodyChar);
      const auto token = text.substr(i, end - i);

      //narrow to std::string because std::from_chars only accepts char* and not wchar_t*
      const std::string narrow(token.begin(), token.end());
      const bool isNumber = IsNumber(narrow);
      tokens.push_back({.Start = static_cast<int>(i), .Length = static_cast<int>(end - i), .Color = isNumber ? m_crNumber : m_crText});
      i = end;
      continue;
    }

    //identifier or keyword: C identifiers must open with a letter or underscore
    if (IsAlpha(c) || c == L'_') {
      const size_t end     = ScanEnd(text, i, IsWordChar);
      const auto word      = text.substr(i, end - i);
      const bool isKeyword = m_Keywords.contains(word);
      tokens.push_back({.Start = static_cast<int>(i), .Length = static_cast<int>(end - i), .Color = isKeyword ? m_crKeyword : m_crText });
      i = end;
      continue;
    }

    //anything unrecognised
    tokens.push_back({.Start = static_cast<int>(i), .Length = 1, .Color = m_crText});
    i++;
  }
  return tokens;
}

bool CustomEditBox::ResizeBackBuffer(HDC refDC, int width, int height) {
  DeleteBackBuffer();

  m_hdcBack = CreateCompatibleDC(refDC);
  if (!m_hdcBack) {
    Error(L"CreateCompatibleDC");
    return false;
  }

  m_Bitmap = CreateCompatibleBitmap(refDC, width, height);
  if (!m_Bitmap) {
    Error(L"CreateCompatibleBitmap");

    if (!DeleteDC(m_hdcBack))
      Error(L"DeleteDC");
    m_hdcBack = nullptr;
    return false;
  }

  m_BitmapOld = static_cast<HBITMAP>(SelectObject(m_hdcBack, m_Bitmap));
  if (!m_BitmapOld) {
    Error(L"SelectObject");

    if (!DeleteObject(m_Bitmap))
      Error(L"DeleteObject");

    if (!DeleteDC(m_hdcBack))
      Error(L"DeleteDC");
    m_Bitmap    = nullptr;
    m_hdcBack   = nullptr;
    m_BitmapOld = nullptr;
    return false;
  }
  return true;
}

void CustomEditBox::DeleteBackBuffer() {
  if (m_hdcBack) {
    if (m_BitmapOld)
      SelectObject(m_hdcBack, m_BitmapOld);
    
    if (m_Bitmap && !DeleteObject(m_Bitmap))
      Error(L"DeleteObject");
    
    if (!DeleteDC(m_hdcBack))
      Error(L"DeleteDC");
  }
  m_hdcBack   = nullptr;
  m_Bitmap    = nullptr;
  m_BitmapOld = nullptr;
}

void CustomEditBox::Blit(HDC dcFront, int width, int height) const {
  if (!BitBlt(dcFront, 0, 0, width, height, m_hdcBack, 0, 0, SRCCOPY)) {
    Error(L"BitBlt");
  }
}

void CustomEditBox::OnPaint() {
  PAINTSTRUCT ps;
  const HDC hdc = BeginPaint(m_hWnd, &ps);
  if (!hdc) {
    Error(L"BeginPaint");
    return;
  }

  PaintContent();               //render everything into the off-screen bitmap
  Blit(hdc, m_Width, m_Height); //copy to screen

  if (!EndPaint(m_hWnd, &ps)) {
    Error(L"EndPaint");
  }
}

void CustomEditBox::PaintContent() {
  const RECT rect = {0, 0, m_Width, m_Height};

  //background fill
  if (HBRUSH brush = CreateSolidBrush(m_crBackground); brush) {
    if (!FillRect(m_hdcBack, &rect, brush))
      Error(L"FillRect");

    if (!DeleteObject(brush))
      Error(L"DeleteObject");
  }


  HFONT oldFont = static_cast<HFONT>(SelectObject(m_hdcBack, m_Font));
  if (!oldFont)
    Error(L"SelectObject(font)");
  SetBkMode(m_hdcBack, TRANSPARENT);

  //x is the left edge of first character adjusted by the scroll offset
  const int x = PADDING_LEFT - m_ScrollPx;

  //vertically center
  const int y = (m_Height - m_LineHeight) / 2;

  //selection highlight
  if (HasSelection()) {
    const int s = std::min(m_CaretPos, m_SelAnchor);
    const int e = std::max(m_CaretPos, m_SelAnchor);

    //start/end x of the selection relative to the scrolled origin
    const int sx       = x + TextWidth(m_hdcBack, 0, s);
    const int ex       = x + TextWidth(m_hdcBack, 0, e);
    const RECT selRect = {sx, 0, ex, m_Height};

    HBRUSH selBrush = CreateSolidBrush(m_crSelectionBkg);
    if (!selBrush) {
      Error(L"CreateSolidBrush(selection)");
    }
    else {
      if (!FillRect(m_hdcBack, &selRect, selBrush))
        Error(L"FillRect(selection)");
      if (!DeleteObject(selBrush))
        Error(L"DeleteObject(sel brush)");
    }
  }

  //tokenized text
  int tx = x; //running x position
  for (const auto& [Start, Length, Color]: Tokenize(m_Text)) {
    if (::SetTextColor(m_hdcBack, Color) == CLR_INVALID)
      Error(L"SetTextColor");
    if (!TextOutW(m_hdcBack, tx, y, m_Text.c_str() + Start, Length)) {
      Error(L"TextOutW");
    }
    //advance by the actual token width
    SIZE sz{};
    if (!GetTextExtentPoint32W(m_hdcBack, m_Text.c_str() + Start, Length, &sz)) {
      Error(L"GetTextExtentPoint32W");
      sz.cx = 0; //fallback
    }
    tx += sz.cx;
  }

  //caret line (only when focused)
  if (m_Focused) {
    tx = x + TextWidth(m_hdcBack, 0, m_CaretPos); //insertion point

    //3-pixel solid line
    if (HPEN pen = CreatePen(PS_SOLID, 3, m_crCaret); pen) {
      if (HPEN oldPen = static_cast<HPEN>(SelectObject(m_hdcBack, pen)); oldPen) {

        //draw from 2px below the top edge to 2px above the bottom edge for a clean look
        if (!MoveToEx(m_hdcBack, tx, 2, nullptr))
          Error(L"MoveToEx");

        if (!LineTo(m_hdcBack, tx, m_Height - 2))
          Error(L"LineTo");

        SelectObject(m_hdcBack, oldPen);        
      }
      if (!DeleteObject(pen))
        Error(L"DeleteObject(pen)");
    }
  }

  //border rectangle
  if (HBRUSH brush = CreateSolidBrush(m_Focused ? m_crFocusRect : m_crUnfocusRect); brush) {
    if (!FrameRect(m_hdcBack, &rect, brush))
      Error(L"FrameRect");

    if (!DeleteObject(brush))
      Error(L"DeleteObject");
  }

  if (oldFont)
    SelectObject(m_hdcBack, oldFont);
}

int CustomEditBox::TextWidth(HDC dc, int start, int end) const {
  if (end <= start)
    return 0;
  SIZE sz{};
  if (!GetTextExtentPoint32W(dc, m_Text.c_str() + start, end - start, &sz)) {
    Error(L"GetTextExtentPoint32W");
    return 0;
  }
  return sz.cx;
}

int CustomEditBox::HitTest(int mx) const {
  int best = m_CaretPos; //fallback to the current caret

  if (HDC hDC = GetDC(m_hWnd); hDC) {
    if (HFONT old = static_cast<HFONT>(SelectObject(hDC, m_Font)); old) {

      //convert from window coordinates to text-space coordinates
      const int x   = mx - PADDING_LEFT + m_ScrollPx;
      const int len = static_cast<int>(m_Text.size());

      //start with the distance to position 0 (left edge)
      best = 0;
      int bestDist = std::abs(x);

      int tw = 0;
      for (int k = 1; k <= len; k++) {
        SIZE sz{};
        GetTextExtentPoint32W(hDC, &m_Text[k - 1], 1, &sz);
        tw += sz.cx;
        if (const int dist = std::abs(tw - x); dist < bestDist) {
          bestDist = dist;
          best     = k;
        }
      }
      SelectObject(hDC, old);
    }
    if (!ReleaseDC(m_hWnd, hDC))
      Error(L"ReleaseDC");
  }
  return best;
}

void CustomEditBox::EnsureCaretVisible() {
  if (!m_hWnd)
    return;
  HDC dc = GetDC(m_hWnd);
  if (!dc) {
    Error(L"GetDC");
    return;
  }
  HFONT old = static_cast<HFONT>(SelectObject(dc, m_Font));
  if (!old)
    Error(L"SelectObject(font)");

  const int cx = TextWidth(dc, 0, m_CaretPos);

  //clamp visibleWidth to at least 10px so the control doesn't behave strangely when very narrow
  const int visibleWidth = std::max(10, m_Width - PADDING_LEFT * 2);

  //scroll left: caret is to the left of the visible window
  if (cx - m_ScrollPx < 0) {
    m_ScrollPx = cx;
  }
  //scroll right: caret is to the right of the visible window
  else if (cx - m_ScrollPx > visibleWidth) {
    m_ScrollPx = cx - visibleWidth;
  }
  //guard against a negative scroll
  if (m_ScrollPx < 0)
    m_ScrollPx = 0;

  if (old)
    SelectObject(dc, old);
  if (!ReleaseDC(m_hWnd, dc))
    Error(L"ReleaseDC");
}

bool CustomEditBox::HasSelection() const {
  return m_CaretPos != m_SelAnchor;
}

void CustomEditBox::DeleteSelection() {
  const int s = std::min(m_CaretPos, m_SelAnchor);
  const int e = std::max(m_CaretPos, m_SelAnchor);

  m_Text.erase(s, e - s);
  m_CaretPos = m_SelAnchor = s;
  AfterEdit();
}

void CustomEditBox::ReplaceSelection(const std::wstring& str) {
  const int s = std::min(m_CaretPos, m_SelAnchor);
  const int e = std::max(m_CaretPos, m_SelAnchor);
  m_Text.erase(s, e - s); //delete the selected region (no-op if s == e)
  m_Text.insert(s, str);  //insert the new text at the gap

  m_CaretPos = m_SelAnchor = s + static_cast<int>(str.size());
  AfterEdit();
}

int CustomEditBox::PrevWord() const {
  int i = m_CaretPos;
  //skip any non-word characters immediately to the left (e.g. spaces or punctuation)
  while (i > 0 && !iswalnum(m_Text[i - 1]))
    i--;
  //then skip the preceding word itself
  while (i > 0 && iswalnum(m_Text[i - 1]))
    i--;
  return i;
}

int CustomEditBox::NextWord() const {
  int i         = m_CaretPos;
  const int len = static_cast<int>(m_Text.size());
  //skip any non-word characters immediately to the right
  while (i < len && !iswalnum(m_Text[i]))
    i++;
  //then skip the following word
  while (i < len && iswalnum(m_Text[i]))
    i++;
  return i;
}

void CustomEditBox::SelectAll() {
  m_SelAnchor = 0;
  m_CaretPos  = static_cast<int>(m_Text.size());
  AfterEdit();
}

void CustomEditBox::HistoryPrev() {
  //incrementing moves toward older entries
  //history is stored oldest-first (index 0 from the back is the most recent entry)
  if (!m_History.empty() && m_HistoryIndex < static_cast<int>(m_History.size()) - 1) {
    m_HistoryIndex++;
    SetText(m_History[m_History.size() - 1 - m_HistoryIndex]);
  }
}

void CustomEditBox::HistoryNext() {
  if (m_HistoryIndex > 0) {
    m_HistoryIndex--;
    SetText(m_History[m_History.size() - 1 - m_HistoryIndex]);
  }
  else if (m_HistoryIndex == 0) {
    //returning past the most-recent entry clears the text
    m_HistoryIndex = -1;
    SetText(L"");
  }
}

void CustomEditBox::OnChar(wchar_t ch) {
  //filter C0 control characters (0x00–0x1F) and DEL (0x7F)
  if (ch < 0x20 || ch == 0x7F)
    return;
  ReplaceSelection(std::wstring(1, ch));
}

void CustomEditBox::OnKeyDown(int vk) {
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

  switch (vk) {
    case VK_LEFT:
      m_CaretPos = ctrl ? PrevWord() : std::max(0, m_CaretPos - 1);
      if (!shift)
        m_SelAnchor = m_CaretPos;
      AfterEdit();
      return;

    case VK_RIGHT:
      m_CaretPos = ctrl ? NextWord() : std::min(static_cast<int>(m_Text.size()), m_CaretPos + 1);
      if (!shift)
        m_SelAnchor = m_CaretPos;
      AfterEdit();
      return;

    case VK_HOME:
      m_CaretPos = 0;
      if (!shift)
        m_SelAnchor = m_CaretPos;
      AfterEdit();
      return;

    case VK_END:
      m_CaretPos = static_cast<int>(m_Text.size());
      if (!shift)
        m_SelAnchor = m_CaretPos;
      AfterEdit();
      return;

    case VK_BACK:
      if (HasSelection())
        DeleteSelection();
      else if (m_CaretPos > 0) {
        m_Text.erase(m_CaretPos - 1, 1);
        m_CaretPos--;
        m_SelAnchor = m_CaretPos;
      }
      AfterEdit();
      return;

    case VK_DELETE:
      if (HasSelection())
        DeleteSelection();
      else if (m_CaretPos < static_cast<int>(m_Text.size())) {
        m_Text.erase(m_CaretPos, 1);
      }
      AfterEdit();
      return;

    case VK_RETURN: {
      if (std::wstring cmd = GetText(); !cmd.empty()) {
        m_History.push_back(cmd);
        m_HistoryIndex = -1; //reset navigation back to the live prompt
      }
      SetText(L"");
    }
      return;

    case VK_UP:
      HistoryPrev();
      return;

    case VK_DOWN:
      HistoryNext();
      return;

    case 'A':
      if (ctrl) {
        SelectAll();
        return;
      }
      break;
    case 'C':
      if (ctrl) {
        Copy();
        return;
      }
      break;
    case 'X':
      if (ctrl) {
        Cut();
        return;
      }
      break;
    case 'V':
      if (ctrl) {
        Paste();
        return;
      }
      break;
  }
}

void CustomEditBox::OnLButtonDown(int mx, bool shift) {
  if (!SetFocus(m_hWnd))
    Error(L"SetFocus");
  const int idx = HitTest(mx);
  m_CaretPos    = idx;

  //shift+click extends the current selection while plain click resets the anchor
  if (!shift)
    m_SelAnchor = idx;
  m_Dragging = true;
  SetCapture(m_hWnd);
  AfterEdit();
}

void CustomEditBox::OnMouseMove(int mx) {
  //only update during an active button-down drag
  m_CaretPos = HitTest(mx); //anchor stays fixed (only the caret moves to create a selection)
  AfterEdit();
}

void CustomEditBox::OnDoubleClick(int mx) {
  const int idx = HitTest(mx);
  int s         = idx;
  int e         = idx;
  const int len = static_cast<int>(m_Text.size());

  //expand left while the preceding character is part of a word
  while (s > 0 && iswalnum(m_Text[s - 1]))
    s--;

  //expand right while the current character is part of a word
  while (e < len && iswalnum(m_Text[e]))
    e++;
  m_SelAnchor = s;
  m_CaretPos  = e;
  AfterEdit();
}

void CustomEditBox::SetClipboardText(const std::wstring& str) {
  if (!OpenClipboard(nullptr)) {
    Error(L"OpenClipboard");
    return;
  }
  EmptyClipboard();

  if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (str.size() + 1) * sizeof(wchar_t)); hMem) {
    if (wchar_t* p = static_cast<wchar_t*>(GlobalLock(hMem)); p) {
      std::memcpy(p, str.c_str(), (str.size() + 1) * sizeof(wchar_t)); //copy including null terminator
      GlobalUnlock(hMem);
    }
    SetClipboardData(CF_UNICODETEXT, hMem);
    GlobalFree(hMem);    
  }
  CloseClipboard();
}

std::wstring CustomEditBox::GetClipboardText() {
  std::wstring result;
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    return result;

  if (!OpenClipboard(nullptr)) {
    Error(L"OpenClipboard");
    return result;
  }

  if (HANDLE h = GetClipboardData(CF_UNICODETEXT); h) {
    if (wchar_t* p = static_cast<wchar_t*>(GlobalLock(h)); p) {
      result = p;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return result;
}

void CustomEditBox::Copy() {
  if (!HasSelection())
    return;

  const int s = std::min(m_CaretPos, m_SelAnchor);
  const int e = std::max(m_CaretPos, m_SelAnchor);
  SetClipboardText(m_Text.substr(s, e - s));
}

void CustomEditBox::Cut() {
  if (!HasSelection())
    return;
  Copy();            //put selected text on clipboard first
  DeleteSelection(); //then remove it from the buffer
}

void CustomEditBox::Paste() {
  const std::wstring clip = GetClipboardText();
  if (clip.empty())
    return;

  //strip newlines (this is a single-line control)
  std::wstring clean;
  clean.reserve(clip.size());
  for (const wchar_t c: clip)
    if (c != L'\r' && c != L'\n')
      clean += c;
  ReplaceSelection(clean);
}

void CustomEditBox::AfterEdit() {
  EnsureCaretVisible();
  if (m_hWnd && !InvalidateRect(m_hWnd, nullptr, FALSE))
    Error(L"InvalidateRect");
}

LRESULT CustomEditBox::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 0;

    case WM_SIZE:
      m_Width = LOWORD(lParam);
      m_Height = HIWORD(lParam);
      if (HDC hDC = GetDC(hWnd); hDC) {
        if (ResizeBackBuffer(hDC, m_Width, m_Height))
          InvalidateRect(hWnd, nullptr, FALSE);

        ReleaseDC(hWnd, hDC);
      }
      return 0;

    case WM_PAINT:
      OnPaint();
      return 0;

    case WM_SETFOCUS:
      m_Focused = true;
      if (!InvalidateRect(hWnd, nullptr, FALSE))
        Error(L"InvalidateRect(ColorEdit)");
      return 0;

    case WM_KILLFOCUS:
      m_Focused = false;
      if (!InvalidateRect(hWnd, nullptr, FALSE))
        Error(L"InvalidateRect(ColorEdit)");
      return 0;

    case WM_CHAR:
      OnChar(static_cast<wchar_t>(wParam));
      return 0;

    case WM_KEYDOWN:
      OnKeyDown(static_cast<int>(wParam));
      return 0;

    case WM_LBUTTONDOWN:
      OnLButtonDown(static_cast<short>(LOWORD(lParam)), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      return 0;

    case WM_LBUTTONDBLCLK:
      OnDoubleClick(static_cast<short>(LOWORD(lParam)));
      return 0;

    case WM_MOUSEMOVE:
      //idle hover should not move the caret
      if (m_Dragging)
        OnMouseMove(static_cast<short>(LOWORD(lParam)));
      return 0;

    case WM_LBUTTONUP:
      if (m_Dragging) {
        m_Dragging = false;
        if (!ReleaseCapture())
          Error(L"ReleaseCapture(ColorEdit)");
      }
      return 0;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}
