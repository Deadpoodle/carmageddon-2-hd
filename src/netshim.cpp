// Network: the game's IPX, carried over UDP by us (re/network.md).
//
// The game speaks only IPX (one AF_IPX datagram socket, IPX socket 0x2FFE,
// broadcasts to find games). The store's ipxwrapper 0.4.0 tunnels that over
// UDP but can't run two copies on one PC and can't reach a host by address.
// So the exe's 11 WSOCK32 imports are taken over here: an IPX socket becomes
// a UDP socket on a port in [Network] Port .. Port+7 (the first free one, so
// several copies can run on one PC); every datagram carries a small header
// with the sender's and receiver's IPX node and socket; IPX broadcasts go to
// every port of the range on the LAN broadcast address (and, for join by IP,
// to [Network] JoinAddress); unicasts go to the UDP address last seen for
// that node. Each copy picks its own random node number.
//
// Test runs bind to 127.0.0.1 and "broadcast" to the loopback ports only, so
// nothing listens on the network (and no firewall prompt appears).
#include "netshim.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "headless.h"
#include "hook.h"
#include "log.h"
#include "settings.h"

namespace netshim {
namespace {

constexpr int kAfIpx = 6;
constexpr uint32_t kMagic = 0x32484332;  // "2CH2"
constexpr int kPorts = 8;

#pragma pack(push, 1)
struct SockaddrIpx {
    short family;
    char net[4];
    char node[6];
    unsigned short socket;  // network byte order
};
struct Header {
    uint32_t magic;
    uint8_t srcNode[6];
    uint16_t srcSocket;  // network byte order, as in SOCKADDR_IPX
    uint8_t dstNode[6];
    uint16_t dstSocket;
};
#pragma pack(pop)

// The system's winsock, not the folder's wsock32.dll (ipxwrapper).
decltype(&::socket) w_socket;
decltype(&::sendto) w_sendto;
decltype(&::recvfrom) w_recvfrom;
decltype(&::bind) w_bind;
decltype(&::closesocket) w_closesocket;
decltype(&::ioctlsocket) w_ioctlsocket;
decltype(&::setsockopt) w_setsockopt;
decltype(&::getsockname) w_getsockname;
decltype(&::WSAStartup) w_WSAStartup;
decltype(&::WSACleanup) w_WSACleanup;
decltype(&::WSAGetLastError) w_WSAGetLastError;
decltype(&::WSASetLastError) w_WSASetLastError;

int g_basePort = 47700;
bool g_loopback;            // test runs: loopback only
bool g_noBroadcast;         // test runs (C2HD_NET_NOBROADCAST=1): reach peers only by JoinAddress
sockaddr_in g_join;         // [Network] JoinAddress, or port 0
std::string g_joinText;     // resolved at the first bind (winsock is started by then)
uint8_t g_node[6];
std::map<SOCKET, uint16_t> g_ipxSockets;  // our UDP socket -> its IPX socket number (net order)
std::map<uint64_t, sockaddr_in> g_peers;  // IPX node -> last UDP address

uint64_t NodeKey(const uint8_t* n) {
    uint64_t k = 0;
    memcpy(&k, n, 6);
    return k;
}

bool IsBroadcast(const char* node) {
    for (int i = 0; i < 6; i++)
        if ((uint8_t)node[i] != 0xff) return false;
    return true;
}

// ---------------------------------------------------------------------------
// The exe's WSOCK32 imports

SOCKET WINAPI h_socket(int af, int type, int protocol) {
    if (af != kAfIpx) return w_socket(af, type, protocol);
    SOCKET s = w_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        g_ipxSockets[s] = 0;
        BOOL on = TRUE;
        w_setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof on);
    }
    Log("net: IPX socket -> UDP socket %u", (unsigned)s);
    return s;
}

int WINAPI h_bind(SOCKET s, const sockaddr* name, int len) {
    auto it = g_ipxSockets.find(s);
    if (it == g_ipxSockets.end()) return w_bind(s, name, len);
    const SockaddrIpx* ipx = (const SockaddrIpx*)name;
    it->second = ipx->socket;
    if (!g_joinText.empty() && !g_join.sin_port) {
        char host[128] = "";
        int port = g_basePort;
        sscanf(g_joinText.c_str(), "%127[^:]:%d", host, &port);
        unsigned long ip = inet_addr(host);
        if (ip == INADDR_NONE)
            if (hostent* he = gethostbyname(host)) memcpy(&ip, he->h_addr_list[0], 4);
        if (ip != INADDR_NONE && ip) {
            g_join.sin_family = AF_INET;
            g_join.sin_addr.s_addr = ip;
            g_join.sin_port = htons((u_short)port);
        }
        Log("net: join address %s -> %s", g_joinText.c_str(), g_join.sin_port ? inet_ntoa(g_join.sin_addr) : "not resolved");
    }
    for (int i = 0; i < kPorts; i++) {
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)(g_basePort + i));
        a.sin_addr.s_addr = htonl(g_loopback ? INADDR_LOOPBACK : INADDR_ANY);
        if (w_bind(s, (sockaddr*)&a, sizeof a) == 0) {
            Log("net: IPX socket %04x on UDP port %d (%s)", ntohs(ipx->socket), g_basePort + i,
                g_loopback ? "loopback, test" : "all interfaces");
            return 0;
        }
    }
    Log("net: no free UDP port in %d..%d", g_basePort, g_basePort + kPorts - 1);
    w_WSASetLastError(WSAEADDRINUSE);
    return SOCKET_ERROR;
}

