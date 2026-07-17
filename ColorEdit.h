// SPDX-License-Identifier: MIT
// Copyright (c) github.com/vblendpd

#pragma once
#include <Windows.h>
#include <set>
#include <string>
#include <vector>

class CustomEditBox {
  friend LRESULT CALLBACK ColorEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
public:
  CustomEditBox() = default;
  ~CustomEditBox();

  CustomEditBox(const CustomEditBox&)            = delete;
  CustomEditBox& operator=(const CustomEditBox&) = delete;

  /**
   * @brief Registers the window class and creates the edit control
   *
   * @param parent  Owner/parent window handle
   * @param hInst   Module instance used for class registration
   * @param x       Left edge relative to the parent client area
   * @param y       Top edge relative to the parent client area
   * @param w       Control width
   * @param h       Control height
   * @param font    Pre-created HFONT for all text rendering
   * @return        The created HWND or nullptr on failure
   */
  HWND Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, HFONT font);

  /**
   * @brief Returns the underlying HWND
   * @return HWND handle
   */
  HWND GetHwnd() const;

  /**
   * @brief Sets keyboard focus to this control
   */
  void Focus() const;

  /**
   * @brief Sets the background fill color
   * @param cr New background COLORREF
   */
  void SetBkgColor(COLORREF cr);

  /**
   * @brief Sets the default text color
   * @param cr New text COLORREF
   */
  void SetTextColor(COLORREF cr);

  /**
   * @brief Sets the selection highlight color
   * @param cr New selection background COLORREF
   */
  void SetSelColor(COLORREF cr);

  /**
   * @brief Sets the caret color
   * @param cr New caret COLORREF
   */
  void SetCaretColor(COLORREF cr);

  /**
   * @brief Registers a keyword to be drawn in the keyword highlight color
   * @param str Keyword string case-sensitive
   */
  void RegisterKeyword(std::wstring_view str);
