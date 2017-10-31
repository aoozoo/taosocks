#include "client_socket.h"

#include "log.h"

namespace taosocks {
void ClientSocket::Close()
{
    if(!(_flags & Flags::Closed)) {
        _flags |= Flags::Closed;
        WSAIntRet ret = closesocket(_fd);
        LogLog("�ر�client,fd=%d,ret=%d", _fd, ret.Code());
        assert(ret.Succ());
    }
}
void ClientSocket::OnRead(OnReadT onRead)
{
    _onRead = onRead;
}
void ClientSocket::OnWrite(OnWriteT onWrite)
{
    _onWrite = onWrite;
}
void ClientSocket::OnClose(OnCloseT onClose)
{
    _onClose = onClose;
}
void ClientSocket::OnConnect(OnConnectT onConnect)
{
    _onConnect = onConnect;
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
        // LogLog("���������ɹ�");
    }
    else if(ret.Fail()) {
        LogFat("���ӵ���ʧ�ܣ�%d", ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("�����첽");
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
        DWORD dwBytes;
        auto r = writeio->GetResult(_fd, &dwBytes);
        assert(r && dwBytes == size);
        // LogLog("д�����ɹ���fd=%d,size=%d", _fd, size);
    }
    else if(ret.Fail()) {
        LogFat("д����fd=%d,code=%d", _fd, ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("д�첽��fd=%d", _fd);
    }
    return ret;
}
WSARet ClientSocket::Read()
{
    auto readio = new ReadIOContext();
    auto ret = readio->Read(_fd);
    if(ret.Succ()) {
        // LogLog("_Read �����ɹ�, fd:%d", _fd);
    }
    else if(ret.Fail()) {
        LogFat("������fd:%d,code=%d", _fd, ret.Code());
    }
    else if(ret.Async()) {
        // LogLog("���첽 fd:%d", _fd);
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
        Read();
    }
    else {
        if(_flags & Flags::Closed) {
            LogWrn("�������ر����ӣ�fd:%d", _fd);
            CloseDispatchData data;
            data.reason = CloseReason::Actively;
            Dispatch(data);
        }
        else if(ret.Succ() && dwBytes == 0) {
            LogWrn("�ѱ����ر����ӣ�fd:%d", _fd);
            CloseDispatchData data;
            data.reason = CloseReason::Passively;
            Dispatch(data);
        }
        else if(ret.Fail()) {
            LogFat("��ʧ�ܣ�fd=%d,code:%d", _fd, ret.Code());
            CloseDispatchData data;
            data.reason = CloseReason::Reset;
            Dispatch(data);
        }
    }
}
WSARet ClientSocket::_OnWrite(WriteIOContext& io)
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
        LogFat("дʧ�� %d", ret.Code());
    }

    return ret;
}
WSARet ClientSocket::_OnConnect(ConnectIOContext& io)
{
    WSARet ret = io.GetResult(_fd);
    if(ret.Succ()) {
        ConnectDispatchData data;
        Dispatch(data);
        Read();
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
        _onWrite(this, d.size);
        break;
    }
    case OpType::Close:
    {
        // �������߳�ǰ����û�ر�
        // �������Ͳ�һ����
        // �������ڴ���Զ�˹رյ�ʱ�������ر�
        // �������Ѿ��Ǳ��ر�״̬
        auto closed = IsClosed();
        auto d = static_cast<CloseDispatchData&>(data);
        if(d.reason != CloseReason::Actively) {
            if(!closed) {
                Close();
            }
        }
        // ����Ҳͬ��
        if(!closed) {
            _onClose(this, d.reason);
        }
        break;
    }
    case OpType::Connect:
    {
        auto d = static_cast<ConnectDispatchData&>(data);
        _onConnect(this, true);
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
        _OnWrite(static_cast<WriteIOContext&>(bio));
    }
    else if(bio.optype == OpType::Connect) {
        _OnConnect(static_cast<ConnectIOContext&>(bio));
    }
}

int ClientSocket::GetDescriptor()
{
    return static_cast<int>(GetSocket());
}

}
