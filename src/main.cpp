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
        logger.level = Logging::Logger::Level::WARNING; // for debug, toggle: WARNING/DEBUG
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

        // Step 3: configure parameters for server->Start(...)
        workers = 3; // default
        if (options.count("workers") > 0) {
            workers = options["workers"].as<uint32_t>();
        }

        if (network_type == "mt_block") {
            acceptors = workers; // default
        } else {
            acceptors = 3; // default
        }
        if (options.count("acceptors") > 0) {
            acceptors = options["acceptors"].as<uint32_t>();
        }

        timeout = std::chrono::seconds{5}; // default
        if (options.count("timeout") > 0) {
            timeout = std::chrono::milliseconds{options["timeout"].as<uint32_t>()};
        }
        port = 8080; // default
        if (options.count("port") > 0) {
            port = options["port"].as<uint16_t>();
        }
    }

    // Start services in correct order
    void Start() {
        logService->Start();
        auto log = logService->select("root");
        log->warn("Start afina server {}", Afina::get_version());

        log->warn("Start storage");
        storage->Start();

        // DONE: configure network service
        log->warn("Start network on {}, acceptors={}, workers={}, timeout={} ms", port, acceptors, workers,
                  timeout.count());
        server->Start(port, acceptors, workers, timeout);
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

    uint32_t workers;
    uint32_t acceptors;
    std::chrono::milliseconds timeout;
    uint16_t port;
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
        // DONE?: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("w,workers", "Max number of workers (def=1)", cxxopts::value<uint32_t>());
        options.add_options()("a,acceptors", "Max number of acceptable connections (def=1)",
                              cxxopts::value<uint32_t>());
        options.add_options()("t,timeout", "Timeout in ms (def=5000)", cxxopts::value<uint32_t>());
        options.add_options()("p,port", "Server port (def=8080)", cxxopts::value<uint16_t>());
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
