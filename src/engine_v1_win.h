// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.

#include <string>
#include <vector>

class CodeSearch {
public:
  enum Options {
    None,
    Substring,
    BeginsWith
  };

  class Client {
  public:
    virtual bool OnIndexProgress(CodeSearch* engine, size_t files, size_t dirs) = 0;
    virtual bool OnError(int error_code) = 0;
  };

  virtual int Index(const wchar_t* root_dir, Client* client) = 0;
  virtual std::vector<std::wstring> Search(const wchar_t* txt, Options options) = 0;
  virtual std::vector<std::wstring> Continue() = 0;
};

// The default is |name| = NULL.
CodeSearch* CodeSearchFactory(const char* name);