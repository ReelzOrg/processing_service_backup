// #pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class HelloController : public HttpController<HelloController> {
  public:
    METHOD_LIST_BEGIN
    METHOD_ADD(HelloController::hello, "/", Get);
    METHOD_ADD(HelloController::hello, "", Get);
    METHOD_LIST_END

    void hello(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
};