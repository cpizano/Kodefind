// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.

#include "target_version_win.h"
#include "engine_v1_win.h"
#include "resource.h"

#include <vector>
#include <string>
#include <algorithm>

typedef std::vector<std::wstring> VoWStr;
typedef std::vector<VoWStr*> VoVoWS;

HWND        g_dlg  = 0;
HANDLE      g_term = NULL;
HANDLE      g_thrd = NULL;
CodeSearch* g_cs   = NULL;
VoVoWS      g_vovows;

void DeleteVoWStr(VoWStr* item) {
  delete item;
}

bool SelectFolder(HWND parent, std::wstring* path) {
  IFileDialog *fdia;
  IShellItem* item;
  HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC, __uuidof(IFileDialog), (void**)&fdia); 
  if (SUCCEEDED(hr)) {
    hr = fdia->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(hr)) {
      hr = fdia->Show(parent);
      if (SUCCEEDED(hr)) {
        hr = fdia->GetResult(&item);
        if (SUCCEEDED(hr)) {
          wchar_t* name = NULL;
          hr = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
          if (SUCCEEDED(hr)) {
            *path = name;
            ::CoTaskMemFree(name);
            fdia->Release();
            return true;
          }
        }
      } 
    }
    fdia->Release();
  }
  return false;
}

class Progress : public CodeSearch::Client {
  public:
    Progress() {
      tc_ = ::GetTickCount();
    }

    virtual bool OnIndexProgress(CodeSearch* engine, size_t files, size_t dirs) override {
      DWORD ctc = ::GetTickCount();
      // Throttle the posted messages to about 10 per second.
      if (ctc - tc_ > 100) {
        tc_ = ctc;
        ::PostMessageW(g_dlg, WM_USER+1, files, dirs);
      }
      return true;
    }

    virtual bool OnError(int error_code) override {
      __debugbreak();
      return false;
    }

private:
  DWORD tc_;
};


DWORD WINAPI IndexSearchTreadProc(void* ctx) {
  g_cs = CodeSearchFactory(NULL);
  if (!g_cs)
    return 1;
  Progress progress;
  std::wstring* dir = reinterpret_cast<std::wstring*>(ctx);
  int rv = g_cs->Index(dir->c_str(), &progress);
  
  delete dir;
  if (rv != 0) {
    ::PostMessageW(g_dlg, WM_USER + 2, rv, 0);
    return 1;
  }

  // Done indexing, now wait for queries, or if the handle is signaled, exit.
  ::PostMessageW(g_dlg, WM_USER + 2, 0, 0);
  while (true) {
    DWORD v = ::WaitForSingleObjectEx(g_term, INFINITE, TRUE);
    if (WAIT_IO_COMPLETION != v)
      break;
  }

  return 0;
}

void CALLBACK ApcNewTextInput(ULONG_PTR ctx) {
  wchar_t* txt = reinterpret_cast<wchar_t*>(ctx);
  VoWStr* res = new VoWStr(g_cs->Search(txt));
  delete txt;
  while (true) {
    if (res->empty()) {
      delete res;
      return;
    }
    ::PostMessageW(g_dlg, WM_USER + 3, reinterpret_cast<WPARAM>(res), 0);
    res = new VoWStr(g_cs->Continue());
  }
}

bool InsertListViewItems(HWND list, VoWStr* matches) {
  g_vovows.push_back(matches);
  LVITEM lvI = {0};
  // Parent gets an LVN_GETDISPINFO when its time to display.
  lvI.pszText   = LPSTR_TEXTCALLBACKW;
  lvI.mask      = LVIF_TEXT|LVIF_PARAM;
  
  int next = ListView_GetItemCount(list);

  for (size_t ix = 0; ix < matches->size(); ++ix) {
    lvI.iItem  = static_cast<int>(next + ix);
    lvI.lParam = reinterpret_cast<LPARAM>(&matches->at(ix));
    if (ListView_InsertItem(list, &lvI) == -1)
      return false;
  }
  return true;
}

bool DeleteAllListViewItems(HWND list) {
  ListView_DeleteAllItems(list);
  std::for_each(g_vovows.begin(), g_vovows.end(), DeleteVoWStr);
  g_vovows.clear();
  return true;
}

bool InitListViewColumns(HWND list) { 
    LVCOLUMN lvc;
    lvc.mask = LVCF_FMT | LVCF_WIDTH;
    lvc.cx = 1000;
    lvc.fmt = LVCFMT_LEFT;
    return (ListView_InsertColumn(list, 0, &lvc) == -1)? false : true;
}

