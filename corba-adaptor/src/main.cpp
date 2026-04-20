// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Entry Point
//
// Initializes the ORB, parses configuration from command-line arguments,
// constructs the Adaptor, and runs until shutdown is signalled.
//
// Signal handling:  SIGINT/SIGTERM set an atomic flag via
// Adaptor::request_shutdown() — no mutex locks or CORBA calls in the
// signal handler.  The main thread detects the flag and performs orderly
// teardown.
//
// Naming service:
//   Suppliers and CommandReceiver are registered in the CORBA Naming
//   Service under "CorbaAdaptor/<Name>". Clients discover them by name;
//   no IOR files are exchanged.  Pass
//     -ORBInitRef NameService=corbaloc:iiop:<host>:<port>/NameService
//   on the command line.
//
// Usage:
//   corba-adaptor [ORB options]
//       --tcp-host <host>           TCP peer server host (default: 127.0.0.1)
//       --tcp-port <port>           TCP peer server port (required)
//       --corba-peer-ior <ior>      IOR/corbaname of the RawDataChannel
//       --health-interval <secs>    Consumer health-check interval (default: 30)
//       --orb-threads <n>           ORB thread pool size (default: 1)

#include "adaptor/adaptor.hpp"

#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>
#include <ace/Get_Opt.h>
#include <ace/Log_Msg.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

// Signal-safe: only stores to a sig_atomic_t flag.
volatile std::sig_atomic_t g_shutdown_flag = 0;

void signal_handler(int) {
    g_shutdown_flag = 1;
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [ORB options] [adaptor options]\n"
        << "\n"
        << "Required ORB option:\n"
        << "  -ORBInitRef NameService=corbaloc:iiop:<host>:<port>/NameService\n"
        << "\n"
        << "Adaptor options:\n"
        << "  --tcp-host <host>           TCP peer host (default: 127.0.0.1)\n"
        << "  --tcp-port <port>           TCP peer port (required)\n"
        << "  --corba-peer-ior <ior>      IOR of the remote RawDataChannel\n"
        << "  --health-interval <secs>    Health-check interval (default: 30)\n"
        << "  --orb-threads <n>           ORB thread pool size (default: 1)\n"
        << "  --help                      Show this help\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    try {
        // Initialize the ORB (consumes -ORB* arguments).
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);

        // Resolve the RootPOA.
        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var root_poa = PortableServer::POA::_narrow(poa_obj.in());
        PortableServer::POAManager_var poa_mgr = root_poa->the_POAManager();

        // Parse adaptor-specific arguments.
        adaptor::AdaptorConfig config;
        config.tcp.host = "127.0.0.1";

        static const ACE_TCHAR options[] = ACE_TEXT("");
        ACE_Get_Opt get_opt(argc, argv, options, 0);

        get_opt.long_option(ACE_TEXT("tcp-host"),        'H', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("tcp-port"),        'P', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("corba-peer-ior"),  'C', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("health-interval"), 'I', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("orb-threads"),     'T', ACE_Get_Opt::ARG_REQUIRED);
        get_opt.long_option(ACE_TEXT("help"),            'h', ACE_Get_Opt::NO_ARG);

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
                case 'I':
                    config.health_check_interval_seconds =
                        static_cast<uint32_t>(std::atoi(get_opt.opt_arg()));
                    break;
                case 'T':
                    config.orb_threads = static_cast<uint32_t>(std::atoi(get_opt.opt_arg()));
                    break;
                case 'h':
                    print_usage(argv[0]);
                    return 0;
                default:
                    break;
            }
        }

        if (config.tcp.port == 0) {
            std::cerr << "Error: --tcp-port is required\n\n";
            print_usage(argv[0]);
            return 1;
        }

        // Install signal handlers (signal-safe: only set a flag).
        std::signal(SIGINT,  signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Activate the POA manager.
        poa_mgr->activate();

        // Create and start the adaptor.
        adaptor::Adaptor the_adaptor(orb.in(), root_poa.in(), config);
        the_adaptor.start();

        ACE_DEBUG((LM_INFO, "Adaptor: running (Ctrl+C to stop)\n"));

        // Monitor for shutdown: poll the signal flag then delegate to adaptor.
        while (!g_shutdown_flag) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        ACE_DEBUG((LM_INFO, "\nAdaptor: signal received, initiating shutdown...\n"));
        the_adaptor.stop();

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
