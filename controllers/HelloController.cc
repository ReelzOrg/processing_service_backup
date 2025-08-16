#include "HelloController.h"
#include <json/json.h>

using namespace drogon;

void HelloController::hello(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {
  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->setContentTypeCode(ContentType::CT_APPLICATION_JSON);

  Json::Value json_response;
  json_response["message"] = "Hello from controller!";
  json_response["status"] = "success";

  resp->setBody(json_response.toStyledString());
  callback(resp);
}