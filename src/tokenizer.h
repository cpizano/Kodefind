#pragma once
// Copyright (c) 2011 Carlos Pizano-Uribe
// Please see the README file for attribution and license details.

#include <string>
#include <list>

struct Buffer {
	union {
		void*	pbuff;
		char*	cbuff;
	};
	size_t size;
	Buffer(char* cb, size_t bsize)
			:cbuff(cb), size(bsize){}
};

class DataStream {
 public:
	virtual size_t Read(Buffer& buffer) = 0;
	virtual size_t GetPos() = 0;
	virtual size_t SetPos(size_t) = 0;
};

class MemDataStream : public DataStream {
 public:
  MemDataStream(char* start, char* end);
  ~MemDataStream();

	virtual size_t Read(Buffer& buffer) override;
	virtual size_t GetPos() override;
	virtual size_t SetPos(size_t) override;

 private:
   char* start_;
   char* end_;
   size_t pos_;
};

bool Tokenize(DataStream& stream, std::list<std::string>& tlist);

bool Tokenize(const char* beg, const char* end, std::list<std::string>& tlist);