private:
  /**
   * @brief Describes a single highlighted text run
   */
  struct Token {
    int      Start;
    int      Length;
    COLORREF Color;
  };

  /**
   * @brief Returns the current content
   * @return Copy of the internal text string
   */
  std::wstring GetText() const;

  /**
   * @brief Replaces the entire content and resets caret and scroll state
   * @param str New text to display
   */
  void SetText(std::wstring_view str);

  /**
   * @brief Returns whether str is a complete well-formed numeric literal
   *        Accepts decimal integers, hex (0x), binary (0b), and floats with optional f/F suffix
   *
   * @param str Narrow ASCII view of the candidate token
   * @return    true if str parses entirely as a valid number false otherwise
   */
  bool IsNumber(std::string_view str);

  /**
   * @brief Returns the index one past the last character satisfying pred
   *
   * @param text View of the full content
   * @param from Start index for the scan
   * @param pred Unary predicate on wchar_t
   * @return     Index of the first character that did not satisfy pred or text.size() if all
   *             remaining characters satisfy it
   */
  size_t ScanEnd(std::wstring_view text, size_t from, auto pred);

  /**
   * @brief Splits text into colored token runs for rendering
   *
   * @param text Full content string
   * @return     Ordered vector of Token structs covering every character in text
   */
  std::vector<Token> Tokenize(std::wstring_view text);

  /**
   * @brief Allocates the GDI back buffer for the given dimensions
   *
   * @param refDC  Reference DC used to create a compatible surface
   * @param width  Buffer width
   * @param height Buffer height
   * @return       true on success false otherwise
   */
  bool ResizeBackBuffer(HDC refDC, int width, int height);

  /**
   * @brief Releases all GDI resources associated with the back buffer
   */
  void DeleteBackBuffer();

  /**
   * @brief Blit the back buffer to the front DC
   *
   * @param dcFront  Destination DC
   * @param width    Pixels to blit horizontally
   * @param height   Pixels to blit vertically
   */
  void Blit(HDC dcFront, int width, int height) const;

  /**
   * @brief WM_PAINT handler (renders into the back buffer then blits to front)
   */
  void OnPaint();

  /**
   * @brief Renders the full control into the back buffer
   *        Draws background, selection highlight, tokenized text, caret, and border
   */
  void PaintContent();

  /**
   * @brief Returns the pixel width of the substring m_Text[start...end)
   *
   * @param dc    DC with the font already selected
   * @param start First character index (inclusive)
   * @param end   One-past-last character index (exclusive)
   * @return      Width in pixels or 0 on failure or empty range
   */
  int TextWidth(HDC dc, int start, int end) const;

  /**
   * @brief Maps a mouse x coordinate to the nearest character index in m_Text
   *
   * @param mx Mouse x in client coordinates
   * @return   Character index in [0, m_Text.size()]
   */
  int HitTest(int mx) const;

  /**
   * @brief Adjusts m_ScrollPx to keep the caret character within the visible area
   */
  void EnsureCaretVisible();

  /**
   * @brief Returns true when a non-empty selection exists
   * @return true if m_CaretPos != m_SelAnchor
   */
  bool HasSelection() const;

  /**
   * @brief Erases the selected text and collapses the selection to its start
   */
  void DeleteSelection();

  /**
   * @brief Deletes any selection and inserts str at the caret position
   * @param str Text to insert (if empty act as delete)
   */
  void ReplaceSelection(const std::wstring& str);

  /**
   * @brief Returns the character index of the start of the previous word (Ctrl+Left)
   * @return New caret position in [0, m_CaretPos]
   */
  int PrevWord() const;

  /**
   * @brief Returns the character index of the end of the next word (Ctrl+Right)
   * @return New caret position in [m_CaretPos, m_Text.size()]
   */
  int NextWord() const;

  /**
   * @brief Selects all text (Ctrl+A)
   */
  void SelectAll();

  /**
   * @brief Navigates to the previous command history entry (Up arrow)
   */
  void HistoryPrev();

  /**
   * @brief Navigates to the next command history entry (Down arrow)
   */
  void HistoryNext();

  /**
   * @brief WM_CHAR handler (inserts a single character)
   * @param ch Wide character from WM_CHAR wParam
   */
  void OnChar(wchar_t ch);

  /**
   * @brief WM_KEYDOWN handler (dispatches all keyboard editing actions)
   * @param vk Virtual key code from WM_KEYDOWN wParam
   */
  void OnKeyDown(int vk);

  /**
   * @brief WM_LBUTTONDOWN handler (place the caret and begins a mouse drag)
   *
   * @param mx    Mouse x in client coordinates
   * @param shift true if Shift was held
   */
  void OnLButtonDown(int mx, bool shift);

  /**
   * @brief WM_MOUSEMOVE handler (extends the selection during a mouse drag)
   * @param mx Mouse x in client coordinates
   */
  void OnMouseMove(int mx);

  /**
   * @brief WM_LBUTTONDBLCLK handler (selects the word under the cursor)
   * @param mx Mouse x in client coordinates
   */
  void OnDoubleClick(int mx);

  /**
   * @brief Writes str to the clipboard
   * @param str Text to place on the clipboard
   */
  void SetClipboardText(const std::wstring& str);

  /**
   * @brief Reads text from the clipboard
   * @return The clipboard text or an empty string if unavailable
   */
  std::wstring GetClipboardText();

  /**
   * @brief Copies the selected text to the clipboard
   */
  void Copy();

  /**
   * @brief Cuts the selected text to the clipboard
   */
  void Cut();

  /**
   * @brief Pastes clipboard text at the caret (stripping any CR/LF characters)
   */
  void Paste();

  /**
   * @brief Scrolls the caret into view and queues a repaint (called after every edit or caret move)
   */
  void AfterEdit();

  /**
   * @brief Central dispatcher routes messages to the appropriate handler
   *
   * @param hWnd   Window handle
   * @param msg    Windows message identifier
   * @param wParam Message-specific first parameter
   * @param lParam Message-specific second parameter
   * @return       0 for all handled messages or DefWindowProcW for the rest
   */
  LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  //registered keywords drawn in m_crKeyword color
  std::set<std::wstring, std::less<>> m_Keywords;

  //committed command strings (0 is oldest, back() is most recent)
  std::vector<std::wstring> m_History;

  HWND m_hWnd               = nullptr;               //the control handle
  int m_Width               = 0;                     //client width
  int m_Height              = 0;                     //client height
  HDC m_hdcBack             = nullptr;               //compatible DC holding the back buffer bitmap
  HBITMAP m_Bitmap          = nullptr;               //back buffer surface
  HBITMAP m_BitmapOld       = nullptr;               //default bitmap saved on select
  HFONT m_Font              = nullptr;               //font selected for all text operations
  COLORREF m_crBackground   = RGB(24, 24, 28);       //background fill
  COLORREF m_crText         = RGB(230, 230, 230);    //default/plain text color
  COLORREF m_crSelectionBkg = RGB(60, 90, 140);      //selection highlight fill
  COLORREF m_crCaret        = RGB(255, 150, 0);      //caret line color
  COLORREF m_crNumber       = RGB(140, 200, 255);    //numeric literal color
  COLORREF m_crKeyword      = RGB(255, 190, 110);    //registered keyword color
  COLORREF m_crFocusRect    = RGB(200, 200, 210);    //border color when focused
  COLORREF m_crUnfocusRect  = RGB(50, 50, 50);       //border color when unfocused
  std::wstring m_Text       = L"Enter text here..."; //current text content
  int m_CaretPos            = 0;                     //character index of the insertion point
  int m_SelAnchor           = 0;                     //fixed selection end (equals m_CaretPos when there is no selection)
  int m_ScrollPx            = 0;                     //horizontal scroll offset in pixels (0 = left-aligned)
  int m_LineHeight          = 16;                    //font tmHeight measured for vertical centering
  bool m_Focused            = false;                 //true while this control has keyboard focus
  bool m_Dragging           = false;                 //true between WM_LBUTTONDOWN and WM_LBUTTONUP
  int m_HistoryIndex        = -1;                    //offset from back() currently shown (-1 = live input)
  const int PADDING_LEFT    = 6;                     //pixel gap between left edge and first character
};
