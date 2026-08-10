#include "HalStorage.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
std::string rootPath = "/tmp/crosspoint-rss-cache-test";

std::string mapPath(const std::string& path) {
  if (path.empty()) return rootPath;
  if (path.front() == '/') return rootPath + path;
  return rootPath + "/" + path;
}
}  // namespace

struct HalFile::Impl {
  fs::path path;
  std::fstream stream;
  bool directory = false;
  bool writable = false;
  std::vector<fs::directory_entry> entries;
  size_t entryIndex = 0;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
HalFile::~HalFile() { close(); }
HalFile::HalFile(HalFile&&) noexcept = default;
HalFile& HalFile::operator=(HalFile&&) noexcept = default;

void HalFile::flush() {
  if (impl_ && impl_->stream.is_open()) impl_->stream.flush();
}

size_t HalFile::getName(char* name, const size_t len) {
  if (!name || len == 0 || !impl_) return 0;
  const std::string value = impl_->path.filename().string();
  const size_t copied = std::min(value.size(), len - 1);
  std::copy_n(value.data(), copied, name);
  name[copied] = '\0';
  return copied;
}

size_t HalFile::fileSize() {
  if (!impl_ || impl_->directory || !fs::exists(impl_->path)) return 0;
  return static_cast<size_t>(fs::file_size(impl_->path));
}

size_t HalFile::size() { return fileSize(); }
uint64_t HalFile::fileSize64() { return fileSize(); }

size_t HalFile::position() const {
  if (!impl_ || !impl_->stream.is_open()) return 0;
  const auto readPosition = impl_->stream.tellg();
  if (readPosition >= 0) return static_cast<size_t>(readPosition);
  const auto writePosition = impl_->stream.tellp();
  return writePosition >= 0 ? static_cast<size_t>(writePosition) : 0;
}

bool HalFile::seek(const size_t pos) { return seekSet(pos); }
bool HalFile::seek64(const uint64_t pos) { return seekSet(static_cast<size_t>(pos)); }

bool HalFile::seekSet(const size_t pos) {
  if (!impl_ || impl_->directory || !impl_->stream.is_open()) return false;
  impl_->stream.clear();
  impl_->stream.seekg(static_cast<std::streamoff>(pos));
  if (impl_->writable) impl_->stream.seekp(static_cast<std::streamoff>(pos));
  return !impl_->stream.fail();
}

bool HalFile::seekCur(const int64_t offset) {
  const int64_t next = static_cast<int64_t>(position()) + offset;
  return next >= 0 && seekSet(static_cast<size_t>(next));
}

int HalFile::available() const {
  if (!impl_ || impl_->directory) return 0;
  const size_t pos = position();
  const size_t total = const_cast<HalFile*>(this)->fileSize();
  return pos < total ? static_cast<int>(std::min<size_t>(total - pos, INT_MAX)) : 0;
}

int HalFile::read(void* buffer, const size_t count) {
  if (!impl_ || impl_->directory || !impl_->stream.is_open()) return 0;
  impl_->stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
  return static_cast<int>(impl_->stream.gcount());
}

int HalFile::read() {
  uint8_t byte = 0;
  return read(&byte, 1) == 1 ? byte : -1;
}

size_t HalFile::write(const void* buffer, const size_t count) {
  if (!impl_ || impl_->directory || !impl_->stream.is_open()) return 0;
  impl_->stream.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));
  return impl_->stream.fail() ? 0 : count;
}

size_t HalFile::write(const uint8_t* buffer, const size_t count) { return write(static_cast<const void*>(buffer), count); }

size_t HalFile::write(const uint8_t byte) { return write(&byte, 1); }

bool HalFile::rename(const char* newPath) {
  if (!impl_) return false;
  std::error_code error;
  fs::rename(impl_->path, mapPath(newPath), error);
  if (error) return false;
  impl_->path = mapPath(newPath);
  return true;
}

bool HalFile::isDirectory() const { return impl_ && impl_->directory; }
void HalFile::rewindDirectory() {
  if (impl_ && impl_->directory) impl_->entryIndex = 0;
}

bool HalFile::close() {
  if (impl_ && impl_->stream.is_open()) impl_->stream.close();
  return true;
}

HalFile HalFile::openNextFile() {
  if (!impl_ || !impl_->directory || impl_->entryIndex >= impl_->entries.size()) return {};
  const auto entry = impl_->entries[impl_->entryIndex++];
  auto child = std::make_shared<Impl>();
  child->path = entry.path();
  child->directory = entry.is_directory();
  if (!child->directory) {
    child->writable = true;
    child->stream.open(child->path, std::ios::in | std::ios::out | std::ios::binary);
  }
  return HalFile(std::move(child));
}

bool HalFile::isOpen() const { return impl_ && (impl_->directory || impl_->stream.is_open()); }
HalFile::operator bool() const { return isOpen(); }

HalStorage& HalStorage::getInstance() {
  static HalStorage storage;
  return storage;
}

void HalStorage::setRoot(std::string root) {
  rootPath = std::move(root);
  fs::remove_all(rootPath);
  fs::create_directories(rootPath);
}

std::string HalStorage::hostPath(const std::string& path) { return mapPath(path); }

HalFile HalStorage::open(const char* path, const oflag_t flags) {
  const fs::path mapped = mapPath(path ? path : "");
  if (fs::is_directory(mapped)) {
    auto impl = std::make_shared<HalFile::Impl>();
    impl->path = mapped;
    impl->directory = true;
    for (const auto& entry : fs::directory_iterator(mapped)) impl->entries.push_back(entry);
    return HalFile(std::move(impl));
  }
  if (!fs::exists(mapped)) return {};
  auto impl = std::make_shared<HalFile::Impl>();
  impl->path = mapped;
  impl->writable = flags == O_RDWR;
  const auto mode = impl->writable ? std::ios::in | std::ios::out | std::ios::binary : std::ios::in | std::ios::binary;
  impl->stream.open(mapped, mode);
  return impl->stream.is_open() ? HalFile(std::move(impl)) : HalFile{};
}

bool HalStorage::mkdir(const char* path, const bool) {
  std::error_code error;
  fs::create_directories(mapPath(path), error);
  return !error;
}

bool HalStorage::exists(const char* path) { return fs::exists(mapPath(path)); }

bool HalStorage::remove(const char* path) {
  std::error_code error;
  const bool removed = fs::remove(mapPath(path), error);
  return removed && !error;
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  std::error_code error;
  fs::rename(mapPath(oldPath), mapPath(newPath), error);
  return !error;
}

bool HalStorage::rmdir(const char* path) { return removeDir(path); }
bool HalStorage::removeDir(const char* path) {
  std::error_code error;
  fs::remove_all(mapPath(path), error);
  return !error;
}

bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  file = open(path, O_RDONLY);
  return static_cast<bool>(file);
}

bool HalStorage::openFileForRead(const char* module, const std::string& path, HalFile& file) {
  return openFileForRead(module, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  const fs::path mapped = mapPath(path);
  fs::create_directories(mapped.parent_path());
  auto impl = std::make_shared<HalFile::Impl>();
  impl->path = mapped;
  impl->writable = true;
  impl->stream.open(mapped, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
  file = impl->stream.is_open() ? HalFile(std::move(impl)) : HalFile{};
  return static_cast<bool>(file);
}

bool HalStorage::openFileForWrite(const char* module, const std::string& path, HalFile& file) {
  return openFileForWrite(module, path.c_str(), file);
}
