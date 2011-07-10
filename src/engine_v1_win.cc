#include "target_version_win.h"
#include "engine_v1_win.h"

#include <algorithm>
#include <string>
#include <vector>

template <class C>
class scoped_ptr {
 public:

  // The element type
  typedef C element_type;

  // Constructor.  Defaults to initializing with NULL.
  // There is no way to create an uninitialized scoped_ptr.
  // The input parameter must be allocated with new.
  explicit scoped_ptr(C* p = NULL) : ptr_(p) { }

  // Destructor.  If there is a C object, delete it.
  // We don't need to test ptr_ == NULL because C++ does that for us.
  ~scoped_ptr() {
    enum { type_must_be_complete = sizeof(C) };
    delete ptr_;
  }

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  void reset(C* p = NULL) {
    if (p != ptr_) {
      enum { type_must_be_complete = sizeof(C) };
      delete ptr_;
      ptr_ = p;
    }
  }

  // Accessors to get the owned object.
  // operator* and operator-> will assert() if there is no current object.
  C& operator*() const {
    assert(ptr_ != NULL);
    return *ptr_;
  }
  C* operator->() const  {
    assert(ptr_ != NULL);
    return ptr_;
  }
  C* get() const { return ptr_; }

  // Comparison operators.
  // These return whether two scoped_ptr refer to the same object, not just to
  // two different but equal objects.
  bool operator==(C* p) const { return ptr_ == p; }
  bool operator!=(C* p) const { return ptr_ != p; }

  // Release a pointer.
  // The return value is the current pointer held by this object.
  C* release() {
    C* retVal = ptr_;
    ptr_ = NULL;
    return retVal;
  }

 private:
  C* ptr_;

  // Disallow evil constructors
  scoped_ptr(const scoped_ptr&);
  void operator=(const scoped_ptr&);
};

//================================================================================================================

namespace {
  // Should fit batches of 4000 files.
  const size_t dir_buf_sz = 512 * 1024;

  bool IgnoreDirName(const wchar_t* name, size_t len) {
    if ((len == 1) && (name[0] == L'.'))
      return true;
    if ((len == 2) && (name[0] == L'.') && (name[1] == L'.'))
      return true;
    if (len != 4)
      return false;
    return (0 == wcsncmp(name, L".svn", len));
  }

  bool IgnoreFileName(const wchar_t* name, size_t len) {
    size_t of = len - 1;
    if ((len > 2) && (name[of] == L'h') && (name[of-1] == L'.'))
      return false;
    if ((len > 2) && (name[of] == L'c') && (name[of-1] == L'.'))
      return false;
    if ((len > 3) && (name[of] == L'c') && (name[of-1] == L'c') && (name[of-2] == L'.'))
      return false;
    if ((len > 4) && (name[of] == L'p') && (name[of-1] == L'p') && (name[of-2] == L'c') && (name[of-3] == L'.'))
      return false;
    if ((len > 4) && (name[of] == L'p') && (name[of-1] == L'y') && (name[of-2] == L'g') && (name[of-3] == L'.'))
      return false;

    return true;
  }

  HANDLE OpenDirectory(const std::wstring& dir) {
    DWORD share = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
    return ::CreateFileW(dir.c_str(), GENERIC_READ , share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  }

}

class V1CodeSearch : public CodeSearch {
public:
  V1CodeSearch();
  virtual int Index(const wchar_t* root_dir, Client* client) override;
  virtual std::vector<std::wstring> Search(const wchar_t* txt) override;
  virtual std::vector<std::wstring> Continue() override;

private:
  // Represents a single file from the tree.
  struct FileNode {
    std::wstring name;
    size_t dir_ix;

    FileNode(const std::wstring fname, size_t dir)
      : name(fname), dir_ix(dir) {
    }

    bool operator<(const FileNode& rhs) {
      return (name < rhs.name);
    }
  };

  typedef std::vector<FileNode> FileNodes;
  typedef std::vector<std::wstring> DirVect;

  struct Stats {
    size_t dirs_discarded;
    size_t files_discarded;
    size_t hidden_discarded;
    size_t time_taken_secs;
    Stats() 
      : dirs_discarded(0), files_discarded(0), hidden_discarded(0), time_taken_secs(0) {
    }
  };

  int ProcessDir(const FILE_ID_BOTH_DIR_INFO* fbdi, size_t parent_dir_ix);
  std::vector<std::wstring> V1CodeSearch::SearchImpl(const wchar_t* txt, bool reset);

  DirVect dirs_;
  FileNodes files_;