int WINAPI h_getsockname(SOCKET s, sockaddr* name, int* len) {
    auto it = g_ipxSockets.find(s);
    if (it == g_ipxSockets.end()) return w_getsockname(s, name, len);
    if (!len || *len < (int)sizeof(SockaddrIpx)) {
        w_WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    SockaddrIpx* ipx = (SockaddrIpx*)name;
    memset(ipx, 0, sizeof *ipx);
    ipx->family = kAfIpx;
    memcpy(ipx->node, g_node, 6);
    ipx->socket = it->second;
    *len = sizeof *ipx;
    return 0;
}

int SendUdp(SOCKET s, const char* buf, int len, const sockaddr_in& to) {
    return w_sendto(s, buf, len, 0, (const sockaddr*)&to, sizeof to);
}

int WINAPI h_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen) {
    auto it = g_ipxSockets.find(s);
    if (it == g_ipxSockets.end()) return w_sendto(s, buf, len, flags, to, tolen);
    const SockaddrIpx* dst = (const SockaddrIpx*)to;
    if (!dst || tolen < (int)sizeof(SockaddrIpx) || len < 0 || len > 1400) {
        w_WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    char packet[sizeof(Header) + 1400];
    Header* h = (Header*)packet;
    h->magic = kMagic;
    memcpy(h->srcNode, g_node, 6);
    h->srcSocket = it->second;
    memcpy(h->dstNode, dst->node, 6);
    h->dstSocket = dst->socket;
    memcpy(packet + sizeof(Header), buf, len);
    int size = (int)sizeof(Header) + len;

    auto peer = g_peers.find(NodeKey((const uint8_t*)dst->node));
    if (!IsBroadcast(dst->node) && peer != g_peers.end()) {
        if (SendUdp(s, packet, size, peer->second) == SOCKET_ERROR) return SOCKET_ERROR;
        return len;
    }
    // Broadcast (or a node we haven't heard from): every port of the range,
    // and the configured host for join by IP.
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(g_loopback ? INADDR_LOOPBACK : INADDR_BROADCAST);
    for (int i = 0; i < kPorts && !g_noBroadcast; i++) {
        a.sin_port = htons((u_short)(g_basePort + i));
        SendUdp(s, packet, size, a);
    }
    if (g_join.sin_port) SendUdp(s, packet, size, g_join);
    return len;
}

int WINAPI h_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen) {
    auto it = g_ipxSockets.find(s);
    if (it == g_ipxSockets.end()) return w_recvfrom(s, buf, len, flags, from, fromlen);
    for (;;) {
        char packet[sizeof(Header) + 1500];
        sockaddr_in src;
        int srclen = sizeof src;
        int n = w_recvfrom(s, packet, sizeof packet, 0, (sockaddr*)&src, &srclen);
        if (n == SOCKET_ERROR) {
            if (w_WSAGetLastError() == WSAECONNRESET) continue;  // an ICMP "port unreachable" from a gone peer
            return SOCKET_ERROR;                                 // WSAEWOULDBLOCK when nothing waits
        }
        if (n < (int)sizeof(Header)) continue;
        const Header* h = (const Header*)packet;
        if (h->magic != kMagic || memcmp(h->srcNode, g_node, 6) == 0) continue;  // not ours, or our own echo
        if (!IsBroadcast((const char*)h->dstNode) && memcmp(h->dstNode, g_node, 6) != 0) continue;
        uint64_t key = NodeKey(h->srcNode);
        if (!g_peers.count(key))
            Log("net: peer %02x%02x%02x%02x%02x%02x at %s:%u", h->srcNode[0], h->srcNode[1], h->srcNode[2],
                h->srcNode[3], h->srcNode[4], h->srcNode[5], inet_ntoa(src.sin_addr), ntohs(src.sin_port));
        g_peers[key] = src;
        int payload = n - (int)sizeof(Header);
        if (payload > len) payload = len;
        memcpy(buf, packet + sizeof(Header), payload);
        if (from && fromlen && *fromlen >= (int)sizeof(SockaddrIpx)) {
            SockaddrIpx* ipx = (SockaddrIpx*)from;
            memset(ipx, 0, sizeof *ipx);
            ipx->family = kAfIpx;
            memcpy(ipx->node, h->srcNode, 6);
            ipx->socket = h->srcSocket;
            *fromlen = sizeof *ipx;
        }
        return payload;
    }
}

