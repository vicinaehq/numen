#include <windows.h>
#include <winhttp.h>
#include <memory>
#include "http-get.hpp"

namespace {
using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;

std::string lastError(const char *what) {
  return std::string{what} + " failed (error " + std::to_string(GetLastError()) + ")";
}
} // namespace

std::expected<std::string, std::string> httpGet(const std::string &origin, const std::string &path) {
  const std::string narrow = origin + path;
  const std::wstring url{narrow.begin(), narrow.end()}; // ASCII urls only

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return std::unexpected{lastError("WinHttpCrackUrl")};
  const std::wstring host{parts.lpszHostName, parts.dwHostNameLength};

  const Handle session{WinHttpOpen(L"libnumen", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0),
                       WinHttpCloseHandle};
  if (!session) return std::unexpected{lastError("WinHttpOpen")};

  const Handle conn{WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0), WinHttpCloseHandle};
  if (!conn) return std::unexpected{lastError("WinHttpConnect")};

  const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  const Handle req{WinHttpOpenRequest(conn.get(), L"GET", parts.lpszUrlPath, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0),
                   WinHttpCloseHandle};
  if (!req) return std::unexpected{lastError("WinHttpOpenRequest")};

  if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(req.get(), nullptr)) {
    return std::unexpected{lastError("request")};
  }

  DWORD status = 0;
  DWORD size = sizeof(status);
  WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status < 200 || status >= 300) return std::unexpected{"HTTP status " + std::to_string(status)};

  std::string body;
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req.get(), &avail)) {
      return std::unexpected{lastError("WinHttpQueryDataAvailable")};
    }
    if (avail == 0) break;
    const auto offset = body.size();
    body.resize(offset + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req.get(), body.data() + offset, avail, &read)) {
      return std::unexpected{lastError("WinHttpReadData")};
    }
    body.resize(offset + read);
  }

  return body;
}
