#ifndef CWEB_CWEB_HTTPCODE_H_
#define CWEB_CWEB_HTTPCODE_H_

#include <unordered_map>
#include <string>

namespace cweb {

enum HttpStatusCode {
    StatusOK = 200,
    StatusMovedPermanently = 301,
    StatusBadRequest = 400,
    StatusNotFound = 404,
};

//static std::unordered_map<HttpStatusCode, std::string> HttpStatusCodeString = {
//    {StatusOK ,"OK"},
//    {StatusMovedPermanently, "Moved Permanently"},
//    {StatusNotFound, "Bad Request"},
//    {StatusNotFound, "Not Found"}
//};

}

#endif
