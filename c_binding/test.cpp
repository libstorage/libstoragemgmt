#include "lsm_ipc.h"

#include <iostream>
#include <stdlib.h>

std::string gen_random(int len) {
    static const char alphanum[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";
    std::string rc(len, 'x');

    for (int i = 0; i < len; ++i) {
        rc[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return rc;
}

void TestIt(int fd) {
    int rc = 0;
    int ec = 0;
    Transport t(fd);

    for (int i = 1; i < 1024 * 1024 * 16; i += 1) {

        std::string msg = gen_random(i);

        std::cout << "sending msg" << std::endl;
        rc = t.sendMsg(msg, ec);
        std::cout << "message sent: " << rc << std::endl;
        if (rc == 0) {

            std::cout << "Receiving msg" << std::endl;
            std::string rmsg = t.recvMsg(ec);
            std::cout << "Message received " << rmsg.size() << " Byte(s)"
                      << std::endl;

            if (rmsg.size() > 0) {

                if (msg != rmsg) {
                    std::cout << "Data miss-compare" << std::endl;
                    std::cout << "Recv: " << rmsg << std::endl;
                }
            } else {
                std::cout << "Error recv: " << ec << std::endl;
                break;
            }
        } else {
            std::cout << "Error send: " << ec << std::endl;
            break;
        }
    }
}

int main(void) {
    std::string path("/tmp/testing");

    int ec = 0;
    int fd = 0;

    fd = Transport::getSocket(path, ec);

    if (fd >= 0) {
        TestIt(fd);
    } else {
        std::cout << "Error getting connected socket: " << ec << std::endl;
    }

    return 0;
}
