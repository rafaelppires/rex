#include <argp.h>
#include <args_rex.h>
#include <communication_manager.h>
#include <data_splitter.h>
#include <enclave_interface.h>
#include <generic_utils.h>
#include <pwd.h>
#include <stringtools.h>
#include <sync_zmq.h>
#include <sys/types.h>
#include <unistd.h>
#include <csignal>
#include <iostream>
#include <set>
#include <thread>

#define DEFAULT_PORT 4444
#define quote(s) #s
#define quotevalue(m) quote(m)

const char *argp_program_version = "Rex SGX Recommender";
const char *argp_program_bug_address = "<rafael.pires@epfl.ch>";

static char doc[] = "Rex SGX Recommender: data sharing inside enclaves";
static char args_doc[] = "";
static struct argp_option options[] = {
    {"filename", 'f', "filename", 0, "Input data file."},
    {"sharedata", 's', 0, 0, "Share raw data."},
    {"disable_model_sharing", 'x', 0, 0,
     "Disable sharing of models. Enables data sharing by default."},
    {"share_howmany", 'h', "howmany", 0,
     "Number of ratings shared by node in each iteration."},
    {"steps_per_iteration", 'u', "steps", 0,
     "Number of local steps in each iteration or epoch."},
    {"local", 'l', "number", 0, "Local iterations. Default: 1."},
    {"dpsgd", 'd', 0, 0, "Switch to DPSGD. Default: RMW."},
    {"port", 'p', "port", 0, "Listening port"},
    {"machines", 'm', "\"host1 host2:port2 [...]\"", 0,
     "List of machines in host:port format, separated by space and enclosed by "
     "quotes. In case no port is provided, default port " quotevalue(
         DEFAULT_PORT) " is assumed. All nodes should provide this list in the "
                       "same order."},
    {"epochs", 'e', "howmany", 0, "Number of epochs. Deafult 10."},
    {"usersdata", 'c', "howmany", 0,
     "Cap the amount of users in the input file. Default: unlimited."},
    {0}};

//------------------------------------------------------------------------------
struct Arguments {
    Arguments()
        : port(DEFAULT_PORT),
          modelshare(true),
          datashare(false),
          dpsgd(false),
          share_howmany(20),
          local(1),
          steps_per_iteration(30),
          epochs(10) {}
    uint16_t port;
    bool datashare, modelshare, dpsgd;
    std::string machines, input_fname;
    unsigned share_howmany, local, epochs;
    size_t steps_per_iteration, capusers;
};

//------------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    Arguments *args = (Arguments *)state->input;
    switch (key) {
        case 'f':
            args->input_fname = arg;
            break;
        case 's':
            args->datashare = true;
            break;
        case 'x':
            args->modelshare = false;
            break;
        case 'h':
            args->share_howmany = std::atoi(arg);
            break;
        case 'u':
            args->steps_per_iteration = std::atoi(arg);
            break;
        case 'l':
            args->local = std::atoi(arg);
            break;
        case 'd':
            args->dpsgd = true;
            break;
        case 'p':
            args->port = std::stoi(arg);
            break;
        case 'e':
            args->epochs = std::stoi(arg);
            break;
        case 'c':
            args->capusers = std::stoi(arg);
            break;
        case 'm':
            args->machines = arg;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    };
    return 0;
}

//------------------------------------------------------------------------------
int farewell_message(int s) {
    std::string unit;
    double amount = bytes_human(NetStats::bytes_in, unit);
    std::cout << "\nData in: " << amount << " " << unit << std::endl;
    amount = bytes_human(NetStats::bytes_out, unit);
    std::cout << "Data out: " << amount << " " << unit << std::endl;

    printf("\033[0m\n(%d) bye!\n", s);
    return s;
}

//------------------------------------------------------------------------------
std::thread *headsman_thread = nullptr;
void ctrlc_handler(int s) {
    if (headsman_thread == nullptr) {
        EnclaveInterface::finish();
        headsman_thread = new std::thread(
            []() { CommunicationManager<CommunicationZmq>::finish(); });
    }
}

