#pragma once

#include <Stream.h>

#include "RssParser.h"

class RssParserStream : public Stream {
 public:
  explicit RssParserStream(RssParser& parser);

  // These functions are not implemented for that stream
  int available() override;
  int peek() override;
  int read() override;

  virtual size_t write(uint8_t c) override;
  virtual size_t write(const uint8_t* buffer, size_t size) override;

  ~RssParserStream() override;

 private:
  RssParser& parser;
};
