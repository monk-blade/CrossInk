#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

using oflag_t = int;
constexpr oflag_t O_RDONLY = 0;
constexpr oflag_t O_RDWR = 1;

class HalFile : public Print {
 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&) noexcept;
  HalFile& operator=(HalFile&&) noexcept;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush() override;
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();
  size_t write(const void* buf, size_t count);
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(uint8_t byte) override;
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;

 private:
  struct Impl;
  explicit HalFile(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
  friend class HalStorage;
};

class HalStorage {
 public:
  static HalStorage& getInstance();
  static void setRoot(std::string root);
  static std::string hostPath(const std::string& path);

  HalFile open(const char* path, oflag_t flags = O_RDONLY);
  bool mkdir(const char* path, bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);
  bool removeDir(const char* path);
  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
};

#define Storage HalStorage::getInstance()

using FsFile = HalFile;
