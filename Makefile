CC       :=gcc
CXX      :=g++
CFlags   := -std=c11
CXXFlags := -std=c++11
SharedTUFlags := -m64 -DUSE_OPENSSL

Rex           := rex
RexNative     := rex_native
LocalTrain    := local_training
LocalP2P      := local_decentralized_training
EnclaveName   := enclave_$(Rex)

BinDir=bin
SrcDir=src
ObjDir=obj

Targets        := $(LocalTrain) $(LocalP2P) $(Rex) $(RexNative)
EnclaveSources := $(SrcDir)/enclave
App_Libs       := pthread boost_filesystem boost_system
SgxApp_Libs    := pthread sgx_uae_service sgx_urts sgx_dcap_quoteverify zmq\
                  sgx_dcap_ql
RexNativeLibs  := $(filter-out sgx_uae_service sgx_urts sgx_dcap_quoteverify,\
	                    $(Rex_Libs))

WholeArchiveEnclaveLibs := sgx_trts sgx_tsgxssl
Enclave_Libs   := sgx_tstdc sgx_tservice sgx_uae_service sgx_pthread\
                  sgx_dcap_tvl sgx_tsgxssl_crypto
TrustedFlags   := -DENCLAVED

include Makefile.sgx
include runtest.mk

EnclaveCXXIDirs  := $(SGX_SDK)/include/libcxx $(SGX_COMMONDIR)/generic\
                    $(SSL_ROOT)/include
Enclave_IncDirs  := $(SrcDir) $(EnclaveSources) $(SGX_SDK)/include \
                    $(SGX_SDK)/include/tlibc $(SGX_COMMONDIR)\
                    $(SGX_COMMONDIR)/enclave_include $(SrcDir)/simd
EnclCInclude     := $(addprefix -I, $(Enclave_IncDirs))
EnclCXXInclude   := $(EnclCInclude) $(addprefix -I, $(EnclaveCXXIDirs))

ifeq ($(DEBUG), 1)
	Encl_CFlags   += -g
	Encl_CXXFlags += -g
	Natv_CXXFlags += -g
	Natv_CFlags   += -g
endif

ifeq (,$(wildcard $(SGX_SDK)))
	Targets:=$(filter-out $(Rex), $(Targets)) nosgx
endif


NatvIncludeDirs := $(SrcDir) $(SGX_COMMONDIR)/generic $(EnclaveSources)\
	               $(SGX_SDK)/include $(SrcDir)/communication $(SGX_COMMONDIR)\
                   $(SGX_COMMONDIR)/sgx
CommonObjs      := csv stringtools ratings_parser data_splitter
NonSgxCommon    := $(CommonObjs) thread_pool matrix_factorization mf_weights
LocalTrainObjs  := $(addprefix $(ObjDir)/,$(addsuffix _u.o, $(LocalTrain)\
	                    $(NonSgxCommon) matrix_serializer time_probe \
                        mf_centralized))
LocalP2PObjs    := $(addprefix $(ObjDir)/,$(addsuffix _u.o, $(LocalP2P)\
	                    $(NonSgxCommon) mf_coordinator random_model_walk\
	                    dpsgd mf_decentralized time_probe mf_node))
RexObjs         := $(addprefix $(ObjDir)/,$(addsuffix _u.o, $(Rex)\
                        $(CommonObjs) $(EnclaveName) enclave_interface\
                        sgx_initenclave sgx_errlist generic_utils sync_zmq\
                        netstats ocalls_rex sgx_qe_errlist sgx_qv_errlist\
                        time_probe))
EnclaveObjs     := $(addprefix $(ObjDir)/, $(addsuffix _t.o, $(EnclaveName)\
                        ecalls_$(Rex) mf_node matrix_factorization libcpp_mock\
                        mf_weights time_probe mf_decentralized dpsgd\
                        random_model_walk libc_proxy file_mock json_utils\
                        node_protocol stringtools ecdh attestor crypto_common\
                        sgx_qve_errlist aes_utils\
                    ))
RexNativeObjs   := $(filter-out \
                        $(addprefix $(ObjDir)/,$(addsuffix _u.o, \
                        $(Rex) $(EnclaveName) sgx_qe_errlist sgx_qv_errlist \
                        ocalls_rex enclave_interface sgx_initenclave\
                        sgx_errlist)), \
                    $(RexObjs)\
                    )\
                   $(patsubst %_t.o, %_u.o, $(filter-out \
                        $(addprefix $(ObjDir)/, $(addsuffix _t.o, \
                        $(EnclaveName) ecalls_rex json_utils node_protocol\
                        libcpp_mock libc_proxy file_mock ecdh attestor \
                        sgx_qve_errlist aes_utils crypto_common)), \
                    $(EnclaveObjs)))
RexNativeObjs   +=  $(addprefix $(ObjDir)/, $(addsuffix _n.o, \
                        ecalls_rex json_utils node_protocol ocalls_rex\
                        enclave_interface $(Rex)))

NatvInclude     := $(addprefix -I, $(NatvIncludeDirs))
App_Link_Flags  := $(addprefix -L, $(App_Lib_Dirs)) \
	               $(addprefix -l, $(App_Libs))
RexNativeLinkFlags := $(addprefix -l, pthread zmq)

all: $(Targets)
$(filter-out nosgx, $(Targets)) : % : $(BinDir)/%
$(Rex) : % : $(BinDir)/enclave_%.signed.so
$(ObjDir)/$(Rex)_u.o : $(ObjDir)/$(EnclaveName)_u.o
$(ObjDir)/ecalls_$(Rex)_t.o : $(EnclaveSources)/$(EnclaveName)_t.c