//------------------------------------------------------------------------------
std::pair<int, size_t> find_userrank_and_neigh(
    const std::string machines, uint16_t port,
    std::set<std::pair<std::string, uint16_t>> &neigh, std::string &nlist) {
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);

    auto hosts = split(machines, " ");
    int index = 0, myindex = -1;
    for (const auto &h : hosts) {
        auto hostport = split(h, ":");
        try {
            if (myindex < 0 && hostname == hostport[0] &&
                (hostport.size() == 1 && port == DEFAULT_PORT ||
                 hostport.size() > 1 && port == std::stoi(hostport[1]))) {
                myindex = index;
                nlist += " -";
            } else {
                auto endpoint = std::make_pair(
                    hostport[0], hostport.size() == 1 ? DEFAULT_PORT
                                                      : std::stoi(hostport[1]));
                neigh.insert(endpoint);
                nlist += " " + endpoint.first + ":" +
                         std::to_string(endpoint.second);
            }
        } catch (const std::invalid_argument &e) {
            std::cerr << "Invalid endpoint: " << h << std::endl;
            index = -1;
            break;
        }
        index++;
    }

    if (myindex < 0) {
        std::cerr << "Could not myself in endpoint list " << machines << "."
                  << std::endl;
    }
    return std::make_pair(myindex, hosts.size());
}

//------------------------------------------------------------------------------
bool read_data(const std::string &fname, TripletVector<uint8_t> &train,
               TripletVector<uint8_t> &test, int total_nodes, int userrank,
               size_t cap) {
    std::pair<int, int> dim =
        read_and_split(fname, train, test, cap, total_nodes, userrank);
    if (dim.first == 0 || dim.second == 0) return false;

    return true;
}

//------------------------------------------------------------------------------
int main(int argc, char **argv) {
    struct argp argp = {options, parse_opt, 0, doc};
    Arguments args;
    argp_parse(&argp, argc, argv, 0, 0, &args);

    struct passwd *pw = getpwuid(getuid());
    const char *homedir = pw->pw_dir;
    std::string fname =
        std::string(homedir) + "/movielens/ml-latest-small/ratings.csv";
    if (args.input_fname.empty()) {
        std::cerr << "Input data file not provided. See option -f ($ "
                  << argv[0] << " -?)\nTrying default: " << fname << "\n";
    } else {
        fname = args.input_fname;
    }

    if (args.machines.empty()) {
        std::cerr << "Nodes list not provided. See option -m." << std::endl;
        return 1;
    }

    std::set<std::pair<std::string, uint16_t>> neighbors;
    std::string nlist;
    auto index_count =
        find_userrank_and_neigh(args.machines, args.port, neighbors, nlist);
    for (const auto &n : neighbors) {
        CommunicationZmq::add_endpoint(n.first, n.second);
    }

    struct EnclaveArguments enclave_args;
    enclave_args.userrank = index_count.first;
    if (index_count.first < 0) {
        return 2;
    }

    TripletVector<uint8_t> train, test;
    if (!read_data(fname, train, test, index_count.second,
                   enclave_args.userrank, args.capusers)) {
        return 3;
    }

    change_dir(argv[0]);

    // Arguments passed on to the enclave
    enclave_args.train = reinterpret_cast<uint8_t *>(train.data());
    enclave_args.train_size = train.size();
    enclave_args.test = reinterpret_cast<uint8_t *>(test.data());
    enclave_args.test_size = test.size();
    enclave_args.degree = neighbors.size();
    enclave_args.datashare = uint8_t(args.datashare);
    enclave_args.modelshare = uint8_t(args.modelshare);
    enclave_args.dpsgd = uint8_t(args.dpsgd);
    enclave_args.share_howmany = args.share_howmany;
    enclave_args.local = args.local;
    enclave_args.steps_per_iteration = args.steps_per_iteration;
    enclave_args.epochs = args.epochs;

    strncpy(enclave_args.nodes, nlist.c_str(), sizeof(enclave_args.nodes));
    if (EnclaveInterface::init(enclave_args)) {
        CommunicationManager<CommunicationZmq>::init(
            EnclaveInterface::input<std::string>, args.port);
        std::signal(SIGINT, ctrlc_handler);
        CommunicationManager<CommunicationZmq>::iterate();
        if (headsman_thread) {
            headsman_thread->join();
        }
    }
    return farewell_message(0);
}
