# Rex: SGX decentralized recommender
This repository contains the code of a decentralized recommender system that 
shares data instead of models and protects user's privacy by using Intel SGX.
Its description and evaluation were published in the paper
"TEE-based decentralized recommender systems: The raw data sharing redemption",
which will be presented in the 36th International Parallel and Distributed Processing
Symposium (IPDPS) in Lyon, France, from May 30 - June 3, 2022.

If this code or the ideas in the paper are somehow helpful to you,
please cite us according to this bib(liographic) entry:
```
@inproceedings{dhasade:2022:rex,
  author={Dhasade, Akash
      and Dresevic, Nevena
      and Kermarrec, Anne-Marie
      and Pires, Rafael},
  booktitle={36th {IEEE} International Parallel and Distributed Processing Symposium ({IPDPS})}, 
  title={{TEE}-based decentralized recommender systems: The raw data sharing redemption}, 
  year={2022},
}
```

# Dependencies and compilation
If your machine has SGX support, you need the [Intel SGX SDK](https://01.org/intel-software-guard-extensions) to run Rex. We tried it with version [2.9.1](https://01.org/intel-softwareguard-extensions/downloads/intel-sgx-linux-2.9.1-release).
In case you also want to try remote attestation, you also need [DCAP](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-software-guard-extensions-data-center-attestation-primitives-quick-install-guide.html). We tried it with version [1.8](https://01.org/intel-softwareguard-extensions/downloads/intel-sgx-dcap-1.8-release).

Otherwise, if you do not have access to SGX servers, you can still try a non-SGX version of Rex (with no security or attestation, of course), along with a simulation environment of the same decentralized recommender.
On Ubuntu, you can meet all non-SGX requirements with the following command:
```
$ sudo apt install build-essential libboost-all-dev libzmq3-dev
```
Be sure that you have also fetched the submodules:
```
git submodule update --init --recursive
```
To compile, simple type:
```
$ make
```

# SGX Decentralized recommender
```
$ ./bin/rex -?
Usage: rex [OPTION...]
Rex SGX Recommender: data sharing inside enclaves

  -d, --dpsgd                Switch to DPSGD. Default: RMW.
  -e, --epochs=howmany       Number of epochs. Deafult 10.
  -f, --filename=filename    Input data file.
  -h, --share_howmany=howmany   Number of ratings shared by node in each
                             iteration.
  -l, --local=number         Local iterations. Default: 1.
  -m, --machines="host1 host2:port2 [...]"
                             List of machines in host:port format, separated by
                             space and enclosed by quotes. In case no port is
                             provided, default port 4444 is assumed. All nodes
                             should provide this list in the same order.
  -p, --port=port            Listening port
  -s, --sharedata            Share raw data.
  -u, --steps_per_iteration=steps
                             Number of local steps in each iteration or epoch.
  -x, --disable_model_sharing   Disable sharing of models. Enables data sharing
                             by default.
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```

# Decentralized recommender: simulation environment
```
$ ./bin/local_decentralized_training -?
Usage: local_decentralized_training [OPTION...]
MF decentralized training: PoC to check implementation correctness

  -d, --dpsgd                Switch to DPSGD. Default: RMW.
  -e, --epochs=howmany       Number of epochs. Deafult 100.
  -f, --filename=filename    Input data file.
  -h, --share_howmany=howmany   Number of ratings shared by node in each
                             iteration.
  -l, --local=number         Local iterations. Default: 1.
  -m, --sharedmemory         Switch to shared memory communication. You cannot
                             get network measurements in this mode.
  -n, --num_nodes=num_nodes  Number of nodes in the graph.
  -o, --outdir=directory     Output log directory. Default 'out'.
  -r, --randomgraph          Switch to Random Graph. Default: Small World.
  -s, --sharedata            Share raw data.
  -u, --steps_per_iteration=steps
                             Number of local steps in each iteration or epoch.
  -x, --disable_model_sharing   Disable sharing of models. Enables data sharing
                             by default.
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version
```

# Contact
If you think we omitted something that could be helpful for anyone willing to
try our code or reproduce our results, please do not hesitate to contact us.

# Disclaimer
This is a research prototype and not intended for production environments.

# License

Copyright 2022 Rafael Pires

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
