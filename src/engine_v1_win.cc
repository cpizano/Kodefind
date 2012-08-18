// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.

#include "target_version_win.h"
#include "engine_v1_win.h"

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

#include "scoped_ptr.h"
#include "thread_pool.h"
#include "tokenizer.h"

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

  enum FileType {
    kUnknown,
    kGyp,
    kCpp
  };

  FileType ClassifyFile(const wchar_t* name, size_t len) {
    size_t of = len - 1;
    if ((len > 2) && (name[of] == L'h') && (name[of-1] == L'.'))
      return kCpp;
    if ((len > 2) && (name[of] == L'c') && (name[of-1] == L'.'))
      return kCpp;
    if ((len > 3) && (name[of] == L'c') && (name[of-1] == L'c') && (name[of-2] == L'.'))
      return kCpp;
    if ((len > 4) && (name[of] == L'p') && (name[of-1] == L'p') && (name[of-2] == L'c') && (name[of-3] == L'.'))
      return kCpp;
    if ((len > 4) && (name[of] == L'p') && (name[of-1] == L'y') && (name[of-2] == L'g') && (name[of-3] == L'.'))
      return kGyp;
    if ((len > 5) && (name[of] == L'y') && (name[of-1] == L'p') && (name[of-2] == L'y') && (name[of-3] == L'g') && (name[of-4] == L'.'))
      return kGyp;

    return kUnknown;
  }

  HANDLE OpenDirectory(const std::wstring& dir) {
    DWORD share = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
    return ::CreateFileW(dir.c_str(), GENERIC_READ , share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  }

  const DWORD kShareAll = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;

}

class V1CodeSearch : public CodeSearch {
public:
  V1CodeSearch();
  virtual int Index(const wchar_t* root_dir, Client* client) override;
  virtual std::vector<std::wstring> Search(const wchar_t* txt, Options options) override;
  virtual std::vector<std::wstring> Continue() override;

private:
  // Represents a single file from the tree.
  struct FileNode {
    std::wstring name;
    size_t dir_ix;
    size_t size;

