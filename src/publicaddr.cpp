// The host's address for friends joining over the internet, and the port
// opened on the router while hosting.
//
// When the player hosts, a background thread asks the router (UPnP: an SSDP
// search for an Internet Gateway Device, its description, then the
// WANIPConnection/WANPPPConnection service) for the public address, and with
// [Network] OpenPort=1 maps the game's UDP port to this PC, with a two-hour
// lease renewed every hour while hosting; the mapping is removed when
// hosting ends or the game shuts down. If the router doesn't answer (UPnP
// off), or reports a private address (a second router, or the provider's
// shared address), the address comes from [Network] PublicIPService instead
// (one HTTPS GET of plain text; empty = never). [Network] UPnP=0 skips the
// router. Test runs (loopback) contact nothing unless C2HD_NET_PUBLIC=1, and
// never open a port.
#include "publicaddr.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "log.h"
#include "netshim.h"
#include "settings.h"

namespace publicaddr {
namespace {

CRITICAL_SECTION g_lock;
std::string g_public, g_lan, g_portText;  // shown in the lobby
std::string g_control, g_service;         // the router's control URL and service type, once found
std::string g_mappedClient;
int g_mappedPort;                          // the external port mapped on the router (0 = none)
HANDLE g_thread;
volatile bool g_hosting, g_stop;

void Set(std::string& field, const std::string& value) {
    EnterCriticalSection(&g_lock);
    field = value;
    LeaveCriticalSection(&g_lock);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// The text between <tag> and </tag> at or after `from` (empty if none).
std::string Tag(const std::string& xml, const char* tag, size_t from = 0, size_t* end = nullptr) {
    std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t a = xml.find(open, from);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t b = xml.find(close, a);
    if (b == std::string::npos) return "";
    if (end) *end = b;
    return xml.substr(a, b - a);
}

// One HTTP(S) request through WinHTTP: the body, and the status in *status.
bool Http(const std::string& url, const char* method, const std::string& headers, const std::string& body,
          std::string* out, DWORD* status) {
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS u = {};
    u.dwStructSize = sizeof u;
    wchar_t host[256], path[1024];
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u)) return false;
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"Carma2HD", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : nullptr;
    std::wstring wmethod(method, method + strlen(method));
    HINTERNET r = c ? WinHttpOpenRequest(c, wmethod.c_str(), path, nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (r) {
        WinHttpSetTimeouts(r, 3000, 3000, 3000, 4000);
        std::wstring wh(headers.begin(), headers.end());
        if (WinHttpSendRequest(r, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), (DWORD)-1L,
                               (void*)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, size = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &size,
                                nullptr);
            *status = code;
            out->clear();
            char buf[4096];
            DWORD n = 0;
            while (WinHttpReadData(r, buf, sizeof buf, &n) && n && out->size() < 256 * 1024) out->append(buf, n);
            ok = true;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

// A SOAP action on the router's connection service; the response body.
bool Soap(const char* action, const std::string& args, std::string* out, DWORD* status) {
    std::string body =
        "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
        std::string(action) + " xmlns:u=\"" + g_service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    std::string headers = "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + g_service + "#" + action + "\"";
    return Http(g_control, "POST", headers, body, out, status);
}

bool PrivateAddress(const std::string& ip) {
    unsigned a = 0, b = 0;
    if (sscanf(ip.c_str(), "%u.%u", &a, &b) != 2) return true;
    return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254) || a == 0;
}

// SSDP: the routers' description URLs (LOCATION headers), 2.5 s of answers.
std::string FindGateway() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return "";
    DWORD timeout = 500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    to.sin_addr.s_addr = inet_addr("239.255.255.250");
    const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                             "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                             "urn:schemas-upnp-org:service:WANIPConnection:1"};
    for (const char* st : targets) {
        char msg[256];
        int n = snprintf(msg, sizeof msg,
                         "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                         "ST: %s\r\n\r\n",
                         st);
        sendto(s, msg, n, 0, (const sockaddr*)&to, sizeof to);
    }
    std::string location;
    DWORD until = GetTickCount() + 2500;
    while (location.empty() && GetTickCount() < until && !g_stop) {
        char buf[1500];
        int n = recv(s, buf, sizeof buf - 1, 0);
        if (n <= 0) continue;
        buf[n] = 0;
        std::string reply(buf), low = Lower(reply);
        size_t at = low.find("\r\nlocation:");
        if (at == std::string::npos) continue;
        at += 11;
        size_t end = reply.find("\r\n", at);
        location = reply.substr(at, end - at);
        while (!location.empty() && location[0] == ' ') location.erase(0, 1);
    }
    closesocket(s);
    return location;
}

// The router's connection service: control URL and service type.
bool FindService(const std::string& location) {
    std::string xml;
    DWORD status = 0;
    if (!Http(location, "GET", "", "", &xml, &status) || status != 200) return false;
    std::string base = Tag(xml, "URLBase");
    if (base.empty()) {  // scheme://host:port of the description's URL
        size_t slash = location.find('/', location.find("//") + 2);
        base = location.substr(0, slash);
    }
    while (!base.empty() && base.back() == '/') base.pop_back();
    const char* services[] = {"urn:schemas-upnp-org:service:WANIPConnection:2",
                              "urn:schemas-upnp-org:service:WANIPConnection:1",
                              "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    for (const char* service : services) {
        size_t at = xml.find(std::string(">") + service + "<");
        if (at == std::string::npos) continue;
        std::string control = Tag(xml, "controlURL", at);
        if (control.empty()) continue;
        g_service = service;
        g_control = control.find("://") != std::string::npos ? control : base + (control[0] == '/' ? "" : "/") + control;
        return true;
    }
    return false;
}

// This PC's address on the way to the router (the mapping's internal client).
std::string LocalAddressTowards(const std::string& url) {
    size_t a = url.find("//");
    if (a == std::string::npos) return "";
    a += 2;
    size_t b = url.find_first_of(":/", a);
    std::string host = url.substr(a, b - a);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    to.sin_addr.s_addr = inet_addr(host.c_str());
    std::string ip;
    sockaddr_in me = {};
    int len = sizeof me;
    if (connect(s, (const sockaddr*)&to, sizeof to) == 0 && getsockname(s, (sockaddr*)&me, &len) == 0)
        ip = inet_ntoa(me.sin_addr);
    closesocket(s);
    return ip;
}

bool AddMapping(int port, const std::string& client, int lease) {
    char args[512];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>"
             "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>Carmageddon II HD</NewPortMappingDescription>"
             "<NewLeaseDuration>%d</NewLeaseDuration>",
             port, port, client.c_str(), lease);
    std::string out;
    DWORD status = 0;
    bool ok = Soap("AddPortMapping", args, &out, &status) && status == 200;
    if (!ok) Log("publicaddr: router refused the port mapping (HTTP %lu, error %s)", status, Tag(out, "errorCode").c_str());
    return ok;
}

void RemoveMapping() {
    if (!g_mappedPort) return;
    char args[256];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>",
             g_mappedPort);
    std::string out;
    DWORD status = 0;
    bool ok = Soap("DeletePortMapping", args, &out, &status) && status == 200;
    Log("publicaddr: port %d mapping removed from the router: %s", g_mappedPort, ok ? "ok" : "failed");
    g_mappedPort = 0;
}

