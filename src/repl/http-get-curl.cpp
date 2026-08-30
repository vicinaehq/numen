#include <curl/curl.h>
#include <memory>
#include "http-get.hpp"

namespace {
size_t writeBody(char *data, size_t size, size_t nmemb, void *userp) {
  static_cast<std::string *>(userp)->append(data, size * nmemb);
  return size * nmemb;
}
} // namespace

std::expected<std::string, std::string> httpGet(const std::string &origin, const std::string &path) {
  const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl{curl_easy_init(), curl_easy_cleanup};
  if (!curl) return std::unexpected{"curl_easy_init failed"};

  const std::string url = origin + path;
  std::string body;
  char errbuf[CURL_ERROR_SIZE] = {};

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeBody);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, errbuf);

  if (auto rc = curl_easy_perform(curl.get()); rc != CURLE_OK) {
    return std::unexpected{errbuf[0] != '\0' ? errbuf : curl_easy_strerror(rc)};
  }

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (status < 200 || status >= 300) return std::unexpected{"HTTP status " + std::to_string(status)};

  return body;
}
