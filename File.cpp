/*	MCM file compressor

  Copyright (C) 2015, Google Inc.
  Authors: Mathieu Chartier

  LICENSE

    This file is part of the MCM file compressor.

    MCM is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MCM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MCM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "File.hpp"

void FileList::read(Stream* stream) {
  resize(stream->leb128Decode());
  std::vector<size_t> lens(size());
  for (auto& f : *this) {
    f.name_ = stream->readString();
  }
  for (auto& len : lens) {
    len = stream->leb128Decode();
  }
  // Add prefixes.
  for (size_t i = 1; i < size(); ++i) {
    if (lens[i] > 0) {
      at(i).name_ = at(i - 1).name_.substr(0, lens[i]) + at(i).name_;
    }
  }
  // Encode attributes.
  for (auto& f : *this) {
    f.attributes_ = stream->get();
  }
}

void FileList::write(Stream* stream) {
  // Arrays of elements, prefix elimination for filenames.
  stream->leb128Encode(size());
  // Encode file names.
  const std::string* last_name = nullptr;
  std::vector<size_t> lens;
  for (const auto& f : *this) {
    const std::string& name = f.getName();
    check(!name.empty());
    // Find matching chars.
    size_t len = 0;
    for (;last_name != nullptr && len < last_name->length() && len < name.length(); ++len) {
      if (last_name->operator[](len) != name[len]) {
        break;
      }
    }
    lens.push_back(len);
    stream->writeString(&name[0] + len, '\0');
    last_name = &name;
  }
  for (size_t len : lens) {
    stream->leb128Encode(len);
  }
  // Encode attributes.
  for (const auto& f : *this) {
    stream->put(f.getAttributes());
  }
}

bool FileList::addDirectoryRec(const std::string& dir, const std::string* prefix) {
  auto start_pos = size();
  check(addDirectory(dir, prefix));
  auto end_pos = size();
  for (size_t i = start_pos; i < end_pos; ++i) {
    auto& f = at(i);
    if ((f.getAttributes() & FileInfo::kAttrDirectory) != 0) {
      check(addDirectoryRec(std::string(f.getName()), prefix));
    }
  }
  return true;
}

std::string FileInfo::attrToStr(uint16_t attr) {
  std::ostringstream oss;
  if ((attr & kAttrDirectory) != 0) oss << 'd';
  if ((attr & kAttrReadPermission) != 0) oss << 'r';
  if ((attr & kAttrWritePermission) != 0) oss << 'w';
  if ((attr & kAttrExecutePermission) != 0) oss << 'x';
  if ((attr & kAttrSystem) != 0) oss << 's';
  if ((attr & kAttrHidden) != 0) oss << 'h';
  return oss.str();
}

#ifdef WIN32
#include <Windows.h>
#pragma comment(lib, "User32.lib")

void FileInfo::CreateDir(const std::string& name) {
  CreateDirectoryA(name.c_str(), nullptr);
}

FileInfo::FileInfo(const std::string& name, const std::string* prefix)
  : FileInfo(name, prefix, GetFileAttributesA(name.c_str())) {
}

FileInfo::FileInfo(const std::string& name, const std::string* prefix, uint32_t attributes)
  : name_(name), prefix_(prefix) {
  convertAttributes(attributes);
}

void FileInfo::convertAttributes(uint32_t attrs) {
  attributes_ = 0;
  if ((attrs & FILE_ATTRIBUTE_HIDDEN) != 0) {
    attributes_ |= FileInfo::kAttrHidden;
  }
  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    attributes_ |= FileInfo::kAttrDirectory;
  } else {
    attributes_ |= FileInfo::kAttrExecutePermission | FileInfo::kAttrReadPermission;
  }
  if ((attrs & FILE_ATTRIBUTE_READONLY) == 0) {
    attributes_ |= FileInfo::kAttrWritePermission;
  }
}

bool FileList::addDirectory(const std::string& dir, const std::string* prefix) {
  WIN32_FIND_DATAA ffd;
  std::string name;
  if (prefix != nullptr) {
    name = *prefix;
    if (!dir.empty()) name += "/";
  }
  name += dir;
  auto handle = FindFirstFileA((name + "\\*").c_str(), &ffd);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  do {
    std::string file_name = ffd.cFileName;
    if (file_name != "." && file_name != "..") {
      push_back(FileInfo(dir + "/" + file_name, prefix, ffd.dwFileAttributes));
    }
  } while (FindNextFileA(handle, &ffd) != 0);
  auto err = GetLastError();
  if (err != ERROR_NO_MORE_FILES) {
    return false;
  }
  FindClose(handle);
  return true;
}

#else
#include <sys/stat.h>
#include <dirent.h>

static inline uint32_t stat_mode(const char *fname) {
  struct stat st;
  if (stat(fname, &st) == 0)
    return st.st_mode;
  return 0;
}

void FileInfo::CreateDir(const std::string& name) {
  mkdir(name.c_str(), 0777);
}

FileInfo::FileInfo(const std::string& name, const std::string* prefix)
  : FileInfo(name, prefix, stat_mode(name.c_str())) {
}

FileInfo::FileInfo(const std::string& name, const std::string* prefix, uint32_t attributes)
  : name_(name), prefix_(prefix) {
  convertAttributes(attributes);
}

void FileInfo::convertAttributes(uint32_t attrs) {
  attributes_ = 0;
  if (S_ISDIR(attrs)) {
    attributes_ |= FileInfo::kAttrDirectory;
  } else {
    if (attrs & 0111) {
      attributes_ |= FileInfo::kAttrExecutePermission;
    }
  }
}

bool FileList::addDirectory(const std::string& dir, const std::string* prefix) {
  std::string name;
  if (prefix != nullptr) {
    name = *prefix;
    if (!dir.empty()) name += "/";
  }
  name += dir;
  DIR *dirp = opendir(name.c_str());
  if (dirp == nullptr) {
    return false;
  }
  struct dirent *dent;
  while ((dent = readdir(dirp)) != nullptr) {
    std::string file_name = dent->d_name;
    if (file_name != "." && file_name != "..") {
      push_back(FileInfo(dir + "/" + file_name, prefix));
    }
  }
  return true;
}

#endif
