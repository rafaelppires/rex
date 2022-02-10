#include <argp.h>
#include <data_splitter.h>
#include <machine_learning/matrix_factorization.h>
#include <threads/thread_pool.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <future>
#include <iostream>

using std::string;
//------------------------------------------------------------------------------
const char *argp_program_version = "MF local training";
const char *argp_program_bug_address = "<rafael.pires@epfl.ch>";

static char doc[] =
    "MF local training: PoC to check implementation correctness";
static char args_doc[] = "";
static struct argp_option options[] = {
    {"filename", 'f', "filename", 0, "Data file"}, {0}};

//------------------------------------------------------------------------------
struct Arguments {
    std::string input_fname;
};

//------------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    Arguments *args = (Arguments *)state->input;
    switch (key) {
        case 'f':
            args->input_fname = arg;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    };
    return 0;
}

//------------------------------------------------------------------------------
/*
MatrixFactorizationModel train(Eigen::SparseMatrix<uint8_t> &ratings) {
    return MFSGD::trainX(ratings, 2, 10, 20, 0.01, 0.01, 10);
}
*/

//------------------------------------------------------------------------------
void run(Ratings &ratings, TripletVector<uint8_t> &test) {
    /*
        MatrixFactorizationModel model = train(ratings);
        printf("RMSE = %lf\n", test_model(test, model));
    */
    ThreadPool tp(std::thread::hardware_concurrency());
    std::vector<int> iters = {201};
    std::vector<int> ranks = {10};
    std::vector<double> lambdas = {0.1};
    std::vector<double> etas = {0.005};
    std::vector<std::pair<string, std::future<MatrixFactorizationModel>>>
        results;
    for (int iter : iters) {
        for (int rank : ranks) {
            for (double eta : etas) {
                for (double lambda : lambdas) {
                    auto shared = std::make_shared<
                        std::packaged_task<MatrixFactorizationModel()>>(
                        std::bind(&MFSGD::trainX, ratings, 2, 10, rank, eta,
                                  lambda, iter, test));
                    std::stringstream ss;
                    ss << "r=" << rank << " n=" << eta << " l=" << lambda
                       << " n=" << iter;
                    results.emplace_back(
                        std::make_pair(ss.str(), shared->get_future()));
                    tp.add_task([shared]() { (*shared)(); });
                }
            }
        }
    }

    bool once = false;
    for (auto &r : results) {
        r.second.wait();
        MatrixFactorizationModel model = r.second.get();
        std::cout << r.first << ": " << model.rmse(test) << std::endl;
        /*
                if (!once) {
                    JsonSerializer js;
                    std::vector<uint8_t> serial =
           js.serialize(model.item_features()); std::cout <<
           string(serial.begin(), serial.end()) << std::endl; once = true;
                }*/
    }
}

//------------------------------------------------------------------------------
#include <matrices/matrix_serializer.h>
int main(int argc, char **argv) {
    Ratings ratings;
    TripletVector<uint8_t> test;

    Arguments args;
    struct argp argp = {options, parse_opt, 0, doc};
    argp_parse(&argp, argc, argv, 0, 0, &args);

    const char *homedir;
    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    std::string fname(homedir);
    fname += "/movielens/ml-latest-small/ratings.csv";
    if (args.input_fname.empty()) {
        std::cerr << "Data file not provided. See option -f ($ " << argv[0]
                  << " -?)\nTrying default: " << fname << "\n";

    } else {
        fname = args.input_fname;
    }

    if (!read_data(fname, ratings, test)) return 1;

    run(ratings, test);

    return 0;
}
