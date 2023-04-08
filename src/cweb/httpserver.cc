#include "httpserver.h"
#include "eventloop.h"
#include "tcpserver.h"
#include "tcpconnection.h"
//#include "co_tcpserver.h"
#include "logger.h"
#include "http_parser.h"

//using namespace cweb::tcpserver::coroutine;
using namespace cweb::log;

namespace cweb {

static std::unordered_map<unsigned int, std::string> Methods = {
    {0, "DELETE"},
    {1, "GET"},
    {2, "HEAD"},
    {3, "POST"},
    {4, "PUT"}
};

bool HttpRequestFormBody::SetData(ByteBuffer *data) {
    const char* start = data->Peek();
    const char* end = data->Back();
    const char* flag = nullptr;
    const char* equal = nullptr;
    do {
        flag = std::find(start, end, '&');
        equal = std::find(start, flag, '=');
        if(flag == end && equal == flag) {
            return false;
        }
        formdatas_[std::string(start, equal)] = std::string(equal + 1, flag);
    }while(flag != end);
    
    return true;
}

std::string HttpRequestFormBody::FormData(const std::string &key) {
    return formdatas_[key];
}


HttpRequest::HttpRequest() {
    parser_ = (http_parser*)malloc(sizeof(http_parser));
    parser_settings_ = (http_parser_settings*)malloc(sizeof(http_parser_settings));
    binary_body_ = new ByteBuffer();
    http_parser_init(parser_, HTTP_REQUEST);
    parser_->data = this;
    http_parser_settings_init(parser_settings_);
    parser_settings_->on_message_begin = handleMessageBegin;
    parser_settings_->on_url = handleURL;
    parser_settings_->on_header_field = handleHeaderField;
    parser_settings_->on_header_value = handleHeaderValue;
    parser_settings_->on_body = handleBody;
    parser_settings_->on_message_complete = handleMessageComplete;
}

HttpRequest::~HttpRequest() {
    free(parser_);
    free(parser_settings_);
    delete binary_body_;
}

int HttpRequest::Parse(const void *data, size_t len) {
    size_t n = http_parser_execute(parser_, parser_settings_, (const char*)data, len);
    if(n != len) {
        parser_process_ = FAIL;
    }
    
    if(parser_process_ == PROCESS) return 1;
    if(parser_process_ == SUCCESS) return 0;
    return -1;
}

int HttpRequest::handleMessageBegin(http_parser* parser) {
    ((HttpRequest*)(parser->data))->parser_process_ = PROCESS;
    return 0;
}

int HttpRequest::handleURL(http_parser* parser, const char *at, size_t length) {
    HttpRequest* req = (HttpRequest*)(parser->data);
    req->method_ = Methods[parser->method];
    req->url_.assign(at, length);
    const char* start = at;
    const char* end = at + length;
    const char* flag = std::find(start, end, '?');
    if(start != flag) {
        req->path_.assign(start, flag);
    }
    
    const char* equal = nullptr;
    start = flag + 1;
    
    if(start >= end) return 0;
    
    do {
        flag = std::find(start, end, '&');
        equal = std::find(start, flag, '=');
        if(flag == end && equal == flag) {
            req->parser_process_ = FAIL;
            return 0;
        }
        req->querys_[std::string(start, equal)] = std::string(equal+1, flag);
        start = flag + 1;
    }while(flag != end);
    return 0;
}

int HttpRequest::handleHeaderField(http_parser* parser, const char *at, size_t length) {
    HttpRequest* req = (HttpRequest*)(parser->data);
    req->header_key_temp_.assign(at, length);
    return 0;
}

int HttpRequest::handleHeaderValue(http_parser* parser, const char *at, size_t length) {
    HttpRequest* req = (HttpRequest*)(parser->data);
    req->headers_[req->header_key_temp_] = std::string(at, length);
    return 0;
}

int HttpRequest::handleBody(http_parser* parser, const char *at, size_t length) {
    HttpRequest* req = (HttpRequest*)(parser->data);
    //如果是大文件上传请求应当写入文件 暂时不考虑
    req->binary_body_->Append(at, length);
    return 0;
}

int HttpRequest::handleMessageComplete(http_parser* parser) {
    HttpRequest* req = (HttpRequest*)(parser->data);
    req->parser_process_ = SUCCESS;
    if(req->headers_["content-type"] == "application/x-www-form-urlencoded") {
        req->body_ = new HttpRequestFormBody();
        req->body_->SetData(req->binary_body_);
    }
    return 0;
}

std::string HttpRequest::PostForm(const std::string &key) {
    if(headers_["content-type"] == "application/x-www-form-urlencoded") return "";
    return ((HttpRequestFormBody*)body_)->FormData(key);
}

HttpReponse::HttpReponse() : send_data_(new ByteBuffer()) {}
HttpReponse::~HttpReponse() {delete send_data_;}

void HttpReponse::SetStatusCode(HttpStatusCode code) {
    //HttpStatusCodeString[code]
    send_data_->Append("HTTP/1.1 " + std::to_string(code) + " " + "OK" + "\r\n");
}

void HttpReponse::SetHeader(const std::string &key, const std::string &value) {
    send_data_->Append(key + ": " + value + "\r\n");
}

void HttpReponse::SetBody(StringPiece body) {
    send_data_->Append("\r\n");
    send_data_->Append(body);
}

ByteBuffer* HttpReponse::SendData() {
    return send_data_;
}

HttpServer::HttpServer(EventLoop* loop, uint16_t port, bool loopbackonly, bool ipv6) {
    tcpserver_ = new TcpServer(loop, port, loopbackonly, ipv6);
    //tcpserver_ = new CoTcpServer((CoEventLoop*)loop, port, loopbackonly, ipv6);
    tcpserver_->SetMessageCallback(std::bind(&HttpServer::handleMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

HttpServer::HttpServer(EventLoop* loop, const std::string& ip, uint16_t port, bool ipv6) {
    tcpserver_ = new TcpServer(loop, ip, port, ipv6);
    //tcpserver_ = new CoTcpServer((CoEventLoop*)loop, ip, port, ipv6);
    tcpserver_->SetMessageCallback(std::bind(&HttpServer::handleMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

HttpServer::~HttpServer() {
    delete (TcpServer*)tcpserver_;
    //TODO delete request;
}

void HttpServer::Start(int threadcnt) {
    tcpserver_->Start(threadcnt);
}

void HttpServer::Quit() {
    tcpserver_->Quit();
}

void HttpServer::handleMessage(Connection* conn, ByteBuffer* buf, Time time) {
    if(conn->KeepAlive()) {
        handleKeepAliveMessage(conn, buf, time);
    }else {
        HttpRequest* req = conn_req_[conn];
        if(req == nullptr) {
            req = new HttpRequest();
        }
        
        int res = req->Parse(buf->Peek(), buf->ReadableBytes());
        //解析完
        if(res == 0) {
            LOG(LOGLEVEL_INFO, CWEB_MODULE, "httpserver", "数据解析成功");
            buf->ReadAll();
            if(request_callback_) {
                request_callback_(conn, req);
            }
            delete req;
        //出错
        }else if(res < 0) {
            LOG(LOGLEVEL_WARN, CWEB_MODULE, "httpserver", "数据解析失败");
            conn->Send("HTTP/1.1 400 Bad Request\r\n\r\n");
            delete req;
        //解析中
        }else {
            LOG(LOGLEVEL_INFO, CWEB_MODULE, "httpserver", "数据解析中");
            buf->ReadAll();
            //在短连接中不需要单独为request设置销毁定时器，跟随connection一同消亡
            if(conn_req_[conn] == nullptr) {
                conn_req_[conn] = req;
                conn->AddCloseHandlers(std::bind(&HttpServer::removeRequest, this, std::placeholders::_1));
            }
        }
    }
}

void HttpServer::handleKeepAliveMessage(Connection *conn, ByteBuffer *buf, Time time) {
    //TODO
    //分帧
    //解析头 上一针解析完才解析头
    //某个请求超时无法销毁
    //解析中给一个定时器 一直没有拿到完整数据 req中有一个timer
    //conn->SetRemoveRequestCallback(std::bind(&HttpServer::removeRequest, this, std::placeholders::_1));
    conn->AddCloseHandlers(std::bind(&HttpServer::removeRequest, this, std::placeholders::_1));
    HttpRequest* req = new HttpRequest();
    if(req->remove_timer_ != nullptr) {
        req->remove_timer_->Cancel();
        req->remove_timer_ = nullptr;
    }
    int res = req->Parse(buf, buf->ReadableBytes());
    if(res == 0) {
        //交由上层处理
        //remove
        delete req;
    }else if(res < 0) {
        //remove
        delete req;
    }else {
        ((TcpConnection*)conn)->Ownerloop()->AddTimer(10000, [conn, req](){
            delete req;
        });
    }
}

void HttpServer::removeRequest(Connection *conn) {
    if(conn->KeepAlive()) {
        if(conn_reqs_.find(conn) != conn_reqs_.end()) {
            std::unordered_map<std::string, HttpRequest*> map = conn_reqs_[conn];
            for(std::unordered_map<std::string, HttpRequest*>::iterator iter = map.begin(); iter != map.end(); ++iter) {
                delete iter->second;
            }
            conn_reqs_.erase(conn);
        }
    }else {
        HttpRequest* req = conn_req_[conn];
        if(req != nullptr) {
            conn_req_.erase(conn);
            delete req;
        }
    }
}

}
