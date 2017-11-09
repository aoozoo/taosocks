#include "server_socket.h"
#include "log.h"

namespace taosocks {

void ServerSocket::Start(ULONG ip, USHORT port)
{
    SOCKADDR_IN addrServer = {0};
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = ip;
    addrServer.sin_port = htons(port);

    if(::bind(_fd, (sockaddr*)&addrServer, sizeof(addrServer)) == SOCKET_ERROR)
        assert(0);

    if(::listen(_fd, SOMAXCONN) == SOCKET_ERROR)
        assert(0);

    _Accept();
}

int ServerSocket::GenId()
{
    return _next_id++;
}

void ServerSocket::OnAccept(std::function<void(ClientSocket*)> onAccepted)
{
    _onAccepted = onAccepted;
}

void ServerSocket::_OnAccept(AcceptIOContext* io)
{
    SOCKADDR_IN *local, *remote;
    io->GetAddresses(&local, &remote);

    auto client = new ClientSocket(GenId(), io->fd, *local, *remote);
    _onAccepted(client);

    _Accept();
}

void ServerSocket::_Accept()
{
    for(;;) {
        auto acceptio = new AcceptIOContext();
        auto ret = acceptio->Accept(_fd);
        if(ret.Succ()) {

        }
        else if(ret.Fail()) {
            LogFat("_Accept ����code=%d", ret.Code());
            assert(0);
        }
        else if(ret.Async()) {
            break;
        }
    }
}

void ServerSocket::OnTask(BaseIOContext* bio)
{
    if(bio->optype == OpType::Accept) {
        _OnAccept(static_cast<AcceptIOContext*>(bio));
    }
}

}