int WINAPI h_closesocket(SOCKET s) {
    g_ipxSockets.erase(s);
    return w_closesocket(s);
}

int WINAPI h_setsockopt(SOCKET s, int level, int name, const char* val, int len) {
    if (g_ipxSockets.count(s) && level == SOL_SOCKET && name != SO_BROADCAST) return 0;  // SO_LINGER etc.: not for UDP
    return w_setsockopt(s, level, name, val, len);
}

int WINAPI h_ioctlsocket(SOCKET s, long cmd, u_long* arg) { return w_ioctlsocket(s, cmd, arg); }
int WINAPI h_WSAStartup(WORD v, LPWSADATA d) { return w_WSAStartup(v, d); }
int WINAPI h_WSACleanup() { return w_WSACleanup(); }
int WINAPI h_WSAGetLastError() { return w_WSAGetLastError(); }

// The exe imports WSOCK32 by ordinal, so its 11 slots are named here
// (re/network.md); each must point into the loaded wsock32.dll before it is
// taken.
template <typename T>
bool Take(uintptr_t slot, T fn) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "";
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            *(LPCSTR*)slot, &m))
        return false;
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = strrchr(path, '\\');
    if (_stricmp(base ? base + 1 : path, "wsock32.dll") != 0) return false;
    T old;
    return hook::Import(slot, fn, &old);
}

}  // namespace

void Install() {
    if (settings::Get("Network", "Transport", "udp") == std::string("ipxwrapper")) {
        Log("net: the store's ipxwrapper (Transport=ipxwrapper)");
        return;
    }
    HMODULE ws = LoadLibraryA("ws2_32.dll");
    if (!ws) return;
    w_socket = (decltype(w_socket))GetProcAddress(ws, "socket");
    w_sendto = (decltype(w_sendto))GetProcAddress(ws, "sendto");
    w_recvfrom = (decltype(w_recvfrom))GetProcAddress(ws, "recvfrom");
    w_bind = (decltype(w_bind))GetProcAddress(ws, "bind");
    w_closesocket = (decltype(w_closesocket))GetProcAddress(ws, "closesocket");
    w_ioctlsocket = (decltype(w_ioctlsocket))GetProcAddress(ws, "ioctlsocket");
    w_setsockopt = (decltype(w_setsockopt))GetProcAddress(ws, "setsockopt");
    w_getsockname = (decltype(w_getsockname))GetProcAddress(ws, "getsockname");
    w_WSAStartup = (decltype(w_WSAStartup))GetProcAddress(ws, "WSAStartup");
    w_WSACleanup = (decltype(w_WSACleanup))GetProcAddress(ws, "WSACleanup");
    w_WSAGetLastError = (decltype(w_WSAGetLastError))GetProcAddress(ws, "WSAGetLastError");
    w_WSASetLastError = (decltype(w_WSASetLastError))GetProcAddress(ws, "WSASetLastError");

    g_basePort = atoi(settings::Get("Network", "Port", "47700").c_str());
    if (g_basePort < 1024 || g_basePort > 65000) g_basePort = 47700;
    g_loopback = headless::Active();
    char nb[8] = "";
    g_noBroadcast = g_loopback && GetEnvironmentVariableA("C2HD_NET_NOBROADCAST", nb, sizeof nb) && nb[0] == '1';
    g_joinText = settings::Get("Network", "JoinAddress", "");
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    uint32_t r = (uint32_t)t.QuadPart ^ (GetCurrentProcessId() << 16) ^ GetTickCount();
    g_node[0] = 0x02;  // locally administered
    g_node[1] = 0xc2;
    memcpy(g_node + 2, &r, 4);

    bool ok = Take(0x589244, &h_socket) && Take(0x589248, &h_sendto) && Take(0x58924c, &h_WSAStartup) &&
              Take(0x589250, &h_closesocket) && Take(0x589254, &h_bind) && Take(0x589258, &h_getsockname) &&
              Take(0x58925c, &h_recvfrom) && Take(0x589260, &h_WSAGetLastError) && Take(0x589264, &h_WSACleanup) &&
              Take(0x589268, &h_ioctlsocket) && Take(0x58926c, &h_setsockopt);
    Log("net: IPX over UDP %s (ports %d..%d, node %02x%02x%02x%02x%02x%02x)", ok ? "on" : "FAILED", g_basePort,
        g_basePort + kPorts - 1, g_node[0], g_node[1], g_node[2], g_node[3], g_node[4], g_node[5]);
}

}  // namespace netshim
