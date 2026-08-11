#include "network/HttpDownloader.h"

HttpDownloader::Handler HttpDownloader::postHandler;
HttpDownloader::Handler HttpDownloader::getHandler;
std::vector<HttpDownloader::Request> HttpDownloader::requests;

bool HttpDownloader::postForm(const std::string& url, const std::string& body, const DataCallback& callback,
                              const std::vector<Header>& headers, CancelCallback /*shouldCancel*/) {
  const Request request{"POST", url, body, headers};
  requests.push_back(request);
  return postHandler && postHandler(request, callback);
}

bool HttpDownloader::fetchUrlWithHeaders(const std::string& url, const DataCallback& callback,
                                         const std::vector<Header>& headers, CancelCallback /*shouldCancel*/) {
  const Request request{"GET", url, {}, headers};
  requests.push_back(request);
  return getHandler && getHandler(request, callback);
}

void HttpDownloader::reset() {
  postHandler = nullptr;
  getHandler = nullptr;
  requests.clear();
}
