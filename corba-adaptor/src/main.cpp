// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Entry Point
//
// Initializes the ORB, parses configuration from command-line arguments
// and environment variables, constructs the Adaptor, and runs until
// shutdown is signalled.
//
// Usage:
//   corba-adaptor [ORB options]
//       --tcp-host <host>           TCP peer server host (default: 127.0.0.1)
//       --tcp-port <port>           TCP peer server port (required)
//       --corba-peer-ior <ior>      IOR/corbaname of the RawDataChannel
//       --supplier-ior-file <path>  Write supplier IOR to file
//       --command-ior-file <path>   Write command receiver IOR to file
//       --health-interval <secs>    Consumer health-check interval (default: 30)

#include <adaptor/adaptor.hpp>

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>
#include <ace/Get_Opt.h>
#include <ace/Log_Msg.h>
#include <ace/Signal.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Global adaptor pointer for signal handling
std::atomic<adaptor::Adaptor*> g_adaptor{nullptr};

void signal_handler(int signum) {
    ACE_DEBUG((LM_INFO, "\nReceived signal %d, shutting down...\n", signum));
    if (auto* a = g_adaptor.load()) {
        a->stop();
    }
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [ORB options] [adaptor options]\n"
        << "\n"
        << "Adaptor options:\n"
        << "  --tcp-host <host>           TCP peer host (default: 127.0.0.1)\n"
        << "  --tcp-port <port>           TCP peer port (required)\n"
        << "  --corba-peer-ior <ior>      IOR of the remote RawDataChannel\n"
        << "  --supplier-ior-file <path>  Write DataSupplier IOR to file\n"
        << "  --command-ior-file <path>   Write CommandReceiver IOR to file\n"
        << "  --health-interval <secs>    Health-check interval (default: 30)\n"
        << "  --help                      Show this help\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    try {
        // Initialize the ORB (consumes -ORB* arguments)
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);

        // Resolve the RootPOA
        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var root_poa = PortableServer::POA::_narrow(poa_obj.in());
        PortableServer::POAManager_var poa_mgr = root_poa->the_POAManager();

        // Parse adaptor-specific arguments
        adaptor::AdaptorConfig config;
        config.tcp.host = "127.0.0.1";

        // Use ACE_Get_Opt for long options
        static const ACE_TCHAR options[] = ACE_TEXT("");
        ACE_Get_Opt get_opt(argc, argv, options, 0);

        get_opt.long_option(ACE_TEXT("tcp-host"),        'H', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("tcp-port"),        'P', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("corba-peer-ior"),  'C', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("supplier-ior-file"), 'S', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("command-ior-file"),  'R', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("health-interval"),   'I', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("help"),               'h', ACE_Get_Opt::NO_ARG);

        int c;
        while ((c = get_opt()) != -1) {
            switch (c) {
                case 'H':
                    config.tcp.host = get_opt.opt_arg();
                    break;
                case 'P':
                    config.tcp.port = static_cast<uint16_t>(std::atoi(get_opt.opt_arg()));
                    break;
                case 'C':
                    config.corba_peer.channel_ior = get_opt.opt_arg();
                    break;
                case 'S':
                    config.supplier_ior_file = get_opt.opt_arg();
                    break;
                case 'R':
                    config.command_ior_file = get_opt.opt_arg();
                    break;
                case 'I':
                    config.health_check_interval =
                        std::chrono::seconds(std::atoi(get_opt.opt_arg()));
                    break;
                case 'h':
                    print_usage(argv[0]);
                    return 0;
                default:
                    break;
            }
        }

        // Validate required arguments
        if (config.tcp.port == 0) {
            std::cerr << "Error: --tcp-port is required\n\n";
            print_usage(argv[0]);
            return 1;
        }

        // Install signal handlers
        std::signal(SIGINT,  signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Activate the POA manager
        poa_mgr->activate();

        // Create and start the adaptor
        adaptor::Adaptor the_adaptor(orb.in(), root_poa.in(), std::move(config));
        g_adaptor = &the_adaptor;

        the_adaptor.start();

        ACE_DEBUG((LM_INFO, "Adaptor: running (Ctrl+C to stop)\n"));

        // Run the ORB event loop (blocks until orb->shutdown())
        // We run this in a separate thread so we can also wait for
        // the adaptor's own shutdown signal.
        std::thread orb_thread([&orb] {
            orb->run();
        });

        the_adaptor.wait_for_shutdown();

        // Shut down the ORB
        orb->shutdown(false);
        if (orb_thread.joinable()) {
            orb_thread.join();
        }

        g_adaptor = nullptr;

        orb->destroy();
        ACE_DEBUG((LM_INFO, "Adaptor: clean shutdown complete\n"));
        return 0;

    } catch (const CORBA::Exception& ex) {
        std::cerr << "CORBA exception: " << ex._info() << std::endl;
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return 1;
    }
}