void OpenFileWithApplication(const std::wstring& file) {
  ::ShellExecuteW(NULL, L"open", file.c_str(), NULL, NULL, SW_SHOW);
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	
	switch (message) {
	  case WM_INITDIALOG: {
        g_dlg = hDlg;
        ::SetWindowTextW(hDlg, L"select source directory");
		    return static_cast<INT_PTR>(TRUE);
      }

    case WM_SYSCOMMAND:
      if (SC_CLOSE == wParam) {
        ::SignalObjectAndWait(g_term, g_thrd, INFINITE, FALSE);
        ::EndDialog(hDlg, LOWORD(wParam));
			  return static_cast<INT_PTR>(TRUE);
      }
      break;

    case WM_COMMAND:
      if (LOWORD(wParam) == IDC_BUTTON1) {
        std::wstring* dir_to_scan = new std::wstring;
        if (!SelectFolder(hDlg, dir_to_scan)) {
          delete dir_to_scan;
          break;
        }
        // Start a new thread and index the directory.
        ::EnableWindow(::GetDlgItem(hDlg, IDC_BUTTON1), FALSE);
        g_thrd = ::CreateThread(NULL, 0, IndexSearchTreadProc, dir_to_scan, 0, NULL);

      } else if (LOWORD(wParam) == IDC_EDIT1) {
        // Change on the edit control.
        switch (HIWORD(wParam)) {
          case EN_CHANGE:
            wchar_t* txt = new wchar_t[64];
            UINT count = ::GetDlgItemTextW(hDlg, IDC_EDIT1, txt, 64);
            if (count > 2) {
              // clean the list results and free the previous vectors.
              DeleteAllListViewItems(::GetDlgItem(hDlg, IDC_LIST1));
              // Ask the other thread to start a new search.
              ::QueueUserAPC(ApcNewTextInput, g_thrd, reinterpret_cast<ULONG_PTR>(txt));
            } else {
              delete txt;
            }
            break;
        }
      }
      break;

    case WM_USER + 1: {
        // Progess indexing files.
        wchar_t buf[30];
        ::wsprintfW(buf, L"files: %d dirs: %d", wParam, lParam);
        ::SetWindowTextW(hDlg, buf);
      }
      break;

    case WM_USER + 2: {
         // Finished indexing files, or unrecoverable error.
        if (wParam == 0) {
          ::RegisterHotKey(hDlg, 0, MOD_WIN|MOD_NOREPEAT, 0x5A);

          HWND list = ::GetDlgItem(hDlg, IDC_LIST1);
          InitListViewColumns(list);
          ::EnableWindow(list, TRUE);
          HWND edit = ::GetDlgItem(hDlg, IDC_EDIT1);
          ::EnableWindow(edit, TRUE);
          ::SetFocus(edit);
        } else {
          ::SetWindowTextW(hDlg, L"error indexing");
        }

      }
      break;

    case WM_USER + 3: {
        // A set of results is available, insert them in the UI.
        VoWStr* matches = reinterpret_cast<VoWStr*>(wParam);
        InsertListViewItems(::GetDlgItem(hDlg, IDC_LIST1), matches);
      }
      break;

    case WM_NOTIFY: {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->code == LVN_GETDISPINFO) {
          NMLVDISPINFO* lvdi = (NMLVDISPINFO*)lParam;
          // we don't have sub-items and we don't have anything but text.
          if (lvdi->item.iSubItem != 0) __debugbreak();
          if (lvdi->item.mask != LVIF_TEXT) __debugbreak();

          const std::wstring* txt = reinterpret_cast<std::wstring*>(lvdi->item.lParam);
          // msdn says that strcpy into pszText works!! ??
          lvdi->item.pszText = const_cast<wchar_t*>(txt->c_str());
        } else if (hdr->code == LVN_ITEMACTIVATE) {
          // Item activated. (Double-clicked).
          NMITEMACTIVATE* nmia = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
          LVITEM item = {0};
          item.mask = LVIF_PARAM;
          item.iItem = nmia->iItem;
          if (ListView_GetItem(nmia->hdr.hwndFrom, &item)) {
            std::wstring* file = reinterpret_cast<std::wstring*>(item.lParam);
            OpenFileWithApplication(*file);
          }

        }
      }
      break;
  
    case WM_HOTKEY: {
        ::SetFocus(::GetDlgItem(hDlg, IDC_EDIT1));
        ::SetForegroundWindow(hDlg);
      }
      break;
  }

	return (INT_PTR)FALSE;
}

int APIENTRY _tWinMain(HINSTANCE hInst, HINSTANCE,
                       LPTSTR lpCmdLine, int nCmdShow) {

  ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  g_term = ::CreateEventW(NULL, TRUE, FALSE, NULL);

  return ::DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_FORMVIEW), NULL, DlgProc, 0L);

}
