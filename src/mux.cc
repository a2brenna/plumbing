#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <set>
#include <unistd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <sstream>

size_t UNIX_MAX_PATH = 108;

std::string UDS;
int PORT = 0;

std::mutex recvers_mutex;
std::set<int> recvers;

namespace po = boost::program_options;

void accept_new_connections(const int &fd){
    while(true){
        const int new_connection = accept(fd, nullptr, nullptr);
        if(new_connection < 0){
            continue;
        }
        else{
            std::lock_guard<std::mutex> l(recvers_mutex);
            recvers.insert(new_connection);
            raise(SIGUSR1);
        }
    }
};

int main(int argc, const char* argv[]){

    po::options_description desc("Options");
    desc.add_options()
        ("help", "Produce help message")
        ("port", po::value<int>(&PORT), "Port to listen on")
        ("uds", po::value<std::string>(&UDS), "Unix Domain Socket to listen on")
        ;

    po::variables_map vm;
    const auto command_line_parameters = po::parse_command_line(argc, argv, desc);
    po::store(command_line_parameters, vm);
    po::notify(vm);

    char *buf = (char*)malloc(4096);

    //set all signals to be blocked except SIGINT to be handled by a signalfd, signal_fd;
    const int signal_fd = [](){
        sigset_t mask;
        sigfillset(&mask);
        sigdelset(&mask, SIGINT);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1);
        return signalfd(-1, &mask, 0);
    }();

    //spawn accept_new_connections threads for port and/or uds
    if(PORT != 0){
		std::stringstream s;
		s << PORT;
		const std::string port_string = s.str();

		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockfd >= 0);

		const int yes = 1;
		const auto sa = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		const auto sb = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));
        assert( (sa == 0) && (sb == 0) );

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(PORT);
        address.sin_addr.s_addr = INADDR_ANY;

        const auto bind_result = bind(sockfd, (struct sockaddr *)(&address), sizeof(address));
        assert(bind_result == 0);

		const int l = ::listen(sockfd, SOMAXCONN);
        assert(l >= 0);

        std::thread t(accept_new_connections, sockfd);
        t.detach();
    }
    if(!UDS.empty()){
        struct addrinfo res;
        struct sockaddr_un address;

        const auto m = memset(&address, 0, sizeof(struct sockaddr_un));
        assert( m == &address);

        address.sun_family = AF_UNIX;
        strncpy(address.sun_path, UDS.c_str(), UNIX_MAX_PATH);
        address.sun_path[UNIX_MAX_PATH - 1] = '\0';

        res.ai_addr = (struct sockaddr *)&address;
        res.ai_addrlen = sizeof(struct sockaddr_un);

        int sockfd =  socket(AF_UNIX, SOCK_STREAM, 0);
        assert(sockfd >= 0);

        const int yes = 1;
        const auto a = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        const auto b = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int));
        assert( (a == 0) && (b == 0) );

        const int bind_result = bind(sockfd, res.ai_addr, res.ai_addrlen);
        assert(bind_result >= 0);

        const int l = ::listen(sockfd, SOMAXCONN);
        assert(l >= 0);

        std::thread t(accept_new_connections, sockfd);
        t.detach();
    }

    //Gotta check stdin
    recvers.insert(0);

    while(true){
        std::pair<fd_set, int> current_connections = [](){
            fd_set fdset;
            int nfds = 0;
            std::lock_guard<std::mutex> l(recvers_mutex);
            for(const auto &i: recvers){
                FD_SET(i, &fdset);
                nfds = std::max(nfds, i + 1);
            }
            return std::pair<fd_set, int>(fdset, nfds);
        }();

        const int select_result = select(current_connections.second, &(current_connections.first), nullptr, nullptr, nullptr);
        assert(select_result > 0);

        if(FD_ISSET(signal_fd, &(current_connections.first))){
            const int r = read(signal_fd, buf, 4096);
            assert(r > 0);
        }
        else{
            for(int f = 0; f < current_connections.second; f++){
                if(FD_ISSET(f, &(current_connections.first))){
                    bool dirty = false;
                    while(true){
                        int bytes = read(f, buf, 4096);
                        if(bytes <= 0){
                            //error of some sort, or fd is closed
                            std::lock_guard<std::mutex> l(recvers_mutex);
                            recvers.erase(f);
                            close(f);
                            break;
                        }
                        else{
                            //forward the bytes
                            int r = write(1, buf, bytes);
                            assert(r == bytes);
                            //check to see if we wrote an end of line
                            if(buf[bytes - 1] == '\n'){
                                //end of line
                                dirty = false;
                                break;
                            }
                            else{
                                //not end of line, expect more data, should always recv lines
                                dirty = true;
                                continue;
                            }
                        }
                    }
                    if(dirty){
                        int r = write(1, "\n", 1);
                        assert(r == 1);
                    }
                }
                else{
                    continue;
                }
            }
        }
    }

    return 0;
}
