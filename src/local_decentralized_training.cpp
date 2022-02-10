#include <argp.h>
#include <data_splitter.h>
#include <machine_learning/mf_coordinator.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <iostream>

const char *argp_program_version = "MF local training";
const char *argp_program_bug_address = "<rafael.pires@epfl.ch>";

#define DEFAULTDIR "out"
static char doc[] =
    "MF decentralized training: PoC to check implementation correctness";
static char args_doc[] = "";
static struct argp_option options[] = {
    {"filename", 'f', "filename", 0, "Input data file."},
    {"sharedata", 's', 0, 0, "Share raw data."},
    {"disable_model_sharing", 'x', 0, 0,
     "Disable sharing of models. Enables data sharing by default."},
    {"local", 'l', "number", 0, "Local iterations. Default: 1."},
    {"dpsgd", 'd', 0, 0, "Switch to DPSGD. Default: RMW."},
    {"embedding", 'k', "size", 0, "Size of feature vectors (embeddings)"},
    {"sharedmemory", 'm', 0, 0,
     "Switch to shared memory communication. You cannot get network "
     "measurements in this mode."},
    {"randomgraph", 'r', 0, 0, "Switch to Random Graph. Default: Small World."},
    {"outdir", 'o', "directory", 0,
     "Output log directory. Default '" DEFAULTDIR "'."},
    {"num_nodes", 'n', "num_nodes", 0, "Number of nodes in the graph."},
    {"steps_per_iteration", 'u', "steps", 0,
     "Number of local steps in each iteration or epoch."},
    {"share_howmany", 'h', "howmany", 0,
     "Number of ratings shared by node in each iteration."},
    {"epochs", 'e', "howmany", 0, "Number of epochs. Deafult 100."},
    {"usersdata", 'c', "howmany", 0,
     "Cap the amount of users in the input file. Default: unlimited."},
    {0}};

//------------------------------------------------------------------------------
struct Arguments {
    Arguments()
        : datashare(false),
          modelshare(true),
          dpsgd(false),
          randgraph(false),
          local(1),
          output_dir(DEFAULTDIR),
          num_nodes(10),
          steps_per_iteration(30),
          share_howmany(20),
          shared_memory(false),
          epochs(100),
          capusers(-1), embedding_size(10) {}

    std::string input_fname, output_dir;
    bool datashare, modelshare, dpsgd, randgraph, shared_memory;
    unsigned local, num_nodes, share_howmany, epochs;
    size_t steps_per_iteration, capusers, embedding_size;
};

//------------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    Arguments *args = (Arguments *)state->input;
    switch (key) {
        case 'k':
            args->embedding_size = std::atoi(arg);
            break;
        case 'f':
            args->input_fname = arg;
            break;
        case 's':
            args->datashare = true;
            break;
        case 'd':
            args->dpsgd = true;
            break;
        case 'r':
            args->randgraph = true;
            break;
        case 'o':
            args->output_dir = arg;
            break;
        case 'l':
            args->local = std::atoi(arg);
            break;
        case 'n':
            args->num_nodes = std::atoi(arg);
            break;
        case 'u':
            args->steps_per_iteration = std::atoi(arg);
            break;
        case 'm':
            args->shared_memory = true;
            break;
        case 'h':
            args->share_howmany = std::atoi(arg);
            break;
        case 'e':
            args->epochs = std::atoi(arg);
            break;
        case 'x':
            args->modelshare = false;
            break;
        case 'c':
            args->capusers = std::atoi(arg);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    };
    return 0;
}

//------------------------------------------------------------------------------
bool read_data(const std::string &fname, std::vector<MFNode> &nodes,
               const int num_nodes, bool modelshare, bool datashare,
               const int cap, const std::string &outdir) {
    typedef Eigen::SparseMatrix<uint8_t, Eigen::RowMajor> Matrix;
    Matrix matrix_train, matrix_test;
    TripletVector<uint8_t> train, test;
    std::pair<int, int> dim = read_and_split(fname, train, test, cap);
    if (dim.first == 0 || dim.second == 0) return false;

    int num_users_per_node = dim.first / num_nodes,
        remaining_users = dim.first % num_nodes;
    std::vector<int> users_per_node(num_nodes, num_users_per_node);
    if (remaining_users > 0) {
        do {
            users_per_node[remaining_users]++;
        } while (--remaining_users);
    }

    size_t train_count = 0, test_count = 0;  // for later validation

    int i = 0,  // counter for train data
        j = 0,  // counter for test data
        node_index = 0;
    while (node_index < num_nodes) {
        // create training data
        auto node_train_data = std::make_shared<DataStore>();
        std::set<int> user_ids;
        while (i < train.size()) {
            auto &triplet = train[i];
            user_ids.insert(triplet.row());
            if (user_ids.size() > users_per_node[node_index]) {
                user_ids.clear();
                break;
            }
            node_train_data->insert(std::make_pair(
                std::make_pair(triplet.row(), triplet.col()), triplet.value()));
            i++;
        }

        // create testing data
        TripletVector<uint8_t> node_test_data;
        while (j < test.size()) {
            auto &triplet = test[j];
            user_ids.insert(triplet.row());
            if (user_ids.size() > users_per_node[node_index]) {
                user_ids.clear();
                break;
            }
            node_test_data.push_back(triplet);
            j++;
        }

        train_count += node_train_data->size();
        test_count += node_test_data.size();
        nodes.emplace_back(node_index, node_train_data, node_test_data,
                           modelshare, datashare, outdir);
        node_index++;
    }

    assert(train_count == train.size());
    assert(test_count == test.size());

    return true;
}

//------------------------------------------------------------------------------
int main(int argc, char **argv) {
    Arguments args;
    struct argp argp = {options, parse_opt, 0, doc};
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

    boost::filesystem::create_directories(args.output_dir);

    std::vector<MFNode> nodes;
    if (!read_data(fname, nodes, args.num_nodes, args.modelshare,
                   args.datashare, args.capusers, args.output_dir))
        return 1;

    //std::cout << "Shared Memory: " << (args.shared_memory ? "Yes" : "No")
    //          << std::endl;
    MFCoordinator coordinator(nodes, args.shared_memory);

    //std::cout << (args.dpsgd ? "DPSGD" : "RMW") << std::endl;

    // lowscore, highscore, matrix_rank, learning, regularization, iterations
    coordinator.run(1, 10, args.embedding_size, 0.005, 0.1, args.epochs, args.dpsgd,
                    args.randgraph, args.local, args.steps_per_iteration,
                    args.share_howmany);
    return 0;
}
