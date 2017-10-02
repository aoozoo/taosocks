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

    auto clients = _Accept();
    for(auto& client : clients) {
        client->Read();
    }
}

void ServerSocket::OnAccept(std::function<void(ClientSocket*)> onAccepted)
{
    _onAccepted = onAccepted;
}

ClientSocket * ServerSocket::_OnAccepted(AcceptIOContext & io)
{
    SOCKADDR_IN *local, *remote;
    io.GetAddresses(&local, &remote);

    auto client = new ClientSocket(_disp, io.fd, *local, *remote);
    AcceptDispatchData data;
    data.client = client;
    Dispatch(data);

    return client;
}

std::vector<ClientSocket*> ServerSocket::_Accept()
{
    std::vector<ClientSocket*> clients;

    for(;;) {
        auto acceptio = new AcceptIOContext();
        auto ret = acceptio->Accept(_fd);
        if(ret.Succ()) {
            auto client = _OnAccepted(*acceptio);
            clients.push_back(client);
            LogLog("_Accept ������ɣ�client fd:%d", client->GetDescriptor());
        }
        else if(ret.Fail()) {
            LogFat("_Accept ����code=%d", ret.Code());
            assert(0);
        }
        else if(ret.Async()) {
            LogLog("_Accept �첽");
            break;
        }
    }

    return std::move(clients);
}

void ServerSocket::OnDispatch(BaseDispatchData & data)
{
    switch(data.optype) {
    case OpType::Accept:
    {
        auto d = static_cast<AcceptDispatchData&>(data);
        _onAccepted(d.client);
        break;
    }
    }
}

void ServerSocket::OnTask(BaseIOContext& bio)
{
    if(bio.optype == OpType::Accept) {
        auto aio = static_cast<AcceptIOContext&>(bio);
        auto client = _OnAccepted(aio);
        client->Read();
        _Accept();
    }
}

int ServerSocket::GetDescriptor()
{
    return static_cast<int>(GetSocket());
}

}