    FileNode(const std::wstring fname, size_t dir, size_t fsize)
      : name(fname), dir_ix(dir), size(fsize) {
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
  std::vector<std::wstring> V1CodeSearch::SearchImpl(const wchar_t* txt, bool reset, Options options);

  void BuildInvertedIndexAsync(Client* client);

  DWORD MasterThread();
  DWORD FileReadThread();


  DirVect dirs_;
  FileNodes files_;

  FileNodes::const_iterator it_;
  std::wstring search_term_;
  Options current_options_;

  Stats stats_;

  ThreadPool file_io_pool_;
  ThreadPool index_pool_;
};

CodeSearch* CodeSearchFactory(const char* name) {
  if (!name) return new V1CodeSearch();
  return NULL;
}

V1CodeSearch::V1CodeSearch() : current_options_(CodeSearch::None) {
  dirs_.reserve(200);
  files_.reserve(2000);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

class FileWorker : public Worker<FileWorker> {
public:
  FileWorker(ThreadPool* index_pool) : index_pool_(index_pool) {}

  struct Context {
    size_t ix;
    HANDLE file;
    size_t size;
    std::list<std::string> tlist;

    Context(size_t i_ix, HANDLE i_file, size_t i_size)
      : ix(i_ix), file(i_file), size(i_size) {}
  };

  bool OnWork(Context* ctx) {
    if (ctx->ix == -1)
      return false;

    LARGE_INTEGER li = {0};
    if (!::GetFileSizeEx(ctx->file, &li)) {
      __debugbreak();
    }
    if (li.HighPart != 0) {
      __debugbreak();
    }
    ctx->size = li.LowPart;

    HANDLE m = ::CreateFileMappingW(ctx->file, NULL, PAGE_READONLY, 0, 0, NULL);
    ::CloseHandle(ctx->file);

    const char* buf = reinterpret_cast<const char*>(::MapViewOfFile(m, FILE_MAP_READ, 0, 0,  0));
    if (!buf) {
      __debugbreak();
    }

    if (!Tokenize(buf, (buf + ctx->size), ctx->tlist)) {
      // Tokenizer error.
      __debugbreak();
    }

    ::UnmapViewOfFile(buf);
    ::CloseHandle(m);

    index_pool_->PostJob(ctx);
    return true;
  }

private:
  ThreadPool* index_pool_;

};

class IndexWorker : public Worker<IndexWorker> {
public:
  IndexWorker() : count_(0), work_count_(1) {}

  typedef FileWorker::Context Context;

  bool OnWork(Context* ctx) {
    
    for (auto it = ctx->tlist.begin(); it != ctx->tlist.end(); ++it) {
      auto& r = inv_index_[*it];
      if (r.empty() || (r.back() != ctx->ix))
        r.push_back(ctx->ix);
    }

    delete ctx;
    return (++count_ != work_count_);
  }

  void SetCount(int work_count) {
    work_count_ = work_count;
    count_ = 0;
  }

private:
  int count_;
  int work_count_;
  std::unordered_map<std::string, std::vector<size_t>> inv_index_;
};

template <typename C, DWORD (C::*pmf)()>
DWORD __stdcall ThreadProcX(void* ctx) {
  V1CodeSearch* cs = reinterpret_cast<C*>(ctx);
  return (cs->*pmf)();
}

DWORD V1CodeSearch::FileReadThread() {
  FileWorker worker(&index_pool_);
  file_io_pool_.EnterLoop(&worker);
  return 0;
}

DWORD V1CodeSearch::MasterThread() {
  // Post 50 file read IO jobs to the file threads
  // Process at least 25 of them.
  // repeat.
  IndexWorker index_worker;

  HANDLE threads[4];
  for (int ix = 0; ix != 4; ++ix) {
    threads[ix] = ::CreateThread(NULL, 0, &ThreadProcX<V1CodeSearch, &V1CodeSearch::FileReadThread>, this, 0, NULL);
  }

  size_t curr = 0;
  
  while(true) {
    int count = 0;  
    do {
      FileNode fn = files_[curr];
      if (ClassifyFile(fn.name.c_str(), fn.name.size()) == kCpp) {
        // It is code, we need to process it.
        std::wstring path(dirs_[fn.dir_ix]);
        path.append(1, L'\\');
        path.append(fn.name);
        HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, kShareAll, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (f != INVALID_HANDLE_VALUE) {
          ++count;
          file_io_pool_.PostJob(new FileWorker::Context(curr, f, fn.size));
        }
      }
      ++curr;
    } while((curr != files_.size()) && (count != 50));

    index_worker.SetCount(count);
    index_pool_.EnterLoop(&index_worker);

    if (curr == files_.size()) {
      for (int ix = 0; ix != 4; ++ix) {
        file_io_pool_.PostJob(new FileWorker::Context(-1, 0, 0));
      }
      break;
    }
  }

  DWORD wr = ::WaitForMultipleObjects(4, threads, TRUE, INFINITE);

  return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
          ULONGLONG time_taken = ::GetTickCount64() - time_start;
          stats_.time_taken_secs = static_cast<size_t>(time_taken / 1000);
#if 0
          // Start indexing the content. Not yet ready to be used.
          HANDLE th = ::CreateThread(NULL, 0, &ThreadProcX<V1CodeSearch, &V1CodeSearch::MasterThread>, this, 0, NULL);
#endif
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
    } else if (fbdi->FileAttributes & (FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_NORMAL)) {
      if (ClassifyFile(fbdi->FileName, len) == kUnknown) {
        ++stats_.files_discarded;
      } else {
        // Add this file.
        FileNode file(std::wstring(fbdi->FileName, len), parent_dir_ix, fbdi->AllocationSize.LowPart);
        files_.push_back(file);
      }
    } else if (fbdi->FileAttributes & FILE_ATTRIBUTE_HIDDEN) {
      // Hidden files and directories.
      ++stats_.hidden_discarded;
    } else {
      ::OutputDebugStringA("kodefind:weird FileAttributes\n"); 
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

std::vector<std::wstring> V1CodeSearch::Search(const wchar_t* txt, Options options) {
  return SearchImpl(txt, true, options);
}

std::vector<std::wstring> V1CodeSearch::Continue() {
  return SearchImpl(NULL, false, CodeSearch::None);
}

std::vector<std::wstring> V1CodeSearch::SearchImpl(const wchar_t* txt, bool reset, Options options) {

  if (reset) {
    it_ = files_.begin();
    search_term_ = txt;
    current_options_ = options;
  } else {
    txt = search_term_.c_str();
    options = current_options_;
  }

  const FileNodes::const_iterator end = files_.end();
  std::vector<std::wstring> matches;

  size_t len = wcslen(txt);

  if (options == CodeSearch::BeginsWith) {
    for (; it_ != end; ++it_) {
      
      if (it_->name[0] != txt[0])
        continue;
      if (0 != it_->name.find(txt, 0, len))
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
  } else if (options == CodeSearch::Substring) {
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
  } else {
    __debugbreak();
  }

  return matches;
}