std::string WebAddress() {
    std::string url = settings::Get("Network", "PublicIPService", "https://api.ipify.org");
    if (url.empty()) return "";
    std::string out;
    DWORD status = 0;
    if (!Http(url, "GET", "", "", &out, &status) || status != 200) return "";
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    unsigned a, b, c, d;
    return sscanf(out.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256 ? out
                                                                                                                    : "";
}

DWORD WINAPI Work(void*) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    int port = netshim::BoundPort() ? netshim::BoundPort() : atoi(settings::Get("Network", "Port", "47700").c_str());
    bool test = netshim::Loopback();
    bool openPort = !test && settings::Get("Network", "OpenPort", "1") != std::string("0");
    std::string portName = std::to_string(port);
    Set(g_public, "FINDING YOUR ADDRESS...");
    Set(g_portText, "");
    std::string routerIp, lan;
    if (settings::Get("Network", "UPnP", "1") != std::string("0")) {
        std::string location = FindGateway();
        if (!location.empty() && FindService(location)) {
            lan = LocalAddressTowards(location);
            std::string out;
            DWORD status = 0;
            if (Soap("GetExternalIPAddress", "", &out, &status) && status == 200)
                routerIp = Tag(out, "NewExternalIPAddress");
            Log("publicaddr: router %s (%s), its address %s, this PC %s", location.c_str(), g_service.c_str(),
                routerIp.empty() ? "unknown" : routerIp.c_str(), lan.c_str());
        } else {
            Log("publicaddr: no router answered over UPnP");
        }
    }
    // No router answered: the address on the default route (a connected UDP
    // socket only picks the route; nothing is sent to the documentation address).
    // A port forwarded by hand needs it.
    if (lan.empty()) lan = LocalAddressTowards("udp://192.0.2.1/");
    bool routerPublic = !routerIp.empty() && !PrivateAddress(routerIp);
    std::string shown = routerPublic ? routerIp : WebAddress();
    if (!routerPublic) Log("publicaddr: address from the web service: %s", shown.empty() ? "none" : shown.c_str());
    if (!lan.empty()) Set(g_lan, "ON YOUR LAN: " + lan);
    Set(g_public, shown.empty() ? "YOUR ADDRESS: NOT FOUND" : "YOUR ADDRESS: " + shown + (port == 47700 ? "" : ":" + portName));
    if (!routerIp.empty() && !routerPublic)
        Set(g_portText, "DOUBLE NAT: FRIENDS MAY NOT CONNECT");
    else if (openPort && !g_control.empty() && !lan.empty() && AddMapping(port, lan, 7200)) {
        g_mappedPort = port;
        g_mappedClient = lan;
        Log("publicaddr: UDP port %d mapped on the router to %s", port, lan.c_str());
        Set(g_portText, "PORT " + portName + " OPENED ON YOUR ROUTER");
    } else {
        Set(g_portText, "FORWARD UDP PORT " + portName + " ON ROUTER");
    }
    // While hosting: renew the lease every hour; then remove the mapping.
    DWORD renew = GetTickCount() + 3600 * 1000;
    while (g_hosting && !g_stop) {
        Sleep(250);
        if (g_mappedPort && (int)(GetTickCount() - renew) >= 0) {
            AddMapping(g_mappedPort, g_mappedClient, 7200);
            renew = GetTickCount() + 3600 * 1000;
        }
    }
    RemoveMapping();
    return 0;
}

}  // namespace

void Install() { InitializeCriticalSection(&g_lock); }

void HostingChanged(bool hosting) {
    if (hosting == g_hosting) return;
    g_hosting = hosting;
    if (hosting) {
        bool test = netshim::Loopback();
        char v[4] = "";
        if (test && !(GetEnvironmentVariableA("C2HD_NET_PUBLIC", v, sizeof v) && v[0] == '1')) {
            Set(g_public, "YOUR ADDRESS: (TEST RUN)");
            return;
        }
        if (g_thread) {  // the previous one is finishing (removing its mapping)
            WaitForSingleObject(g_thread, 5000);
            CloseHandle(g_thread);
        }
        g_stop = false;
        g_thread = CreateThread(nullptr, 0, &Work, nullptr, 0, nullptr);
    }
}

void Shutdown() {
    g_hosting = false;
    g_stop = true;
    if (g_thread) WaitForSingleObject(g_thread, 2000);  // it removes the mapping on its way out
}

void Lines(std::string* address, std::string* port, std::string* lan) {
    EnterCriticalSection(&g_lock);
    *address = g_public;
    *port = g_portText;
    *lan = g_lan;
    LeaveCriticalSection(&g_lock);
}

}  // namespace publicaddr
