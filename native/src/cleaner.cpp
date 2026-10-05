// cleaner.cpp — port of shell/Services/Cleaner.cs.
#include "cleaner.h"

#include <windows.h>

#include <string>
#include <vector>

namespace px {
namespace cleaner {
namespace {

int DeleteOne(const std::wstring &path, bool dir) {
  if (dir) {
    // RemoveDirectory only works when empty; retry with attributes reset.
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) ? 1 : 0;
  }
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
  return DeleteFileW(path.c_str()) ? 1 : 0;
}

} // namespace

int CleanPath(const std::wstring &path) {
  int n = 0;
  std::wstring pat = path;
  if (!pat.empty() && pat.back() != L'\\')
    pat += L'\\';
  pat += L"*";
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW(pat.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return 0;
  do {
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
      continue;
    std::wstring full = path;
    if (!full.empty() && full.back() != L'\\')
      full += L'\\';
    full += fd.cFileName;
    bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (dir) {
      // recursive delete like Directory.Delete(d, true)
      std::wstring sub = full + L"\\*";
      WIN32_FIND_DATAW sfd{};
      HANDLE sh = FindFirstFileW(sub.c_str(), &sfd);
      if (sh != INVALID_HANDLE_VALUE) {
        do {
          if (wcscmp(sfd.cFileName, L".") == 0 ||
              wcscmp(sfd.cFileName, L"..") == 0)
            continue;
          // best effort: recurse via ForceCleanCount on the child dir
          n += ForceCleanCount(full);
          break;
        } while (FindNextFileW(sh, &sfd));
        FindClose(sh);
      }
      n += DeleteOne(full, true);
    } else {
      n += DeleteOne(full, false);
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return n;
}

int ForceCleanCount(const std::wstring &path) {
  int n = 0;
  std::wstring root = path;
  // enumerate files recursively
  std::vector<std::wstring> dirs{root};
  std::vector<std::wstring> allDirs;
  for (size_t i = 0; i < dirs.size(); ++i) {
    allDirs.push_back(dirs[i]);
    std::wstring pat = dirs[i];
    if (!pat.empty() && pat.back() != L'\\')
      pat += L'\\';
    pat += L"*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
      continue;
    do {
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
        continue;
      std::wstring full = dirs[i];
      if (!full.empty() && full.back() != L'\\')
        full += L'\\';
      full += fd.cFileName;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        dirs.push_back(full);
      } else {
        n += DeleteOne(full, false);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  // delete dirs deepest-first
  for (auto it = allDirs.rbegin(); it != allDirs.rend(); ++it) {
    if (*it == root)
      continue;
    n += DeleteOne(*it, true);
  }
  return n;
}

} // namespace cleaner
} // namespace px
