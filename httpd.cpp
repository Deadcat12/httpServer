#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace std;

namespace fs = filesystem; 

static map<string, string> mime_map = {
    {".html","text/html"},
    {".htm","text/html"},
    {".css","text/css"},
    {".js","application/javascript"},
    {".json","application/json"},
    {".png","image/png"},
    {".jpg","image/jpg"},
    {".jpeg","image/jpeg"},
    {".svg","image/svg+xml"},
    {".txt","text/plain"},

};

string get_mime(const string& path) {
    auto ext = fs::path(path).extension().string();
    auto it = mime_map.find(ext);
    if (it != mime_map.end()) return it->second;
    return "application/octet-stream";
}

void log_request(const string& client_ip, const string& method, const string& target, int status, size_t bytes) {
    // Простой общий журнал
    cout << client_ip << " \"" << method << " " << target << "\" " << status <<" " << bytes << endl; 
}

string status_page(int code, const string& reason) {
    ostringstream o; 
    o << "<html><head><title>" << code << " " << reason << "</title><head>\n" << "<body><h1>" << code << " " << reason << "</h1></body></hrml>";
    return o.str();
}

bool contains_dotdot(const string& s) {
    //Проверка на предотвращение ../ в path
    return s.find("..") != string::npos; 
}

int handle_connection(int client_fd, const string& client_ip, const fs::path& docroot) {
    constexpr size_t BUF_SIZE = 8192; 
    char buf[BUF_SIZE]; 
    ssize_t r = recv(client_fd, buf, BUF_SIZE - 1, 0);
    if (r <= 0) return 0; 
    buf[r] = '\0'; 
    string req(buf, r);


//очень простой анализ строки

istringstream reqs(req);
string method, target, version;
if(!(reqs >> method >> target >> version)) {
    string body = status_page(400, "Bad Request");
    ostringstream resp;
    resp << "HTTP/1.1 400 Bad Request\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n" 
        << body;
    send(client_fd, resp.str().c_str(), resp.str().size(), 0);
    log_request(client_ip, method, target, 400, body.size());
    return 0;
}

if (method != "GET") {
    string body = status_page(400, "Only GET supported");
    ostringstream resp;
    resp << "HTTP/1.1 400 Bad Request\r\n"                 << "Content-Type: text/html\r\n"                 << "Content-Length: " << body.size() <<"\r\n"                                                     << "Connection: close\r\n\r\n"                     << body;
    send(client_fd, resp.str().c_str(), resp.str(
).size(), 0);
    log_request(client_ip, method, target, 400, body.size());
}

//Обработать таргеt

if (target.empty() || target[0] != '/' || contains_dotdot(target)) {                                    string body = status_page(400, "Bad Request");                                                    ostringstream resp;                              resp << "HTTP/1.1 400 Bad Request\r\n"                << "Content-Type: text/html\r\n"                 << "Content-Length: " << body.size() <<    "\r\n"                                                 << "Connection: close\r\n\r\n"                   << body;
          send(client_fd, resp.str().c_str(), resp.str().size(), 0);                                      log_request(client_ip, method, target, 400, body.size());
       }

//map "/" -> "index.html"

if (target == "/") target = "/index.html";
fs::path full = docroot / fs::path(target.substr(1));

//Проверка на существоыание файла
if (!fs::exists(full) || !fs::is_regular_file(full)) {
    string body = status_page(404, "Not Found");
        ostringstream resp;
        resp << "HTTP/1.1 404 Bad Request\r\n"
             << "Content-Type: text/html\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        send(client_fd, resp.str().c_str(), resp.str().size(), 0);
        log_request(client_ip, method, target, 404, body.size());
        return 0;
}

//чтение файла 
ifstream ifs(full, ios::binary);
if(!ifs) {
    string body = status_page(500, "Bad Request");
        ostringstream resp;
        resp << "HTTP/1.1 500 Bad Request\r\n"
             << "Content-Type: text/html\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        send(client_fd, resp.str().c_str(), resp.str().size(), 0);
        log_request(client_ip, method, target, 500, body.size());
        return 0;
}

vector<char> data((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
string mime = get_mime(full.string());

ostringstream header; 
header << "HTTP/1.1 200 OK\r\n"
       << "Content-Type: " << mime << "\r\n"
       << "Content-Length: " << data.size() << "\r\n"
       << "Connection: close\r\n\r\n";
string hstr = header.str();
send(client_fd, hstr.c_str(), hstr.size(), 0);
if (!data.empty()) send(client_fd, data.data(), data.size(), 0);
                                                 log_request(client_ip, method, target, 200, data.size());
                                                     return 0;
                                                 }

int main(int argc, char** argv) {

    int port = 8080;
    fs::path docroot = fs::current_path(); 
    if (argc >= 2) port = stoi(argv[1]);
    if (argc >= 3) docroot = fs::path(argv[2]);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET; 
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if(::bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(srv); return 1; }
    if (listen(srv, 16) < 0) { perror("listen"); close(srv); return 1; }

    cout << "Listening on 0.0.0.0:" << port << " serving " << docroot << endl;

    while (true) {
        sockaddr_in cli{};
        socklen_t clilen = sizeof(cli);
        int client_fd = accept(srv, (sockaddr*)&cli, &clilen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        char ipbuff[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ipbuff, sizeof(ipbuff));
        handle_connection(client_fd, ipbuff, docroot);
        close(client_fd);
    }
    close(srv);
    return 0; 

}
