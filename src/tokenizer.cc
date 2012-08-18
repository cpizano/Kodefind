#include "tokenizer.h"

MemDataStream::MemDataStream(char* start, char* end)
    : start_(start), end_(end), pos_(0ul) {
}

MemDataStream::~MemDataStream() {
}

size_t MemDataStream::Read(Buffer& buffer) {
  size_t left = end_ - &start_[pos_];
  size_t read = std::min(buffer.size, left);
  memcpy(buffer.pbuff, &start_[pos_], read);
  return read;
}

size_t MemDataStream::GetPos() {
  return pos_;
}

size_t MemDataStream::SetPos(size_t pos) {
  size_t last_pos = pos;
  pos_ = pos;
  return last_pos;
}

void VetoInsertTokens(const std::string token, std::list<std::string>& tlist) {
  //
  if ((token == "auto") ||
      (token == "const") ||
      (token == "double") ||
      (token == "float") ||
      (token == "int") ||
      (token == "short") ||
      (token == "struct") ||
      (token == "unsigned") ||
      (token == "for") ||
      (token == "long") ||
      (token == "signed") ||
      (token == "switch") ||
      (token == "void") ||
      (token == "case") ||
      (token == "default") ||
      (token == "enum") ||
      (token == "goto") ||
      (token == "sizeof") ||
      (token == "typedef") ||
      (token == "volatile") ||
      (token == "char") ||
      (token == "do") ||
      (token == "extern") ||
      (token == "if") ||
      (token == "return") ||
      (token == "static") ||
      (token == "union") ||
      (token == "while") ||
      (token == "dynamic_cast") ||
      (token == "namespace") ||
      (token == "reinterpret_cast") ||
      (token == "bool") ||
      (token == "explicit") ||
      (token == "new") ||
      (token == "static_cast") ||
      (token == "operator") ||
      (token == "template") ||
      (token == "typename") ||
      (token == "class") ||
      (token == "friend") ||
      (token == "private") ||
      (token == "this") ||
      (token == "using") ||
      (token == "const_cast") ||
      (token == "inline") ||
      (token == "public") ||
      (token == "virtual") ||
      (token == "delete") ||
      (token == "protected") ||
      (token == "wchar_t") ||
      (token == "is") ||
      (token == "at") ||
      (token == "of") ||
      (token == "a") ||
      (token == "c"))
    return;

  tlist.push_back(token);
}

bool Tokenize(const char* beg, const char* end, std::list<std::string>& tlist) {
  const char* tok_start = NULL;

	while (beg < end) {   
    char c = *beg;
    if (c > 0) {
      // Some files are UTF-8 encoded. Here we ignore anything that is not latin.
      if (iscntrl(c) && (isspace(c) == 0))
        return false;

      if (!tok_start) {
        // No token yet.
        if (isalnum(c))
          tok_start = beg;
      } else {
        // Middle of a token.
        if (!isalnum(c)) {
          VetoInsertTokens(std::string(tok_start, beg), tlist);
          //tlist.push_back(std::string(tok_start, beg));
          tok_start = NULL;
        }
      }
    }

    ++beg;
  }

  if (tok_start) {
    VetoInsertTokens(std::string(tok_start, beg - 1), tlist);
    //tlist.push_back(std::string(tok_start, beg - 1));
  }

  return true;
}

bool Tokenize(DataStream& stream, std::list<std::string>& tlist) {
	char backing[16] = {0};
	Buffer buffer(backing, sizeof(backing));
	size_t retrieved = 0;
	bool exit = false;
	std::string token;
	
	do {
		retrieved = stream.Read(buffer);
		if (retrieved < buffer.size) {
			buffer.size = retrieved;
			exit = true;
		}

		for(size_t ix =0; ix != buffer.size; ++ix) {
			char c = buffer.cbuff[ix];

			if (isalnum(c) || (c == '_') || (c == '.')) {
				token.append(1, c);
			} else {
				if (isspace(c)) {
					if (token.empty())
						continue;
					tlist.push_back(token);
					token.clear();

				} else if (isprint(c)) {
					switch (c) {
						case ';':             // C token.
						case ',':             // C token.
						case '{':             // C token.
						case '}':             // C token.
						case '(':             // C token.
						case ')':             // C token.
						case '*':             // C token.
						case '<':             // ApiSpec token.
						case '>':             // ApiSpec token.
						case '=':             // ApiSpec token.
						case '"':             // ApiSpec token.
							if (!token.empty()) {
								tlist.push_back(token);
								token.clear();
							}
							tlist.push_back(std::string(1, c));
							break;
						default:
							return false;							
					}
				} else {
					continue;
				}
			}
		}

	} while(!exit);
	return true;
}
