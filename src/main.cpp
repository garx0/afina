#include <chrono>
#include <iostream>
#include <memory>

#include <atomic>
#include <semaphore.h>
#include <signal.h>
#include <thread>

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/logging/Service.h>
#include <afina/network/Server.h>

#include "logging/ServiceImpl.h"
#include "network/mt_blocking/ServerImpl.h"
#include "network/mt_nonblocking/ServerImpl.h"
#include "network/st_blocking/ServerImpl.h"
#include "network/st_nonblocking/ServerImpl.h"

#include "storage/SimpleLRU.h"
#include "storage/ThreadSafeSimpleLRU.h"

using namespace Afina;

/**
 * Whole application class
 */
class Application {
public:
    // Loading application config
    void Configure(const cxxopts::Options &options) {
        // Step 0: logger config
        logConfig.reset(new Logging::Config);
        Logging::Appender &console = logConfig->appenders["console"];
        console.type = Logging::Appender::Type::STDOUT;
        console.color = true;

        Logging::Logger &logger = logConfig->loggers["root"];
        logger.level = Logging::Logger::Level::WARNING;
        logger.appenders.push_back("console");
        logger.format = "[%H:%M:%S %z] [thread %t] [%n] [%l] %v";
        logService.reset(new Logging::ServiceImpl(logConfig));

        // Step 1: configure storage
        std::string storage_type = "st_lru";
        if (options.count("storage") > 0) {
            storage_type = options["storage"].as<std::string>();
        }

        if (storage_type == "st_lru") {
            storage = std::make_shared<Afina::Backend::SimpleLRU>();
        } else if (storage_type == "mt_lru") {
            storage = std::make_shared<Afina::Backend::ThreadSafeSimplLRU>();
        } else {
            throw std::runtime_error("Unknown storage type");
        }

        // Step 2: Configure network
        std::string network_type = "st_block";
        if (options.count("network") > 0) {
            network_type = options["network"].as<std::string>();
        }

        if (network_type == "st_block") {
            server = std::make_shared<Afina::Network::STblocking::ServerImpl>(storage, logService);
        } else if (network_type == "mt_block") {
            server = std::make_shared<Afina::Network::MTblocking::ServerImpl>(storage, logService);
        } else if (network_type == "st_nonblock") {
            server = std::make_shared<Afina::Network::STnonblock::ServerImpl>(storage, logService);
        } else if (network_type == "mt_nonblock") {
            server = std::make_shared<Afina::Network::MTnonblock::ServerImpl>(storage, logService);
        } else {
            throw std::runtime_error("Unknown network type");
        }
    }

    // Start services in correct order
    void Start() {
        logService->Start();
        auto log = logService->select("root");
        log->warn("Start afina server {}", Afina::get_version());

        log->warn("Start storage");
        storage->Start();

        // TODO: configure network service
        const uint16_t port = 8080;
        log->warn("Start network on {}", port);
        server->Start(port, 2, 2);
    }

    // Stop services in correct order
    void Stop() {
        auto log = logService->select("root");
        log->warn("Stop application");
        server->Stop();
        server->Join();

        storage->Stop();
        logService->Stop();
    }

private:
    std::shared_ptr<Afina::Logging::Config> logConfig;
    std::shared_ptr<Afina::Logging::Service> logService;

    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
};

// Signal set that to notify application about time to stop
sem_t stop_semaphore;
volatile sig_atomic_t stop_reason = 0;

// Catch user desire to stop the server
void on_term(int signum, siginfo_t *siginfo, void *data) {
    stop_reason = signum;
    sem_post(&stop_semaphore);
}

int main(int argc, char **argv) {
    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("h,help", "Print usage info");
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    // Start boot sequence
    Application app;
    app.Configure(options);

    // POSIX specific staff
    {
        // Using semaphore for communication between main thread AND signal handler
        if (sem_init(&stop_semaphore, 0, 0) != 0) {
            throw std::runtime_error("Failed to create semaphore");
        }

        // Signal handler...
        struct sigaction act;
        sigfillset(&act.sa_mask);

        act.sa_flags = 0;
        act.sa_sigaction = on_term;

        sigaction(SIGINT, &act, NULL);
        sigaction(SIGTERM, &act, NULL);
    }

    // Run app
    try {
        // Start services
        app.Start();

        // Freeze main thread until one of signals arrive
        while (stop_reason == 0 && ((sem_wait(&stop_semaphore) == -1) && (errno == EINTR))) {
            continue;
        }

        // Stop services
        app.Stop();
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}

//#include <iomanip>
//#include <iostream>
//#include <set>
//#include <vector>

//#include <afina/execute/Add.h>
//#include <afina/execute/Append.h>
//#include <afina/execute/Delete.h>
//#include <afina/execute/Get.h>
//#include <afina/execute/Set.h>

//#include "storage/SimpleLRU.h"

//using namespace Afina::Backend;
//using namespace Afina::Execute;
//using namespace std;

//void EXPECT_TRUE(bool b) {
//    if(!b) {
//        throw std::runtime_error("false");
//    }
//}

//std::string pad_space(const std::string &s, size_t length) {
//    std::string result = s;
//    result.resize(length, ' ');
//    return result;
//}

//int main() {
//    const size_t length = 20;
//    SimpleLRU storage(2 * 100000 * length);

//    for (long i = 0; i < 100000; ++i) {
//        std::cerr << i << " ";
//        auto key = pad_space("Key " + std::to_string(i), length);
//        auto val = pad_space("Val " + std::to_string(i), length);
//        EXPECT_TRUE(storage.Put(key, val));
//    }
//    std::cerr << "cycle1 done\n\n";
//    for (long i = 99999; i >= 0; --i) {
//        std::cerr << i << " ";
//        auto key = pad_space("Key " + std::to_string(i), length);
//        auto val = pad_space("Val " + std::to_string(i), length);

//        std::string res;
//        EXPECT_TRUE(storage.Get(key, res));

//        EXPECT_TRUE(val == res);
//    }
//    std::cerr << "cycle2done\n\n";
//    storage._PrintDebug(std::cerr);
//    return 0;

//    SimpleLRU storage;
//    storage.Put("KEY1", "val1"); storage._PrintDebug(std::cerr);
//    storage.Put("KEY2", "val2"); storage._PrintDebug(std::cerr);
//    storage.Put("KEY3", "val3"); storage._PrintDebug(std::cerr);
//    storage.Put("KEY3", "val3_new"); storage._PrintDebug(std::cerr);

//    std::string value;
//    storage._PrintDebug(std::cerr);
//    storage.Get("KEY2", value);
//    std::cerr << "value=" << value << "\n";
//    storage._PrintDebug(std::cerr);
//    storage.Get("KEY3", value);
//    std::cerr << "value=" << value << "\n";
//    storage._PrintDebug(std::cerr);
//    storage.Get("KEY2", value);
//    std::cerr << "value=" << value << "\n";
//    storage._PrintDebug(std::cerr);
//    storage.Put("KEY4", "val4");
//    storage._PrintDebug(std::cerr);
//    storage.Delete("KEY1");
//    std::cerr << __LINE__ << "\n";
//    storage.Delete("KEY3");
//    std::cerr << __LINE__ << "\n";
//    storage.Delete("KEY2");
//    std::cerr << __LINE__ << "\n";
//}
