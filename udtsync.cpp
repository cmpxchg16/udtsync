#include <cstdlib>
#include <netdb.h>
#include <fstream>
#include <iostream>
#include <cstring>

#include <udt.h>
#include <cc.h>

using namespace std;

void* server_handler(void*);

const char* ccc_arr[] = {"CUDPBlast", "CTCP"};
const char* cmd_arr[] = {"GET", "PUT"};
const static int cmd_len = 3;

void validate_ccc(string cccstr)
{
    bool found = false;
    for (size_t i = 0; i < sizeof(ccc_arr)/sizeof(*ccc_arr); ++i)
    {
        if (!strcasecmp(ccc_arr[i], cccstr.c_str()))
        {
            found = true;
        }
    }

    if (!found)
    {
        cout << "unsupported congestion control class (CCC): " << cccstr << " using default" << endl;
    }
}

void ccc(string cccstr, UDTSOCKET sock)
{
    if (!strcasecmp(cccstr.c_str(), "CTCP"))
    {
        UDT::setsockopt(sock, 0, UDT_CC, new CCCFactory<CTCP>, sizeof(CCCFactory<CTCP>));
    }
    else if (!strcasecmp(cccstr.c_str(), "CUDPBlast"))
    {
        UDT::setsockopt(sock, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
    }
}

int client(const char* server_ip, const char* server_port, const char* remote_filename, const char* local_filename, string cccstr, string cmd)
{
    // use this function to initialize the UDT library
    UDT::startup();

    struct addrinfo hints, *peer;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    UDTSOCKET fhandle = UDT::socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);

    ccc(cccstr, fhandle);

    if (0 != getaddrinfo(server_ip, server_port, &hints, &peer))
    {
        cerr << "incorrect server/peer address. " <<  server_ip << ":" << server_port << endl;
        exit(EXIT_FAILURE);
    }

    // connect to the server, implict bind
    if (UDT::ERROR == UDT::connect(fhandle, peer->ai_addr, peer->ai_addrlen))
    {
        cerr << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    cout << "successfully connect to: " << server_ip << ":" << server_port << endl;

    freeaddrinfo(peer);

    if (UDT::ERROR == UDT::send(fhandle, cmd.c_str(), cmd_len, 0))
    {
        cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    // send name information of the requested file
    int len = strlen(remote_filename);

    if (UDT::ERROR == UDT::send(fhandle, (char*)&len, sizeof(int), 0))
    {
        cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    if (UDT::ERROR == UDT::send(fhandle, remote_filename, len, 0))
    {
        cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    if (strcasecmp(cmd.c_str(), "GET") == 0)
    {
        // get size information
        int64_t size;

        if (UDT::ERROR == UDT::recv(fhandle, (char*)&size, sizeof(int64_t), 0))
        {
            cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        if (size < 0)
        {
            cerr << "no such file " << remote_filename << " on the server\n";
            exit(EXIT_FAILURE);
        }

        cout << cmd << " " << remote_filename << " " << "(size: " << size << " bytes)" << endl;

        // receive the file
        fstream ofs(local_filename, ios::out | ios::binary | ios::trunc);
        int64_t recvsize;
        int64_t offset = 0;

        if (UDT::ERROR == (recvsize = UDT::recvfile(fhandle, ofs, offset, size)))
        {
            cerr << "recvfile: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        ofs.close();
    }
    else //PUT
    {
        // open the file
        fstream ifs(local_filename, ios::in | ios::binary);

        ifs.seekg(0, ios::end);
        int64_t size = ifs.tellg();
        ifs.seekg(0, ios::beg);

        cout << cmd << " " << local_filename << " " << "(size: " << size << " bytes)" << endl;

        // send file size information
        if (UDT::ERROR == UDT::send(fhandle, (char*)&size, sizeof(int64_t), 0))
        {
            cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        // send the file
        int64_t offset = 0;

        if (UDT::ERROR == UDT::sendfile(fhandle, ifs, offset, size))
        {
            cerr << "sendfile: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        ifs.close();
    }

    UDT::close(fhandle);
    // use this function to release the UDT library
    UDT::cleanup();

    return 0;
}

void server(const char* port, string cccstr)
{
    // use this function to initialize the UDT library
    UDT::startup();

    addrinfo hints;
    addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (0 != getaddrinfo(NULL, port, &hints, &res))
    {
        cerr << "illegal port number or port is busy.\n" << endl;
        exit(EXIT_FAILURE);
    }

    UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (UDT::ERROR == UDT::bind(serv, res->ai_addr, res->ai_addrlen))
    {
        cerr << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);

    cout << "server is ready at port: " << port << endl;

    UDT::listen(serv, 10);

    sockaddr_storage clientaddr;
    int addrlen = sizeof(clientaddr);

    UDTSOCKET fhandle;

    while (true)
    {
        if (UDT::INVALID_SOCK == (fhandle = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen)))
        {
            cerr << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        char clienthost[NI_MAXHOST];
        char clientservice[NI_MAXSERV];
        getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
        cout << "new connection: " << clienthost << ":" << clientservice << endl;

        ccc(cccstr, fhandle);

        pthread_t filethread;
        pthread_create(&filethread, NULL, server_handler, new UDTSOCKET(fhandle));
        pthread_detach(filethread);
    }

    UDT::close(serv);

    // use this function to release the UDT library
    UDT::cleanup();
}

void* server_handler(void* usocket)
{
    UDTSOCKET fhandle = *(UDTSOCKET*)usocket;
    delete (UDTSOCKET*)usocket;

    // aquiring file name information from client
    char cmd[cmd_len+1];
    cmd[cmd_len] = '\0';
    char file[4096];
    int len;

    if (UDT::ERROR == UDT::recv(fhandle, cmd, cmd_len, 0))
    {
        cerr << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    if (UDT::ERROR == UDT::recv(fhandle, (char*)&len, sizeof(int), 0))
    {
        cerr << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }

    if (UDT::ERROR == UDT::recv(fhandle, file, len, 0))
    {
        cerr << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
        exit(EXIT_FAILURE);
    }
    file[len] = '\0';

    if (strcasecmp(cmd, "GET") == 0)
    {
        // open the file
        fstream ifs(file, ios::in | ios::binary);

        ifs.seekg(0, ios::end);
        int64_t size = ifs.tellg();
        ifs.seekg(0, ios::beg);

        cout << cmd << " " << file << " " << "(size: " << size << " bytes)" << endl;

        // send file size information
        if (UDT::ERROR == UDT::send(fhandle, (char*)&size, sizeof(int64_t), 0))
        {
            cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        UDT::TRACEINFO trace;
        UDT::perfmon(fhandle, &trace);

        // send the file
        int64_t offset = 0;
        if (UDT::ERROR == UDT::sendfile(fhandle, ifs, offset, size))
        {
            cerr << "sendfile: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        UDT::perfmon(fhandle, &trace);
        cout << "speed = " << trace.mbpsSendRate << "Mbits/sec" << endl;

        ifs.close();
    }
    else //PUT
    {
        int64_t size;

        if (UDT::ERROR == UDT::recv(fhandle, (char*)&size, sizeof(int64_t), 0))
        {
            cerr << "send: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        cout << cmd << " " << file << " " << "(size: " << size << " bytes)" << endl;

        // receive the file
        fstream ofs(file, ios::out | ios::binary | ios::trunc);
        int64_t recvsize;
        int64_t offset = 0;

        UDT::TRACEINFO trace;
        UDT::perfmon(fhandle, &trace);

        if (UDT::ERROR == (recvsize = UDT::recvfile(fhandle, ofs, offset, size)))
        {
            cerr << "recvfile: " << UDT::getlasterror().getErrorMessage() << endl;
            exit(EXIT_FAILURE);
        }

        UDT::perfmon(fhandle, &trace);
        cout << "speed = " << trace.mbpsRecvRate << "Mbits/sec" << endl;

        ofs.close();
    }

    UDT::close(fhandle);

    return NULL;
}

void usage(const char* argv)
{
    cerr << "Usage: " << argv << "\n\n" << "server [port] [CCC]\n"
         "\n"
         "client [server_ip] [server_port] [remote_filename] [local_filename] [GET|PUT] [CCC]\n"
         "\n"
         "{CCC (Congetion control class - UDT is default): CTCP | CUDPBlast}\n" << endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    string cccstr = "";

    if (argc < 3 || (strcmp(argv[1], "server") != 0 && strcmp(argv[1], "client") != 0))
    {
        usage(argv[0]);
    }

    if (strcmp(argv[1], "server") == 0)
    {
        if (0 == atoi(argv[2]))
        {
            cerr << "wrong server port" << endl;
            usage(argv[0]);
        }

        if (argc == 4)
        {
            cccstr = argv[3];
            validate_ccc(cccstr);
        }

        server(argv[2], cccstr);
    }
    else
    {
        if (0 == atoi(argv[3]))
        {
            cerr << "wrong server port" << endl;
            usage(argv[0]);
        }

        if (argc < 7)
        {
            usage(argv[0]);
        }

        if ((strcasecmp(argv[6], "GET") != 0 && strcasecmp(argv[6], "PUT") != 0))
        {
            cerr << "cmd must be GET|PUT" << endl;
            usage(argv[0]);
        }

        if (argc == 8)
        {
            cccstr = argv[7];
            validate_ccc(cccstr);
        }

        client(argv[2], argv[3], argv[4], argv[5], cccstr, argv[6]);
    }

    exit(EXIT_SUCCESS);
}
