#include "AIOxx/event_loop.hpp"
#include "AIOxx/main.hpp"
#include "AIOxx/net_fd.hpp"

constexpr char END_OF_MSG = '\n';

struct Client {
    size_t id;
    std::list<Client>::iterator iter;
    AIO::BufferedStreamFD<AIO::StreamSocketFD> sock;

    [[nodiscard]] std::string name() const {
        return "#" + std::to_string(id);
    }
};

void AIO_MAIN(AIO::BasicEventLoop &loop, int argc, const char *const *argv) {
    std::list<Client> clients;

    auto serve_client = loop.async([&](Client &client) {
        while (true) {
            std::string msg_in = loop.await(client.sock.read_until(END_OF_MSG));
            if (!msg_in.ends_with(END_OF_MSG)) {
                break;
            }
            msg_in.pop_back();

            std::cout << client.name() << ": " << msg_in << std::endl;
            auto msg_out = std::make_shared<std::string>(client.name() + ": " + msg_in + END_OF_MSG);
            for (auto &peer : clients) {
                loop.execute([&, msg_out] { loop.await(peer.sock.write_string(*msg_out)); })
                    .then(loop.async([&] { loop.await(peer.sock.flush()); }))
                    .except<AIO::SystemError>(loop.async([&peer](auto &err) {
                        std::cout << "Error sending to " << peer.name() << ' ' << err.what() << std::endl;
                    }))
                    .detach();
            }
        }
    });

    auto run_server = loop.async([&] [[noreturn]] (auto host, auto port) {
        std::cout << "Starting server..." << std::endl;
        AIO::StreamServerFD server(loop, host, port);

        std::cout << "Listening on " << host << ":" << port << "..." << std::endl;
        for (size_t id = 1;; id++) {
            auto client_sock = loop.await(server.accept());
            auto iter = clients.emplace(clients.end(),
                id, clients.end(), AIO::BufferedStreamFD(std::move(client_sock))
            );
            auto &client = *iter;
            client.iter = iter;

            std::cout << client.name() << " connected." << std::endl;
            serve_client(client)
                .except<AIO::SystemError>(loop.async([&client](auto &&err) {
                    std::cout << "Error listening from " << client.name() << ' ' << err.what() << std::endl;
                }))
                .then(loop.async([&] {
                    std::cout << client.name() << " disconnected." << std::endl;
                    clients.erase(client.iter);
                }))
                .detach();
        }
    });

    if (argc != 3) {
        std::cerr << argv[0] << ": usage:" << std::endl << argv[0] << " <IP> <PORT>" << std::endl;
        std::exit(1);
    }

    return loop.await(run_server(argv[1], argv[2]));
}
