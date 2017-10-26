#include "client_socket.h"

#define LogLog(...)
#define LogFat(...)
#define LogWrn(...)

namespace taosocks {
void ClientSocket::Close()
{
    flags |= Flags::Closed;
    WSAIntRet ret = closesocket(_fd);
    LogLog("�ر�client,fd=%d,ret=%d", fd, ret.Code());
    assert(ret.Succ());
}
void ClientSocket::OnRead(OnReadT onRead)
{
    _onRead = onRead;
}
void ClientSocket::OnWritten(OnWrittenT onWritten)
{
    _onWritten = onWritten;
}
void ClientSocket::OnClosed(OnClosedT onClose)
{
    _onClosed = onClose;
}
void ClientSocket::OnConnected(OnConnectedT onConnected)
{
    _onConnected = onConnected;
}
WSARet ClientSocket::Connect(in_addr& addr, unsigned short port)
{
    sockaddr_in sai = {0};
    sai.sin_family = PF_INET;
    sai.sin_addr.S_un.S_addr = INADDR_ANY;
    sai.sin_port = 0;
    WSAIntRet r = ::bind(_fd, (sockaddr*)&sai, sizeof(sai));
    assert(r.Succ());
    sai.sin_addr = addr;
    sai.sin_port = ::htons(port);

    auto connio = new ConnectIOContext();
    auto ret = connio->Connect(_fd, sai);
    if(ret.Succ()) {
        LogLog("���������ɹ�");
    }
    else if(ret.Fail()) {
        LogFat("���ӵ���ʧ�ܣ�%d", ret.Code());
    }
    else if(ret.Async()) {
        LogLog("�����첽");
    }
    return ret;
}
WSARet ClientSocket::Write(const char * data, size_t size, void * tag)
{
    return Write((const unsigned char*)data, size, tag);
}
WSARet ClientSocket::Write(const char * data, void * tag)
{
    return Write(data, std::strlen(data), tag);
}
WSARet ClientSocket::Write(const unsigned char * data, size_t size, void * tag)
{
    auto writeio = new WriteIOContext();
    auto ret = writeio->Write(_fd, data, size);
    if(ret.Succ()) {
        LogLog("д�����ɹ���fd=%d,size=%d", fd, size);
    }
    else if(ret.Fail()) {
        LogFat("д����fd=%d,code=%d", fd, ret.Code());
    }
    else if(ret.Async()) {
        LogLog("д�첽��fd=%d", fd);
    }
    return ret;
}
WSARet ClientSocket::_Read()
{
    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        LogLog("_Read �����ɹ�, fd:%d", fd);
    }
    else if(ret.Fail()) {
        LogFat("������fd:%d,code=%d", fd, ret.Code());
    }
    else if(ret.Async()) {
        LogLog("���첽 fd:%d", fd);
    }
    return ret;
}
void ClientSocket::_OnRead(ReadIOContext& io)
{
    DWORD dwBytes = 0;
    WSARet ret = io.GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        ReadDispatchData data;
        data.data = io.buf;
        data.size = dwBytes;
        Dispatch(data);
        _Read();
    }
    else {
        if(flags & Flags::Closed) {
            LogWrn("�������ر����ӣ�fd:%d", fd);
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("�ѱ����ر����ӣ�fd:%d", fd);
            CloseDispatchData data;
            Dispatch(data);
        }
        else if(ret.Fail()) {
            LogFat("��ʧ�ܣ�fd=%d,code:%d", fd, ret.Code());
        }
    }
}
WSARet ClientSocket::_OnWritten(WriteIOContext& io)
{
    DWORD dwBytes = 0;
    WSARet ret = io.GetResult(_fd, &dwBytes);
    if(ret.Succ() && dwBytes > 0) {
        assert(dwBytes == io.wsabuf.len);
        WriteDispatchData data;
        data.size = dwBytes;
        Dispatch(data);
    }
    else {
        LogFat("дʧ��");
    }

    return ret;
}
WSARet ClientSocket::_OnConnected(ConnectIOContext& io)
{
    WSARet ret = io.GetResult(_fd);
    if(ret.Succ()) {
        ConnectDispatchData data;
        Dispatch(data);
    }
    else {
        LogFat("����ʧ��");
    }

    return ret;
}
void ClientSocket::OnDispatch(BaseDispatchData & data)
{
    switch(data.optype) {
    case OpType::Read:
    {
        auto d = static_cast<ReadDispatchData&>(data);
        _onRead(this, d.data, d.size);
        break;
    }
    case OpType::Write:
    {
        auto d = static_cast<WriteDispatchData&>(data);
        _onWritten(this, d.size);
        break;
    }
    case OpType::Close:
    {
        auto d = static_cast<CloseDispatchData&>(data);
        _onClosed(this);
        break;
    }
    case OpType::Connect:
    {
        auto d = static_cast<ConnectDispatchData&>(data);
        _onConnected(this);
        break;
    }
    }
}

void ClientSocket::OnTask(BaseIOContext& bio)
{
    if(bio.optype == OpType::Read) {
        _OnRead(static_cast<ReadIOContext&>(bio));
    }
    else if(bio.optype == OpType::Write) {
        _OnWritten(static_cast<WriteIOContext&>(bio));
    }
    else if(bio.optype == OpType::Connect) {
        _OnConnected(static_cast<ConnectIOContext&>(bio));
    }
}

int ClientSocket::GetDescriptor()
{
    return static_cast<int>(GetSocket());
}

}