  FileNodes::const_iterator it_;
  std::wstring search_term_;

  Stats stats_;
};

CodeSearch* CodeSearchFactory(const char* name) {
  if (!name) return new V1CodeSearch();
  return NULL;
}

V1CodeSearch::V1CodeSearch() {
  dirs_.reserve(200);
  files_.reserve(2000);
}

int V1CodeSearch::Index(const wchar_t* root_dir, Client* client) {

  ULONGLONG time_start = ::GetTickCount64();

  HANDLE hdir = OpenDirectory(root_dir);
  if (hdir == INVALID_HANDLE_VALUE)
    return -1;

  scoped_ptr<char> dir_buf(new char[dir_buf_sz]);
  int status = 0;

  dirs_.push_back(root_dir);
  size_t curr_dir = 0;

  do {
    if (client) {
      client->OnIndexProgress(this, files_.size(), dirs_.size());
    }

    if (!::GetFileInformationByHandleEx(hdir, FileIdBothDirectoryInfo, dir_buf.get(), dir_buf_sz)) {
      DWORD gle = ::GetLastError();
      if (ERROR_NO_MORE_FILES == gle) {
        ::CloseHandle(hdir); 
        ++curr_dir;
        if (curr_dir == dirs_.size()) {
          // We are done collecting all the files. Now it is time to sort?
          // std::sort(files_.begin(), files_.end());
          ULONGLONG time_taken = ::GetTickCount64() - time_start;
          stats_.time_taken_secs = static_cast<size_t>(time_taken / 1000);
          return 0;
        }

        hdir = OpenDirectory(dirs_[curr_dir]);
        if (hdir == INVALID_HANDLE_VALUE)
          return -1;
        else
          continue;

      } else {
        // Unexpected.
        ::CloseHandle(hdir);
        return gle;
      }
    }

    status = ProcessDir(reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(dir_buf.get()), curr_dir);

  } while(status == 0);

  return status;
}

int V1CodeSearch::ProcessDir(const FILE_ID_BOTH_DIR_INFO* fbdi, size_t parent_dir_ix) {
  do {
    size_t len = fbdi->FileNameLength / sizeof(wchar_t);
    if (0 == len) {
      __debugbreak();
      return 1;
    }

    if (fbdi->FileAttributes == FILE_ATTRIBUTE_DIRECTORY) {
      
      if (IgnoreDirName(fbdi->FileName, len)) {
        ++stats_.dirs_discarded;
      } else {
        // Add this directory.
        std::wstring dir_name(dirs_[parent_dir_ix]);
        dir_name.append(1, L'\\');
        dir_name.append(fbdi->FileName, len);
        dirs_.push_back(dir_name);
      }
    } else if (fbdi->FileAttributes == FILE_ATTRIBUTE_ARCHIVE) {
      if (IgnoreFileName(fbdi->FileName, len)) {
        ++stats_.files_discarded;
      } else {
        // Add this file.
        FileNode file(std::wstring(fbdi->FileName, len), parent_dir_ix);
        files_.push_back(file);
      }
    } else if (fbdi->FileAttributes & FILE_ATTRIBUTE_HIDDEN) {
      // Hidden files and directories.
      ++stats_.hidden_discarded;
    } else {
      __debugbreak();
    }

    if (!fbdi->NextEntryOffset) {
      // Done with this batch.
      return 0;
    }

    // Move to the next entry.
    fbdi = reinterpret_cast<FILE_ID_BOTH_DIR_INFO*>(ULONG_PTR(fbdi) + fbdi->NextEntryOffset);
  } while(true);
  return 0;
}

std::vector<std::wstring> V1CodeSearch::Search(const wchar_t* txt) {
  return SearchImpl(txt, true);
}

std::vector<std::wstring> V1CodeSearch::Continue() {
  return SearchImpl(NULL, false);
}



std::vector<std::wstring> V1CodeSearch::SearchImpl(const wchar_t* txt, bool reset) {

  if (reset) {
    it_ = files_.begin();
    search_term_ = txt;
  } else {
    txt = search_term_.c_str();
  }

  const FileNodes::const_iterator end = files_.end();
  std::vector<std::wstring> matches;

  for (; it_ != end; ++it_) {
    if (it_->name.find(txt) == std::string::npos)
      continue;
    // A match has been found.
    std::wstring result(dirs_[it_->dir_ix]);
    result.append(1, L'\\');
    result.append(it_->name);
    matches.push_back(result);
    if (matches.size() == 25) {
      return matches;
    }
  }

  return matches;
}

