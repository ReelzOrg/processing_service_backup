#include <drogon/drogon.h>
#include <iostream>
#include "controllers/HelloController.h"

using namespace std;
using namespace drogon;

int main() {
    long cpp_version = __cplusplus;
    cout << "C++ version is: " << cpp_version << endl;
    //Set HTTP listener address and port
    app().addListener("0.0.0.0", 5555);
    app().setLogLevel(trantor::Logger::kDebug);
    //Load config file
    //drogon::app().loadConfigFile("../config.json"); 
    //drogon::app().loadConfigFile("../config.yaml");
    //Run HTTP framework,the method will block in the internal event loop
    app().run();
    return 0;
}