############## UNTRUSTED #######################################################
$(BinDir)/$(Rex) : $(RexObjs) | $(BinDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) -o $@ $^ $(SgxApp_Link_Flags),"Link")

$(BinDir)/$(RexNative) : $(RexNativeObjs) | $(BinDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) -o $@ $^ $(RexNativeLinkFlags),"Link")

$(BinDir)/$(LocalTrain) : $(LocalTrainObjs) | $(BinDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) -o $@ $^ \
	            $(filter-out -lsgx%, $(App_Link_Flags)),"Link")

$(BinDir)/$(LocalP2P) : $(LocalP2PObjs) | $(BinDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) -o $@ $^ \
	            $(filter-out -lsgx%, $(App_Link_Flags)),"Link")

$(EnclaveSources)/%_u.c : $(EnclaveSources)/%.edl $(SGX_EDGER8R)
	@$(call run_and_test,\
	        cd $(dir $@) && \
	        $(SGX_EDGER8R) --untrusted ./$(notdir $<) \
	            $(addprefix --search-path ,. $(EDLSearchPaths)),"Edger SGX")

$(ObjDir)/%_u.o : $(EnclaveSources)/%_u.c | $(ObjDir)
	@$(call run_and_test,\
	        $(CC) $(Natv_CFlags) $(NatvInclude) -c $< -o $@,"CC")

$(ObjDir)/%_u.o: $(SrcDir)/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_n.o : $(SrcDir)/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -DNATIVE $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_n.o : $(EnclaveSources)/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -DNATIVE $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/machine_learning/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/model_merging/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/threads/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/matrices/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/utils/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SrcDir)/communication/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SGX_COMMONDIR)/generic/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

$(ObjDir)/%_u.o : $(SGX_COMMONDIR)/sgx/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $(Natv_CXXFlags) $(NatvInclude) -c -o $@ $<,"CXX")

############### TRUSTED ########################################################
$(BinDir)/%.signed.so : $(ObjDir)/%.so \
	                    $(EnclaveSources)/enclave-key.pem | $(BinDir)
	$(eval EName := $(patsubst %.so, %, $(notdir $<)))
	@$(call run_and_test,\
	        $(SGX_ENCLAVE_SIGNER) sign -enclave $< \
	            -config $(EnclaveSources)/$(EName).config.xml -out $@ \
	            -key $(EnclaveSources)/enclave-key.pem -ignore-init-sec-error \
	            > /dev/null 2>&1,"Sign SGX")

$(EnclaveSources)/enclave-key.pem:
	@openssl genrsa -out $@ -3 3072

$(ObjDir)/$(EnclaveName).so : $(EnclaveObjs) | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) $^ -o $@ $(EnclaveLFlags),"Link SGX")

$(EnclaveSources)/%_t.c : $(EnclaveSources)/%.edl $(SGX_EDGER8R)
	@$(call run_and_test,\
	        cd $(dir $@) && \
	        $(SGX_EDGER8R) --trusted ./$(notdir $<) \
	            $(addprefix --search-path ,. $(EDLSearchPaths)),"Edger SGX")

$(ObjDir)/%_t.o : $(EnclaveSources)/%_t.c | $(ObjDir)
	@$(call run_and_test,\
	        $(CC) -c $< -o $@ $(Encl_CFlags) $(EnclCInclude),"CC SGX")

$(ObjDir)/%_t.o : $(EnclaveSources)/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SrcDir)/machine_learning/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SrcDir)/model_merging/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SrcDir)/utils/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SGX_COMMONDIR)/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SGX_COMMONDIR)/libc_mock/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SGX_COMMONDIR)/generic/%.cpp | $(ObjDir)
	@$(call run_and_test,\
	        $(CXX) -c $< -o $@ $(Encl_CXXFlags) $(EnclCXXInclude),"CXX SGX")

$(ObjDir)/%_t.o : $(SGX_COMMONDIR)/libc_mock/%.c | $(ObjDir)
	@$(call run_and_test,\
	        $(CC) -c $< -o $@ $(Encl_CFlags) $(EnclCInclude),"CC SGX")
################################################################################

src/sgx_common/generic/csv.cpp src/sgx_common/generic/stringtools.cpp:
	git submodule update --init --recursive

/usr/lib/x86_64-linux-gnu/libboost_system.a:
	@sudo apt install build-essential libboost-filesystem-dev\
                      libboost-chrono-dev libboost-system-dev

/usr/lib/libboost_system.a:
	@sudo pacman -S boost boost-libs 

$(BinDir) $(ObjDir):
	@mkdir -p $@

clean:
	@bash -c "rm -rf $(BinDir) $(ObjDir)"

nosgx:
	@echo "*** You do not seem to have SGX SDK installed, Rex was excluded from list of targets ***"

obj/stringtools_u.o: src/sgx_common/generic/stringtools.cpp
obj/csv_u.o: src/sgx_common/generic/csv.cpp

DISTRO=$(shell uname -a | grep -o 'Ubuntu\|arch')
ifeq ($(DISTRO), arch)
obj/local_training_u.o: /usr/lib/libboost_system.a
else ifeq ($(DISTRO), ubuntu)
obj/local_training_u.o: /usr/lib/x86_64-linux-gnu/libboost_system.a
endif

.PHONY: clean all $(Targets)
